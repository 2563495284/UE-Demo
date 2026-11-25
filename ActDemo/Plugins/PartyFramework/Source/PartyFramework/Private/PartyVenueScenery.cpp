#include "PartyVenueScenery.h"
#include "PartyVoxelArena.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace
{
    FName AuthoredGroup(const AActor& Actor)
    {
        for (FName Tag : Actor.Tags)
            if (Tag.GetNumber() > 0 && FName(Tag, 0) == PartyVenueScenery::StructureTag)
                return Tag;
        return NAME_None;
    }

    // The original six maps predate group tags. They use these two distinct sized
    // cylinders for the lagoon and ten island shelves. Everything atop each shelf
    // was authored as one skyline structure.
    bool IsLegacyCylinder(const UStaticMeshComponent& Part, FVector Scale)
    {
        return Part.GetStaticMesh()->GetFName() == TEXT("Cylinder")
            && Part.GetComponentScale().GetAbs().Equals(Scale, .01);
    }

    void EnableToolTrace(UStaticMeshComponent& Part)
    {
        Part.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part.SetCollisionResponseToAllChannels(ECR_Ignore);
        Part.SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    }

    void DropFragment(AActor& FX, UStaticMeshComponent& Source, FVector BuildingCenter)
    {
        auto* Fragment = NewObject<UStaticMeshComponent>(&FX);
        FX.AddInstanceComponent(Fragment);
        Fragment->SetMobility(EComponentMobility::Movable);
        Fragment->SetStaticMesh(Source.GetStaticMesh());
        for (int32 Slot = 0; Slot < Source.GetNumMaterials(); ++Slot)
            Fragment->SetMaterial(Slot, Source.GetMaterial(Slot));
        const FVector Center = Source.Bounds.Origin;
        const FVector Out = (Center - BuildingCenter).GetSafeNormal2D();
        Fragment->SetWorldLocationAndRotation(Center, Source.GetComponentRotation());
        Fragment->SetWorldScale3D(Source.GetComponentScale() * .58);
        Fragment->SetCanEverAffectNavigation(false);
        Fragment->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
        Fragment->SetCollisionResponseToAllChannels(ECR_Ignore);
        Fragment->RegisterComponent();
        Fragment->SetSimulatePhysics(true);
        Fragment->SetPhysicsLinearVelocity(Out * 240. + FVector(0., 0., -230.));
        Fragment->SetPhysicsAngularVelocityInDegrees(FVector(65., 37., 18.));
    }
}

void APartyVoxelSession::BindVenueLevel(ULevel& Level)
{
    if (BoundVenue.Get() == &Level || Level.GetWorld() != GetWorld()) return;
    BoundVenue = &Level;
    VenueStructures.Reset();
    VenuePartIds.Reset();

    TArray<UStaticMeshComponent*> Shelves, Parts;
    bool bHasOriginalLagoon = false;
    for (AActor* Actor : Level.Actors)
    {
        auto* Scenery = Cast<AStaticMeshActor>(Actor);
        if (!Scenery) continue;
        auto* Part = Scenery->GetStaticMeshComponent();
        if (!Part || !Part->GetStaticMesh() || !Part->IsVisible()) continue;
        if (IsLegacyCylinder(*Part, FVector(245.,245.,1.8)))
        {
            bHasOriginalLagoon = true;
            continue;
        }
        if (Actor->ActorHasTag(PartyVenueScenery::WaterTag)) continue;
        if (Actor->ActorHasTag(PartyVenueScenery::ShelfTag) || IsLegacyCylinder(*Part, FVector(17.5,14.2,13.2)))
        {
            Shelves.Add(Part);
            EnableToolTrace(*Part);
        }
        else Parts.Add(Part);
    }

    TMap<FName, TArray<TWeakObjectPtr<UStaticMeshComponent>>> Groups;
    const bool bOriginalLayout = bHasOriginalLagoon && Shelves.Num() == 10;
    for (UStaticMeshComponent* Part : Parts)
    {
        FName Group = AuthoredGroup(*Part->GetOwner());
        if (Group.IsNone() && bOriginalLayout)
        {
            UStaticMeshComponent* Nearest = nullptr;
            double NearestDistance = FMath::Square(1000.);
            for (UStaticMeshComponent* Shelf : Shelves)
            {
                const double Distance = FVector::DistSquared2D(Part->GetComponentLocation(), Shelf->GetComponentLocation());
                if (Distance < NearestDistance)
                {
                    Nearest = Shelf;
                    NearestDistance = Distance;
                }
            }
            if (Nearest)
            {
                Group = AuthoredGroup(*Nearest->GetOwner());
                if (Group.IsNone()) Group = Nearest->GetOwner()->GetFName();
            }
        }
        if (!Group.IsNone()) Groups.FindOrAdd(Group).Add(Part);
    }

    // A streaming instance has its own object/package pointers on each peer.
    // Sort package-local group names so one replicated index selects the same building.
    TArray<FName> Names;
    Groups.GetKeys(Names);
    Names.Sort(FNameLexicalLess());
    for (FName Name : Names)
    {
        const int32 Id = VenueStructures.Add(MoveTemp(Groups.FindChecked(Name)));
        for (const auto& Weak : VenueStructures[Id])
        {
            VenuePartIds.Add(Weak, Id);
            EnableToolTrace(*Weak.Get());
        }
    }
    ApplyVenueDestruction(false);
}

void APartyVoxelSession::GetVenueStructureParts(int32 Id, TArray<UStaticMeshComponent*>& Out) const
{
    Out.Reset();
    if (VenueStructures.IsValidIndex(Id))
        for (const auto& Weak : VenueStructures[Id])
            if (UStaticMeshComponent* Part = Weak.Get()) Out.Add(Part);
}

int32 APartyVoxelSession::TraceVenueStructure(FVector From, FVector Direction, const AActor* Ignore) const
{
    if (!BoundVenue.IsValid() || Direction.IsNearlyZero()) return INDEX_NONE;
    FHitResult Hit;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyVenueDemolition), false, Ignore);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, From, From + Direction.GetSafeNormal() * StructureReach, ECC_Visibility, Query))
        return INDEX_NONE;
    auto* Part = Cast<UStaticMeshComponent>(Hit.GetComponent());
    const int32* Id = Part ? VenuePartIds.Find(Part) : nullptr;
    return Id && !IsVenueStructureDestroyed(*Id) ? *Id : INDEX_NONE;
}

void APartyVoxelSession::OnRep_VenueStructures()
{
    ApplyVenueDestruction(true);
}

void APartyVoxelSession::ApplyVenueDestruction(bool bShowDebris)
{
    for (int32 Id : DestroyedVenueStructures)
    {
        if (!VenueStructures.IsValidIndex(Id)) continue;
        TArray<UStaticMeshComponent*> Visible;
        FBox Bounds(ForceInit);
        for (const auto& Weak : VenueStructures[Id])
        {
            UStaticMeshComponent* Part = Weak.Get();
            if (!Part || !Part->IsVisible()) continue;
            Bounds += Part->Bounds.GetBox();
            Visible.Add(Part);
            Part->SetVisibility(false);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        if (Visible.IsEmpty() || !bShowDebris || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) continue;
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        if (AActor* FX = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Bounds.GetCenter(), FRotator::ZeroRotator, Params))
        {
            for (UStaticMeshComponent* Part : Visible) DropFragment(*FX, *Part, Bounds.GetCenter());
            FX->SetLifeSpan(1.f);
        }
    }
}

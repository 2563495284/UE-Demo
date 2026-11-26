#include "Island/IslandGameMode.h"

#include "Components/CapsuleComponent.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

int32 AIslandGameMode::FDecoyWorldPlan::Mask() const
{
    int32 Result = 0;
    if (!RallyLandings.IsEmpty()) Result |= IslandRules::DecoyEventBit(EIslandDecoyEvent::CrewRally);
    if (!SandCenters.IsEmpty()) Result |= IslandRules::DecoyEventBit(EIslandDecoyEvent::SandRamp);
    if (!Sails.IsEmpty()) Result |= IslandRules::DecoyEventBit(EIslandDecoyEvent::SailGust);
    return Result;
}

AIslandGameMode::FDecoyWorldPlan AIslandGameMode::PlanDecoyWorldEvents() const
{
    FDecoyWorldPlan Plan;
    if (PartyChestIndex < 0 || PartyChestIndex >= HiddenLayout.Num() || !HiddenLayout.IsDecoy(PartyChestIndex)) return Plan;
    PlanRallyLandings(Plan);
    Plan.SandCenters = FindDecoySandRamp();
    for (AIslandNeutralProp* Prop : NeutralProps)
        if (IsValid(Prop) && Prop->CanToggleSail()) Plan.Sails.Add(Prop);
    return Plan;
}

void AIslandGameMode::PlanRallyLandings(FDecoyWorldPlan& Plan) const
{
    TArray<AIslandPlayerController*> SeatedPlayers;
    SeatedPlayers.SetNumZeroed(RoomCapacity());
    TArray<FVector> Reserved;
    for (const auto& Pair : PartyPlayers)
    {
        if (!Pair.Key.IsValid() || !Pair.Value.Original.IsValid() || !SeatedPlayers.IsValidIndex(Pair.Value.Seat)) continue;
        SeatedPlayers[Pair.Value.Seat] = Pair.Key.Get();
        Reserved.Add(Pair.Value.Original->GetActorLocation());
    }
    SeatedPlayers.Remove(nullptr);

    for (int32 Team = 0; Team < TeamClues.Num(); ++Team)
    {
        for (AIslandPlayerController* Anchor : SeatedPlayers)
        {
            if (Anchor->GetTeam() != Team) continue;
            const FPartyPlayer& Entry = PartyPlayers.FindChecked(Anchor);
            if (Entry.bReturnToDock || IsPartyReturnBlocked(*Entry.Original.Get())) continue;
            const auto* Hero = Entry.Original.Get();
            const FVector Origin = Hero->GetActorLocation();
            const auto Ground = FindStandingFloor(Hero, Origin + FVector(0, 0, 30.), 240.);
            if (!Ground || FMath::Abs(Ground->Z - Origin.Z) > 60.) continue;

            const int32 Before = Plan.RallyLandings.Num();
            for (AIslandPlayerController* Member : SeatedPlayers)
            {
                if (Member == Anchor || Member->GetTeam() != Team) continue;
                const auto Landing = FindRallyLanding(PartyPlayers.FindChecked(Member), Origin, Reserved);
                if (!Landing) continue;
                Plan.RallyLandings.Add(Member, Landing.GetValue());
                Reserved.Add(Landing->GetLocation());
            }
            // A safe perch can be too narrow for the crew. Try the next member's island site.
            if (Plan.RallyLandings.Num() > Before) break;
        }
    }
}

bool AIslandGameMode::IsPartyReturnBlocked(const AVoxelCharacter& Hero) const
{
    const UCapsuleComponent* Capsule = Hero.GetCapsuleComponent();
    return GetWorld()->OverlapBlockingTestByChannel(Hero.GetActorLocation(), Hero.GetActorQuat(),
        Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
        FCollisionQueryParams(SCENE_QUERY_STAT(IslandPartyReturn), false, &Hero), FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
}

TOptional<FTransform> AIslandGameMode::FindRallyLanding(const FPartyPlayer& Entry, FVector Origin, TConstArrayView<FVector> Reserved) const
{
    const AVoxelCharacter* Hero = Entry.Original.Get();
    const FVector Before = Hero->GetActorLocation();
    for (double Radius : {140., 250., 380.})
    {
        // Leave an already assembled crew in place. The pool needs a real gathering.
        if (!Entry.bReturnToDock && FVector::Dist2D(Before, Origin) < Radius + 50.) continue;
        for (int32 Step = 0; Step < 12; ++Step)
        {
            const double Angle = (Step + Entry.Seat) * UE_TWO_PI / 12.;
            const FVector Site = Origin + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.);
            if (Reserved.ContainsByPredicate([&](const FVector& P) { return FVector::DistSquared2D(Site, P) < FMath::Square(110.); })) continue;
            const auto Ground = FindStandingFloor(Hero, Site + FVector(0,0,220.), 460.);
            if (Ground && FMath::Abs(Ground->Z - Origin.Z) <= 160.)
                return FTransform(FRotator(0., Entry.View.Yaw, 0.), Ground.GetValue());
        }
    }
    return {};
}

TArray<FVector> AIslandGameMode::FindDecoySandRamp() const
{
    const AVoxelTerrain* IslandTerrain = GetTerrain();
    const FVoxelDensity* Density = IslandTerrain ? IslandTerrain->GetDensity() : nullptr;
    if (!Density || !IslandTerrain->IsReady() || IslandTerrain->IsBusy()) return {};
    const FVector Chest = HiddenLayout.GetCenter(PartyChestIndex);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandDecoyRamp), false, IslandTerrain);
    for (double Distance : {320., 600., 880.})
    {
        for (int32 Step = 0; Step < 12; ++Step)
        {
            const FRotator Rotation(0., Step * 30., 0.);
            const FVector Forward = Rotation.Vector();
            const FVector Side(-Forward.Y, Forward.X, 0.);
            FVector Base = Chest + Forward * Distance;
            const auto Ground = Density->SurfaceAt(FVector2D(Base));
            if (!Ground || Ground.GetValue() <= 35.) continue;
            Base.Z = Ground.GetValue();
            const FVector Middle = Base + Forward * 205.;
            // Reject existing structures, sleeping island pawns and the standing space over the ramp.
            if (GetWorld()->OverlapBlockingTestByChannel(Middle + FVector(0,0,155.), Rotation.Quaternion(), ECC_Pawn,
                FCollisionShape::MakeBox(FVector(280., 145., 170.)), Query)) continue;
            bool bClear = true;
            for (int32 Index = 0; Index < HiddenLayout.Num(); ++Index)
            {
                if (Index == PartyChestIndex || ConsumedDecoys.Contains(Index)) continue;
                FVector ToChest = HiddenLayout.GetCenter(Index) - Middle;
                if (FMath::Abs(ToChest.Dot(Forward)) < 350. && FMath::Abs(ToChest.Dot(Side)) < 215.)
                {
                    bClear = false;
                    break;
                }
            }
            if (!bClear) continue;
            for (double Along : {0., 140., 280., 410.})
            {
                for (double Across : {-90., 0., 90.})
                {
                    const auto Height = Density->SurfaceAt(FVector2D(Base + Forward * Along + Side * Across));
                    if (!Height || Height.GetValue() <= 25. || FMath::Abs(Height.GetValue() - Base.Z) > 40.) bClear = false;
                }
            }
            if (!bClear) continue;
            auto Centers = AIslandNeutralProp::MakeSandRamp(FTransform(Rotation, Base), Density);
            if (CanStartEnvironmentFill(Centers)) return Centers;
        }
    }
    return {};
}

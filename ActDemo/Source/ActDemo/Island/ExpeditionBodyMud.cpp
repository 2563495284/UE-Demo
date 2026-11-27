#include "Island/ExpeditionBodyMud.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Island/IslandAttachedMud.h"
#include "Island/IslandSceneryVoxels.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "VoxelSurfaceKernel.h"

namespace
{
    constexpr double BodyMudRadius = UExpeditionBodyMudComponent::ClodRadius;
    // Quantization and collision contact offsets can separate two touching surfaces slightly.
    constexpr double Seam = .75;
    constexpr double LinkSpan = BodyMudRadius * 2. + Seam;
    const FColor BodyMudColor(163,102,64);
    DECLARE_MULTICAST_DELEGATE_OneParam(FClaySupportChanged,UWorld*);
    FClaySupportChanged SupportChanges;

    struct FClayNode
    {
        UExpeditionBodyMudComponent* Body = nullptr;
        int32 Index = INDEX_NONE;
        FVector Center = FVector::ZeroVector;
        TArray<int32, TInlineAllocator<6>> Neighbors;
        int32 StepsFromScene = INDEX_NONE;
    };

    FIntVector BucketFor(FVector Point)
    {
        return FIntVector(FMath::FloorToInt(Point.X / LinkSpan), FMath::FloorToInt(Point.Y / LinkSpan),
            FMath::FloorToInt(Point.Z / LinkSpan));
    }

    // A spatial hash bounds the work for many unrelated clods on an eight-player island.
    void LinkTouching(TArray<FClayNode>& Nodes)
    {
        TMap<FIntVector, TArray<int32>> Buckets;
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            FClayNode& Current = Nodes[I];
            const FIntVector Cell = BucketFor(Current.Center);
            for (int32 Z = -1; Z <= 1; ++Z)
                for (int32 Y = -1; Y <= 1; ++Y)
                    for (int32 X = -1; X <= 1; ++X)
                        if (const auto* Nearby = Buckets.Find(Cell + FIntVector(X,Y,Z)))
                            for (int32 Other : *Nearby)
                                if (FVector::DistSquared(Current.Center, Nodes[Other].Center) <= FMath::Square(LinkSpan))
                                {
                                    Current.Neighbors.Add(Other);
                                    Nodes[Other].Neighbors.Add(I);
                                }
            Buckets.FindOrAdd(Cell).Add(I);
        }
    }

    void Flood(TArray<FClayNode>& Nodes, TArray<int32>& Queue)
    {
        for (int32 Head = 0; Head < Queue.Num(); ++Head)
        {
            const auto& Node = Nodes[Queue[Head]];
            for (const int32 I : Node.Neighbors)
                if (Nodes[I].StepsFromScene == INDEX_NONE)
                {
                    Nodes[I].StepsFromScene = Node.StepsFromScene + 1;
                    Queue.Add(I);
                }
        }
    }

    FVector SoilNormal(const FVoxelDensity& Soil, FVector At)
    {
        constexpr double Step = 2.;
        const auto Axis = [&](FVector Direction)
        {
            return Soil.SignedDistanceAt(At + Direction*Step) - Soil.SignedDistanceAt(At - Direction*Step);
        };
        return FVector(Axis(FVector::ForwardVector),Axis(FVector::RightVector),Axis(FVector::UpVector))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    }

    bool FindSoilFoot(const FVoxelDensity& Soil, FVector Center, FVector& Point, FVector& Normal)
    {
        // The edited volume is not an exact signed-distance field. Test an actual zero crossing
        // within the clod, including vertical overhangs and previously player-built mud platforms.
        if (Soil.SignedDistanceAt(Center) >= VoxelSurfaceKernel::Band) return false;
        Normal = SoilNormal(Soil,Center);
        FVector Air = Center + Normal*(BodyMudRadius + Seam);
        FVector Solid = Center - Normal*(BodyMudRadius + Seam);
        if (Soil.SignedDistanceAt(Solid) > 0.) return false;
        if (Soil.SignedDistanceAt(Air) < 0.)
        {
            Point = Air;
            return true;
        }
        for (int32 Step = 0; Step < 7; ++Step)
        {
            const FVector Middle = (Air + Solid)*.5;
            if (Soil.SignedDistanceAt(Middle) > 0.) Air = Middle;
            else Solid = Middle;
        }
        Point = (Air + Solid)*.5;
        Normal = SoilNormal(Soil, Point);
        return true;
    }

    bool FindStructureFoot(UWorld& World, const AVoxelCharacter& Hero, FVector Center, FVector& Point, FVector& Normal)
    {
        const FCollisionShape Sphere = FCollisionShape::MakeSphere(BodyMudRadius + Seam);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionClayFoot), false, &Hero);
        TArray<FOverlapResult> Parts;
        World.OverlapMultiByChannel(Parts, Center, FQuat::Identity, ECC_Visibility, Sphere, Query);
        for (const auto& Overlap : Parts)
        {
            auto* Part = Overlap.GetComponent();
            // The collision is rebuilt or removed as soon as a beam, platform or attached
            // wall-clod loses its support. Falling scenery has no load-bearing surface.
            if (!AIslandAttachedMud::CanReceive(Part)) continue;
            static const FVector Directions[] = {FVector::UpVector, FVector::DownVector, FVector::ForwardVector,
                FVector::BackwardVector, FVector::RightVector, FVector::LeftVector};
            for (FVector Axis : Directions)
            {
                FHitResult Hit;
                if (!Part->SweepComponent(Hit, Center + Axis*(BodyMudRadius*3.), Center, FQuat::Identity,Sphere)
                    || Hit.bStartPenetrating || FVector::DistSquared(Hit.ImpactPoint,Center) > FMath::Square(BodyMudRadius+Seam+0.1))
                    continue;
                Point = Hit.ImpactPoint;
                Normal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER,Axis);
                return true;
            }
            // Already embedded in a newly placed volume: narrow-phase overlap above proves
            // the contact even when all the inward probes started inside.
            const FVector FromPart = Center - Part->Bounds.Origin;
            Normal = FromPart.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
            Point = Center - Normal*BodyMudRadius;
            return true;
        }
        return false;
    }

    bool SameFeet(TConstArrayView<FExpeditionClayFoot> A, TConstArrayView<FExpeditionClayFoot> B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 I = 0; I < A.Num(); ++I)
            if (A[I].Index != B[I].Index || !A[I].LocalPoint.Equals(B[I].LocalPoint,.5)
                || !A[I].LocalNormal.Equals(B[I].LocalNormal,.025)) return false;
        return true;
    }
}

UExpeditionBodyMudComponent::UExpeditionBodyMudComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Game/TreasureIsland/Art/Neutral/SM_ReefStone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    Stone = Shape.Object;
    ClayMaterial = Material.Object;
}

void UExpeditionBodyMudComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshVisuals();
}

void UExpeditionBodyMudComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    SupportChanges.Remove(SceneChangedHandle);
    if (!Clods.IsEmpty()) NotifySceneChanged(GetWorld());
    Super::EndPlay(Reason);
}

void UExpeditionBodyMudComponent::NotifySceneChanged(UWorld* World)
{
    if (World && World->GetNetMode() != NM_Client) SupportChanges.Broadcast(World);
}

void UExpeditionBodyMudComponent::InvalidateSupport(UWorld* World)
{
    if (World == GetWorld()) bSupportChanged = true;
}

bool UExpeditionBodyMudComponent::NeedsSupportCheck(double Now) const
{
    return bSupportChanged || Now >= NextSupportCheck || !SampledFrame.Equals(GetOwner()->GetActorTransform(),.0001);
}

bool UExpeditionBodyMudComponent::GetClodWorld(int32 Index, FVector& Center, float& OutRadius) const
{
    if (!Clods.IsValidIndex(Index)) return false;
    Center = GetOwner()->GetActorTransform().TransformPosition(Clods[Index].LocalCenter);
    OutRadius = ClodRadius;
    return true;
}

bool UExpeditionBodyMudComponent::Trace(const FVector& Start, const FVector& End, float SweepRadius, FHitResult& OutHit) const
{
    const auto* Hero = CastChecked<AVoxelCharacter>(GetOwner());
    const FTransform Frame = Hero->GetActorTransform();
    const FVector LocalStart = Frame.InverseTransformPosition(Start);
    const FVector Travel = Frame.InverseTransformVectorNoScale(End - Start);
    const double Reach = BodyMudRadius + SweepRadius;
    const double TravelSquared = Travel.SizeSquared();
    double Best = 2.;
    int32 BestIndex = INDEX_NONE;
    for (int32 I = 0; I < Clods.Num(); ++I)
    {
        const FVector Offset = LocalStart - FVector(Clods[I].LocalCenter);
        const double Gap = Offset.SizeSquared() - Reach*Reach;
        double Time = 0.;
        if (Gap > 0.)
        {
            const double Towards = -FVector::DotProduct(Offset,Travel);
            if (Towards < 0. || TravelSquared < UE_DOUBLE_SMALL_NUMBER) continue;
            const double Discriminant = Towards*Towards - TravelSquared*Gap;
            if (Discriminant < 0.) continue;
            Time = (Towards - FMath::Sqrt(Discriminant)) / TravelSquared;
        }
        if (Time >= 0. && Time <= 1. && Time < Best)
        {
            Best = Time;
            BestIndex = I;
        }
    }
    if (BestIndex == INDEX_NONE) return false;
    const FVector Center = Frame.TransformPosition(Clods[BestIndex].LocalCenter);
    const FVector Location = FMath::Lerp(Start,End,Best);
    const FVector Normal = (Location - Center).GetSafeNormal(UE_SMALL_NUMBER,
        Frame.TransformVectorNoScale(Clods[BestIndex].LocalNormal));
    OutHit = FHitResult(GetOwner(), Hero->GetCapsuleComponent(), Center + Normal*BodyMudRadius, Normal);
    OutHit.TraceStart = Start;
    OutHit.TraceEnd = End;
    OutHit.Time = Best;
    OutHit.Distance = (Location - Start).Size();
    OutHit.Location = Location;
    OutHit.Item = BestIndex;
    OutHit.bBlockingHit = true;
    OutHit.bStartPenetrating = Best == 0.;
    return true;
}

bool UExpeditionBodyMudComponent::AddHit(const FHitResult& Hit)
{
    AVoxelCharacter* Hero = CastChecked<AVoxelCharacter>(GetOwner());
    if (!Hero->HasAuthority() || Hit.GetActor() != Hero || Hit.ImpactPoint.ContainsNaN()) return false;
    const bool OnClod = Clods.IsValidIndex(Hit.Item);
    if (!SceneChangedHandle.IsValid())
        SceneChangedHandle = SupportChanges.AddUObject(this,&ThisClass::InvalidateSupport);
    const FVector SurfaceNormal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER,
        (Hit.TraceStart-Hit.ImpactPoint).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector));
    // Each new clod overlaps the struck surface. Successive throws grow outward from
    // that actual point, regardless of which side of the pawn the thrower stands on.
    const FVector Center = Hit.ImpactPoint + SurfaceNormal * (OnClod ? BodyMudRadius-5. : 2.);
    const FTransform Frame = Hero->GetActorTransform();
    FExpeditionClod& Clod = Clods.AddDefaulted_GetRef();
    Clod.LocalCenter = Frame.InverseTransformPosition(Center);
    Clod.LocalNormal = Frame.InverseTransformVectorNoScale(SurfaceNormal);
    const UCapsuleComponent* Capsule = Hero->GetCapsuleComponent();
    const double R = Capsule->GetScaledCapsuleRadius(), Stem = Capsule->GetScaledCapsuleHalfHeight()-R;
    const FVector BodyAxis(0.,0.,FMath::Clamp(Clod.LocalCenter.Z,-Stem,Stem));
    Clod.bOnBody = !OnClod || FVector::DistSquared(BodyAxis,Clod.LocalCenter) <= FMath::Square(R+BodyMudRadius+Seam);
    RefreshContacts();
    RefreshVisuals();
    Hero->ForceNetUpdate();
    return true;
}

void UExpeditionBodyMudComponent::DropLooseClods()
{
    TArray<FClayNode> Nodes;
    TArray<int32> Roots;
    for (int32 I = 0; I < Clods.Num(); ++I)
    {
        FClayNode& Node = Nodes.AddDefaulted_GetRef();
        Node.Center = Clods[I].LocalCenter;
        if (Clods[I].bOnBody) { Node.StepsFromScene = 0; Roots.Add(I); }
    }
    LinkTouching(Nodes);
    Flood(Nodes,Roots);
    for (int32 I = Nodes.Num() - 1; I >= 0; --I)
        if (Nodes[I].StepsFromScene == INDEX_NONE) Clods.RemoveAt(I,1,EAllowShrinking::No);
}

bool UExpeditionBodyMudComponent::Strike(int32 Index)
{
    if (!GetOwner()->HasAuthority() || !Clods.IsValidIndex(Index)) return false;
    const FTransform Frame = GetOwner()->GetActorTransform();
    const FExpeditionClod Removed = Clods[Index];
    const FVector Normal = Frame.TransformVectorNoScale(Removed.LocalNormal);
    const FVector Impact = Frame.TransformPosition(Removed.LocalCenter) + Normal*BodyMudRadius;
    FHitResult Hit(GetOwner(), nullptr, Impact, Normal);
    Hit.TraceStart = Impact + Normal*BodyMudRadius;
    AIslandVoxelEdits::EmitChips(*GetWorld(),Hit,EIslandChipSurface::Sand,BodyMudColor);
    Clods.RemoveAt(Index,1,EAllowShrinking::No);
    DropLooseClods();
    if (Clods.IsEmpty()) Clear();
    else
    {
        RefreshContacts();
        RefreshVisuals();
        GetOwner()->ForceNetUpdate();
    }
    return true;
}

bool UExpeditionBodyMudComponent::Peel()
{
    if (Clods.IsEmpty()) return false;
    const int32 Removed = Clods.IsValidIndex(PeelIndex) ? PeelIndex : Clods.Num()-1;
    const bool bChanged = Strike(Removed);
#if !UE_BUILD_SHIPPING
    if (bChanged && FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        UE_LOG(LogTemp, Display, TEXT("MUD_QA_PEEL player=%s removed=%d remaining=%d anchored=%d"),
            *GetOwner()->GetName(), Removed, Clods.Num(), IsAnchored());
#endif
    return bChanged;
}

void UExpeditionBodyMudComponent::Clear()
{
    if (!GetOwner()->HasAuthority()) return;
    SupportChanges.Remove(SceneChangedHandle);
    SceneChangedHandle.Reset();
    Clods.Reset();
    Feet.Reset();
    PeelIndex = INDEX_NONE;
    if (bAnchored)
    {
        bAnchored = false;
        ApplyAnchored();
    }
    RefreshVisuals();
    GetOwner()->ForceNetUpdate();
    RefreshContacts(); // A neighbour may have depended on this character's real mud bridge.
}

void UExpeditionBodyMudComponent::ApplyAnchored()
{
    if (auto* Hero = Cast<AVoxelCharacter>(GetOwner())) Hero->SetMudAnchored(bAnchored);
}

void UExpeditionBodyMudComponent::RefreshContacts()
{
    RebuildContacts(true);
}

void UExpeditionBodyMudComponent::RebuildContacts(bool bForce)
{
    UWorld* World = GetWorld();
    if (!GetOwner()->HasAuthority() || !World) return;
    TArray<FClayNode> Nodes;
    TArray<int32> Supported;
    struct FResult
    {
        TArray<FExpeditionClayFoot> Feet;
        int32 Shortest = MAX_int32;
        int32 Peel = INDEX_NONE;
    };
    TMap<UExpeditionBodyMudComponent*, FResult> Results;
    const auto* Terrain = AVoxelTerrain::Find(World);
    const auto* Soil = Terrain ? Terrain->GetDensity() : nullptr;
    const double Now = World->GetTimeSeconds();
    for (TActorIterator<AVoxelCharacter> It(World); It; ++It)
    {
        auto* Body = It->FindComponentByClass<UExpeditionBodyMudComponent>();
        if (!Body || Body->Clods.IsEmpty()) continue;
        Body->LastRefreshFrame = GFrameCounter;
        FResult& Result = Results.Add(Body);
        const FTransform Frame = It->GetActorTransform();
        const bool Sample = bForce || Body->NeedsSupportCheck(Now);
        const int32 Start = Nodes.Num();
        for (int32 I = 0; I < Body->Clods.Num(); ++I)
        {
            FClayNode& Node = Nodes.AddDefaulted_GetRef();
            Node.Center = Frame.TransformPosition(Body->Clods[I].LocalCenter);
            Node.Body = Body;
            Node.Index = I;
            if (!Sample) continue;
            FVector Point, Normal;
            if ((Soil && FindSoilFoot(*Soil,Node.Center,Point,Normal))
                || FindStructureFoot(*World,**It,Node.Center,Point,Normal))
            {
                FExpeditionClayFoot& Foot = Result.Feet.AddDefaulted_GetRef();
                Foot.Index = I;
                Foot.LocalPoint = Frame.InverseTransformPosition(Point);
                Foot.LocalNormal = Frame.InverseTransformVectorNoScale(Normal);
                Node.StepsFromScene = 0;
                Supported.Add(Nodes.Num()-1);
            }
        }
        if (Sample)
        {
            Body->SampledFrame = Frame;
            Body->bSupportChanged = false;
            Body->NextSupportCheck = Now + .20;
        }
        else
        {
            Result.Feet = Body->Feet;
            for (const auto& Foot : Body->Feet)
            {
                Nodes[Start + Foot.Index].StepsFromScene = 0;
                Supported.Add(Start + Foot.Index);
            }
        }
    }
    LinkTouching(Nodes);
    Flood(Nodes,Supported);
    // A character is never a conductive node: two isolated chest/boot clumps can
    // only share support if their actual volumes meet, even across different players.
    for (const FClayNode& Node : Nodes)
    {
        if (Node.StepsFromScene == INDEX_NONE || !Node.Body->Clods[Node.Index].bOnBody) continue;
        FResult& Result = Results.FindChecked(Node.Body);
        if (Node.StepsFromScene <= Result.Shortest)
        {
            Result.Shortest = Node.StepsFromScene;
            Result.Peel = Node.Index;
        }
    }
    TArray<UExpeditionBodyMudComponent*> ChangedAnchors;
    for (auto& Entry : Results)
    {
        auto* Body = Entry.Key;
        auto& Result = Entry.Value;
        Body->PeelIndex = Result.Peel;
        const bool Anchored = Result.Peel != INDEX_NONE;
        const bool Changed = !SameFeet(Body->Feet,Result.Feet);
        if (Changed)
        {
            Body->Feet = MoveTemp(Result.Feet);
            Body->RefreshVisuals();
        }
        if (Changed || Anchored != Body->bAnchored) Body->GetOwner()->ForceNetUpdate();
        if (Anchored != Body->bAnchored)
        {
            Body->bAnchored = Anchored;
            ChangedAnchors.Add(Body);
        }
    }
    // Ability cancellation or chest settlement may teleport or clear a character.
    // Publish every result before invoking game mode callbacks.
    for (auto* Body : ChangedAnchors) Body->ApplyAnchored();
}

void UExpeditionBodyMudComponent::RefreshVisuals()
{
    SetComponentTickEnabled(!Clods.IsEmpty());
    if (!GetWorld() || GetNetMode() == NM_DedicatedServer || !Stone || !GetOwner()->GetRootComponent()) return;
    if (!Visuals && !Clods.IsEmpty())
    {
        Visuals = NewObject<UInstancedStaticMeshComponent>(GetOwner());
        Visuals->SetupAttachment(GetOwner()->GetRootComponent());
        Visuals->SetMobility(EComponentMobility::Movable);
        Visuals->SetStaticMesh(Stone);
        Visuals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visuals->SetGenerateOverlapEvents(false);
        Visuals->SetCanEverAffectNavigation(false);
        Visuals->SetReceivesDecals(false);
        if (UMaterialInstanceDynamic* Finish = UMaterialInstanceDynamic::Create(ClayMaterial,this))
        {
            Finish->SetVectorParameterValue(TEXT("Color"),FLinearColor(BodyMudColor));
            Visuals->SetMaterial(0,Finish);
        }
        GetOwner()->AddInstanceComponent(Visuals);
        Visuals->RegisterComponent();
    }
    if (!Visuals) return;
    const FBox Source = Stone->GetBoundingBox();
    const FVector Extent = Source.GetExtent().ComponentMax(FVector(1.));
    const auto* Hero = CastChecked<AVoxelCharacter>(GetOwner());
    bFirstPersonVisuals = Hero->IsInFirstPersonView();
    const FVector Eye = Hero->GetActorTransform().InverseTransformPosition(Hero->GetAimOrigin());
    TArray<FTransform> Instances;
    auto Add = [&](FVector LocalCenter,FVector Normal,FVector Radii)
    {
        // Keep the near-camera face and shoulder clear. Feet, lower clods and extended
        // bridges remain visible in first person so the player can aim at their bond.
        if (bFirstPersonVisuals && FVector::DistSquared(LocalCenter,Eye) < FMath::Square(60.)) return;
        const FQuat Rotation = FRotationMatrix::MakeFromZ(Normal).ToQuat();
        const FVector Scale = Radii / Extent;
        Instances.Emplace(Rotation,LocalCenter - Rotation.RotateVector(Source.GetCenter()*Scale),Scale);
    };
    for (const FExpeditionClod& Clod : Clods)
        Add(Clod.LocalCenter,Clod.LocalNormal,FVector(BodyMudRadius,BodyMudRadius,BodyMudRadius));
    for (const auto& Foot : Feet)
        if (Clods.IsValidIndex(Foot.Index))
            Add(Foot.LocalPoint,Foot.LocalNormal,FVector(BodyMudRadius*1.13,BodyMudRadius*1.05,4.));
    Visuals->ClearInstances();
    Visuals->AddInstances(Instances,false,false,false);
}

void UExpeditionBodyMudComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,TickType,Tick);
    if (GetOwner()->HasAuthority() && LastRefreshFrame != GFrameCounter && NeedsSupportCheck(GetWorld()->GetTimeSeconds()))
        RebuildContacts(false);
    if (Visuals && bFirstPersonVisuals != CastChecked<AVoxelCharacter>(GetOwner())->IsInFirstPersonView())
        RefreshVisuals();
}

void UExpeditionBodyMudComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UExpeditionBodyMudComponent,Clods);
    DOREPLIFETIME(UExpeditionBodyMudComponent,Feet);
    DOREPLIFETIME(UExpeditionBodyMudComponent,bAnchored);
}

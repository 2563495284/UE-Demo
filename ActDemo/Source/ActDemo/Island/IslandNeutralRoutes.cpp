#include "Island/IslandNeutralProp.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapInfo.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandSceneryVoxels.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    const FColor Timber(148,95,59), Tar(63,76,71), Canvas(224,189,133);
    const FColor Sand(224,166,89), Mud(157,100,62), Sea(48,155,169), FoamWhite(207,237,224);
    const FColor Iron(72,94,93), BasaltGrey(99,123,127), WindBlue(113,192,183);
    constexpr double SectionInterval = .13;
    constexpr double PanelFall = .45;
    constexpr double ChannelHalfWidth = 165.;
    constexpr double WindMouthRadius = 145.;

    FVector Across(const FVector& Direction)
    {
        return FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal2D();
    }

    void ShowPart(UStaticMeshComponent& Part, bool bShow)
    {
        if (Part.GetVisibleFlag() == bShow) return;
        Part.SetVisibility(bShow);
        IslandSceneryVoxels::RefreshPart(Part);
    }
}

bool AIslandNeutralProp::ReceiveMudImpact(const FHitResult& Hit)
{
    if (!HasAuthority() || !bBuilt || IsDemolished() || State.Phase >= 3 ||
        (State.Kind != EIslandNeutralKind::SandDepot && State.Kind != EIslandNeutralKind::WindTower)) return false;
    const auto* Source = IslandSceneryVoxels::SourceFor(Hit.GetComponent());
    if (!Source || Source->GetOwner() != this || !Source->ComponentHasTag(TEXT("IslandVoxelPart"))) return false;
    if (State.Kind == EIslandNeutralKind::SandDepot && State.Phase == 2) PrepareSandSections();
    SetPhase(State.Phase + 1);
    return true;
}

bool AIslandNeutralProp::IsSectionComplete(int32 Section) const
{
    return Section >= 0 && Section < GetRouteSectionCount() && (State.CompletedSections & (1u << Section));
}

void AIslandNeutralProp::SetSectionComplete(int32 Section)
{
    State.CompletedSections |= 1u << Section;
    UpdateRouteVisuals(ServerNow());
    ForceNetUpdate();
}

FVector AIslandNeutralProp::GetBridgeDeckPoint(int32 Node) const
{
    if (!State.RouteNodes.IsValidIndex(Node)) return GetActorLocation();
    const FVector Ground = State.RouteNodes[Node];
    const double ToEnd = FMath::Min(FVector::Dist2D(Ground, State.RouteNodes[0]),
        FVector::Dist2D(Ground, State.RouteNodes.Last()));
    // The first and last spans meet the ground. Leave standing headroom over the middle spans.
    return Ground + FVector::UpVector * (12. + FMath::Min(240., ToEnd * .27));
}

FTransform AIslandNeutralProp::BridgePanelPose(int32 Section, double Unfold) const
{
    const FVector A = GetBridgeDeckPoint(Section) - FVector(0,0,12.);
    const FVector B = GetBridgeDeckPoint(Section + 1) - FVector(0,0,12.);
    const FVector Span = B - A;
    FRotator Rotation = Span.Rotation();
    Rotation.Pitch = FMath::Lerp(79., Rotation.Pitch, Unfold);
    return FTransform(Rotation, A + Rotation.Vector() * (Span.Length() * .5));
}

FVector AIslandNeutralProp::GetWindJumpStart(int32 Index) const
{
    const int32 Count = GetRouteSectionCount();
    if (Count == 0 || Index < 0 || Index > 2) return GetActorLocation();
    return State.RouteNodes[Index * Count / 2];
}

FVector AIslandNeutralProp::GetWindJumpLanding(int32 Index) const
{
    if (Index < 2) return GetWindJumpStart(Index + 1);
    const auto& Nodes = State.RouteNodes;
    if (Nodes.Num() < 2) return GetActorLocation();
    FVector Landing = Nodes.Last() + (Nodes.Last()-Nodes[Nodes.Num()-2]).GetSafeNormal2D()*200.;
    const auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (const auto* Density = Terrain ? Terrain->GetDensity() : nullptr)
        Landing.Z = Density->SurfaceAt(FVector2D(Landing)).Get(Landing.Z);
    return Landing;
}

TOptional<FVector> AIslandNeutralProp::WindJumpVelocity(const AVoxelCharacter& Hero) const
{
    if (State.Kind != EIslandNeutralKind::WindTower || IsDemolished() || State.Phase != 3) return {};
    const FVector Foot = Hero.GetActorLocation() - FVector::UpVector * Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for (int32 I = 0; I < 3; ++I)
    {
        if (!IsSectionComplete(FMath::Min(GetRouteSectionCount()-1, I*GetRouteSectionCount()/2))) continue;
        const FVector Mouth = GetWindJumpStart(I);
        if (FVector::DistSquared2D(Foot,Mouth) > FMath::Square(WindMouthRadius) || FMath::Abs(Foot.Z-Mouth.Z)>125.) continue;
        const FVector Delta = GetWindJumpLanding(I) - Foot;
        const double Gravity = FMath::Max(1., double(-Hero.GetCharacterMovement()->GetGravityZ()));
        const double SpeedZ = I == 2 ? 670. : 1260.;
        const double Discriminant = SpeedZ * SpeedZ - 2.*Gravity*Delta.Z;
        if (Discriminant <= 0.) return {};
        const double Flight = (SpeedZ + FMath::Sqrt(Discriminant))/Gravity;
        return FVector(Delta.X/Flight,Delta.Y/Flight,SpeedZ);
    }
    return {};
}

float AIslandNeutralProp::CurrentWalkSpeed(const AVoxelCharacter& Hero, FVector Intent, float BaseSpeed)
{
    const FVector Direction = Intent.GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return BaseSpeed;
    const UCapsuleComponent* Body = Hero.GetCapsuleComponent();
    const FVector Foot = Body->GetComponentLocation() - FVector::UpVector * Body->GetScaledCapsuleHalfHeight();
    for (const FOverlapInfo& Overlap : Body->GetOverlapInfos())
    {
        const auto* Volume = Cast<UBoxComponent>(Overlap.OverlapInfo.GetComponent());
        if (!Volume || !Volume->ComponentHasTag(TEXT("NeutralCurrent"))) continue;
        const FVector Local = Volume->GetComponentTransform().InverseTransformPosition(Foot);
        if (FMath::Abs(Local.Y) <= ChannelHalfWidth && Local.Z >= -140. && Local.Z <= 70. &&
            FVector::DotProduct(Direction,Volume->GetForwardVector().GetSafeNormal2D()) >= .55)
            return BaseSpeed * 2.45f;
    }
    return BaseSpeed;
}

void AIslandNeutralProp::PrepareSandSections()
{
    SandSections.Reset();
    const auto* Terrain = AVoxelTerrain::Find(GetWorld());
    const auto* Density = Terrain ? Terrain->GetDensity() : nullptr;
    for (int32 I=0; I<GetRouteSectionCount(); ++I)
    {
        auto& Centers = SandSections.AddDefaulted_GetRef();
        const FVector A = State.RouteNodes[I], B = State.RouteNodes[I+1];
        const FVector Side = Across(B-A);
        // Row/lane spacing is less than the 75 cm fill radius. Capture each bed just once,
        // so retries cannot repeatedly raise a section already deposited by its neighbour.
        const int32 Rows = FMath::Clamp(FMath::CeilToInt(FVector::Dist2D(A,B)/52.), 1, 11);
        for (int32 Row=0; Row<Rows; ++Row)
        {
            const FVector Mid = FMath::Lerp(A,B,(Row+.5)/Rows);
            const double EndSlope = FMath::Clamp(FMath::Min(FVector::Dist2D(Mid,State.RouteNodes[0]),
                FVector::Dist2D(Mid,State.RouteNodes.Last())) / 480.,0.,1.);
            for (double Lane : {-140.,-70.,0.,70.,140.})
            {
                const FVector Site = Mid + Side*Lane;
                const double Floor = Density ? Density->SurfaceAt(FVector2D(Site)).Get(Site.Z) : Site.Z;
                const double Crown = 102. - 72.*FMath::Square(Lane / 140.);
                const double Top = FMath::Clamp(Mid.Z + Crown*EndSlope - (1.-EndSlope)*53., Floor-55., Floor+104.);
                Centers.Emplace(Site.X,Site.Y,Top);
                if (Top - Floor > 48.) Centers.Emplace(Site.X,Site.Y,Floor+12.);
            }
        }
    }
}

void AIslandNeutralProp::AdvanceRoute(double Now)
{
    const int32 Count = GetRouteSectionCount();
    if (Count == 0 || GetCompletedRouteSections() == Count || Now < NextSectionAt || PendingSandSection != INDEX_NONE) return;
    if (State.Kind == EIslandNeutralKind::SandDepot)
    {
        if (SandSections.Num() != Count) return;
        // A player standing on one future section must not hold up the rest of the embankment.
        for (int32 Tried=0; Tried<Count; ++Tried)
        {
            const int32 Section = NextSandSection;
            NextSandSection = (Section+1)%Count;
            if (IsSectionComplete(Section)) continue;
            if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
            {
                PendingSandSection = Section;
                Mode->QueueHopperSand(*this,SandSections[Section]);
            }
            return;
        }
        return;
    }
    const int32 Next = GetCompletedRouteSections();
    if (Now-State.ChangedAt < (Next+1)*SectionInterval + (State.Kind == EIslandNeutralKind::Bridge ? PanelFall : 0.)) return;
    if (State.Kind == EIslandNeutralKind::Bridge)
    {
        const FTransform End = BridgePanelPose(Next,1.);
        const FVector Half(FVector::Dist(GetBridgeDeckPoint(Next),GetBridgeDeckPoint(Next+1))*.5+6.,110.,13.);
        if (GetWorld()->OverlapAnyTestByObjectType(End.GetLocation(),End.GetRotation(),FCollisionObjectQueryParams(ECC_Pawn),
            FCollisionShape::MakeBox(Half),FCollisionQueryParams(SCENE_QUERY_STAT(BridgePanelLanding),false,this)))
        {
            NextSectionAt = Now + .12;
            return;
        }
    }
    SetSectionComplete(Next);
}

void AIslandNeutralProp::BuildRouteStation()
{
    switch (State.Kind)
    {
        case EIslandNeutralKind::Bridge:
            BuildSail();
            for (int32 I=0; I<3; ++I)
                StationMarkers.Add(Beam({-60.,-194.+I*14.,45.},{0,-194.+I*14.,269.},9.,Canvas,EPieceCollision::ShovelOnly));
            Piece(SheaveWheel,{-58,-190,82},{90,30,90},Iron);
            break;
        case EIslandNeutralKind::Dam:
            for (double Side : {-1.,1.})
            {
                Piece(Basalt,{-10,Side*190,68},{150,96,252},BasaltGrey);
                Piece(Cube,{-10,Side*186,183},{45,55,100},Tar);
            }
            Beam({-14,-192,213},{-14,192,213},25.,Tar);
            for (int32 I=0; I<3; ++I)
                StationMarkers.Add(Piece(Cube,{-16,0,37.+I*53.},{31,347,55},I%2 ? Timber : Tar));
            HitTarget = Piece(SheaveWheel,{-48.,-186.,98.},{78,27,78},Iron,EPieceCollision::Solid,FRotator(0.,90.,0.));
            Spillway = Piece(TidePool,{43.,0.,14.},{105,56,18},Sea,EPieceCollision::Effect);
            break;
        case EIslandNeutralKind::SandDepot:
            for (double X : {-86.,86.}) for (double Y : {-262.,-130.})
                Piece(Cube,{X,Y,132.},{26,29,337},Tar);
            for (double Y : {-266.,-123.})
                Piece(HopperPanel,{0,Y,217},{217,23,120},Sand,EPieceCollision::Solid,FRotator(0,0,Y<-200.?12.:-12.));
            Piece(HopperThroat,{-97,-195,203},{173,24,103},Canvas,EPieceCollision::Solid,FRotator(0,90,0));
            Piece(SluiceChute,{44,-74,134},{192,88,31},Canvas,EPieceCollision::Solid,FRotator(-22,55,0));
            HitTarget = Piece(Cube,{-88,-194,86},{52,127,53},Iron);
            for (int32 I=0; I<3; ++I)
                StationMarkers.Add(Piece(SandHeap,{-87,-194,115.+I*19.},{65.+9*I,112.+6*I,35.},Mud,
                    EPieceCollision::ShovelOnly));
            break;
        case EIslandNeutralKind::WindTower:
            Piece(Basalt,{0,-219,65},{180,153,222},BasaltGrey);
            Piece(Cylinder,{0,-219,220},{108,105,165},WindBlue);
            for (int32 I=0; I<3; ++I)
            {
                Piece(Cylinder,{0,-219,157.+I*54.},{133,126,21},Iron);
                StationMarkers.Add(Piece(SandHeap,{-43,-219,149.+I*49.},{59,90,40},Mud,EPieceCollision::ShovelOnly));
            }
            HitTarget = Piece(Basalt,{-59,-217,78},{100,98,159},BasaltGrey);
            Piece(TidePool,{0,-219,302},{132,132,20},WindBlue,EPieceCollision::ShovelOnly);
            break;
        default: break;
    }
}

void AIslandNeutralProp::BuildRoute()
{
    if (GetRouteSectionCount() == 0) return;
    BuildRouteStation();
    const FTransform Frame = GetActorTransform();
    const AVoxelTerrain* Terrain = HasAuthority() ? AVoxelTerrain::Find(GetWorld()) : nullptr;
    const FVoxelDensity* Ground = Terrain ? Terrain->GetDensity() : nullptr;
    int32 NextFooting = 0;
    const auto SeatOnGround = [&](FVector Local)
    {
        FVector World = Frame.TransformPosition(Local);
        if (HasAuthority())
        {
            const double Height = Ground ? Ground->SurfaceAt(FVector2D(World)).Get(World.Z) : World.Z;
            State.FootingHeights.Add(static_cast<int16>(FMath::RoundToInt(Height)));
        }
        if (State.FootingHeights.IsValidIndex(NextFooting)) World.Z = State.FootingHeights[NextFooting];
        ++NextFooting;
        return Frame.InverseTransformPosition(World);
    };
    for (int32 I=0; I<GetRouteSectionCount(); ++I)
    {
        const FVector A = Frame.InverseTransformPosition(State.RouteNodes[I]);
        const FVector B = Frame.InverseTransformPosition(State.RouteNodes[I+1]);
        const FVector Side = Across(B-A);
        const double Span = FVector::Dist(A,B);
        if (State.Kind == EIslandNeutralKind::Bridge)
        {
            const FVector Hinge = Frame.InverseTransformPosition(GetBridgeDeckPoint(I))-FVector(0,0,14.);
            for (const double Sign : {-1.,1.})
                Beam(A+Side*(91.*Sign)-FVector(0,0,50), Hinge+Side*(91.*Sign),19.,Tar);
            Beam(Hinge-Side*110.,Hinge+Side*110.,19.,Tar);
            const FTransform Raised = BridgePanelPose(I,0.).GetRelativeTransform(Frame);
            RoutePanels.Add(Piece(Cube,Raised.GetLocation(),
                {FVector::Dist(GetBridgeDeckPoint(I),GetBridgeDeckPoint(I+1))+16.,218.,24.},
                I%3==0 ? Tar : Timber,EPieceCollision::ShovelOnly,Raised.Rotator()));
        }
        else if (State.Kind == EIslandNeutralKind::Dam)
        {
            // Pebble banks outline the dry route and its open side exits before the gate is breached.
            if (I%2 == 0)
                for (double Sign : {-1.,1.})
                    Piece(Basalt,SeatOnGround(A+Side*(188.*Sign)),{112,54,38},BasaltGrey,
                        EPieceCollision::ShovelOnly,(B-A).Rotation());
            RoutePanels.Add(Piece(TidePool,(A+B)*.5+FVector(0,0,14.),{Span+75.,343.,26.},Sea,
                EPieceCollision::Effect,(B-A).Rotation()));
            RouteCrests.Add(Piece(FoamCrest,A,{69,220,15},FoamWhite,EPieceCollision::Effect,(B-A).Rotation()));
            UBoxComponent* Channel = NewObject<UBoxComponent>(this,FName(*FString::Printf(TEXT("Current_%d"),I)));
            Channel->SetupAttachment(GetRootComponent());
            Channel->SetRelativeLocationAndRotation((A+B)*.5+FVector(0,0,64.),(B-A).Rotation());
            Channel->SetBoxExtent({Span*.5+20.,ChannelHalfWidth,130.});
            Channel->ComponentTags.Add(TEXT("NeutralCurrent"));
            Channel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Channel->SetCollisionResponseToAllChannels(ECR_Ignore);
            Channel->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
            Channel->SetGenerateOverlapEvents(true);
            AddInstanceComponent(Channel);
            Channel->RegisterComponent();
            ChannelVolumes.Add(Channel);
        }
        else if (State.Kind == EIslandNeutralKind::SandDepot)
        {
            // A line of low-sided delivery chutes makes the destination legible before filling the weight.
            if (I%3 == 0)
            {
                const FVector Bed = SeatOnGround(A-Side*217.);
                Beam(Bed-FVector(0,0,40),Bed+FVector(0,0,166),19.,Timber);
                Piece(SluiceChute,Bed+Side*43.+FVector(0,0,143),{132,79,27},Canvas,
                    EPieceCollision::ShovelOnly,FRotator(-25,Side.Rotation().Yaw,0));
            }
            RouteCrests.Add(Piece(SandHeap,A,{90,87,69},Sand,EPieceCollision::Effect));
        }
    }
    if (State.Kind == EIslandNeutralKind::WindTower)
    {
        for (int32 I=0; I<3; ++I)
        {
            const FVector Mouth = Frame.InverseTransformPosition(GetWindJumpStart(I));
            const FVector Along = Frame.InverseTransformVectorNoScale(GetWindJumpLanding(I)-GetWindJumpStart(I)).GetSafeNormal2D();
            const FVector Pole = SeatOnGround(Mouth + Across(Along)*136.);
            Beam(Pole-FVector(0,0,24),Pole+FVector(0,0,143),12.,Tar);
            Piece(WindPennon,Pole+Along*38.+FVector(0,0,112),{17,79,54},FColor::White,
                EPieceCollision::ShovelOnly,FRotator(0,Along.Rotation().Yaw-90.,0));
            for (int32 J=0; J<6; ++J)
            {
                const double Angle = J * UE_TWO_PI / 6.;
                const FVector Offset(FMath::Cos(Angle)*119., FMath::Sin(Angle)*119.,0.);
                Piece(Basalt,SeatOnGround(Mouth+Offset),{73,54,53},BasaltGrey,EPieceCollision::VisibilityOnly,FRotator(0,J*60.,0));
            }
            RoutePanels.Add(Piece(TideJet,Mouth,{116,120,140},WindBlue,EPieceCollision::Effect));
            RouteCrests.Add(Piece(FoamCrest,Mouth,{116,116,16},FoamWhite,EPieceCollision::Effect));
        }
    }
}

void AIslandNeutralProp::UpdateRouteVisuals(double Now)
{
    if (GetRouteSectionCount() == 0) return;
    const bool bStarted = State.Phase == 3;
    const bool bBreak = State.Kind == EIslandNeutralKind::Bridge || State.Kind == EIslandNeutralKind::Dam;
    for (int32 I=0; I<StationMarkers.Num(); ++I)
        ShowPart(*StationMarkers[I], bBreak ? I >= State.Phase : I < State.Phase);
    if (State.Kind == EIslandNeutralKind::Bridge)
    {
        AnimateSail(Now);
        for (int32 I=0; I<RoutePanels.Num(); ++I)
        {
            const uint32 Bit = 1u << I;
            if (CollisionSections & Bit) continue;
            const bool bSettled = IsSectionComplete(I);
            const double T = bStarted ? FMath::Clamp((Now-State.ChangedAt-I*SectionInterval)/PanelFall,0.,.96) : 0.;
            const FTransform Pose = BridgePanelPose(I,bSettled ? 1. : FMath::Square(T));
            RoutePanels[I]->SetWorldLocationAndRotation(Pose.GetLocation(),Pose.GetRotation());
            if (bSettled)
            {
                SetPieceCollision(*RoutePanels[I],EPieceCollision::Solid);
                IslandSceneryVoxels::RefreshPart(*RoutePanels[I]);
                CollisionSections |= Bit;
            }
        }
    }
    else if (State.Kind == EIslandNeutralKind::Dam)
    {
        const FTransform Frame = GetActorTransform();
        Spillway->SetVisibility(State.Phase > 0 && State.Phase < 3);
        if (State.Phase == 1 || State.Phase == 2)
            Spillway->SetRelativeScale3D({1.05*State.Phase,.56*State.Phase,.18});
        for (int32 I=0; I<RoutePanels.Num(); ++I)
        {
            const bool bFlow = IsSectionComplete(I);
            RoutePanels[I]->SetVisibility(bFlow);
            RouteCrests[I]->SetVisibility(bFlow);
            if (!bFlow) continue;
            if (!(CollisionSections & (1u << I)))
            {
                ChannelVolumes[I]->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                CollisionSections |= 1u << I;
            }
            const double T = FMath::Frac(Now*.72 + I*.22);
            const FVector Wave = FMath::Lerp(State.RouteNodes[I], State.RouteNodes[I+1],T) + FVector(0,0,28.);
            RouteCrests[I]->SetRelativeLocation(Frame.InverseTransformPosition(Wave));
        }
    }
    else if (State.Kind == EIslandNeutralKind::WindTower)
    {
        for (int32 I=0; I<RoutePanels.Num(); ++I)
        {
            const bool bOn = IsSectionComplete(FMath::Min(GetRouteSectionCount()-1,I*GetRouteSectionCount()/2));
            RoutePanels[I]->SetVisibility(bOn);
            RouteCrests[I]->SetVisibility(bOn);
            if (!bOn) continue;
            const FVector Mouth = GetActorTransform().InverseTransformPosition(GetWindJumpStart(I));
            const double H = 113.+43.*FMath::Sin(Now*3.-I*.7);
            RoutePanels[I]->SetRelativeLocation(Mouth+FVector(0,0,H*.5));
            RoutePanels[I]->SetRelativeScale3D({1.18,1.14,H/100.});
            RouteCrests[I]->SetRelativeLocation(Mouth+FVector(0,0,H));
        }
    }
    else if (State.Kind == EIslandNeutralKind::SandDepot)
    {
        for (int32 I=0; I<RouteCrests.Num(); ++I)
        {
            const double Age = Now-State.ChangedAt-I*SectionInterval;
            const bool bPour = bStarted && !IsSectionComplete(I) && Age>=0. && Age<2.;
            RouteCrests[I]->SetVisibility(bPour);
            if (bPour) RouteCrests[I]->SetWorldLocation(State.RouteNodes[I]+FVector(0,0,160.-125.*FMath::Frac(Age*2.3)));
        }
    }
}

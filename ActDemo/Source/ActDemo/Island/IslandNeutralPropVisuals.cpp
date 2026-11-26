#include "Island/IslandNeutralProp.h"
#include "Island/ExpeditionWreckArt.h"
#include "Island/IslandSceneryVoxels.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace NeutralPropColors
{
    const FColor Bark(125,89,68), BarkLight(160,117,77), Leaf(81,144,113), LeafLight(144,169,105);
    const FColor LeafDeep(55,113,99), Fruit(226,150,74), Rock(116,133,132), RockLight(153,159,143);
    const FColor Frame(99,72,60), Ochre(166,106,70), Iron(72,94,93), Sand(228,189,129);
    const FColor Mast(144,104,77), Rope(222,197,153);
    const FColor Lagoon(65,158,165), Crest(203,228,204);
}

void AIslandNeutralProp::SetPieceCollision(UStaticMeshComponent& Mesh, EPieceCollision Collision)
{
    Mesh.SetCollisionResponseToAllChannels(Collision == EPieceCollision::Solid ? ECR_Block : ECR_Ignore);
    Mesh.SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    if (Collision == EPieceCollision::ShovelOnly)
        Mesh.SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
    else if (Collision == EPieceCollision::VisibilityOnly)
        Mesh.SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Mesh.SetCollisionEnabled(Collision == EPieceCollision::Effect ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
}

UStaticMeshComponent* AIslandNeutralProp::Piece(UStaticMesh* Shape, FVector Center, FVector Dimensions,
    FColor Color, EPieceCollision Collision, FRotator Rotation, USceneComponent* Parent)
{
    // CharacterMovement replicates the component a player stands on. Give the
    // locally reconstructed deck/trunk the same network-resolvable path on all peers.
    const FName Name(*FString::Printf(TEXT("NeutralPiece_%02d"), NextPieceIndex++));
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, Name);
    Mesh->SetNetAddressable();
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetupAttachment(Parent ? Parent : GetRootComponent());
    Mesh->SetStaticMesh(Shape);
    Mesh->SetRelativeLocationAndRotation(Center, Rotation);
    Mesh->SetRelativeScale3D(Dimensions / 100.);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetReceivesDecals(false);
    Mesh->ComponentTags.Add(Collision == EPieceCollision::Effect ? TEXT("IslandMechanismEffect") : TEXT("IslandVoxelPart"));
    SetPieceCollision(*Mesh, Collision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(true);
    using namespace NeutralPropColors;
    // Different finishes can share the same white tint when the pigment is in the mesh.
    float Roughness=.82f, Metallic=0.f;
    if (State.Kind==EIslandNeutralKind::Tide && Collision==EPieceCollision::Effect)
        Roughness=.34f;
    else if (Shape==Basalt || Color==Rock || Color==RockLight)
    {
        Mesh->ComponentTags.Add(TEXT("IslandChipRock"));
        Roughness=Shape==Basalt ? .62f : .88f;
    }
    else if (Color==Iron)
    {
        Roughness=.48f;
        Metallic=.53f;
    }
    else if (Shape==WindSidePanel || Shape==SignalCanvas || Shape==RolledCanvas || Shape==WindPennon || Color==Rope)
    {
        Mesh->ComponentTags.Add(TEXT("IslandChipCanvas"));
        Roughness=.95f;
    }
    else if (Shape==LeafCrown || Shape==SprayDrop)
    {
        Mesh->ComponentTags.Add(TEXT("IslandChipLeaf"));
        Roughness=.62f;
    }
    else if (Shape==SandHeap || Shape==BarkSpar)
    {
        if (Shape==SandHeap) Mesh->ComponentTags.Add(TEXT("IslandChipSand"));
        Roughness=.93f;
    }
    const FPaint* Existing = Paints.FindByPredicate([&](const FPaint& P)
    {
        return P.Color==Color && P.Roughness==Roughness && P.Metallic==Metallic;
    });
    if (Existing)
    {
        Mesh->SetMaterial(0, Existing->Material);
    }
    else if (UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(WreckFinish ? WreckFinish : PaintBase, this))
    {
        Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color));
        Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
        Material->SetScalarParameterValue(TEXT("Metallic"), Metallic);
        Mesh->SetMaterial(0, Material);
        Paints.Add({Color, Roughness, Metallic, Material});
    }
    AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

UStaticMeshComponent* AIslandNeutralProp::Beam(FVector A, FVector B, double Width, FColor Color,
    EPieceCollision Collision, USceneComponent* Parent)
{
    // UE composes component scale axis by axis. Aim the cylinder after scaling its endpoints
    // so angled spars and the crossbar still join their posts on wide, low structures.
    const FVector ParentScale = (Parent ? Parent : GetRootComponent())->GetComponentScale();
    const FVector Span = (B-A)*ParentScale;
    const double Length = Span.Size() / ParentScale.Z;
    UStaticMesh* Spar = State.Kind == EIslandNeutralKind::Tree ? BarkSpar.Get() : Cylinder.Get();
    return Piece(Spar, (A+B)*.5, FVector(Width,Width,Length), Color, Collision,
        FRotationMatrix::MakeFromZ(Span).Rotator(), Parent);
}

void AIslandNeutralProp::BuildTree()
{
    using namespace NeutralPropColors;
    // Only the two supports sit on the ground. The span in between stays editable.
    Piece(Ball, {-9,7,-51}, {170,157,184}, Rock);
    Piece(Ball, {485,0,12}, {125,172,121}, Rock, EPieceCollision::Solid, FRotator(5,12,0));
    Piece(Ball, {495,46,-5}, {98,97,111}, RockLight);
    HitTarget = Piece(BarkSpar, {0,0,52}, {146,139,144}, BarkLight);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        const double A = Index * UE_TWO_PI / 5.;
        const FVector End(87.*FMath::Cos(A),87.*FMath::Sin(A),-12.);
        Beam({0,0,33}, End, 26., Bark, EPieceCollision::ShovelOnly);
    }
    MovingTree = NewObject<USceneComponent>(this, TEXT("TreePivot"));
    MovingTree->SetupAttachment(GetRootComponent());
    MovingTree->SetRelativeLocation({0,0,90});
    AddInstanceComponent(MovingTree);
    MovingTree->RegisterComponent();
    TreeSolids.Add(Beam({0,0,-9},{0,0,485},92.,Bark,EPieceCollision::Solid,MovingTree));
    TreeSolids.Add(Beam({0,0,225},{-27,102,361},40.,BarkLight,EPieceCollision::Solid,MovingTree));
    TreeSolids.Add(Beam({0,0,300},{19,-75,421},39.,BarkLight,EPieceCollision::Solid,MovingTree));
    Beam({-8,18,321},{77,43,446},28.,Bark,EPieceCollision::ShovelOnly,MovingTree);
    Piece(LeafCrown, {2,99,379},{196,140,114},LeafDeep,EPieceCollision::ShovelOnly,FRotator(-6,19,-7),MovingTree);
    Piece(LeafCrown, {6,-78,440},{211,155,108},Leaf,EPieceCollision::ShovelOnly,FRotator(-4,-29,10),MovingTree);
    Piece(LeafCrown, {54,22,465},{265,193,112},Leaf,EPieceCollision::ShovelOnly,FRotator(4,23,8),MovingTree);
    Piece(LeafCrown, {73,24,509},{212,158,94},LeafLight,EPieceCollision::ShovelOnly,FRotator(6,-13,5),MovingTree);
    Piece(SprayDrop, {36,108,338},{27,25,41}, Fruit, EPieceCollision::ShovelOnly, FRotator(0,15,12), MovingTree);
    Piece(SprayDrop, {51,101,343},{21,21,29}, Fruit, EPieceCollision::ShovelOnly, FRotator(0,-9,-7), MovingTree);
    // An exposed cut at the tree root marks the first strike from either approach.
    for (double Side : {-1.,1.})
        Piece(Cube,{Side*50.,0.,60.},{25.,42.,14.},Sand,EPieceCollision::VisibilityOnly,FRotator(0,0,Side*14.))
            ->ComponentTags.Add(TEXT("TreeNotch"));
}

void AIslandNeutralProp::AnimateTree(double Age)
{
    if (!MovingTree) return;
    double Angle = 0.;
    if (State.Phase == 1)
        Angle = 5. + 7. * FMath::Exp(-Age * 3.2) * FMath::Sin(Age * 13.);
    else if (State.Phase == 2)
        Angle = FMath::Lerp(5.,90.,FMath::Square(FMath::Clamp(Age/.95,0.,1.)));
    else if (State.Phase == 3)
        Angle = 90.;
    MovingTree->SetRelativeRotation(FRotator(-Angle,0,0));
    const bool bSolid = State.Phase != 2;
    if (bTreeSolidsEnabled != bSolid)
    {
        for (UStaticMeshComponent* Mesh : TreeSolids)
        {
            SetPieceCollision(*Mesh, bSolid ? EPieceCollision::Solid : EPieceCollision::ShovelOnly);
            IslandSceneryVoxels::RefreshPart(*Mesh);
        }
        bTreeSolidsEnabled = bSolid;
    }
}

void AIslandNeutralProp::BuildShip()
{
    ExpeditionWreckArt::Build(*this, WreckFinish ? WreckFinish : PaintBase);
}

void AIslandNeutralProp::MulticastWreckChips_Implementation(FName SourceName,int32 Finish,
    FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal)
{
    if (GetNetMode()==NM_DedicatedServer) return;
    if (auto* Part=FindObjectFast<UProceduralMeshComponent>(this,SourceName))
        ExpeditionWreckArt::PlayChips(*Part,Finish,Point,Normal,SprayDrop,WindCanvas);
}

void AIslandNeutralProp::BuildHopper()
{
    using namespace NeutralPropColors;
    const FVector Leg = {-135.,0.,0.};
    for (double X : {-55.,55.})
        for (double Y : {-94.,94.})
        {
            Piece(Cube, Leg+FVector(X,Y,72.), {22,22,227}, Frame);
            Piece(Cube, Leg+FVector(X,Y,169.), {31,31,27}, Iron, EPieceCollision::ShovelOnly);
        }
    for (const double Side : {-1.,1.})
    {
        Beam({-190.,Side*96.,22.},{-79.,Side*96.,167.},15.,Ochre);
        Piece(HopperPanel, {-131.,Side*97.,213.}, {173,17,100}, FColor::White, EPieceCollision::Solid, FRotator(0,0,-Side*18.));
        Piece(Cube, {-134.,Side*113.,268.}, {186,16,15}, Iron);
    }
    Piece(Cube, {-211.,0.,217.}, {17,200,102},Ochre,EPieceCollision::Solid,FRotator(12,0,0));
    Piece(HopperThroat, {-55.,0.,222.}, {209,24,105},FColor::White,EPieceCollision::Solid,FRotator(0,90,0));
    Piece(Cube, {-133.,0.,179.}, {146,150,14}, Frame);
    SandLoad = Piece(SandHeap, {-135.,0.,255.}, {178,186,80}, Sand, EPieceCollision::ShovelOnly);
    Piece(SluiceChute, {9.,0.,142.}, {148,70,15}, FColor::White, EPieceCollision::Solid, FRotator(-24,0,0));
    for (const double Side : {-1.,1.})
        Piece(Cube, {8.,Side*35.,150.}, {151,8,21}, Frame, EPieceCollision::Solid, FRotator(-24,0,0));
    Gate = Piece(Cube, {-53.,0.,179.}, {16,73,58}, Iron);
    // Pin protrudes toward the player, well clear of the sand landing zone.
    HitTarget = Beam({-95.,-100.,151.},{-95.,-171.,97.},34.,Sand);
    for (int32 I=0; I<5; ++I)
        SandSpill.Add(Piece(Ball, FVector::ZeroVector, {15,13,15}, Sand, EPieceCollision::Effect));
    // The sector wheels flank the discharge gate. Its aisle and pull-pin remain clear.
    for (double Side : {-1.,1.})
        HopperWheels.Add(Piece(SheaveWheel,{-74.,Side*111.,216.},{63,19,94},
            FColor::White, EPieceCollision::ShovelOnly));
}

void AIslandNeutralProp::AnimateHopper(double Age)
{
    const bool bReleased = State.Phase != 0;
    const double Open = bReleased ? (State.Phase == 2 ? 1. : FMath::Clamp(Age/.45,0.,1.)) : 0.;
    Gate->SetRelativeLocation({-53.,0.,179.+53.*Open});
    for (UStaticMeshComponent* Wheel : HopperWheels)
        Wheel->SetRelativeRotation(FRotator(-73.*Open,0.,0.));
    const bool bLoaded = State.Phase != 2 && Open < .99;
    SandLoad->SetVisibility(bLoaded);
    SandLoad->SetCollisionEnabled(bLoaded ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    IslandSceneryVoxels::RefreshPart(*SandLoad);
    SandLoad->SetRelativeLocation({-135.,0.,255.-54.*Open});
    HitTarget->SetVisibility(!bReleased);
    HitTarget->SetCollisionEnabled(bReleased ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
    IslandSceneryVoxels::RefreshPart(*HitTarget);
    for (int32 I=0; I<SandSpill.Num(); ++I)
    {
        const double T = FMath::Fmod(Age*1.7 + .19*I,1.);
        SandSpill[I]->SetVisibility(State.Phase == 1);
        SandSpill[I]->SetRelativeLocation({-35.+171.*T, FMath::Sin(I*4.8)*21.,179.-145.*T*T});
    }
}

void AIslandNeutralProp::BuildSail()
{
    using namespace NeutralPropColors;
    for (const double Side : {-1.,1.})
    {
        Beam({0,Side*217.,-76.},{0,Side*217.,309.},20.,Mast);
        Piece(Cylinder,{0,Side*217.,277.},{27,27,18},Rope,EPieceCollision::ShovelOnly);
        Beam({0,Side*217.,172.},{64.,Side*225.,-4.},11.,Mast);
    }
    Beam({0,-239.,291.},{0,239.,291.},19.,Mast);
    Beam({-10.,-181.,292.},{-29.,-169.,85.},7.,Rope,EPieceCollision::ShovelOnly);
    HitTarget = Piece(Ball, {-30,-169,82},{24,27,34},Rope);
    HitTarget->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    for (int32 I=0; I<3; ++I)
    {
        Cloth.Add(Piece(I==1 ? SignalCanvas : WindSidePanel,{0,0,250.},{8,135,251},
            FColor::White,EPieceCollision::VisibilityOnly));
        Cloth.Last()->ComponentTags.Add(TEXT("IslandSailPanel"));
        Cloth.Add(Piece(Cube,{0,0,250.},{9,135,7},Rope,EPieceCollision::ShovelOnly));
    }
    SailRoll = Piece(RolledCanvas,{0,0,288.},{35,433,50},FColor::White,EPieceCollision::ShovelOnly);
    // The halyard sheave and the split pennon identify the frame even with its
    // screen furled. Cloth passes are still driven by the original three panels.
    Piece(SheaveWheel,{-11.,-217.,267.},{42,13,60},FColor::White,
        EPieceCollision::ShovelOnly,FRotator(0,90,0));
    Piece(Cylinder,{0,217.,333.},{14,14,92}, Mast, EPieceCollision::ShovelOnly);
    Piece(WindPennon,{0,177.,354.},{22,81,47},FColor::White,
        EPieceCollision::ShovelOnly,FRotator(0,180,0));
    ClothVolume = NewObject<UBoxComponent>(this, TEXT("SailPassage"));
    ClothVolume->SetupAttachment(GetRootComponent());
    ClothVolume->SetRelativeLocation({0,0,140});
    ClothVolume->SetBoxExtent({24,199,130});
    ClothVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ClothVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    ClothVolume->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    ClothVolume->SetGenerateOverlapEvents(true);
    ClothVolume->OnComponentBeginOverlap.AddDynamic(this,&AIslandNeutralProp::TouchSail);
    AddInstanceComponent(ClothVolume);
    ClothVolume->RegisterComponent();
}

void AIslandNeutralProp::AnimateSail(double Now)
{
    if (Cloth.IsEmpty()) return;
    const double Age = FMath::Max(0.,Now-State.ChangedAt);
    const double Travel = FMath::Clamp(Age/.36,0.,1.);
    const double Open = State.Phase == 1 ? Travel : 1.-Travel;
    const double Life = FMath::Clamp((Now-State.RippleAt)/.68,0.,1.);
    const double Length = 19.+232.*Open;
    SailRoll->SetRelativeScale3D({.35-.17*Open,4.33,.50-.26*Open});
    for (int32 I=0; I<3; ++I)
    {
        const double Y = (I-1)*133.;
        const double Factor = FMath::Max(.15,1.-.7*FMath::Abs(I-1-State.RippleSide));
        const double Bend = 18.*FMath::Sin(Life*UE_TWO_PI)*FMath::Square(1.-Life)*Factor*Open;
        const FRotator Rotation(Bend,0,0);
        const FVector Top(0,Y,279.);
        Cloth[I*2]->SetRelativeRotation(Rotation);
        Cloth[I*2]->SetRelativeLocation(Top+Rotation.RotateVector(FVector(0,0,-Length*.5)));
        Cloth[I*2]->SetRelativeScale3D({.08,1.35,Length/100.});
        Cloth[I*2+1]->SetRelativeRotation(Rotation);
        Cloth[I*2+1]->SetRelativeLocation(Top+Rotation.RotateVector(FVector(0,0,-Length+10.)));
    }
}

void AIslandNeutralProp::BuildTide()
{
    using namespace NeutralPropColors;
    for (int32 Mouth=0; Mouth<2; ++Mouth)
    {
        const FVector Center = GetActorTransform().InverseTransformPosition(GetTideMouth(Mouth));
        for (int32 I=0; I<8; ++I)
        {
            const double T = I*UE_TWO_PI/8.;
            const FVector P = Center + FVector(FMath::Cos(T)*81.,FMath::Sin(T)*81.,1.);
            // Leave the centre ray and the terrain fill brush unobstructed.
            TideLips.Add(Piece(Basalt,P,{51.,69.,35.+(I%3)*7.},FColor::White,EPieceCollision::VisibilityOnly,FRotator(0,I*45.,0)));
        }
        Jets.Add(Piece(TideJet,Center,{55,58,45},Lagoon,EPieceCollision::Effect));
        Foam.Add(Piece(FoamCrest,Center,{55,52,14},Crest,EPieceCollision::Effect));
        for (int32 I=0; I<4; ++I)
            Spray.Add(Piece(SprayDrop,Center,{11,11,17},I%2 ? Crest : Lagoon,EPieceCollision::Effect));
    }
    Spillway = Piece(Ball, {-89,0,11},{174,36,22},Lagoon,EPieceCollision::Effect);
    for (int32 Mouth=0; Mouth<2; ++Mouth)
    {
        const FVector Base=GetActorTransform().InverseTransformPosition(GetTideMouth(Mouth));
        // The shallow pool is water and carries neither a character nor a shovel trace.
        Piece(TidePool,Base+FVector(0,0,1.5),{145,145,6},FColor::White,EPieceCollision::Effect);
    }
}

void AIslandNeutralProp::AnimateTide(double Now)
{
    const double T = TideCycle(Now);
    const double Surge = FMath::Clamp((T-.55)/.48,0.,1.) * FMath::Clamp((2.22-T)/.52,0.,1.);
    const double Bubble = .5+.5*FMath::Sin(T*18.);
    for (int32 I=0; I<2; ++I)
    {
        const bool bPlugged = (State.PlugMask & (1u<<I)) != 0 || !HasTideLip(I);
        const bool bHigh = !bPlugged && State.PlugMask != 0;
        const FVector Base = GetActorTransform().InverseTransformPosition(GetTideMouth(I));
        const double Height = 12. + Surge*(bHigh ? 231. : 54.);
        Jets[I]->SetVisibility(!bPlugged);
        Foam[I]->SetVisibility(!bPlugged);
        Jets[I]->SetRelativeLocation(Base+FVector(0,0,Height*.45));
        Jets[I]->SetRelativeScale3D({1.16+.14*Surge,1.12+.14*Surge,Height/100.});
        Foam[I]->SetRelativeLocation(Base+FVector(0,0,Height*.92));
        Foam[I]->SetRelativeScale3D({.72+.19*Bubble,.68+.19*Bubble,.15+.08*Bubble});
        // Four small ballistic crests make the lift direction visible at a
        // glance, even before a character jumps into the concentrated jet.
        for (int32 J=0; J<4; ++J)
        {
            auto* Drop = Spray[I*4+J].Get();
            const double Flight = FMath::Fmod(T*1.45+.24*J,1.);
            const double Azimuth = J*UE_HALF_PI + .6*I;
            const double Radius = 24.+Flight*(bHigh ? 73. : 45.);
            const double Arc = 4.*Flight*(1.-Flight)*(bHigh ? 53. : 27.);
            Drop->SetVisibility(!bPlugged);
            Drop->SetRelativeLocation(Base+FVector(FMath::Cos(Azimuth)*Radius,FMath::Sin(Azimuth)*Radius,
                Height*.78+Arc-Flight*Flight*Height*.20));
        }
    }
    Spillway->SetVisibility(State.PlugMask == 3 && (HasTideLip(0) || HasTideLip(1)));
    Spillway->SetRelativeScale3D({1.5+.29*Surge,.4+.1*Surge,.15});
}

#include "Island/IslandNeutralProp.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace NeutralPropColors
{
    const FColor Bark(114,68,45), BarkLight(161,104,62), Leaf(54,132,75), LeafLight(108,177,96);
    const FColor Rock(105,115,121), RockLight(132,143,147);
    const FColor Hull(167,71,55), HullLight(198,96,66), Frame(85,56,50), Deck(181,117,86);
    const FColor Ochre(202,110,46), OchreLight(222,142,65), Iron(79,83,87), Sand(235,199,145);
    const FColor Mast(112,84,67), Rope(239,222,183), Violet(115,78,151), VioletLight(152,114,181);
    const FColor Shale(78,97,106), Slate(113,137,143), Lagoon(57,157,184), Crest(213,240,235);
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
    SetPieceCollision(*Mesh, Collision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(true);
    const FPaint* Existing = Paints.FindByPredicate([Color](const FPaint& P){ return P.Color == Color; });
    if (Existing)
    {
        Mesh->SetMaterial(0, Existing->Material);
    }
    else if (UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PaintBase, this))
    {
        Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color));
        Mesh->SetMaterial(0, Material);
        Paints.Add({Color, Material});
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
    return Piece(Cylinder, (A+B)*.5, FVector(Width,Width,Length), Color, Collision,
        FRotationMatrix::MakeFromZ(Span).Rotator(), Parent);
}

void AIslandNeutralProp::BuildTree()
{
    using namespace NeutralPropColors;
    // Only the two supports sit on the ground. The span in between stays editable.
    Piece(Ball, {-9,7,-51}, {170,157,184}, Rock);
    Piece(Ball, {485,0,12}, {125,172,121}, Rock, EPieceCollision::Solid, FRotator(5,12,0));
    Piece(Ball, {495,46,-5}, {98,97,111}, RockLight);
    HitTarget = Piece(Cylinder, {0,0,52}, {123,123,144}, BarkLight);
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
    TreeSolids.Add(Beam({0,0,225},{-22,85,363},36.,BarkLight,EPieceCollision::Solid,MovingTree));
    TreeSolids.Add(Beam({0,0,300},{16,-67,423},35.,BarkLight,EPieceCollision::Solid,MovingTree));
    Beam({-10,25,300},{50,42,394},21.,Bark,EPieceCollision::ShovelOnly,MovingTree);
    Piece(Ball, {25,80,393},{188,155,122},Leaf,EPieceCollision::ShovelOnly,FRotator(8,23,4),MovingTree);
    Piece(Ball, {0,-69,442},{195,165,138},Leaf,EPieceCollision::ShovelOnly,FRotator(-8,-23,7),MovingTree);
    Piece(Ball, {32,12,487},{230,183,144},LeafLight,EPieceCollision::ShovelOnly,FRotator(0,41,12),MovingTree);
    Piece(Ball, {58,17,528},{154,137,100},Leaf,EPieceCollision::ShovelOnly,FRotator(0,-20,-4),MovingTree);
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
            SetPieceCollision(*Mesh, bSolid ? EPieceCollision::Solid : EPieceCollision::ShovelOnly);
        bTreeSolidsEnabled = bSolid;
    }
}

void AIslandNeutralProp::BuildShip()
{
    using namespace NeutralPropColors;
    // +X crosses the wreck. The lower central bay consists entirely of island
    // voxels; the lintels above the open gunports carry the walkable upper deck.
    for (const double Side : {-1.,1.})
    {
        for (const double End : {-1.,1.})
        {
            for (int32 Row = 0; Row < 4; ++Row)
            {
                const double X = Side * (137. + 8. * Row);
                Piece(Cube, {X,End*184.,-130.+Row*67.}, {23,168,69}, Row%2 ? HullLight : Hull);
                Piece(Cube, {Side*(100.+8.*Row), End*310.,-129.+Row*67.}, {24,135,69},
                    Row%2 ? Hull : HullLight, EPieceCollision::Solid, FRotator(0,Side*End*29.,0));
            }
            Beam({Side*130.,End*108.,-148.},{Side*169.,End*108.,150.},19.,Frame);
            Beam({Side*121.,End*255.,-115.},{Side*163.,End*255.,146.},20.,Frame);
            Piece(Cube, {Side*168.,End*183.,138.}, {30,169,22}, Frame);
        }
        Piece(Cube, {Side*170.,0,132.}, {27,210,31}, HullLight);
    }
    for (int32 Plank = -5; Plank <= 5; ++Plank)
    {
        // Leave a collapsed central hatch large enough to stand and shape the
        // original sand. The remaining fore and aft decks are solid landings.
        if (FMath::Abs(Plank) <= 2) continue;
        const double Width = 324. - FMath::Max(0,FMath::Abs(Plank)-3)*58.;
        Piece(Cube, {0,Plank*59.,146.}, {Width,57,17}, Plank%2 ? Deck : HullLight);
    }
    // End panels close only the narrow bow and stern; both side passages are genuinely open.
    for (const double End : {-1.,1.})
    {
        Piece(Cube, {0,End*365.,6.}, {184,32,250}, Hull, EPieceCollision::Solid, FRotator(0,0,End*8.));
        Beam({0,End*329.,100.},{0,End*394.,192.},25.,Frame);
    }
    Beam({38,217.,153.},{67,220.,590.},29.,Frame);
    Beam({59,145.,465.},{59,295.,489.},16.,Frame);
    for (double Z : {187.,202.})
        Piece(Cylinder, {39,218.,Z},{29,29,8},Rope,EPieceCollision::ShovelOnly);
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
        Piece(Cube, {-131.,Side*97.,213.}, {173,17,100}, OchreLight, EPieceCollision::Solid, FRotator(0,0,-Side*18.));
        Piece(Cube, {-134.,Side*113.,268.}, {186,16,15}, Iron);
    }
    Piece(Cube, {-211.,0.,217.}, {17,200,102},Ochre,EPieceCollision::Solid,FRotator(12,0,0));
    Piece(Cube, {-57.,0.,224.}, {17,201,87},Ochre,EPieceCollision::Solid,FRotator(-13,0,0));
    Piece(Cube, {-133.,0.,179.}, {146,150,14}, Frame);
    SandLoad = Piece(Ball, {-135.,0.,247.}, {161,183,59}, Sand, EPieceCollision::ShovelOnly);
    Piece(Cube, {9.,0.,142.}, {148,70,11}, OchreLight, EPieceCollision::Solid, FRotator(-24,0,0));
    for (const double Side : {-1.,1.})
        Piece(Cube, {8.,Side*35.,150.}, {151,10,21}, Ochre, EPieceCollision::Solid, FRotator(-24,0,0));
    Gate = Piece(Cube, {-53.,0.,179.}, {16,73,58}, Iron);
    // Pin protrudes toward the player, well clear of the sand landing zone.
    HitTarget = Beam({-95.,-100.,151.},{-95.,-171.,97.},34.,Sand);
    for (int32 I=0; I<5; ++I)
        SandSpill.Add(Piece(Ball, FVector::ZeroVector, {15,13,15}, Sand, EPieceCollision::Effect));
}

void AIslandNeutralProp::AnimateHopper(double Age)
{
    const bool bReleased = State.Phase != 0;
    const double Open = bReleased ? (State.Phase == 2 ? 1. : FMath::Clamp(Age/.45,0.,1.)) : 0.;
    Gate->SetRelativeLocation({-53.,0.,179.+53.*Open});
    const bool bLoaded = State.Phase != 2 && Open < .99;
    SandLoad->SetVisibility(bLoaded);
    SandLoad->SetCollisionEnabled(bLoaded ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    SandLoad->SetRelativeLocation({-135.,0.,247.-46.*Open});
    HitTarget->SetVisibility(!bReleased);
    HitTarget->SetCollisionEnabled(bReleased ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
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
        Cloth.Add(Piece(Cube,{0,0,250.},{7,135,20},I==1 ? VioletLight : Violet,EPieceCollision::VisibilityOnly));
        Cloth.Add(Piece(Cube,{0,0,250.},{9,135,7},Rope,EPieceCollision::ShovelOnly));
    }
    Beam({0,-209.,282.},{0,209.,282.},31.,Violet,EPieceCollision::ShovelOnly);
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
            Piece(Cube,P,{40.,64.,27.+(I%3)*5.},I%2 ? Slate : Shale,EPieceCollision::VisibilityOnly,FRotator(0,I*45.,0));
        }
        Jets.Add(Piece(Ball,Center,{55,58,45},Lagoon,EPieceCollision::Effect));
        Foam.Add(Piece(Ball,Center,{55,52,14},Crest,EPieceCollision::Effect));
        for (int32 I=0; I<4; ++I)
            Spray.Add(Piece(Ball,Center,{11,11,17},I%2 ? Crest : Lagoon,EPieceCollision::Effect));
    }
    Spillway = Piece(Ball, {-89,0,11},{174,36,22},Lagoon,EPieceCollision::Effect);
}

void AIslandNeutralProp::AnimateTide(double Now)
{
    const double T = TideCycle(Now);
    const double Surge = FMath::Clamp((T-.55)/.48,0.,1.) * FMath::Clamp((2.22-T)/.52,0.,1.);
    const double Bubble = .5+.5*FMath::Sin(T*18.);
    for (int32 I=0; I<2; ++I)
    {
        const bool bPlugged = (State.PlugMask & (1u<<I)) != 0;
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
    Spillway->SetVisibility(State.PlugMask == 3);
    Spillway->SetRelativeScale3D({1.5+.29*Surge,.4+.1*Surge,.15});
}

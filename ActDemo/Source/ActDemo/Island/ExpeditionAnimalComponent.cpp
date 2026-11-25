#include "Island/ExpeditionAnimalComponent.h"

#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Island/IslandState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

UExpeditionAnimalComponent::UExpeditionAnimalComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Pointed(TEXT("/Engine/BasicShapes/Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    Ball = Sphere.Object;
    Cone = Pointed.Object;
    Base = Material.Object;
}

UMaterialInstanceDynamic* UExpeditionAnimalComponent::Paint(FColor Color)
{
    const uint32 Key = Color.ToPackedARGB();
    if (auto* Found = Paints.Find(Key)) return *Found;
    UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
    M->SetVectorParameterValue(TEXT("Color"),FLinearColor(Color));
    Paints.Add(Key,M);
    return M;
}

UStaticMeshComponent* UExpeditionAnimalComponent::Shape(USceneComponent* Parent, const FVector& At,
    const FVector& Size, FColor Color, UStaticMesh* Geometry)
{
    auto* Mesh = NewObject<UStaticMeshComponent>(GetOwner());
    Mesh->SetupAttachment(Parent);
    Mesh->SetStaticMesh(Geometry ? Geometry : Ball.Get());
    Mesh->SetRelativeLocation(At);
    Mesh->SetRelativeScale3D(Size / 100.);
    Mesh->SetMaterial(0,Paint(Color));
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    GetOwner()->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

#if !UE_BUILD_SHIPPING
bool UExpeditionAnimalComponent::ShowsOutfit(bool bDisguised) const
{
    return FogSuit && AnimalFeatures && FogSuit->IsVisible() == bDisguised
        && AnimalFeatures->IsVisible() == !bDisguised;
}
#endif

void UExpeditionAnimalComponent::Build()
{
    auto* Hero = CastChecked<AVoxelCharacter>(GetOwner());
    const auto* PS = Hero->GetPlayerState<AIslandPlayerState>();
    if (!PS) return;
    auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Terrain || Terrain->Layout != EVoxelLayout::Expedition)
    {
        SetComponentTickEnabled(false);
        return;
    }
    const int32 Species = uint32(PS->GetPlayerId()) % 4; // Pig, monkey, fox, calf.
    const FColor Coats[] = {{229,194,188},{186,156,136},{215,167,135},{220,216,208}};
    const FColor Fur = Coats[Species], Cream(246,227,202), Dark(69,61,65);
    Body = NewObject<USceneComponent>(Hero);
    Body->SetupAttachment(Hero->GetRootComponent());
    Hero->AddInstanceComponent(Body);
    Body->RegisterComponent();
    Hero->GetMesh()->SetVisibility(false,true);
    auto MakeOutfit = [&]
    {
        auto* Outfit = NewObject<USceneComponent>(Hero);
        Outfit->SetupAttachment(Body);
        Hero->AddInstanceComponent(Outfit);
        Outfit->RegisterComponent();
        return Outfit;
    };
    AnimalFeatures = MakeOutfit();
    FogSuit = MakeOutfit();
    // Every species uses the same hood, broad oilskin collar and dark visor in the fog.
    const FColor Canvas(143,161,155), Hood(176,192,182), Visor(52,70,71);
    Shape(FogSuit,{0,0,56},{78,78,80},Canvas);
    Shape(FogSuit,{1,0,111},{83,82,79},Hood);
    Shape(FogSuit,{0,0,81},{91,89,22},Canvas);
    Shape(FogSuit,{36,0,117},{17,61,25},Visor);
    FogSuit->SetVisibility(false,true);
    Shape(AnimalFeatures,{0,0,57},{70,76,74},Fur);
    // The face pieces share the unscaled body parent, so their dimensions remain in centimetres.
    Shape(AnimalFeatures,{4,0,112},{81,88,78},Fur);
    CheekMud=Shape(Body,{37,24,111},{9,25,15},FColor(149,95,60));
    CheekMud->SetRelativeRotation(FRotator(0.,29.,-12.));
    CheekMud->SetVisibility(false);
    for (int32 I=0; I<2; ++I)
    {
        const int32 Side = I ? 1 : -1;
        Shape(AnimalFeatures,{40.,Side*17.,122.},{9,10,15},Dark);
        Shape(AnimalFeatures,{44.,Side*17.,125.},{3,4,4},FColor::White);
        const FVector EarSize = Species == 1 ? FVector(21,30,34) : FVector(27,25,38);
        auto* Ear = Shape(AnimalFeatures,{-1.,Side*(Species==1?43.:28.),Species==1?123.:148.},EarSize,Fur,
            Species == 0 || Species == 2 ? Cone.Get() : Ball.Get());
        Ear->SetRelativeRotation(FRotator(Species==0 && I==0 ? -26. : 0.,0.,Side*16.));
        if (Species == 3) Shape(AnimalFeatures,{-1.,Side*20.,157.},{14,15,24},Cream,Cone);
        Hands.Add(Shape(Body,{8.,Side*35.,62.},{24,22,Species==1?46.:33.},Dark));
        Feet.Add(Shape(Body,{8.,Side*18.,13.},{31,25,26},Dark));
        const FColor Gold(252,198,58);
        auto* Mark = Shape(Body,{15.,Side*23.,163.},{8,8,8},Gold);
        // Four points make a readable sparkle from front and rear views.
        for (int32 Point=0; Point<4; ++Point)
        {
            const FRotator Ray(0.,0.,Point*90.);
            auto* Tip=Shape(Mark,Ray.RotateVector(FVector(0,0,50)),{68,75,125},Gold,Cone);
            Tip->SetRelativeRotation(Ray);
        }
        Mark->SetVisibility(false,true);
        Marks.Add(Mark);
    }
    Shape(AnimalFeatures,{39.,0.,104.},Species==2 ? FVector(31,33,23) : FVector(25,47,27),Cream);
    if (Species==0 || Species==3)
        for (const double Side : {-1.,1.}) Shape(AnimalFeatures,{50.,Side*9.,106.},{3,6,8},Dark);
    else Shape(AnimalFeatures,{52,0,110},{9,15,9},Dark);
    if (Species == 2 || Species == 1)
        Shape(AnimalFeatures,{-38,0,52},Species==2 ? FVector(52,30,36) : FVector(39,13,14),Fur)->SetRelativeRotation(FRotator(-28,0,0));
    Badge = Shape(AnimalFeatures,{-28,0,64},{18,57,39},FColor::White);
}

void UExpeditionAnimalComponent::PlayDig() { bThrow=false; Swing=1.f; }
void UExpeditionAnimalComponent::PlayThrow() { bThrow=true; Swing=1.f; }
void UExpeditionAnimalComponent::SetMarks(int32 Count) { MudSpots=Count; MarkSeconds=1.5f; }

void UExpeditionAnimalComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,TickType,Tick);
    if (!Body) { Build(); if (!Body) return; }
    auto* Hero = CastChecked<AVoxelCharacter>(GetOwner());
    const auto* PS = Hero->GetPlayerState<AIslandPlayerState>();
    if (PS && PS->Team != Team)
    {
        Team=PS->Team;
        Badge->SetMaterial(0,Paint(IslandRules::TeamColor(Team).ToFColorSRGB()));
    }
    const float Speed = FMath::Clamp(float(Hero->GetVelocity().Size2D()) / 370.f,0.f,1.f);
    Stride += Delta * 12.f * Speed;
    Swing=FMath::Max(0.f,Swing-Delta*3.2f);
    MarkSeconds=FMath::Max(0.f,MarkSeconds-Delta);
    const bool Compact=Hero->IsMudCompact(), Stun=Hero->IsMudStunned();
    Body->SetRelativeLocation(FVector(0,0,-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+FMath::Abs(FMath::Sin(Stride))*4.f*Speed));
    Body->SetRelativeScale3D(Compact ? FVector(.86,.88,.46) : FVector::OneVector);
    Body->SetRelativeRotation(FRotator(Stun ? -9.f : 0.f,0,Stun ? 9.f : 0.f));
    const bool FirstPerson = Hero->IsInFirstPersonView();
    const bool Visible = !FirstPerson && !(Compact && Hero->IsLocallyControlled());
    if (Body->IsVisible()!=Visible) Body->SetVisibility(Visible,true);
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    const bool Fogged = Round && Round->IsIdentityDisguised();
    if (AnimalFeatures->IsVisible() != (Visible && !Fogged)) AnimalFeatures->SetVisibility(Visible && !Fogged,true);
    if (FogSuit->IsVisible() != (Visible && Fogged)) FogSuit->SetVisibility(Visible && Fogged,true);
    if (Fogged != bFogged)
    {
        // Restore each species' own paws after the shared disguise expires.
        bFogged = Fogged;
        const bool LongPaws = !Fogged && PS && uint32(PS->GetPlayerId()) % 4 == 1;
        for (const auto& Hand : Hands) Hand->SetRelativeScale3D(FVector(.24,.22,LongPaws ? .46 : .33));
    }
    const bool ShowMud=Visible && !Stun && MarkSeconds>0 && MudSpots>=1;
    CheekMud->SetVisibility(ShowMud);
    // Only the player looking through these eyes needs a pair of near-camera paws.
    if (FirstPerson && EyePaws.IsEmpty())
    {
        for (int32 I=0; I<2; ++I)
        {
            auto* Paw=Shape(Hero->GetEyeCamera(),FVector::ZeroVector,{23,12,15},FColor(69,61,65));
            Paw->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
            Paw->SetOnlyOwnerSee(true);
            Paw->SetCastShadow(false);
            EyePaws.Add(Paw);
        }
    }
    for (int32 I=0; I<Hands.Num(); ++I)
    {
        const float Side=I ? 1.f : -1.f, Gait=Side*FMath::Sin(Stride)*Speed;
        const float Reach=I==1 ? FMath::Sin(Swing*PI) : 0.f;
        Hands[I]->SetRelativeLocation(FVector(8.+Gait*9.+Reach*31.,Side*35.,61.+Reach*(bThrow?20.:-20.)));
        Feet[I]->SetRelativeLocation(FVector(8.-Gait*12.,Side*18.,13.+FMath::Max(0.f,Gait)*8.));
        Marks[I]->SetVisibility(ShowMud && !Compact && MudSpots>=2,true);
        if (EyePaws.IsValidIndex(I))
        {
            auto* Paw=EyePaws[I].Get();
            Paw->SetVisibility(FirstPerson && !Stun);
            if (FirstPerson)
            {
                Paw->SetRelativeLocation({38.+Gait*1.5+Reach*14.,Side*(21.-Reach*8.),-18.+Gait+Reach*(bThrow?11.:6.)});
                Paw->SetRelativeRotation(FRotator(14.+Reach*12.,-Side*12.,0.));
            }
        }
    }
}

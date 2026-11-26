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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Game/TreasureIsland/Art/Crew/SM_CarvedRound"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Paw(TEXT("/Game/TreasureIsland/Art/Crew/SM_Mitten"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Pig(TEXT("/Game/TreasureIsland/Art/Crew/SM_Pig"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Monkey(TEXT("/Game/TreasureIsland/Art/Crew/SM_Monkey"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Fox(TEXT("/Game/TreasureIsland/Art/Crew/SM_Fox"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Calf(TEXT("/Game/TreasureIsland/Art/Crew/SM_Calf"));
    Animals = {Pig.Object, Monkey.Object, Fox.Object, Calf.Object};
    Mitten = Paw.Object;
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
    Mesh->SetReceivesDecals(false);
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

UStaticMesh* UExpeditionAnimalComponent::GetCharacterMesh(int32 ModelIndex) const
{
    return Animals.IsValidIndex(ModelIndex) ? Animals[ModelIndex].Get() : nullptr;
}

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
    DisplayedModel = PS->GetCharacterModel();
    const FColor Dark(73,57,53);
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
    // The sculpture is authored in centimetres from the sole; its skin, costume and face share one draw.
    Sculpture = Shape(AnimalFeatures, FVector::ZeroVector, FVector(100.), FColor::White, GetCharacterMesh(DisplayedModel));
    Sculpture->SetMaterial(0, nullptr); // use the sculpture's own painted material when switching mesh
    CheekMud=Shape(Body,{37,24,111},{9,25,15},FColor(149,95,60));
    CheekMud->SetRelativeRotation(FRotator(0.,29.,-12.));
    CheekMud->SetVisibility(false);
    for (int32 I=0; I<2; ++I)
    {
        const int32 Side = I ? 1 : -1;
        auto* Hand = Shape(Body,{8.,Side*35.,62.},{24.,22.*Side,DisplayedModel==1?46.:33.},Dark,Mitten);
        Hands.Add(Hand);
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
    Shape(AnimalFeatures,{-31,0,62},{11,51,38},FColor(91,61,48));
    Badge = Shape(AnimalFeatures,{-36,0,62},{6,43,30},FColor::White);
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
    const int32 Model = PS ? PS->GetCharacterModel() : DisplayedModel;
    if (Fogged != bFogged || Model != DisplayedModel)
    {
        if (Model != DisplayedModel) Sculpture->SetStaticMesh(GetCharacterMesh(Model));
        DisplayedModel = Model;
        bFogged = Fogged;
        const bool LongPaws = !Fogged && Model == 1;
        for (int32 I = 0; I < Hands.Num(); ++I)
            Hands[I]->SetRelativeScale3D(FVector(.24, I ? .22 : -.22, LongPaws ? .46 : .33));
    }
    const bool ShowMud=Visible && !Stun && MarkSeconds>0 && MudSpots>=1;
    CheekMud->SetVisibility(ShowMud);
    // Only the player looking through these eyes needs a pair of near-camera paws.
    if (FirstPerson && EyePaws.IsEmpty())
    {
        for (int32 I=0; I<2; ++I)
        {
            auto* Paw=Shape(Hero->GetEyeCamera(),FVector::ZeroVector,{23.,I ? 14. : -14.,16.},FColor(98,65,49),Mitten);
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

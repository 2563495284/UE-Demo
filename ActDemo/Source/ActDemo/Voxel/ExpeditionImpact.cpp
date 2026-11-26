#include "Voxel/ExpeditionImpact.h"
#include "Island/IslandClientSettings.h"
#include "Island/IslandPlayerController.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "RHIGlobals.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelDensity.h"
#include "Widgets/SLeafWidget.h"

// The veil is composited after the scene and its HUD, including while the two worlds exchange pawns.
class SIslandPassage : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SIslandPassage) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&, UMaterialInstanceDynamic* Material)
    {
        Ink.SetResourceObject(Material);
        Ink.DrawAs = ESlateBrushDrawType::Image;
        SetVisibility(EVisibility::HitTestInvisible);
    }

    void Show(const FIslandPartyTransitionView& View)
    {
        Frame = View;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    bool HasPaintedBlack(uint32 Round, bool bReturning) const
    {
        return PaintedRound == Round && bPaintedReturning == bReturning;
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D(1., 1.); }
    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        const bool bSolid = Frame.Phase == EIslandPartyVeil::Black ||
            (Frame.Phase == EIslandPartyVeil::Closing && Frame.Coverage >= 1.f);
        if (bSolid || !Ink.GetResourceObject())
        {
            FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
                ESlateDrawEffect::None, FLinearColor(0,0,0,bSolid ? 1.f : Frame.Coverage));
            if (bSolid)
            {
                PaintedRound = Frame.Round;
                bPaintedReturning = Frame.bReturning;
            }
        }
        else
        {
            FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), &Ink);
        }
        return Layer;
    }

private:
    FSlateBrush Ink;
    FIslandPartyTransitionView Frame;
    mutable uint32 PaintedRound = 0;
    mutable bool bPaintedReturning = false;
};

AExpeditionImpactCamera::AExpeditionImpactCamera()
{
    static ConstructorHelpers::FObjectFinder<USoundWave> Hit(TEXT("/Game/ActDemo/Audio/RockChip"));
    Chip = Hit.Object;
    Attenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("RockAttenuation"));
    Attenuation->Attenuation.AttenuationShapeExtents.X = 300.f;
    Attenuation->Attenuation.FalloffDistance = 1600.f;
}

void AExpeditionImpactCamera::BeginPlay()
{
    Super::BeginPlay();
    if (FApp::CanEverRender() && !GUsingNullRHI && !IsRunningDedicatedServer())
    {
        if (auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TreasureIsland/FX/M_IslandPassage.M_IslandPassage")))
            PassageMaterial = UMaterialInstanceDynamic::Create(Material, this);
    }
}

void AExpeditionImpactCamera::EndPlay(const EEndPlayReason::Type Reason)
{
    if (PassageLayer && PassageViewport.IsValid())
        PassageViewport->RemoveViewportWidgetContent(PassageLayer.ToSharedRef());
    PassageLayer.Reset();
    Super::EndPlay(Reason);
}

bool AExpeditionImpactCamera::HasPaintedPassageBlack(uint32 Round, bool bReturning) const
{
    return GUsingNullRHI || !FApp::CanEverRender() || (PassageLayer && PassageLayer->HasPaintedBlack(Round,bReturning));
}

void AExpeditionImpactCamera::UpdateCamera(float DeltaTime)
{
    Super::UpdateCamera(DeltaTime);
    const auto* Player = Cast<AIslandPlayerController>(GetOwningPlayerController());
    if (!Player || !Player->IsLocalController()) return;
    const auto View = Player->GetPartyTransitionView();
    if (View.Phase == EIslandPartyVeil::Clear)
    {
        if (PassageLayer && PassageViewport.IsValid())
            PassageViewport->RemoveViewportWidgetContent(PassageLayer.ToSharedRef());
        PassageLayer.Reset();
        return;
    }
    auto* Viewport = GetWorld()->GetGameViewport();
    if (!Viewport || GUsingNullRHI || !FApp::CanEverRender()) return;
    if (!PassageLayer)
    {
        PassageLayer = SNew(SIslandPassage, PassageMaterial.Get());
        PassageViewport = Viewport;
        Viewport->AddViewportWidgetContent(PassageLayer.ToSharedRef(), 1000);
    }
    if (PassageMaterial && View.Phase != EIslandPartyVeil::Black)
    {
        FVector2D Size;
        Viewport->GetViewportSize(Size);
        const auto* Settings = UIslandClientSettings::Get();
        PassageMaterial->SetScalarParameterValue(TEXT("Aspect"), Size.Y > 0. ? float(Size.X/Size.Y) : 16.f/9.f);
        PassageMaterial->SetScalarParameterValue(TEXT("Progress"), View.Coverage);
        PassageMaterial->SetScalarParameterValue(TEXT("Motion"), Settings && !Settings->IsCameraShakeEnabled() ? 0.f : 1.f);
        PassageMaterial->SetScalarParameterValue(TEXT("Returning"), View.bReturning ? 1.f : 0.f);
    }
    PassageLayer->Show(View);
}

void AExpeditionImpactCamera::Play(AActor& Terrain, const FVoxelEdit& Edit)
{
    if (Edit.Action == EVoxelAction::Fill) return;
    auto* Player = Terrain.GetWorld()->GetFirstPlayerController();
    auto* Camera = Player && Player->IsLocalController() ? Cast<AExpeditionImpactCamera>(Player->PlayerCameraManager) : nullptr;
    if (!Camera || !Player->GetPawn()) return;
    const FVector Point(Edit.Center);
    const double Distance = FVector::Distance(Player->GetPawn()->GetActorLocation(), Point);
    if (Distance > 2300.) return;
    UGameplayStatics::PlaySoundAtLocation(Camera, Camera->Chip, Point, .62f, FMath::FRandRange(.93f,1.08f), 0.f, Camera->Attenuation);

    // A short local impulse follows the actual collision commit. Distant drilling is audible only.
    constexpr float Range = 310.f;
    const float Strength = .34f * FMath::Clamp(1.f - float(Distance) / Range, 0.f, 1.f);
    const float Remaining = FMath::Max(0.f, 1.f - Camera->ImpactAge / ImpactDuration);
    if (Strength > Camera->ImpactStrength * Remaining)
    {
        Camera->ImpactAge = 0.f;
        Camera->ImpactStrength = Strength;
    }
}

void AExpeditionImpactCamera::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& View)
{
    Super::ApplyCameraModifiers(DeltaTime, View);
    if (const auto* Settings = UIslandClientSettings::Get(); Settings && !Settings->IsCameraShakeEnabled())
    {
        ImpactAge = ImpactDuration;
        return;
    }
    if (ImpactAge >= ImpactDuration) return;
    ImpactAge += DeltaTime;
    const float Envelope = FMath::Square(FMath::Max(0.f, 1.f - ImpactAge / ImpactDuration));
    const float Impulse = Envelope * ImpactStrength * FMath::Sin(ImpactAge * 76.f);
    View.Location.Z += Impulse * 3.5f;
    View.Rotation.Pitch += Impulse * .65f;
}

#include "Island/IslandHUD.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/RectLightComponent.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomPanel.h"
#include "Island/IslandState.h"
#include "Layout/Children.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    float MenuFraction(SIslandRoomPanel& Panel)
    {
        const FGeometry& View = Panel.GetCachedGeometry();
        const FChildren* Children = Panel.GetChildren();
        if (View.GetLocalSize().X <= 0.f || !Children || Children->Num() == 0) return .40f;
        const FGeometry& Card = Children->GetChildAt(0)->GetCachedGeometry();
        const float Edge = View.AbsoluteToLocal(Card.LocalToAbsolute(FVector2D(Card.GetLocalSize().X, 0))).X;
        return FMath::Clamp(Edge / View.GetLocalSize().X, .18f, .80f);
    }

    void FrameCrew(ACameraActor& Camera, const AVoxelCharacter& Hero, const AIslandPlayerController& Player, float Menu)
    {
        int32 Width = 0, Height = 0;
        Player.GetViewportSize(Width, Height);
        const double Aspect = Height > 0 ? double(Width) / Height : 16. / 9.;
        // Keep a full standing silhouette, including tall fox ears, at every supported window size.
        const double HorizontalTangent = .42 * Aspect;
        const float HorizontalFOV = FMath::RadiansToDegrees(2. * FMath::Atan(HorizontalTangent));
        const FVector Feet = Hero.GetActorLocation() - FVector::UpVector * Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Mid = Feet + FVector(0., 0., 86.);
        const FVector Eye = Mid + Hero.GetActorForwardVector() * 303. - Hero.GetActorRightVector() * 86. + FVector(0., 0., 58.);
        FRotator Look = (Mid - Eye).Rotation();
        // Aim left of the actor, keeping the lens physically in front of the face.
        Look.Yaw -= FMath::RadiansToDegrees(FMath::Atan(Menu * HorizontalTangent));
        Camera.SetActorLocationAndRotation(Eye, Look);
        Camera.GetCameraComponent()->SetFieldOfView(HorizontalFOV);
    }

    void AddPortraitFill(ACameraActor& Camera, const AVoxelCharacter& Hero)
    {
        // Face paint otherwise falls into deep shadow at the west-facing landing.
        auto* Fill = NewObject<URectLightComponent>(&Camera);
        Fill->SetupAttachment(Camera.GetRootComponent());
        Fill->SetMobility(EComponentMobility::Movable);
        Fill->SetCastShadows(false);
        Fill->SetIntensityUnits(ELightUnits::Lumens);
        Fill->SetIntensity(24.f);
        Fill->SetSpecularScale(.25f);
        Fill->SetSourceWidth(140.f);
        Fill->SetSourceHeight(95.f);
        Fill->SetAttenuationRadius(440.f);
        Fill->SetLightColor(FLinearColor(1.f,.95f,.88f));
        Camera.AddInstanceComponent(Fill);
        Fill->RegisterComponent();
        const FVector Face = Hero.GetActorLocation() + FVector(0., 0., 18.);
        const FVector Position = Face + Hero.GetActorForwardVector() * 158. - Hero.GetActorRightVector() * 52. + FVector(0., 0., 94.);
        Fill->SetWorldLocationAndRotation(Position, (Face - Position).Rotation());
    }
}

bool AIslandHUD::UpdateCrewPreview(AIslandPlayerController& Player, bool bShow)
{
    auto* Hero = Player.GetHero();
    if (!bShow || !Hero || Player.GetTeam() < 0)
    {
        if (bCrewPreview)
        {
            for (const auto& Occluder : PreviewOccluders)
                if (Occluder.IsValid()) Player.HiddenActors.Remove(Occluder.Get());
            PreviewOccluders.Reset();
            bCrewPreview = false;
            CameraDock = INDEX_NONE;
        }
        return false;
    }

    if (!bCrewPreview || !DockCamera.IsValid())
    {
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        auto* Camera = GetWorld()->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
        if (!Camera) return false;
        auto* Lens = Camera->GetCameraComponent();
        Lens->SetConstraintAspectRatio(false);
        Lens->bOverrideAspectRatioAxisConstraint = true;
        Lens->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
        Lens->PostProcessSettings.bOverride_MotionBlurAmount = true;
        Lens->PostProcessSettings.MotionBlurAmount = 0.f;
        FrameCrew(*Camera, *Hero, Player, MenuFraction(*Panel));
        AddPortraitFill(*Camera, *Hero);
        if (DockCamera.IsValid()) DockCamera->SetLifeSpan(.35f);
        DockCamera = Camera;
        CameraDock = INDEX_NONE;
        bRoomCamera = bCrewPreview = true;
        if (Player.PlayerCameraManager) Player.PlayerCameraManager->StopCameraFade();
        Player.SetViewTargetWithBlend(Camera, .25f, VTBlend_Cubic, 0.f, true);
    }
    else
    {
        FrameCrew(*DockCamera.Get(), *Hero, Player, MenuFraction(*Panel));
    }

    // Picking a model is private to this viewport. Crew may join or change berths while the page is open.
    if (const auto* Round = GetWorld()->GetGameState<AIslandGameState>())
        for (const auto& State : Round->PlayerArray)
        {
            auto* Other = State ? State->GetPawn() : nullptr;
            if (Other && Other != Hero && FVector::DistSquared(Other->GetActorLocation(), Hero->GetActorLocation()) < FMath::Square(950.)
                && !Player.HiddenActors.Contains(Other))
            {
                Player.HiddenActors.Add(Other);
                PreviewOccluders.Add(Other);
            }
        }
    return true;
}

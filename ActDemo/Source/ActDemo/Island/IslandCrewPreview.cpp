#include "Island/IslandHUD.h"
#include "Island/IslandCrewPortrait.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
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
        const FVector Eye = Mid + Hero.GetActorForwardVector() * 284. - Hero.GetActorRightVector() * 132. + FVector(0., 0., 94.);
        FRotator Look = (Mid - Eye).Rotation();
        // Aim left of the actor, keeping the lens physically in front of the face.
        Look.Yaw -= FMath::RadiansToDegrees(FMath::Atan(Menu * HorizontalTangent));
        Camera.SetActorLocationAndRotation(Eye, Look);
        Camera.GetCameraComponent()->SetFieldOfView(HorizontalFOV);
    }

}

void IslandCrewPortrait::Light(ACameraActor& Camera,const AVoxelCharacter& Hero,bool bComparisonReference)
{
    const auto Bounce=[&](FVector Offset,FLinearColor Tint,float Lumens,float Width,float Specular)
    {
        auto* Lamp=NewObject<URectLightComponent>(&Camera);
        Lamp->SetupAttachment(Hero.GetRootComponent());
        Lamp->SetMobility(EComponentMobility::Movable);
        Lamp->SetCastShadows(false);
        Lamp->SetIntensityUnits(ELightUnits::Lumens);
        Lamp->SetIntensity(Lumens);
        Lamp->SetSpecularScale(Specular);
        Lamp->SetSourceWidth(Width);
        Lamp->SetSourceHeight(Width*.78f);
        Lamp->SetAttenuationRadius(430.f);
        Lamp->SetLightColor(Tint);
        const FVector Face(8.,0.,32.);
        Lamp->SetRelativeLocationAndRotation(Face+Offset,(-Offset).Rotation());
        Camera.AddInstanceComponent(Lamp);
        Lamp->RegisterComponent();
    };
    if (bComparisonReference)
    {
        Bounce({128.,-128.,96.},{1.f,.88f,.72f},82.f,105.f,.48f);
        Bounce({104.,151.,55.},{.70f,.85f,1.f},43.f,172.f,.19f);
        return;
    }
    // Retain the island's metering range. Its authored (or project-default)
    // exposure is the base for this close view of pale canvas and peach skin.
    const auto* DefaultBias=IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.Bias"));
    float Bias=DefaultBias ? DefaultBias->GetFloat() : 0.f;
    float Priority=-MAX_flt;
    for (TActorIterator<APostProcessVolume> It(Camera.GetWorld());It;++It)
    {
        if (It->bEnabled && It->bUnbound && It->Priority>=Priority && It->Settings.bOverride_AutoExposureBias)
        {
            Bias=It->Settings.AutoExposureBias;
            Priority=It->Priority;
        }
    }
    auto& Post=Camera.GetCameraComponent()->PostProcessSettings;
    Post.bOverride_AutoExposureBias=true;
    Post.AutoExposureBias=Bias-.72f;
    // The physical sun remains the broad key. Light the rim and lift the far
    // cheek just enough for its small printed eye instead of flooding the front.
    Bounce({43.,-169.,78.},{1.f,.84f,.68f},32.f,113.f,.47f);
    Bounce({72.,148.,42.},{.67f,.81f,1.f},16.f,175.f,.17f);
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
        IslandCrewPortrait::Light(*Camera,*Hero);
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

#include "ExpeditionDaylight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
    // Both authored maps have a single sun, sky and atmosphere.
    template<typename T> T* FindOrAdd(UWorld& World)
    {
        TActorIterator<T> Existing(&World);
        return Existing ? *Existing : World.SpawnActor<T>();
    }

    APostProcessVolume* GlobalVolume(UWorld& World)
    {
        for (TActorIterator<APostProcessVolume> It(&World); It; ++It)
            if (It->bUnbound) return *It;
        APostProcessVolume* Volume = World.SpawnActor<APostProcessVolume>();
        Volume->bUnbound = true;
        return Volume;
    }
}

void ExpeditionDaylight::Apply(UWorld& World, ELocation Location)
{
    const bool bHarbour = Location == ELocation::Harbour;
    FindOrAdd<ASkyAtmosphere>(World);
    ADirectionalLight* Sun = FindOrAdd<ADirectionalLight>(World);
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->SetActorRotation(bHarbour ? FRotator(-27., -47., 0.) : FRotator(-42., -34., 0.));
    auto* Key = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
    Key->bAtmosphereSunLight = true;
    Key->SetLightColor(FLinearColor(1.f, .89f, .73f));
    Key->SetIntensity(6.f);
    Key->SetLightSourceAngle(2.0f);
    // Screen-space contact rays falsely occlude whole rows of triangles on grazing hillsides.
    // Use the sun's shadow map for feet, props and the terrain they stand on.
    Key->ContactShadowLength = 0.f;
    Key->ContactShadowLengthInWS = false;
    Key->MarkRenderStateDirty();

    USkyLightComponent* Sky = FindOrAdd<ASkyLight>(World)->GetLightComponent();
    Sky->SetMobility(EComponentMobility::Movable);
    Sky->SetIntensity(bHarbour ? 1.35f : 1.65f);
    Sky->SetLightColor(FLinearColor(.86f, .94f, 1.f));
    Sky->SetLowerHemisphereColor(FLinearColor(.09f, .10f, .095f));

    UExponentialHeightFogComponent* Haze = FindOrAdd<AExponentialHeightFog>(World)->GetComponent();
    Haze->SetFogDensity(.013f);
    Haze->SetStartDistance(bHarbour ? 1400.f : 2200.f);
    Haze->SetFogMaxOpacity(.20f);
    Haze->SetFogInscatteringColor(FLinearColor(.24f, .36f, .42f));

    FPostProcessSettings& Post = GlobalVolume(World)->Settings;
    // Project GI is software Lumen on Mac. Let the player's GI quality select its path.
    Post.bOverride_DynamicGlobalIlluminationMethod = false;
    // Limit screen-space reflection work to smoother surfaces in this mostly matte scene.
    Post.bOverride_ReflectionMethod = true;
    Post.ReflectionMethod = EReflectionMethod::ScreenSpace;
    Post.bOverride_ScreenSpaceReflectionMaxRoughness = true;
    Post.ScreenSpaceReflectionMaxRoughness = .70f;
    Post.bOverride_BloomIntensity = true;
    Post.BloomIntensity = .16f;

    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_DAYLIGHT world=%s sun=%s gi=project reflections=screen-space"), *World.GetName(), *Sun->GetActorRotation().ToString());
}

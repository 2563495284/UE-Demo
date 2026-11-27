#pragma once

#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "ExpeditionBodyMud.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

// The authoritative solid is a round clod in the character capsule's local frame.
USTRUCT()
struct FExpeditionClod
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 LocalCenter = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal LocalNormal = FVector::UpVector;
    UPROPERTY() bool bOnBody = false;
};

USTRUCT()
struct FExpeditionClayFoot
{
    GENERATED_BODY()
    UPROPERTY() int32 Index = INDEX_NONE;
    UPROPERTY() FVector_NetQuantize10 LocalPoint = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal LocalNormal = FVector::UpVector;
};

UCLASS()
class ACTDEMO_API UExpeditionBodyMudComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UExpeditionBodyMudComponent();
    static constexpr float ClodRadius = 16.f;
    bool AddHit(const FHitResult& Hit);
    bool Trace(const FVector& Start, const FVector& End, float SweepRadius, FHitResult& OutHit) const;
    bool Strike(int32 Index);
    bool Peel();
    void Clear();
    void RefreshContacts();
    static void NotifySceneChanged(UWorld* World);
    bool IsAnchored() const { return bAnchored; }
    int32 Num() const { return Clods.Num(); }
    bool GetClodWorld(int32 Index, FVector& Center, float& Radius) const;
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Tick) override;

private:
    UPROPERTY(ReplicatedUsing=RefreshVisuals) TArray<FExpeditionClod> Clods;
    UPROPERTY(ReplicatedUsing=RefreshVisuals) TArray<FExpeditionClayFoot> Feet;
    UPROPERTY(ReplicatedUsing=ApplyAnchored) bool bAnchored = false;
    UPROPERTY() TObjectPtr<UStaticMesh> Stone;
    UPROPERTY() TObjectPtr<UMaterialInterface> ClayMaterial;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Visuals;
    // Favour peeling the shortest still-connected route from this character to a support.
    int32 PeelIndex = INDEX_NONE;
    uint64 LastRefreshFrame = MAX_uint64;
    FTransform SampledFrame;
    double NextSupportCheck = 0.;
    bool bSupportChanged = true, bFirstPersonVisuals = false;
    FDelegateHandle SceneChangedHandle;
    UFUNCTION() void RefreshVisuals();
    UFUNCTION() void ApplyAnchored();
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    void InvalidateSupport(UWorld* World);
    bool NeedsSupportCheck(double Now) const;
    void RebuildContacts(bool bForce);
    void DropLooseClods();
};

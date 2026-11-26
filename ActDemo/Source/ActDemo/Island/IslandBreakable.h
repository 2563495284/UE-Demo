#pragma once

#include "GameFramework/Actor.h"
#include "IslandBreakable.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UStaticMesh;
class UMaterialInterface;

UENUM()
enum class EIslandBreakEffect : uint8
{
    Fragments,
    TreeFall
};

USTRUCT()
struct FIslandBreakState
{
    GENERATED_BODY()

    UPROPERTY() bool bBroken = false;
    UPROPERTY() double ServerTime = 0;
    UPROPERTY() FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
};

UCLASS()
class ACTDEMO_API AIslandBreakable : public AActor
{
    GENERATED_BODY()

public:
    AIslandBreakable();

    UStaticMeshComponent& GetMesh() const { return *Mesh; }
    UTextRenderComponent* GetSign() const { return Sign; }
    void SetUpExampleTree();
    bool Break(const FVector& Direction);
    bool IsBroken() const { return State.bBroken; }

    UPROPERTY(EditAnywhere, Category = "Destruction")
    EIslandBreakEffect Effect = EIslandBreakEffect::Fragments;

    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void Tick(float DeltaSeconds) override;

protected:
    void BeginPlay() override;

private:
    UFUNCTION() void OnRep_State();
    double ServerNow() const;
    void HideIntactMesh();
    void StartFragments();

    UPROPERTY(VisibleAnywhere, Category = "Destruction") TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere, Category = "Destruction") TObjectPtr<UStaticMeshComponent> Crown;
    UPROPERTY(VisibleAnywhere, Category = "Destruction") TObjectPtr<UTextRenderComponent> Sign;
    UPROPERTY(ReplicatedUsing = OnRep_State) FIslandBreakState State;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Fragments;
    FTransform IntactMeshTransform;
    FVector Pivot = FVector::ZeroVector;
    bool bVisualStarted = false;
};

UCLASS(NotPlaceable)
class AIslandRescueRaft : public AIslandBreakable
{
    GENERATED_BODY()

public:
    AIslandRescueRaft();
};

// Authored map geometry stays editable. Record the individual missing pieces instead
// of replacing a saved landmark or replicating one actor for every intact module.
USTRUCT()
struct FIslandRemovedPiece
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<AActor> Owner;
    UPROPERTY() FName Component;
};

UCLASS(NotPlaceable)
class AIslandSceneryDamage : public AActor
{
    GENERATED_BODY()
public:
    AIslandSceneryDamage();
    static bool RemoveAuthoredPiece(const FHitResult& Hit);
    static void PlayDebris(UWorld& World, FVector Point, FVector Normal, UMaterialInterface* Material,
        UStaticMesh* FragmentShape=nullptr, FVector FragmentScale=FVector::OneVector, UMaterialInterface* AlternateFinish=nullptr);
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UFUNCTION() void OnRep_Removed();
    UPROPERTY(ReplicatedUsing=OnRep_Removed) TArray<FIslandRemovedPiece> Removed;
    UFUNCTION(NetMulticast,Unreliable) void MulticastDebris(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal);
};

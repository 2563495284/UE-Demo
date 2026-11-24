#pragma once

#include "GameFramework/Actor.h"
#include "IslandBreakable.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

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

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Voxel/VoxelDensity.h"
#include "VoxelCharacter.generated.h"

class AVoxelTerrain;
class UCameraComponent;
class USpringArmComponent;
class UExpeditionAnimalComponent;

UCLASS()
class ACTDEMO_API AVoxelCharacter : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    AVoxelCharacter(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    void EnsureExpeditionAbilities();
    bool ApplyMudStun();
    bool IsMudStunned() const;
    bool IsMudImmune() const;
    bool IsMudCompact() const { return bIsCrouched; }
    UFUNCTION(NetMulticast, Unreliable) void MulticastExpeditionDig();

    FVector GetAimOrigin() const;
    bool IsInFirstPersonView() const;
    UCameraComponent* GetEyeCamera() const { return EyeCamera; }
    void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    void MoveOnGrid(const FVector2D& Input, double ViewYaw);
    void SetWantsClimb(bool bEnabled);
    bool IsClimbingRock() const;
    void PlayTerrainSwing(EVoxelAction Action);
    void ReturnToStart(const FTransform& Start);

protected:
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_Controller() override;
    virtual bool CanJumpInternal_Implementation() const override;
    void Tick(float DeltaSeconds) override;
    void OnJumped_Implementation() override;

private:
    void MudStunChanged(FGameplayTag Tag, int32 Count);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UExpeditionAnimalComponent> ExpeditionAnimal;
    UPROPERTY() TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> EyeCamera;
    float ToolStrokeRemaining = 0.f;
    float ToolStrokeDegrees = 0.f;
    bool bGrantedExpeditionAbilities = false;
    bool bClearingMud = false;
    TWeakObjectPtr<AVoxelTerrain> Terrain;
    FVector ClimbDirection = FVector::ForwardVector;
    bool bWantsClimb = false;
};

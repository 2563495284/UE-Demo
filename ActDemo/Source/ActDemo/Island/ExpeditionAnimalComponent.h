#pragma once

#include "Components/ActorComponent.h"
#include "ExpeditionAnimalComponent.generated.h"

class AVoxelCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

// The carved animal, crew badge and near-camera paws follow replicated actions on each client.
UCLASS()
class UExpeditionAnimalComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UExpeditionAnimalComponent();
    void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
    // Pig, monkey, fox and calf share the same origin at their feet.
    UStaticMesh* GetCharacterMesh(int32 ModelIndex) const;
    void PlayDig();
    void PlayThrow();
    void SetMarks(int32 Count);
#if !UE_BUILD_SHIPPING
    bool ShowsOutfit(bool bDisguised) const;
#endif
private:
    void Build();
    UStaticMeshComponent* Shape(USceneComponent* Parent, const FVector& At, const FVector& Size,
        FColor Color, UStaticMesh* Geometry = nullptr);
    UMaterialInstanceDynamic* Paint(FColor Color);
    UPROPERTY() TObjectPtr<UStaticMesh> Ball;
    UPROPERTY() TObjectPtr<UStaticMesh> Cone;
    UPROPERTY() TObjectPtr<UStaticMesh> Mitten;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> Animals;
    UPROPERTY() TObjectPtr<UMaterialInterface> Base;
    UPROPERTY() TObjectPtr<USceneComponent> Body;
    UPROPERTY() TObjectPtr<USceneComponent> AnimalFeatures;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Sculpture;
    int32 DisplayedModel = INDEX_NONE;
    UPROPERTY() TObjectPtr<USceneComponent> FogSuit;
    bool bFogged = false;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CheekMud;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Badge;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Hands;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> EyePaws;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Feet;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Marks;
    UPROPERTY() TMap<uint32,TObjectPtr<UMaterialInstanceDynamic>> Paints;
    int32 Team = INDEX_NONE;
    int32 MudSpots = 0;
    bool bThrow = false;
    float Swing = 0.f, MarkSeconds = 0.f, Stride = 0.f;
};

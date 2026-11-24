#pragma once

#include "Engine/DataAsset.h"
#include "VoxelInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS(BlueprintType)
class ACTDEMO_API UActInputConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputMappingContext> Gameplay;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputMappingContext> Menu;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Move;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Look;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Sprint;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Light;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Heavy;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Dodge;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Heal;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Lock;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Switch;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Pause;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Restart;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Debug;
};

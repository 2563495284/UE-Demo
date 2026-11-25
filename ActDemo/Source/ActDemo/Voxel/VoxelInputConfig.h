#pragma once

#include "UObject/Object.h"
#include "VoxelInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS(Transient)
class UVoxelInputConfig : public UObject
{
    GENERATED_BODY()

public:
    static UVoxelInputConfig* Create(UObject& Owner);

    UPROPERTY() TObjectPtr<UInputMappingContext> Gameplay;
    UPROPERTY() TObjectPtr<UInputAction> Move;
    UPROPERTY() TObjectPtr<UInputAction> Look;
    UPROPERTY() TObjectPtr<UInputAction> Jump;
    UPROPERTY() TObjectPtr<UInputAction> Dig;
    UPROPERTY() TObjectPtr<UInputAction> SecondaryTool;
    UPROPERTY() TObjectPtr<UInputAction> Fill;
    UPROPERTY() TObjectPtr<UInputAction> Climb;
    UPROPERTY() TObjectPtr<UInputAction> Recall;
};

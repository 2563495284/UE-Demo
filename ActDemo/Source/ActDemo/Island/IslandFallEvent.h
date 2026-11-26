#pragma once

#include "GameFramework/Actor.h"
#include "IslandFallEvent.generated.h"

UENUM()
enum class EIslandChipSurface : uint8 { Timber, Rock, Metal, Canvas, Sand, Leaf };

USTRUCT()
struct FIslandFallingPart
{
    GENERATED_BODY()

    UPROPERTY() FName Source;
    UPROPERTY() FTransform Frame;
    UPROPERTY() TArray<uint8> Shape;
};

USTRUCT()
struct FIslandSceneryFall
{
    GENERATED_BODY()

    UPROPERTY() uint32 Id = 0;
    UPROPERTY() TObjectPtr<AActor> Subject;
    UPROPERTY() FName SubjectName;
    UPROPERTY() FVector_NetQuantize Origin = FVector::ZeroVector;
    UPROPERTY() TArray<FIslandFallingPart> Parts;
    UPROPERTY() double Start = 0.;
    UPROPERTY() EIslandChipSurface Surface = EIslandChipSurface::Rock;
};

USTRUCT()
struct FIslandFallContact
{
    GENERATED_BODY()

    UPROPERTY() FVector_NetQuantize Point = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal Normal = FVector::UpVector;
    UPROPERTY() float Drop = 0.f;
    UPROPERTY() bool bWater = false;
    UPROPERTY() bool bHasContact = false;
    UPROPERTY() bool bConfirmed = false;
};

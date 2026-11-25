#pragma once

#include "Components/ActorComponent.h"
#include "PartyDensityTerrain.generated.h"

class UMaterialInterface;
class UPrimitiveComponent;
class UProceduralMeshComponent;
enum class EPartyBlock : uint8;

/** Arena-local centimetres. Negative samples are solid. Mesh and physics commit together. */
UCLASS()
class PARTYFRAMEWORK_API UPartyDensityTerrain : public UActorComponent
{
    GENERATED_BODY()
public:
    UPartyDensityTerrain();
    void Initialize(const TMap<FIntVector,EPartyBlock>& InitialCells, FColor Theme, TConstArrayView<FVector> Berths);
    bool ApplyBrush(FIntVector Center, FIntVector Normal, bool bFill,
        const TArray<FIntVector>& ProtectedMin, const TArray<FIntVector>& ProtectedMax);
    float DensityAt(FVector LocalPosition) const;
    EPartyBlock MaterialAt(FVector LocalPosition) const;
    bool OwnsSurface(const UPrimitiveComponent* Component) const;
    uint32 Checksum() const { return Digest; }

private:
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    TArray<FLinearColor> Palette;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> MeshChunks;
    TArray<int16> Samples;
    TArray<uint8> Kinds;
    // Original foundation sample per XY column, retired when fully excavated.
    TArray<int32> FoundationZ;
    TBitArray<> BerthFoundation;
    FIntVector Origin = FIntVector::ZeroValue;
    FIntVector Dimensions = FIntVector::ZeroValue;
    FIntVector Chunks = FIntVector::ZeroValue;
    uint32 Digest = 0;
    int32 Index(FIntVector P) const { return P.X + Dimensions.X*(P.Y + Dimensions.Y*P.Z); }
    bool Contains(FIntVector P) const;
    float Interpolate(FVector LocalPosition, EPartyBlock* Material) const;
    FLinearColor SurfaceColor(FVector LocalPosition) const;
    void RebuildChunk(FIntVector Chunk);
};

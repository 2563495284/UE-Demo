#pragma once
#include "PartyMeshVoxelData.h"
#include "PartyCarvableMesh.generated.h"

class UStaticMeshComponent;

// Retains the original painted triangles until the first edit. The occupied
// cells then supply both the surviving surface and character collision.
UCLASS(Transient)
class PARTYFRAMEWORK_API UPartyCarvableMesh : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    static UPartyCarvableMesh* Create(UStaticMeshComponent& Original,double MaxPitch);
    void SelectBrush(FVector Point,FVector Normal,double Radius,double Depth,bool bRestore,TArray<int32>& Out) const;
    bool SetCells(TConstArrayView<int32> Cells,bool bRestore);
    bool FirstMissing(FVector From,FVector UnitDirection,double Range,FVector& Point,double& Distance) const;
    static void DropUnsupported(TConstArrayView<UPartyCarvableMesh*> Parts,TFunctionRef<bool(FVector)> IsGround);
    bool IsSurface(const UPrimitiveComponent* Surface) const;
    FBox CellBounds(int32 ID) const;
    FIslandMeshVoxelData& Data() const { return *Grid; }
    UStaticMeshComponent* Source() const { return Authored; }
private:
    void Rebuild();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Authored;
    TSharedPtr<FIslandMeshVoxelData> Grid;
};

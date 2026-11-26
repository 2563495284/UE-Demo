#pragma once

#include "GameFramework/Actor.h"
#include "Island/IslandNeutralLayout.h"
#include "IslandNeutralProp.generated.h"

class AIslandPlayerController;
class AVoxelCharacter;
class AVoxelTerrain;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
struct FCollisionQueryParams;
struct FVoxelDensity;

USTRUCT()
struct FIslandNeutralState
{
    GENERATED_BODY()

    UPROPERTY() EIslandNeutralKind Kind = EIslandNeutralKind::Tree;
    UPROPERTY() bool bInitialized = false;
    UPROPERTY() uint8 Phase = 0;
    UPROPERTY() bool bDemolished = false;
    UPROPERTY() uint8 PlugMask = 0;
    UPROPERTY() uint8 IntactTideMouths = 3;
    UPROPERTY() double ChangedAt = 0.;
    UPROPERTY() double RippleAt = -10.;
    UPROPERTY() int8 RippleSide = 0;
    UPROPERTY() int16 MouthOffsetA = 0;
    UPROPERTY() int16 MouthOffsetB = 0;
};

UCLASS(NotPlaceable)
class ACTDEMO_API AIslandNeutralProp : public AActor
{
    GENERATED_BODY()

public:
    AIslandNeutralProp();
    void Initialize(EIslandNeutralKind Kind);
    EIslandNeutralKind GetKind() const { return State.Kind; }
    bool IsDemolished() const { return State.bDemolished; }
    uint8 GetPhase() const { return State.Phase; }
    uint8 GetPlugMask() const { return State.PlugMask; }
    FVector GetInteractionPoint() const;
    FVector GetTideMouth(int32 Index) const;
    TArray<FVector> GetSandCenters() const;
    static TArray<FVector> MakeSandRamp(const FTransform& Frame, const FVoxelDensity* Density);
    bool IsTideBoostReady() const;
    static bool AnySailObscures(UWorld& World, const FVector& Start, const FVector& End);

    static bool TraceShovel(UWorld& World, FHitResult& Hit, const FVector& Start, const FVector& End, const FCollisionQueryParams& Query);
    bool Strike(AIslandPlayerController& Player, const FHitResult& Hit);
    UFUNCTION(NetMulticast,Unreliable) void MulticastWreckChips(FName SourceName,int32 Finish,
        FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal);
    void RefreshVoxelMechanism();
    bool CanUseRope(const AIslandPlayerController& Player) const;
    bool CanToggleSail() const;
    bool ToggleSail();
    bool UseRope(AIslandPlayerController& Player);
    void RefreshTide(const AVoxelTerrain& Terrain);
    bool TryTideJump(AVoxelCharacter& Hero);
    void CompleteSandRelease(bool bCommitted);

    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void Tick(float DeltaSeconds) override;

protected:
    void BeginPlay() override;

private:
    struct FPaint
    {
        FColor Color;
        float Roughness;
        float Metallic;
        TObjectPtr<UMaterialInterface> Material;
    };
    enum class EPieceCollision { Solid, ShovelOnly, VisibilityOnly, Effect };
    static void SetPieceCollision(UStaticMeshComponent& Mesh, EPieceCollision Collision);
    // Each source and its voxel surface have stable names on every peer.
    UStaticMeshComponent* Piece(UStaticMesh* Shape, FVector Center, FVector Dimensions, FColor Color,
        EPieceCollision Collision = EPieceCollision::Solid, FRotator Rotation = FRotator::ZeroRotator, USceneComponent* Parent = nullptr);
    UStaticMeshComponent* Beam(FVector A, FVector B, double Width, FColor Color, EPieceCollision Collision = EPieceCollision::Solid,
        USceneComponent* Parent = nullptr);
    void BuildTree();
    void BuildShip();
    void BuildHopper();
    void BuildSail();
    void BuildTide();
    void UpdateVisuals(double Now);
    void AnimateTree(double Age);
    void AnimateHopper(double Age);
    void AnimateSail(double Now);
    void AnimateTide(double Now);
    bool IsTreeLandingClear() const;
    bool IntersectsSail(FVector Start, FVector End) const;
    bool HasTideLip(int32 Mouth) const { return (State.IntactTideMouths & (1u << Mouth)) != 0; }
    void SetPhase(uint8 Next);
    double ServerNow() const;
    double TideCycle(double Now) const;
    UFUNCTION() void OnRep_State();
    UFUNCTION() void TouchSail(UPrimitiveComponent* Overlap, AActor* Other, UPrimitiveComponent* OtherComponent,
        int32 BodyIndex, bool bSwept, const FHitResult& Sweep);

    UPROPERTY(ReplicatedUsing=OnRep_State) FIslandNeutralState State;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY() TObjectPtr<UStaticMesh> Ball;
    UPROPERTY() TObjectPtr<UMaterialInterface> PaintBase;
    UPROPERTY() TObjectPtr<UMaterialInterface> WreckFinish;
    UPROPERTY() TObjectPtr<UStaticMesh> LeafCrown;
    UPROPERTY() TObjectPtr<UStaticMesh> BarkSpar;
    UPROPERTY() TObjectPtr<UStaticMesh> SluiceChute;
    UPROPERTY() TObjectPtr<UStaticMesh> SheaveWheel;
    UPROPERTY() TObjectPtr<UStaticMesh> SandHeap;
    UPROPERTY() TObjectPtr<UStaticMesh> WindSidePanel;
    UPROPERTY() TObjectPtr<UStaticMesh> RolledCanvas;
    UPROPERTY() TObjectPtr<UStaticMesh> WindPennon;
    UPROPERTY() TObjectPtr<UStaticMesh> TidePool;
    UPROPERTY() TObjectPtr<UStaticMesh> Basalt;
    UPROPERTY() TObjectPtr<UStaticMesh> WindCanvas;
    UPROPERTY() TObjectPtr<UStaticMesh> SignalCanvas;
    UPROPERTY() TObjectPtr<UStaticMesh> HopperPanel;
    UPROPERTY() TObjectPtr<UStaticMesh> HopperThroat;
    UPROPERTY() TObjectPtr<UStaticMesh> TideJet;
    UPROPERTY() TObjectPtr<UStaticMesh> FoamCrest;
    UPROPERTY() TObjectPtr<UStaticMesh> SprayDrop;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> MovingTree;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> TreeSolids;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> HitTarget;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Gate;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> SandLoad;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> HopperWheels;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SandSpill;
    UPROPERTY(Transient) TObjectPtr<UBoxComponent> ClothVolume;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> SailRoll;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Cloth;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> TideLips;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Jets;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Foam;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Spray;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Spillway;
    TArray<FPaint> Paints;
    int32 NextPieceIndex = 0;
    bool bBuilt = false;
    bool bSandQueued = false;
    bool bTreeSolidsEnabled = true;
};

#pragma once

#include "EdMode.h"
#include "EditorUndoClient.h"
#include "Voxel/VoxelDensity.h"

class AVoxelTerrain;
class FScopedTransaction;
struct FViewportCursorLocation;

class FActVoxelEditorMode final : public FEdMode, public FEditorUndoClient
{
public:
    static const FEditorModeID ID;
    enum class ETool { Add, Carve, SearchCircles };

    FActVoxelEditorMode();
    ~FActVoxelEditorMode() override;
    void Enter() override;
    void Exit() override;
    bool UsesToolkits() const override { return true; }
    using FEdMode::PostUndo;
    void PostUndo(bool bSuccess) override;
    void PostRedo(bool bSuccess) override;
    void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
    bool InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event) override;
    bool InputAxis(FEditorViewportClient* Client, FViewport* Viewport, int32 ControllerId,
        FKey Key, float Delta, float DeltaTime) override;
    bool MouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y) override;
    bool CapturedMouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y) override;
    void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
    void DrawHUD(FEditorViewportClient* Client, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;

    void SetTool(ETool NewTool);
    ETool GetTool() const { return Tool; }
    void SelectCircle(int32 Index) { CircleIndex = Index; }
    int32 GetCircleIndex() const { return CircleIndex; }
    int32 GetBrushRadius() const { return BrushRadius; }
    void SetBrushRadius(int32 Value) { BrushRadius = Value; }
    TOptional<int32> GetCircleRadius() const;
    void CommitCircleRadius(int32 Value);
    void ConvertSelection();
    void FrameIsland();
    FText GetStatus() const;

private:
    friend class FActVoxelStrokeTest;
    void OnPIE(bool bSimulating);
    void EndPIE(bool bSimulating);
    void EnsurePreview();
    void ReleasePreview();
    void UpdateCursor(const FViewportCursorLocation& Ray);
    bool BeginStroke();
    void ApplyStrokePoint();
    void FinishStroke();
    void ValidateMap();
    FVector CircleWorld(int32 Index) const;

    TWeakObjectPtr<AVoxelTerrain> Terrain;
    TUniquePtr<FScopedTransaction> Stroke;
    TOptional<FVector> Cursor;
    TOptional<FIntVector> LastStamp;
    FVector2D CircleGrabOffset = FVector2D::ZeroVector;
    FString ValidationError;
    ETool Tool = ETool::Add;
    int32 BrushRadius = 125;
    int32 CircleIndex = 0;
    bool bInPIE = false;
    bool bStrokeChanged = false;
};

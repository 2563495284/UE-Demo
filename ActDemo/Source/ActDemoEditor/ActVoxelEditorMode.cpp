#include "ActVoxelEditorMode.h"

#include "ActVoxelEditorProps.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "LevelEditorViewport.h"
#include "Island/IslandBreakable.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"
#include "ScopedTransaction.h"
#include "Toolkits/BaseToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Voxel/VoxelTerrain.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ActVoxelEditor"

const FEditorModeID FActVoxelEditorMode::ID = TEXT("EM_ActVoxelMap");

namespace
{
    constexpr int32 MinBrushRadius = 50;
    constexpr int32 MaxBrushRadius = 500;
    const FLinearColor CircleColors[] = {
        FLinearColor(.98, .65, .30), FLinearColor(.36, .77, 1),
        FLinearColor(.54, .95, .54), FLinearColor(.90, .55, .98)};

    class FActVoxelToolkit final : public FModeToolkit
    {
    public:
        explicit FActVoxelToolkit(FActVoxelEditorMode& InMode) : Mode(InMode) {}

        void Init(const TSharedPtr<IToolkitHost>& Host) override
        {
            using ETool = FActVoxelEditorMode::ETool;
            TSharedRef<SHorizontalBox> Circles = SNew(SHorizontalBox);
            for (int32 Index = 0; Index < IslandRules::ClueCount; ++Index)
            {
                Circles->AddSlot().FillWidth(1).Padding(2)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this, Index] { return Mode.GetCircleIndex() == Index ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState) { Mode.SelectCircle(Index); })
                    [SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%c"), 'A' + Index))).ColorAndOpacity(CircleColors[Index])]
                ];
            }
            Panel = SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(6)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(2)[ToolButton(ETool::Add, LOCTEXT("Add", "Add Rock"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)[ToolButton(ETool::Carve, LOCTEXT("Carve", "Carve Rock"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)[ToolButton(ETool::SearchCircles, LOCTEXT("Circles", "Search Circles"))]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
                [
                    SNew(STextBlock).Text(LOCTEXT("BrushRadius", "Brush radius (cm)"))
                    .Visibility_Lambda([this] { return Mode.GetTool() == ETool::SearchCircles ? EVisibility::Collapsed : EVisibility::Visible; })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
                [
                    SNew(SNumericEntryBox<int32>).MinValue(MinBrushRadius).MaxValue(MaxBrushRadius)
                    .MinSliderValue(MinBrushRadius).MaxSliderValue(MaxBrushRadius)
                    .AllowSpin(true)
                    .Value_Lambda([this] { return TOptional<int32>(Mode.GetBrushRadius()); })
                    .OnValueChanged_Lambda([this](int32 Radius) { Mode.SetBrushRadius(Radius); })
                    .OnValueCommitted_Lambda([this](int32 Radius, ETextCommit::Type) { Mode.SetBrushRadius(Radius); })
                    .Visibility_Lambda([this] { return Mode.GetTool() == ETool::SearchCircles ? EVisibility::Collapsed : EVisibility::Visible; })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
                [
                    SNew(SBox)
                    .Visibility_Lambda([this] { return Mode.GetTool() == ETool::SearchCircles ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[Circles]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                        [SNew(STextBlock).Text(LOCTEXT("CircleRadius", "Search radius (cm)"))]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SNumericEntryBox<int32>).MinValue(IslandRules::MinSearchRadius).MaxValue(IslandRules::MaxSearchRadius)
                            .Value_Lambda([this] { return Mode.GetCircleRadius(); })
                            .OnValueCommitted_Lambda([this](int32 Radius, ETextCommit::Type) { Mode.CommitCircleRadius(Radius); })
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8, 12)
                [
                    SNew(SButton).Text(LOCTEXT("Convert", "Make Selection Breakable"))
                    .OnClicked_Lambda([this] { Mode.ConvertSelection(); return FReply::Handled(); })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
                [
                    SNew(SButton).Text(LOCTEXT("FrameIsland", "Frame Island"))
                    .OnClicked_Lambda([this] { Mode.FrameIsland(); return FReply::Handled(); })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
                [SNew(STextBlock).Text_Lambda([this] { return Mode.GetStatus(); }).AutoWrapText(true)];
            FModeToolkit::Init(Host);
        }

        FName GetToolkitFName() const override { return TEXT("ActVoxelMap"); }
        FText GetBaseToolkitName() const override { return LOCTEXT("Toolkit", "Voxel Map"); }
        FEdMode* GetEditorMode() const override { return &Mode; }
        TSharedPtr<SWidget> GetInlineContent() const override { return Panel; }

    private:
        TSharedRef<SWidget> ToolButton(FActVoxelEditorMode::ETool Tool, FText Label)
        {
            return SNew(SCheckBox)
                .IsChecked_Lambda([this, Tool] { return Mode.GetTool() == Tool ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, Tool](ECheckBoxState) { Mode.SetTool(Tool); })
                [SNew(STextBlock).Text(Label)];
        }

        FActVoxelEditorMode& Mode;
        TSharedPtr<SWidget> Panel;
    };
}

FActVoxelEditorMode::FActVoxelEditorMode() = default;
FActVoxelEditorMode::~FActVoxelEditorMode() = default;

void FActVoxelEditorMode::Enter()
{
    FEdMode::Enter();
    Toolkit = MakeShared<FActVoxelToolkit>(*this);
    Toolkit->Init(Owner->GetToolkitHost());
    GEditor->RegisterForUndo(this);
    FEditorDelegates::PreBeginPIE.AddRaw(this, &FActVoxelEditorMode::OnPIE);
    FEditorDelegates::EndPIE.AddRaw(this, &FActVoxelEditorMode::EndPIE);
    EnsurePreview();
}

void FActVoxelEditorMode::Exit()
{
    ReleasePreview();
    FEditorDelegates::PreBeginPIE.RemoveAll(this);
    FEditorDelegates::EndPIE.RemoveAll(this);
    GEditor->UnregisterForUndo(this);
    FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
    Toolkit.Reset();
    FEdMode::Exit();
}

void FActVoxelEditorMode::OnPIE(bool bSimulating)
{
    bInPIE = true;
    ReleasePreview();
}

void FActVoxelEditorMode::EndPIE(bool bSimulating)
{
    bInPIE = false;
}

void FActVoxelEditorMode::EnsurePreview()
{
    UWorld* World = GetWorld();
    if (bInPIE || !World || World->WorldType != EWorldType::Editor)
    {
        return;
    }
    if (Terrain.IsValid() && Terrain->GetWorld() == World)
    {
        return;
    }
    ReleasePreview();
    AVoxelTerrain* Candidate = AVoxelTerrain::Find(World);
    if (Candidate && Candidate->Layout != EVoxelLayout::Playground)
    {
        Terrain = Candidate;
        Terrain->RebuildEditorPreview();
        ValidateMap();
    }
}

void FActVoxelEditorMode::ReleasePreview()
{
    FinishStroke();
    if (Terrain.IsValid())
    {
        Terrain->ClearEditorPreview();
    }
    Terrain.Reset();
    Cursor.Reset();
}

void FActVoxelEditorMode::PostUndo(bool bSuccess)
{
    if (bSuccess && Terrain.IsValid())
    {
        Terrain->RebuildEditorPreview();
        ValidateMap();
        GEditor->RedrawLevelEditingViewports();
    }
}

void FActVoxelEditorMode::PostRedo(bool bSuccess)
{
    PostUndo(bSuccess);
}

void FActVoxelEditorMode::Tick(FEditorViewportClient* Client, float DeltaTime)
{
    FEdMode::Tick(Client, DeltaTime);
    EnsurePreview();
}

void FActVoxelEditorMode::SetTool(ETool NewTool)
{
    FinishStroke();
    Tool = NewTool;
    Cursor.Reset();
    GEditor->RedrawLevelEditingViewports();
}

TOptional<int32> FActVoxelEditorMode::GetCircleRadius() const
{
    if (Terrain.IsValid() && Terrain->GetSearchCircles().IsValidIndex(CircleIndex))
    {
        return Terrain->GetSearchCircles()[CircleIndex].Radius;
    }
    return {};
}

void FActVoxelEditorMode::CommitCircleRadius(int32 Value)
{
    if (!GetCircleRadius().IsSet() || GetCircleRadius().GetValue() == Value)
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("RadiusTransaction", "Resize search circle"));
    Terrain->Modify();
    FIslandSearchCircle Circle = Terrain->GetSearchCircles()[CircleIndex];
    Circle.Radius = FMath::Clamp(Value, IslandRules::MinSearchRadius, IslandRules::MaxSearchRadius);
    Terrain->SetSearchCircle(CircleIndex, Circle);
    ValidateMap();
    GEditor->RedrawLevelEditingViewports();
}

bool FActVoxelEditorMode::InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event)
{
    if (Key == EKeys::LeftMouseButton && Event == IE_Released && Stroke)
    {
        FinishStroke();
        return true;
    }
    if (Key != EKeys::LeftMouseButton || Event != IE_Pressed || !Terrain.IsValid()
        || Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt)
        || Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl))
    {
        return FEdMode::InputKey(Client, Viewport, Key, Event);
    }
    UpdateCursor(Client->GetCursorWorldLocationFromMousePos());
    Client->Invalidate(false, false);
    return BeginStroke();
}

bool FActVoxelEditorMode::BeginStroke()
{
    if (!Cursor.IsSet())
    {
        return false;
    }
    if (Tool == ETool::SearchCircles)
    {
        double Nearest = FMath::Square(155.0);
        bool bFound = false;
        for (int32 Index = 0; Index < Terrain->GetSearchCircles().Num(); ++Index)
        {
            const double Distance = FVector::DistSquaredXY(CircleWorld(Index), Cursor.GetValue());
            if (Distance < Nearest)
            {
                CircleIndex = Index;
                Nearest = Distance;
                bFound = true;
            }
        }
        if (!bFound)
        {
            return false;
        }
        CircleGrabOffset = FVector2D(Terrain->GetSearchCircles()[CircleIndex].Center) - FVector2D(Cursor.GetValue());
    }
    Stroke = MakeUnique<FScopedTransaction>(Tool == ETool::SearchCircles
        ? LOCTEXT("MoveCircle", "Move search circle") : LOCTEXT("Sculpt", "Sculpt voxel terrain"));
    Terrain->Modify(false);
    ApplyStrokePoint();
    return true;
}

bool FActVoxelEditorMode::InputAxis(FEditorViewportClient* Client, FViewport* Viewport, int32 ControllerId,
    FKey Key, float Delta, float DeltaTime)
{
    return (Stroke && (Key == EKeys::MouseX || Key == EKeys::MouseY))
        || FEdMode::InputAxis(Client, Viewport, ControllerId, Key, Delta, DeltaTime);
}

bool FActVoxelEditorMode::MouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y)
{
    FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Viewport, Client->GetScene(), Client->EngineShowFlags));
    const FSceneView* View = Client->CalcSceneView(&Family);
    UpdateCursor(FViewportCursorLocation(View, Client, X, Y));
    if (Stroke)
    {
        ApplyStrokePoint();
    }
    Client->Invalidate(false, false);
    return Stroke.IsValid();
}

bool FActVoxelEditorMode::CapturedMouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y)
{
    return MouseMove(Client, Viewport, X, Y);
}

void FActVoxelEditorMode::UpdateCursor(const FViewportCursorLocation& Ray)
{
    Cursor.Reset();
    if (!Terrain.IsValid())
    {
        return;
    }
    const FVector Start = Ray.GetOrigin();
    const FVector End = Start + Ray.GetDirection() * 100000.0;
    FHitResult Hit;
    const bool bHit = Terrain->TraceEditorPreview(Start, End, Hit);
    if (bHit)
    {
        Cursor = Hit.ImpactPoint;
    }
    else if (Tool != ETool::Carve && FMath::Abs(Ray.GetDirection().Z) > UE_SMALL_NUMBER)
    {
        const bool bHasCircle = Tool == ETool::SearchCircles && Terrain->GetSearchCircles().IsValidIndex(CircleIndex);
        const double Height = bHasCircle ? CircleWorld(CircleIndex).Z : 0.0;
        const double Distance = (Height - Start.Z) / Ray.GetDirection().Z;
        const FVector Point = Start + Ray.GetDirection() * Distance;
        if (Distance >= 0 && FMath::Abs(Point.X) <= Terrain->GetShoreExtent()+100 && FMath::Abs(Point.Y) <= Terrain->GetShoreExtent()+100)
        {
            Cursor = Point;
        }
    }
}

void FActVoxelEditorMode::ApplyStrokePoint()
{
    if (!Terrain.IsValid() || !Cursor.IsSet())
    {
        LastStamp.Reset();
        return;
    }
    const FVector Point = Cursor.GetValue();
    if (Tool == ETool::SearchCircles)
    {
        FIslandSearchCircle Circle = Terrain->GetSearchCircles()[CircleIndex];
        const FVector2D Dragged = FVector2D(Point) + CircleGrabOffset;
        const int32 Extent = Terrain->GetShoreExtent()+100;
        const FIntPoint Center(FMath::Clamp(FMath::RoundToInt(Dragged.X), -Extent, Extent),
            FMath::Clamp(FMath::RoundToInt(Dragged.Y), -Extent, Extent));
        if (Circle.Center != Center)
        {
            Circle.Center = Center;
            Terrain->SetSearchCircle(CircleIndex, Circle);
            bStrokeChanged = true;
        }
        return;
    }

    const FIntVector Center = VoxelPlayground::QuantizeCenter(Point);
    const double Spacing = BrushRadius * .42;
    TArray<FVoxelBrushStamp, TInlineAllocator<32>> Stamps;
    if (LastStamp.IsSet())
    {
        const FVector Previous(LastStamp.GetValue());
        const double Distance = FVector::Distance(FVector(Center), Previous);
        if (Distance < Spacing)
        {
            return;
        }
        const int32 Steps = FMath::CeilToInt(Distance / Spacing);
        for (int32 Step = 1; Step <= Steps; ++Step)
        {
            const FIntVector Position = VoxelPlayground::QuantizeCenter(FMath::Lerp(Previous, FVector(Center), double(Step) / Steps));
            Stamps.Add({Position, BrushRadius, Tool == ETool::Add});
        }
    }
    else
    {
        Stamps.Add({Center, BrushRadius, Tool == ETool::Add});
    }
    LastStamp = Center;
    bStrokeChanged |= Terrain->ApplyEditorStamps(Stamps);
}

void FActVoxelEditorMode::FinishStroke()
{
    if (!Stroke)
    {
        return;
    }
    if (!bStrokeChanged)
    {
        Stroke->Cancel();
    }
    else if (Terrain.IsValid())
    {
        Terrain->MarkPackageDirty();
    }
    Stroke.Reset();
    LastStamp.Reset();
    bStrokeChanged = false;
    ValidateMap();
}

void FActVoxelEditorMode::ValidateMap()
{
    ValidationError.Reset();
    if (Terrain.IsValid() && Terrain->GetEditorDensity())
    {
        FIslandTreasureLayout Layout;
        int32 Decoys = 4;
        GConfig->GetInt(TEXT("/Script/ActDemo.IslandGameMode"), TEXT("DecoyChestCount"), Decoys, GGameIni);
        if (Terrain->Layout == EVoxelLayout::Expedition) Decoys = 0;
        IslandRules::GenerateLayout(0, Decoys, Terrain->GetSearchCircles(), *Terrain->GetEditorDensity(), Layout, &ValidationError, true);
    }
}

FVector FActVoxelEditorMode::CircleWorld(int32 Index) const
{
    const FVector2D Center(Terrain->GetSearchCircles()[Index].Center);
    const FVoxelDensity* Density = Terrain->GetEditorDensity();
    return FVector(Center, Density ? Density->SurfaceAt(Center).Get(0.0) + 15.0 : 15.0);
}

void FActVoxelEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
    FEdMode::Render(View, Viewport, PDI);
    if (!Terrain.IsValid())
    {
        return;
    }
    for (int32 Index = 0; Index < Terrain->GetSearchCircles().Num() && Index < IslandRules::ClueCount; ++Index)
    {
        const FVector Center = CircleWorld(Index);
        const FLinearColor Color = CircleColors[Index];
        DrawCircle(PDI, Center, FVector::ForwardVector, FVector::RightVector,
            Color, Terrain->GetSearchCircles()[Index].Radius, 48, SDPG_Foreground, CircleIndex == Index ? 2.5 : 1.25);
        PDI->DrawPoint(Center, Color, CircleIndex == Index ? 11.0f : 7.0f, SDPG_Foreground);
    }
    if (Cursor.IsSet() && Tool != ETool::SearchCircles)
    {
        const FColor Color = Tool == ETool::Add ? FColor::Green : FColor(255, 117, 86);
        DrawWireSphere(PDI, Cursor.GetValue(), Color, BrushRadius, 24, SDPG_Foreground);
        PDI->DrawPoint(Cursor.GetValue(), Color, 6.0f, SDPG_Foreground);
    }
}

void FActVoxelEditorMode::DrawHUD(FEditorViewportClient* Client, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
    FEdMode::DrawHUD(Client, Viewport, View, Canvas);
    if (!Terrain.IsValid())
    {
        return;
    }
    for (int32 Index = 0; Index < Terrain->GetSearchCircles().Num() && Index < IslandRules::ClueCount; ++Index)
    {
        FVector2D Pixel;
        if (View->WorldToPixel(CircleWorld(Index), Pixel))
        {
            FCanvasTextItem Label(Pixel / Canvas->GetDPIScale() + FVector2D(9, -20), FText::FromString(FString::Printf(TEXT("%c"), 'A' + Index)),
                GEngine->GetLargeFont(), CircleColors[Index]);
            Label.EnableShadow(FLinearColor::Black);
            Canvas->DrawItem(Label);
        }
    }
}

void FActVoxelEditorMode::ConvertSelection()
{
    FinishStroke();
    if (!Terrain.IsValid())
    {
        return;
    }
    TArray<AStaticMeshActor*> Sources;
    for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
    {
        if (AStaticMeshActor* Source = Cast<AStaticMeshActor>(*It); Source && Source->GetWorld() == GetWorld())
        {
            Sources.Add(Source);
        }
    }
    if (Sources.IsEmpty())
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("ConvertTransaction", "Convert selected static meshes to breakable"));
    for (AStaticMeshActor* Source : Sources)
    {
        if (AActor* Converted = ActVoxelEditorProps::Convert(*Source))
        {
            GEditor->SelectActor(Converted, true, false);
        }
    }
    GEditor->NoteSelectionChange();
}

void FActVoxelEditorMode::FrameIsland()
{
    if (!Terrain.IsValid())
    {
        return;
    }
    for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
    {
        if (Client && Client->IsPerspective() && Client->GetWorld() == Terrain->GetWorld())
        {
            Client->SetViewLocation(FVector(-3100, -3100, 5000) * (Terrain->GetShoreExtent() / 2300.0));
            Client->SetViewRotation(FRotator(-46, 45, 0));
            Client->Invalidate();
        }
    }
}

FText FActVoxelEditorMode::GetStatus() const
{
    if (!Terrain.IsValid())
    {
        return LOCTEXT("NoMap", "Open the Treasure Island level to edit its voxel terrain.");
    }
    if (!ValidationError.IsEmpty())
    {
        return FText::FromString(ValidationError);
    }
    return Tool == ETool::SearchCircles
        ? LOCTEXT("DragCircle", "Drag a lettered circle center. Set its radius above.")
        : LOCTEXT("SculptHint", "Drag on the terrain to sculpt. Each drag is one undo step.");
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActVoxelStrokeTest, "ActDemo.Editor.VoxelStroke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActVoxelStrokeTest::RunTest(const FString& Parameters)
{
    FEditorModeTools& Tools = GLevelEditorModeTools();
    const bool bWasActive = Tools.IsModeActive(FActVoxelEditorMode::ID);
    Tools.ActivateMode(FActVoxelEditorMode::ID);
    auto* Mode = Tools.GetActiveModeTyped<FActVoxelEditorMode>(FActVoxelEditorMode::ID);
    if (!TestNotNull(TEXT("Voxel mode"), Mode) || !TestTrue(TEXT("Open an island map first"), Mode->Terrain.IsValid()))
    {
        return false;
    }

    AVoxelTerrain* Terrain = Mode->Terrain.Get();
    const bool bWasDirty = Terrain->GetOutermost()->IsDirty();
    const uint32 Initial = Terrain->GetEditorDensity()->Checksum();
    Mode->SetTool(FActVoxelEditorMode::ETool::Add);
    Mode->Cursor = FVector(-350, -775, 520);
    if (!TestTrue(TEXT("Begin terrain stroke"), Mode->BeginStroke()))
    {
        return false;
    }
    Mode->Cursor = FVector(-675, -775, 520);
    Mode->ApplyStrokePoint();
    Mode->FinishStroke();
    const uint32 Added = Terrain->GetEditorDensity()->Checksum();
    TestNotEqual(TEXT("Continuous stroke modifies terrain"), Added, Initial);
    TestTrue(TEXT("Undo as a single transaction"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo rebuilds original terrain preview"), Terrain->GetEditorDensity()->Checksum(), Initial);
    TestTrue(TEXT("Redo single transaction"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo rebuilds edited terrain preview"), Terrain->GetEditorDensity()->Checksum(), Added);
    GEditor->UndoTransaction();

    const FIslandSearchCircle Circle = Terrain->GetSearchCircles()[0];
    Mode->SetTool(FActVoxelEditorMode::ETool::SearchCircles);
    Mode->Cursor = Mode->CircleWorld(0);
    if (TestTrue(TEXT("Begin search circle drag"), Mode->BeginStroke()))
    {
        Mode->Cursor = Mode->Cursor.GetValue() + FVector(90, -60, 0);
        Mode->ApplyStrokePoint();
        Mode->FinishStroke();
        TestTrue(TEXT("Search circle follows drag"), Terrain->GetSearchCircles()[0].Center == Circle.Center + FIntPoint(90, -60));
        const int32 Radius = Circle.Radius + 20;
        Mode->CommitCircleRadius(Radius);
        TestEqual(TEXT("Selected search circle radius"), Terrain->GetSearchCircles()[0].Radius, Radius);
        GEditor->UndoTransaction();
        GEditor->UndoTransaction();
        TestTrue(TEXT("Circle restored after undo"), Terrain->GetSearchCircles()[0].Center == Circle.Center);
        TestEqual(TEXT("Radius restored after undo"), Terrain->GetSearchCircles()[0].Radius, Circle.Radius);
    }
    Terrain->GetOutermost()->SetDirtyFlag(bWasDirty);
    if (!bWasActive)
    {
        Tools.DeactivateMode(FActVoxelEditorMode::ID);
    }
    return !HasAnyErrors();
}
#endif

#undef LOCTEXT_NAMESPACE

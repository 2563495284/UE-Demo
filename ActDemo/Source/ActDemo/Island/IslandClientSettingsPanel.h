#pragma once

#include "Island/IslandClientSettings.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

class SIslandClientSettingsPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SIslandClientSettingsPanel) {}
        SLATE_EVENT(FSimpleDelegate, OnClose)
        SLATE_EVENT(FSimpleDelegate, OnAction)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    ~SIslandClientSettingsPanel() override;
    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    TSharedRef<SWidget> FocusTarget();
    void Back();

private:
    void Build();
    void BuildDisplay();
    void BuildGeneral();
    void UpdateResolutions(bool bKeepCurrent);
    void SelectTab(bool bShowDisplay);
    void Apply();
    void Keep();
    void Revert();
    EActiveTimerReturnType CheckDeadline(double Now, float Delta);
    TSharedRef<SWidget> Button(FName WidgetTag, TAttribute<FText> Caption, TFunction<void()> Action,
        bool bStrong = false, TAttribute<bool> Enabled = true, TAttribute<FText> AccessibleName = {});
    void Row(const FText& Name, TSharedRef<SWidget> Control);
    void Choice(FName WidgetTag, const FText& Name, TAttribute<FText> Value, TFunction<void(int32)> Step,
        TAttribute<bool> Enabled = true);
    void Toggle(FName WidgetTag, const FText& Name, bool FIslandSettingsDraft::* Field);
    void Slider(FName WidgetTag, const FText& Name, float FIslandSettingsDraft::* Field, float Minimum,
        float Maximum, float Increment, TAttribute<FText> Readout);

    TWeakObjectPtr<UIslandClientSettings> Settings;
    FIslandSettingsDraft Draft;
    FIslandSettingsDraft Saved;
    TArray<FIntPoint> Resolutions;
    TSharedPtr<SWidget> FirstFocus;
    TSharedPtr<SVerticalBox> Rows;
    FSimpleDelegate OnClose, OnAction;
    bool bDisplayTab = true;
    bool bPreviewing = false;
    double Deadline = 0.0;
};

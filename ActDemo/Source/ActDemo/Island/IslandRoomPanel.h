#pragma once

#include "Island/IslandRoomSettings.h"
#include "Widgets/SCompoundWidget.h"

class AIslandPlayerController;
class AIslandGameState;
class AIslandPlayerState;
class UIslandRoomSession;
class SEditableTextBox;
class SVerticalBox;

enum class EIslandPanel : uint8 { Home, Arriving, Create, Search, Lobby, Settings, Result, Pause, Hidden };

class SIslandRoomPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SIslandRoomPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AIslandPlayerController>, Controller)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Show(EIslandPanel Target);
    void Back();
    void Refresh();
    EIslandPanel Current() const { return Page; }
    uint32 ActionCount() const { return Actions; }
    TSharedRef<SWidget> FocusTarget();
    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
    UIslandRoomSession* Session() const;
    AIslandGameState* Round() const;
    AIslandPlayerState* Me() const;
    void Build();
    void BuildForm();
    void BuildBrowser();
    void BuildLobby();
    void UpdateListings();
    void SubmitForm();
    void Focus(const TSharedPtr<SWidget>& Widget) const;
    void Add(TSharedRef<SWidget> Widget, float Gap = 8.f);
    void AddTitle(const FString& Title);
    void AddMessage(TAttribute<FText> Value, bool bWarning = false);
    TSharedRef<SWidget> Button(TAttribute<FText> Label, TFunction<void()> Press,
        TAttribute<bool> Enabled = true, bool bPrimary = false, FName WidgetTag = NAME_None);
    FString TeamMembers(int32 Team) const;
    int32 CrewSize(int32 Team) const;
    FString RoomStatus() const;
    TWeakObjectPtr<AIslandPlayerController> PC;
    TSharedPtr<SVerticalBox> Body, List;
    TSharedPtr<SEditableTextBox> NameField, AddressField;
    TSharedPtr<SWidget> Primary, RefreshButton;
    TMap<FString, TSharedPtr<SWidget>> Entries;
    FIslandRoomSettings Draft;
    FString Address, LocalError, LastListings, LastRoomId;
    EIslandPanel Page = EIslandPanel::Hidden;
    bool bHostLayout = false;
    bool bChooseEmpty = false;
    uint32 Actions = 0;
    FString LobbyName;
    int32 TeamRows = 0;
    double TransitionAt = 0;
};

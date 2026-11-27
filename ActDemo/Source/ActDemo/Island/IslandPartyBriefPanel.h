#pragma once

#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class AIslandPlayerController;
class AIslandGameState;
class UMediaPlayer;
class UMediaTexture;
class SImage;
struct FIslandPartyBriefingMember;
struct FPartyBriefing;

class SIslandPartyBriefPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SIslandPartyBriefPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AIslandPlayerController>, Controller)
        SLATE_EVENT(FSimpleDelegate, OnAction)
    SLATE_END_ARGS()
    SIslandPartyBriefPanel();
    ~SIslandPartyBriefPanel() override;
    bool SupportsKeyboardFocus() const override { return true; }
    void Construct(const FArguments& Args);
    void Refresh();
    TSharedRef<SWidget> FocusTarget();

protected:
    int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
    const AIslandGameState* Round() const;
    const FIslandPartyBriefingMember* LocalMember() const;
    void Build(const FPartyBriefing& Brief);
    void OpenMovie();
    FReply ToggleMovie();
    FReply Submit(bool bForce);
    float PanelScale() const;
    FVector2D ViewSize() const;
    float ContentWidth() const;
    FMargin PagePadding() const;
    float ColumnWidth(bool bMovie) const;
    FText ReadyCount() const;
    FText WaitingNames() const;
    FText SetupStatus() const;
    bool ForceRequested() const;
    FText AgreeCaption() const;
    FText MovieCaption() const;
    bool MovieFailed() const;

    TWeakObjectPtr<AIslandPlayerController> Controller;
    FSimpleDelegate OnAction;
    TSharedPtr<SWidget> AgreeButton;
    TSharedPtr<SImage> MovieImage;
    TStrongObjectPtr<UMediaPlayer> Player;
    TStrongObjectPtr<UMediaTexture> Texture;
    FSlateBrush MovieBrush;
    FString MoviePath;
    FName Feature;
    uint32 RoundId = 0;
    mutable bool bPainted = false;
    bool bMoviePending = false;
    bool bOpenAccepted = false;
    bool bAgreedSent = false;
    bool bForceSent = false;
#if !UE_BUILD_SHIPPING
    bool bLoggedFrame = false;
#endif
};

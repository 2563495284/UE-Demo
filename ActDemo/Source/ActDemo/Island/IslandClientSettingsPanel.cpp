#include "Island/IslandClientSettingsPanel.h"

#include "ExpeditionText.h"
#include "Framework/Application/SlateApplication.h"
#include "Island/IslandMenuStyle.h"
#include "Types/NavigationMetaData.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace IslandClientPanel
{
    FText Copy(const TCHAR* Value) { return FText::FromString(Value); }

    TAttribute<FText> NameAndValue(FText Name, TAttribute<FText> Value)
    {
        return TAttribute<FText>::CreateLambda([Name, Value]
        {
            return FText::Format(FText::FromString(TEXT("{0}, {1}")), Name, Value.Get());
        });
    }

    TSharedRef<SWidget> Text(TAttribute<FText> Value, int32 Size, FLinearColor Color, bool bBold = false, bool bWrap = true)
    {
        return SNew(STextBlock).Text(Value).AutoWrapText(bWrap)
            .Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size))
            .ColorAndOpacity(Color);
    }

    template <typename T>
    void Cycle(T& Current, const TArray<T>& Choices, int32 Direction)
    {
        if (Choices.IsEmpty()) return;
        const int32 Position = Choices.IndexOfByKey(Current);
        const int32 Next = Position == INDEX_NONE ? (Direction > 0 ? 0 : Choices.Num() - 1)
            : (Position + Direction + Choices.Num()) % Choices.Num();
        Current = Choices[Next];
    }

    FText WindowModeName(EWindowMode::Type Mode)
    {
        switch (Mode)
        {
        case EWindowMode::Windowed: return Copy(EXPEDITION_TEXT("窗口", "Windowed"));
        case EWindowMode::WindowedFullscreen: return Copy(EXPEDITION_TEXT("无边框全屏", "Borderless fullscreen"));
        default: return Copy(EXPEDITION_TEXT("全屏", "Fullscreen"));
        }
    }

    FText QualityName(int32 Quality)
    {
        switch (Quality)
        {
        case 0: return Copy(EXPEDITION_TEXT("低", "Low"));
        case 1: return Copy(EXPEDITION_TEXT("中", "Medium"));
        case 2: return Copy(EXPEDITION_TEXT("高", "High"));
        case 3: return Copy(EXPEDITION_TEXT("极高", "Epic"));
        case 4: return Copy(EXPEDITION_TEXT("影视级", "Cinematic"));
        default: return Copy(EXPEDITION_TEXT("自定义", "Custom"));
        }
    }

    FText SizeText(FIntPoint Resolution)
    {
        return FText::FromString(FString::Printf(TEXT("%d × %d"), Resolution.X, Resolution.Y));
    }
}

using IslandClientPanel::Copy;

void SIslandClientSettingsPanel::Construct(const FArguments& Args)
{
    OnClose = Args._OnClose;
    OnAction = Args._OnAction;
    Settings = UIslandClientSettings::Get();
    if (Settings.IsValid())
    {
        Saved = Settings->ReadDraft();
        Draft = Saved;
        UpdateResolutions(true);
    }
    auto Navigation = MakeShared<FNavigationMetaData>();
    Navigation->SetNavigationWrap(EUINavigation::Next);
    Navigation->SetNavigationWrap(EUINavigation::Previous);
    AddMetadata(Navigation);
    Build();
}

SIslandClientSettingsPanel::~SIslandClientSettingsPanel()
{
    if (bPreviewing && Settings.IsValid()) Settings->CancelDisplayPreview();
}

TSharedRef<SWidget> SIslandClientSettingsPanel::FocusTarget()
{
    return FirstFocus.IsValid() ? FirstFocus.ToSharedRef() : AsShared();
}

void SIslandClientSettingsPanel::Back()
{
    if (bPreviewing) Revert();
    else OnClose.ExecuteIfBound();
}

FReply SIslandClientSettingsPanel::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::Gamepad_FaceButton_Right)
    {
        if (!Event.IsRepeat()) Back();
        return FReply::Handled();
    }
    return SCompoundWidget::OnPreviewKeyDown(Geometry, Event);
}

TSharedRef<SWidget> SIslandClientSettingsPanel::Button(FName Tag, TAttribute<FText> Caption,
    TFunction<void()> Action, bool bStrong, TAttribute<bool> Enabled, TAttribute<FText> AccessibleName)
{
    static const FButtonStyle Plain = IslandMenuStyle::MakeButtonStyle(false);
    static const FButtonStyle Strong = IslandMenuStyle::MakeButtonStyle(true);
    return SNew(SButton).Tag(Tag).ButtonStyle(bStrong ? &Strong : &Plain)
        .IsEnabled(Enabled).AccessibleText(AccessibleName.IsSet() ? AccessibleName : Caption)
        .HAlign(HAlign_Center).VAlign(VAlign_Center)
        .OnClicked_Lambda([this, Press = MoveTemp(Action)]
        {
            OnAction.ExecuteIfBound();
            Press();
            return FReply::Handled();
        })
        [IslandClientPanel::Text(Caption, 16, bStrong ? FLinearColor(.018f,.034f,.034f) : IslandMenuStyle::Ink, bStrong, false)];
}

void SIslandClientSettingsPanel::Build()
{
    using IslandClientPanel::Text;
    using namespace IslandMenuStyle;
    FirstFocus.Reset();
    Rows.Reset();
    auto Content = SNew(SVerticalBox);
    ChildSlot.HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(24,8)
    [
        SNew(SBox).WidthOverride(572)
        [
            SNew(SBorder).BorderImage(&Backing).Padding(0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(3)[ SNew(SBorder).BorderImage(&Accent) ] ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(20,16)[Content]
            ]
        ]
    ];

    if (!Settings.IsValid())
    {
        Content->AddSlot().AutoHeight().Padding(0,0,0,12)
            [Text(Copy(EXPEDITION_TEXT("无法读取游戏设置", "Game settings unavailable")), 22, Alert, true)];
        FirstFocus = Button("Island.Client.Back", Copy(EXPEDITION_TEXT("返回", "Back")), [this]{ Back(); });
        Content->AddSlot().AutoHeight()[FirstFocus.ToSharedRef()];
        return;
    }

    const FText Title = bPreviewing ? Copy(EXPEDITION_TEXT("保留显示设置？", "Keep these display settings?"))
        : Copy(EXPEDITION_TEXT("游戏设置", "Game settings"));
    Content->AddSlot().AutoHeight().Padding(0,0,0,12)[Text(Title, 31, Ink, true)];
    if (bPreviewing)
    {
        Content->AddSlot().AutoHeight().Padding(0,0,0,5)[Text(IslandClientPanel::WindowModeName(Draft.WindowMode), 18, Ink)];
        if (Draft.WindowMode != EWindowMode::WindowedFullscreen)
            Content->AddSlot().AutoHeight().Padding(0,0,0,5)[Text(IslandClientPanel::SizeText(Draft.Resolution), 18, Ink)];
        auto Countdown = TAttribute<FText>::CreateLambda([this]
        {
            const int32 Seconds = FMath::Max(0, FMath::CeilToInt(Deadline - FPlatformTime::Seconds()));
            return FText::Format(Copy(EXPEDITION_TEXT("{0} 秒后恢复原显示设置。", "Previous display settings return in {0} s.")), Seconds);
        });
        Content->AddSlot().AutoHeight().Padding(0,4,0,16)[Text(Countdown, 16, Alert)];
        FirstFocus = Button("Island.Client.Revert", Copy(EXPEDITION_TEXT("恢复原显示", "Revert display")), [this]{ Revert(); });
        Content->AddSlot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[FirstFocus.ToSharedRef()]
            + SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)
                [Button("Island.Client.Keep", Copy(EXPEDITION_TEXT("保留并保存", "Keep and save")), [this]{ Keep(); }, true)]
        ];
        return;
    }

    auto Tab = [this](FName Tag, FText Name, bool bDisplay)
    {
        auto Item = Button(Tag, Name, [this, bDisplay]{ SelectTab(bDisplay); });
        const bool bSelected = bDisplay == bDisplayTab;
        if (bSelected) FirstFocus = Item;
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Item]
            + SVerticalBox::Slot().AutoHeight()
                [SNew(SBox).HeightOverride(3)[SNew(SBorder).BorderImage(&IslandMenuStyle::Accent)
                    .Visibility(bSelected ? EVisibility::HitTestInvisible : EVisibility::Hidden)]];
    };
    Content->AddSlot().AutoHeight().Padding(0,0,0,9)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,2,0)
            [Tab("Island.Client.Display", Copy(EXPEDITION_TEXT("显示", "Display")), true)]
        + SHorizontalBox::Slot().FillWidth(2).Padding(2,0,0,0)
            [Tab("Island.Client.General", Copy(EXPEDITION_TEXT("语言、声音与操作", "Language, audio & controls")), false)]
    ];

    Content->AddSlot().FillHeight(1)
    [
        SNew(SScrollBox).ScrollBarPadding(FMargin(0))
            .ScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll)
            .AllowOverscroll(EAllowOverscroll::No)
        + SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]
    ];
    if (bDisplayTab) BuildDisplay();
    else BuildGeneral();

    Content->AddSlot().AutoHeight().Padding(0,14,0,0)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()
            [Button("Island.Client.Defaults", Copy(EXPEDITION_TEXT("恢复默认", "Defaults")), [this]
            {
                Draft = Settings->DefaultDraft();
                if (!Settings->CanChangeDisplay())
                {
                    Draft.WindowMode = Saved.WindowMode;
                    Draft.Resolution = Saved.Resolution;
                }
                UpdateResolutions(true);
            })]
        + SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
            [Button("Island.Client.Back", Copy(EXPEDITION_TEXT("取消", "Cancel")), [this]{ Back(); })]
        + SHorizontalBox::Slot().AutoWidth()
            [Button("Island.Client.Apply", Copy(EXPEDITION_TEXT("应用", "Apply")), [this]{ Apply(); }, true,
                TAttribute<bool>::CreateLambda([this]{ return !(Draft == Saved); }))]
    ];
}

void SIslandClientSettingsPanel::SelectTab(bool bShowDisplay)
{
    if (bDisplayTab == bShowDisplay) return;
    bDisplayTab = bShowDisplay;
    Build();
    FSlateApplication::Get().SetKeyboardFocus(FirstFocus, EFocusCause::SetDirectly);
}

void SIslandClientSettingsPanel::Row(const FText& Name, TSharedRef<SWidget> Control)
{
    Rows->AddSlot().AutoHeight().Padding(0,3)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(.92f).Padding(0,0,8,0).VAlign(VAlign_Center)
            [IslandClientPanel::Text(Name, 16, IslandMenuStyle::Ink)]
        + SHorizontalBox::Slot().FillWidth(1.08f).VAlign(VAlign_Center)[Control]
    ];
}

void SIslandClientSettingsPanel::Choice(FName Tag, const FText& Name, TAttribute<FText> Value,
    TFunction<void(int32)> Step, TAttribute<bool> Enabled)
{
    const FText Previous = FText::Format(FText::FromString(EXPEDITION_TEXT("上一个{0}选项", "Previous {0} option")), Name);
    const FText Next = FText::Format(FText::FromString(EXPEDITION_TEXT("下一个{0}选项", "Next {0} option")), Name);
    auto PreviousButton = Button(FName(*(Tag.ToString() + TEXT(".Previous"))), Copy(TEXT("‹")), [Step]{ Step(-1); }, false, Enabled, Previous);
    PreviousButton->SetToolTipText(Previous);
    // The value is a forward control too, so both hit targets remain easy to reach in a small window.
    auto Caption = TAttribute<FText>::CreateLambda([Value]
    {
        return FText::Format(FText::FromString(TEXT("{0}   ›")), Value.Get());
    });
    auto NextButton = Button(Tag, Caption, [Step]{ Step(1); }, false, Enabled, IslandClientPanel::NameAndValue(Next, Value));
    NextButton->SetToolTipText(Next);
    Row(Name, SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,2,0)[PreviousButton]
        + SHorizontalBox::Slot().FillWidth(1)[NextButton]);
}

void SIslandClientSettingsPanel::Toggle(FName Tag, const FText& Name, bool FIslandSettingsDraft::* Field)
{
    auto Value = TAttribute<FText>::CreateLambda([this, Field]
    {
        return Copy(Draft.*Field ? EXPEDITION_TEXT("开启", "On") : EXPEDITION_TEXT("关闭", "Off"));
    });
    Row(Name, Button(Tag, Value, [this, Field]{ Draft.*Field = !(Draft.*Field); }, false, true,
        IslandClientPanel::NameAndValue(Name, Value)));
}

void SIslandClientSettingsPanel::UpdateResolutions(bool bKeepCurrent)
{
    Resolutions = Draft.WindowMode == EWindowMode::Fullscreen
        ? Settings->GetFullscreenResolutions() : Settings->GetWindowResolutions();
    if (bKeepCurrent && Draft.Resolution.X > 0 && Draft.Resolution.Y > 0) Resolutions.AddUnique(Draft.Resolution);
    if (!Resolutions.IsEmpty() && !Resolutions.Contains(Draft.Resolution))
    {
        // When moving between monitors or modes, retain the closest available size.
        FIntPoint BestSize = Resolutions[0];
        int64 BestDistance = MAX_int64;
        for (const FIntPoint Size : Resolutions)
        {
            const int64 WidthDelta = int64(Size.X) - Draft.Resolution.X;
            const int64 HeightDelta = int64(Size.Y) - Draft.Resolution.Y;
            const int64 Distance = WidthDelta * WidthDelta + HeightDelta * HeightDelta;
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestSize = Size;
            }
        }
        Draft.Resolution = BestSize;
    }
}

void SIslandClientSettingsPanel::BuildDisplay()
{
    const bool bCanResize = Settings->CanChangeDisplay();
    Choice("Island.Client.WindowMode", Copy(EXPEDITION_TEXT("显示模式", "Display mode")),
        TAttribute<FText>::CreateLambda([this]{ return IslandClientPanel::WindowModeName(Draft.WindowMode); }),
        [this](int32 Direction)
        {
            static const TArray<EWindowMode::Type> Modes = { EWindowMode::Windowed, EWindowMode::WindowedFullscreen
#if !PLATFORM_MAC
                , EWindowMode::Fullscreen
#endif
            };
            IslandClientPanel::Cycle(Draft.WindowMode, Modes, Direction);
            UpdateResolutions(false);
        }, bCanResize);

    Choice("Island.Client.Resolution", Copy(EXPEDITION_TEXT("分辨率", "Resolution")),
        TAttribute<FText>::CreateLambda([this]
        {
            return Draft.WindowMode == EWindowMode::WindowedFullscreen
                ? Copy(EXPEDITION_TEXT("桌面分辨率", "Desktop resolution")) : IslandClientPanel::SizeText(Draft.Resolution);
        }), [this](int32 Direction){ IslandClientPanel::Cycle(Draft.Resolution, Resolutions, Direction); },
        TAttribute<bool>::CreateLambda([this, bCanResize]
        {
            return bCanResize && Draft.WindowMode != EWindowMode::WindowedFullscreen && Resolutions.Num() > 1;
        }));
    if (!bCanResize)
        Rows->AddSlot().AutoHeight().Padding(0,2,0,6)
            [IslandClientPanel::Text(Copy(EXPEDITION_TEXT("在独立游戏窗口中可更改显示模式和分辨率。",
                "Display mode and resolution can be changed in a standalone game window.")), 14, IslandMenuStyle::Muted)];
    Choice("Island.Client.Quality", Copy(EXPEDITION_TEXT("画质预设", "Graphics quality")),
        TAttribute<FText>::CreateLambda([this]{ return IslandClientPanel::QualityName(Draft.Quality); }),
        [this](int32 Direction)
        {
            static const TArray<int32> Levels = { 0, 1, 2, 3, 4 };
            IslandClientPanel::Cycle(Draft.Quality, Levels, Direction);
        });
    Choice("Island.Client.FrameLimit", Copy(EXPEDITION_TEXT("帧率上限", "Frame rate limit")),
        TAttribute<FText>::CreateLambda([this]
        {
            return Draft.FrameLimit == 0 ? Copy(EXPEDITION_TEXT("不限", "Unlimited"))
                : FText::FromString(FString::Printf(TEXT("%d FPS"), Draft.FrameLimit));
        }), [this](int32 Direction)
        {
            static const TArray<int32> Limits = { 30, 60, 120, 144, 0 };
            IslandClientPanel::Cycle(Draft.FrameLimit, Limits, Direction);
        });
    Toggle("Island.Client.VSync", Copy(EXPEDITION_TEXT("垂直同步", "Vertical sync")), &FIslandSettingsDraft::bVSync);
}

void SIslandClientSettingsPanel::Slider(FName Tag, const FText& Name, float FIslandSettingsDraft::* Field,
    float Minimum, float Maximum, float Increment, TAttribute<FText> Readout)
{
    static const FSliderStyle Style = []
    {
        FSliderStyle Result = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
        FSlateColorBrush Handle(IslandMenuStyle::Ink);
        Handle.SetImageSize(FVector2f(10,18));
        Result.SetBarThickness(4).SetNormalThumbImage(Handle).SetHoveredThumbImage(Handle)
            .SetNormalBarImage(FSlateColorBrush(FLinearColor(.25f,.35f,.34f)))
            .SetHoveredBarImage(IslandMenuStyle::Accent);
        return Result;
    }();
    Row(Name, SNew(SBox).MinDesiredHeight(36)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(4,0,8,0)
                [SNew(SSlider).Tag(Tag).AccessibleText(Name).MinValue(Minimum).MaxValue(Maximum).StepSize(Increment)
                    .MouseUsesStep(true).RequiresControllerLock(false)
                    .Style(&Style)
                    .Value_Lambda([this, Field]{ return Draft.*Field; })
                    .OnValueChanged_Lambda([this, Field, Minimum, Maximum, Increment](float Value)
                    {
                        Draft.*Field = FMath::Clamp(FMath::GridSnap(Value - Minimum, Increment) + Minimum, Minimum, Maximum);
                    })]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(62).HAlign(HAlign_Right)[IslandClientPanel::Text(Readout, 16, IslandMenuStyle::Ink)]]
        ]);
}

void SIslandClientSettingsPanel::BuildGeneral()
{
    Choice("Island.Client.Language", Copy(TEXT("语言 / Language")),
        TAttribute<FText>::CreateLambda([this]{ return Copy(Draft.Language == TEXT("en") ? TEXT("English") : TEXT("简体中文")); }),
        [this](int32){ Draft.Language = Draft.Language == TEXT("en") ? TEXT("zh-Hans") : TEXT("en"); });
    Slider("Island.Client.MasterVolume", Copy(EXPEDITION_TEXT("主音量", "Master volume")), &FIslandSettingsDraft::MasterVolume, 0.f, 1.f, .05f,
        TAttribute<FText>::CreateLambda([this]{ return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Draft.MasterVolume * 100.f))); }));
    Slider("Island.Client.LookSensitivity", Copy(EXPEDITION_TEXT("视角灵敏度", "Look sensitivity")), &FIslandSettingsDraft::LookSensitivity, .25f, 3.f, .05f,
        TAttribute<FText>::CreateLambda([this]{ return FText::FromString(FString::Printf(TEXT("%.2f×"), Draft.LookSensitivity)); }));
    Toggle("Island.Client.InvertY", Copy(EXPEDITION_TEXT("反转视角 Y 轴", "Invert vertical look")), &FIslandSettingsDraft::bInvertY);
    Toggle("Island.Client.CameraShake", Copy(EXPEDITION_TEXT("镜头晃动", "Camera shake")), &FIslandSettingsDraft::bCameraShake);
}

void SIslandClientSettingsPanel::Apply()
{
    if (!Settings.IsValid() || bPreviewing) return;
    const bool bEditedDisplay = Draft.WindowMode != Saved.WindowMode || Draft.Resolution != Saved.Resolution;
    if (bEditedDisplay)
    {
        // The native window may have been resized while the menu was open.
        const FIslandSettingsDraft Live = Settings->ReadDraft();
        Saved.WindowMode = Live.WindowMode;
        Saved.Resolution = Live.Resolution;
    }
    if (!bEditedDisplay || !Settings->BeginDisplayPreview(Draft))
    {
        Settings->CommitDraft(Draft);
        OnClose.ExecuteIfBound();
        return;
    }
    bPreviewing = true;
    Deadline = FPlatformTime::Seconds() + 15.0;
    Build();
    FSlateApplication::Get().SetKeyboardFocus(FirstFocus, EFocusCause::SetDirectly);
    RegisterActiveTimer(.2f, FWidgetActiveTimerDelegate::CreateSP(this, &SIslandClientSettingsPanel::CheckDeadline));
}

void SIslandClientSettingsPanel::Keep()
{
    if (!bPreviewing || !Settings.IsValid()) return;
    if (FPlatformTime::Seconds() >= Deadline)
    {
        Revert();
        return;
    }
    Settings->CommitDraft(Draft);
    bPreviewing = false;
    OnClose.ExecuteIfBound();
}

void SIslandClientSettingsPanel::Revert()
{
    if (!bPreviewing || !Settings.IsValid()) return;
    Settings->CancelDisplayPreview();
    bPreviewing = false;
    Draft.WindowMode = Saved.WindowMode;
    Draft.Resolution = Saved.Resolution;
    UpdateResolutions(true);
    Build();
    FSlateApplication::Get().SetKeyboardFocus(FirstFocus, EFocusCause::SetDirectly);
}

EActiveTimerReturnType SIslandClientSettingsPanel::CheckDeadline(double Now, float Delta)
{
    if (!bPreviewing) return EActiveTimerReturnType::Stop;
    if (FPlatformTime::Seconds() < Deadline) return EActiveTimerReturnType::Continue;
    Revert();
    return EActiveTimerReturnType::Stop;
}

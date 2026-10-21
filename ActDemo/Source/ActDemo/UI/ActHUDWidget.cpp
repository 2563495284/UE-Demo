#include "UI/ActHUDWidget.h"
#include "Character/ActCharacter.h"
#include "Character/ActPlayerController.h"
#include "Combat/ActAttributeSet.h"
#include "World/ActGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"

namespace
{
 const FLinearColor Ink(.025f,.032f,.042f,.94f), Gold(.88f,.65f,.28f,1), White(.91f,.91f,.88f,1);
 UTextBlock* Text(UWidgetTree* T, FName Name, FString Value, int32 Size, FLinearColor Color=White)
 {
  auto* W=T->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name); W->SetText(FText::FromString(Value));
  FSlateFontInfo Font=W->GetFont(); Font.Size=Size; W->SetFont(Font); W->SetColorAndOpacity(Color); W->SetAutoWrapText(true); return W;
 }
 void Place(UCanvasPanel* Root, UWidget* Widget, FVector2D Position, FVector2D Size, FAnchors Anchors=FAnchors(0,0), FVector2D Alignment=FVector2D::ZeroVector)
 {
  auto* Slot=Root->AddChildToCanvas(Widget); Slot->SetAnchors(Anchors); Slot->SetAlignment(Alignment); Slot->SetPosition(Position); Slot->SetSize(Size);
 }
 UProgressBar* Bar(UWidgetTree* T, FName Name, FLinearColor Color)
 {
  auto* W=T->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name); W->SetFillColorAndOpacity(Color); W->SetPercent(1.f); return W;
 }
 UButton* Button(UWidgetTree* T, UVerticalBox* Box, FName Name, FString Label)
 {
  auto* B=T->ConstructWidget<UButton>(UButton::StaticClass(),Name); B->SetBackgroundColor(FLinearColor(.18,.20,.23,1));
  auto* LabelWidget=Text(T,NAME_None,Label,18); LabelWidget->SetAutoWrapText(false); LabelWidget->SetJustification(ETextJustify::Center); B->AddChild(LabelWidget);
  auto* Slot=Box->AddChildToVerticalBox(B); Slot->SetPadding(FMargin(0,8)); return B;
 }
}
void UActHUDWidget::CreateLayout(UWidgetTree* Tree)
{
 auto* Root=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("Root")); Tree->RootWidget=Root;
 auto* Panel=Tree->ConstructWidget<UBorder>(); Panel->SetBrushColor(Ink); Place(Root,Panel,FVector2D(32,28),FVector2D(340,145));
 Place(Root,Text(Tree,TEXT("Title"),TEXT("ASHEN COURT  /  ACT DEMO"),16,Gold),FVector2D(48,40),FVector2D(310,28));
 Place(Root,Bar(Tree,TEXT("Health"),FLinearColor(.72,.16,.12,1)),FVector2D(48,78),FVector2D(300,17));
 Place(Root,Bar(Tree,TEXT("Stamina"),FLinearColor(.24,.65,.43,1)),FVector2D(48,103),FVector2D(300,10));
 Place(Root,Text(Tree,TEXT("Stats"),TEXT("FLASKS  3 / 3"),14),FVector2D(48,128),FVector2D(305,32));
 Place(Root,Text(Tree,TEXT("Objective"),TEXT("Reach the beacon"),16),FVector2D(32,190),FVector2D(390,100));
 auto* Instructions=Text(Tree,TEXT("Instructions"),TEXT("WASD  Move     SHIFT  Sprint     SPACE  Evade\nLMB  Light / chain     RMB  Heavy     Q  Flask\nTAB  Lock     E  Switch target     ESC  Pause     F1  Debug"),14);
 Place(Root,Instructions,FVector2D(32,-100),FVector2D(660,85),FAnchors(0,1));
 Place(Root,Text(Tree,TEXT("BossName"),TEXT("WARDEN OF ASH"),18,Gold),FVector2D(0,-170),FVector2D(500,30),FAnchors(.5,1),FVector2D(.5,0));
 Place(Root,Bar(Tree,TEXT("BossHealth"),FLinearColor(.65,.13,.1,1)),FVector2D(0,-130),FVector2D(500,12),FAnchors(.5,1),FVector2D(.5,0));
 auto* Marker=Text(Tree,TEXT("LockMarker"),TEXT("◇"),28,Gold); Marker->SetJustification(ETextJustify::Center); Place(Root,Marker,FVector2D::ZeroVector,FVector2D(40,40));
 auto* Modal=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("Overlay")); Modal->SetBrushColor(Ink); Modal->SetPadding(FMargin(36));
 Place(Root,Modal,FVector2D::ZeroVector,FVector2D(540,390),FAnchors(.5,.5),FVector2D(.5,.5));
 auto* Box=Tree->ConstructWidget<UVerticalBox>(); Modal->AddChild(Box);
 auto* Heading=Text(Tree,TEXT("OverlayTitle"),TEXT("PAUSED"),34,Gold); Heading->SetJustification(ETextJustify::Center); Box->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0,0,0,18));
 auto* Body=Text(Tree,TEXT("OverlayBody"),TEXT("Take a breath. The court can wait."),16); Body->SetJustification(ETextJustify::Center); Box->AddChildToVerticalBox(Body)->SetPadding(FMargin(0,0,0,20));
 Button(Tree,Box,TEXT("ResumeButton"),TEXT("RESUME")); Button(Tree,Box,TEXT("RetryButton"),TEXT("RETRY FROM BEACON  [R]")); Button(Tree,Box,TEXT("RestartButton"),TEXT("START NEW RUN"));
}
TSharedRef<SWidget> UActHUDWidget::RebuildWidget()
{
 if (!WidgetTree->RootWidget) CreateLayout(WidgetTree);
 return Super::RebuildWidget();
}
void UActHUDWidget::NativeConstruct()
{
 Super::NativeConstruct();
 if (!WidgetTree->RootWidget) CreateLayout(WidgetTree);
#define ACT_BIND(Type, Field) Field=Cast<Type>(GetWidgetFromName(TEXT(#Field)))
 ACT_BIND(UProgressBar,Health); ACT_BIND(UProgressBar,Stamina); ACT_BIND(UProgressBar,BossHealth);
 ACT_BIND(UTextBlock,Stats); ACT_BIND(UTextBlock,Objective); ACT_BIND(UTextBlock,BossName); ACT_BIND(UTextBlock,OverlayTitle); ACT_BIND(UTextBlock,OverlayBody); ACT_BIND(UTextBlock,LockMarker);
 ACT_BIND(UBorder,Overlay); ACT_BIND(UButton,ResumeButton); ACT_BIND(UButton,RetryButton); ACT_BIND(UButton,RestartButton);
#undef ACT_BIND
 ResumeButton->OnClicked.AddDynamic(this,&UActHUDWidget::Resume); RetryButton->OnClicked.AddDynamic(this,&UActHUDWidget::Retry); RestartButton->OnClicked.AddDynamic(this,&UActHUDWidget::Restart);
 if (auto* Mode=GetWorld()->GetAuthGameMode<AActGameMode>()) Mode->OnRunChanged.AddUObject(this,&UActHUDWidget::Refresh);
 Refresh();
}
void UActHUDWidget::Observe(AActCharacter* Character)
{
 if (Player.IsValid()) Player->OnChanged.RemoveAll(this);
 Player=Character;
 if (Character) Character->OnChanged.AddUObject(this,&UActHUDWidget::Refresh);
 Refresh();
}
void UActHUDWidget::Refresh()
{
 if (!Health) return;
 if (Player.IsValid())
 {
  Health->SetPercent(Player->HealthFraction()); Stamina->SetPercent(Player->StaminaFraction());
  Stats->SetText(FText::FromString(FString::Printf(TEXT("HP  %.0f / %.0f     FLASKS  %d"),Player->Attributes->GetHealth(),Player->Attributes->GetMaxHealth(),Player->HealingCharges)));
 }
 auto* Mode=GetWorld()->GetAuthGameMode<AActGameMode>(); if (!Mode) return;
 Objective->SetText(Mode->Objective);
 if (ObservedBoss.Get()!=Mode->Boss)
 {
  if (ObservedBoss.IsValid()) ObservedBoss->OnChanged.RemoveAll(this);
  ObservedBoss=Mode->Boss;
  if (ObservedBoss.IsValid()) ObservedBoss->OnChanged.AddUObject(this,&UActHUDWidget::Refresh);
 }
 const bool bBoss=Mode->bCheckpoint && ObservedBoss.IsValid() && ObservedBoss->IsAlive();
 BossHealth->SetVisibility(bBoss?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 BossName->SetVisibility(bBoss?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if (bBoss) { BossHealth->SetPercent(ObservedBoss->HealthFraction()); BossName->SetText(FText::FromString(ObservedBoss->bBossPhaseTwo?TEXT("WARDEN OF ASH  /  UNBOUND"):TEXT("WARDEN OF ASH"))); }
 const bool Paused=GetOwningPlayer()->IsPaused();
 Overlay->SetVisibility(Paused || Mode->Outcome!=EActOutcome::Playing?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 ResumeButton->SetVisibility(Paused?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 const bool Victory=Mode->Outcome==EActOutcome::Victory;
 OverlayTitle->SetText(FText::FromString(Victory?TEXT("COURT RECLAIMED"):Mode->Outcome==EActOutcome::Defeat?TEXT("YOU FELL"):TEXT("PAUSED")));
 OverlayBody->SetText(FText::FromString(Victory?TEXT("The Warden is defeated. The beacon burns again."):Mode->Outcome==EActOutcome::Defeat?TEXT("Read the wind-up. Save stamina for your escape."):TEXT("Take a breath. The court can wait.")));
}
void UActHUDWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
 Super::NativeTick(Geometry,Dt);
 if (!LockMarker) return;
 auto* Target=Player.IsValid()?Player->LockTarget.Get():nullptr; FVector2D Point;
 bool Visible=IsValid(Target) && Target->IsAlive() && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),Target->GetActorLocation()+FVector(0,0,45),Point,false);
 LockMarker->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if (Visible) if (auto* Slot=Cast<UCanvasPanelSlot>(LockMarker->Slot)) Slot->SetPosition(Point-FVector2D(20,20));
}
void UActHUDWidget::NativeDestruct()
{
 if (Player.IsValid()) Player->OnChanged.RemoveAll(this);
 if (ObservedBoss.IsValid()) ObservedBoss->OnChanged.RemoveAll(this);
 if (auto* Mode=GetWorld()->GetAuthGameMode<AActGameMode>()) Mode->OnRunChanged.RemoveAll(this);
 Super::NativeDestruct();
}
void UActHUDWidget::Resume() { if (auto* PC=Cast<AActPlayerController>(GetOwningPlayer())) PC->TogglePause(); }
void UActHUDWidget::Retry() { if (auto* PC=Cast<AActPlayerController>(GetOwningPlayer())) PC->Retry(); }
void UActHUDWidget::Restart() { if (auto* PC=Cast<AActPlayerController>(GetOwningPlayer())) PC->RestartRun(); }
TSharedRef<SWidget> UActLifeBarWidget::RebuildWidget()
{
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget=Box;
 Name=Text(WidgetTree,TEXT("Name"),TEXT("ENEMY"),10); Name->SetJustification(ETextJustify::Center); Box->AddChildToVerticalBox(Name);
 Health=Bar(WidgetTree,TEXT("Health"),FLinearColor(.75,.18,.12,1));
 auto* Size=WidgetTree->ConstructWidget<USizeBox>(); Size->SetHeightOverride(6.f); Size->AddChild(Health); Box->AddChildToVerticalBox(Size);
 return Super::RebuildWidget();
}
void UActLifeBarWidget::NativeConstruct() { Super::NativeConstruct(); Refresh(); }
void UActLifeBarWidget::Observe(AActCharacter* Character)
{
 if (Target.IsValid()) Target->OnChanged.RemoveAll(this); Target=Character;
 if (Character) Character->OnChanged.AddUObject(this,&UActLifeBarWidget::Refresh); Refresh();
}
void UActLifeBarWidget::Refresh() { if (Health && Target.IsValid()) { Health->SetPercent(Target->HealthFraction()); Name->SetText(Target->DisplayName); } }
void UActLifeBarWidget::NativeDestruct() { if (Target.IsValid()) Target->OnChanged.RemoveAll(this); Super::NativeDestruct(); }

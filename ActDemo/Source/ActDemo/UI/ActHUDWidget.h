#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActHUDWidget.generated.h"
class AActCharacter;
class UProgressBar;
class UTextBlock;
class UBorder;
class UWidgetTree;
class UButton;

UCLASS()
class ACTDEMO_API UActHUDWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 static void CreateLayout(UWidgetTree* Tree);
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual void NativeTick(const FGeometry& Geometry, float Dt) override;
 void Observe(AActCharacter* Character);
 UFUNCTION(BlueprintCallable) void Refresh();
 UFUNCTION() void Resume();
 UFUNCTION() void Retry();
 UFUNCTION() void Restart();
private:
 TWeakObjectPtr<AActCharacter> Player;
 TWeakObjectPtr<AActCharacter> ObservedBoss;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> Health;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> Stamina;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> BossHealth;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Stats;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Objective;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BossName;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OverlayTitle;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OverlayBody;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> LockMarker;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> Overlay;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ResumeButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> RetryButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> RestartButton;
};
UCLASS()
class ACTDEMO_API UActLifeBarWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 void Observe(AActCharacter* Character);
 void Refresh();
private:
 TWeakObjectPtr<AActCharacter> Target;
 UPROPERTY() TObjectPtr<UProgressBar> Health;
 UPROPERTY() TObjectPtr<UTextBlock> Name;
};

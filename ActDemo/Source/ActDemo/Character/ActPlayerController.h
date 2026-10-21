#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "ActPlayerController.generated.h"
class UActInputConfig;
class UActHUDWidget;
class AActCharacter;
UCLASS()
class ACTDEMO_API AActPlayerController : public APlayerController
{
 GENERATED_BODY()
public:
 AActPlayerController();
 virtual void BeginPlay() override;
 virtual void SetupInputComponent() override;
 virtual void OnPossess(APawn* Pawn) override;
 UFUNCTION(BlueprintCallable) void TogglePause();
 UFUNCTION(BlueprintCallable) void Retry();
 UFUNCTION(BlueprintCallable) void RestartRun();
 void ShowOutcome();
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<UActInputConfig> InputConfig;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<UActHUDWidget> HUD;
private:
 void UpdateInputMode(bool bMenu);
 void Move(const FInputActionValue& Value);
 void StopMove();
 void Look(const FInputActionValue& Value);
 void Light(); void Heavy(); void Dodge(); void Heal(); void Lock(); void Switch();
 void SprintStart(); void SprintStop(); void Debug();
 AActCharacter* Hero() const;
};

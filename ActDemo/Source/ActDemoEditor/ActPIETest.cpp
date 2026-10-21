#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Character/ActCharacter.h"
#include "Character/ActPlayerController.h"
#include "AI/ActAIController.h"
#include "UI/ActHUDWidget.h"
#include "Components/StateTreeAIComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FActVerifyPIE, FAutomationTestBase*, Test);
bool FActVerifyPIE::Update()
{
 UWorld* World=GEditor->PlayWorld;
 Test->TestNotNull(TEXT("PIE world exists"),World);
 if (!World) return true;
 auto* PC=Cast<AActPlayerController>(World->GetFirstPlayerController());
 Test->TestNotNull(TEXT("PIE player controller"),PC);
 if (PC)
 {
  auto* Hero=Cast<AActCharacter>(PC->GetPawn());
  Test->TestTrue(TEXT("PIE player initializes"),Hero && Hero->IsAlive());
  Test->TestTrue(TEXT("UMG is added to PIE viewport"),PC->HUD && PC->HUD->IsInViewport());
  PC->TogglePause(); Test->TestTrue(TEXT("PIE pause"),PC->IsPaused()); PC->TogglePause();
 }
 int32 Running=0;
 for(TActorIterator<AActAIController> It(World);It;++It) if(It->StateTree->IsRunning()) ++Running;
 Test->TestEqual(TEXT("Six PIE StateTrees"),Running,6);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActPIETest,"ActDemo.Editor.PIE",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActPIETest::RunTest(const FString&)
{
 AutomationOpenMap(TEXT("/Game/ActDemo/Maps/L_AshenCourt"));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
 ADD_LATENT_AUTOMATION_COMMAND(FActVerifyPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif

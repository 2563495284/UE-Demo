#include "ActVoxelEditorMode.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FActDemoEditorModule final : public IModuleInterface
{
    void StartupModule() override
    {
        ToggleVoxelMode = IConsoleManager::Get().RegisterConsoleCommand(TEXT("Act.VoxelMap"),
            TEXT("Open the Voxel Map editor mode"), FConsoleCommandDelegate::CreateLambda([]
            {
                GLevelEditorModeTools().ActivateMode(FActVoxelEditorMode::ID);
            }));
        FEditorModeRegistry::Get().RegisterMode<FActVoxelEditorMode>(FActVoxelEditorMode::ID,
            NSLOCTEXT("ActVoxelEditor", "ModeName", "Voxel Map"), FSlateIcon(), true);
    }

    void ShutdownModule() override
    {
        IConsoleManager::Get().UnregisterConsoleObject(ToggleVoxelMode);
        FEditorModeRegistry::Get().UnregisterMode(FActVoxelEditorMode::ID);
    }

    IConsoleObject* ToggleVoxelMode = nullptr;
};

IMPLEMENT_MODULE(FActDemoEditorModule, ActDemoEditor)

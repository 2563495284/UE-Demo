#include "Voxel/VoxelInputConfig.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

UVoxelInputConfig* UVoxelInputConfig::Create(UObject& Owner)
{
    UVoxelInputConfig* Config = NewObject<UVoxelInputConfig>(&Owner);
    UInputMappingContext* Gameplay = NewObject<UInputMappingContext>(Config, TEXT("IslandGameplay"));
    Config->Gameplay = Gameplay;

    Config->Move = NewObject<UInputAction>(Config, TEXT("Move"));
    Config->Move->ValueType = EInputActionValueType::Axis2D;
    for (const FKey& Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D})
    {
        FEnhancedActionKeyMapping& Mapping = Gameplay->MapKey(Config->Move, Key);
        if (Key == EKeys::A || Key == EKeys::S)
            Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Gameplay));
        if (Key == EKeys::W || Key == EKeys::S)
        {
            auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(Gameplay);
            Swizzle->Order = EInputAxisSwizzle::YXZ;
            Mapping.Modifiers.Add(Swizzle);
        }
    }

    Config->Look = NewObject<UInputAction>(Config, TEXT("Look"));
    Config->Look->ValueType = EInputActionValueType::Axis2D;
    Gameplay->MapKey(Config->Look, EKeys::Mouse2D);

    auto BindButton = [&](const TCHAR* Name, const FKey& Key)
    {
        UInputAction* Button = NewObject<UInputAction>(Config, FName(Name));
        Gameplay->MapKey(Button, Key);
        return Button;
    };
    Config->Jump = BindButton(TEXT("Jump"), EKeys::SpaceBar);
    Config->Dig = BindButton(TEXT("Dig"), EKeys::LeftMouseButton);
    Config->SecondaryTool = BindButton(TEXT("SecondaryTool"), EKeys::RightMouseButton);
    Config->Fill = BindButton(TEXT("Fill"), EKeys::Q);
    Config->Climb = BindButton(TEXT("Climb"), EKeys::LeftShift);
    Config->Recall = BindButton(TEXT("Recall"), EKeys::R);
    Config->Recall->bTriggerWhenPaused = true;
    return Config;
}

#include "Voxel/VoxelInputConfig.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

UVoxelInputConfig* UVoxelInputConfig::Create(UObject& Owner)
{
    UVoxelInputConfig* Config = NewObject<UVoxelInputConfig>(&Owner);
    UInputMappingContext* Character = NewObject<UInputMappingContext>(Config, TEXT("CharacterControls"));
    Config->CharacterControls = Character;
    UInputMappingContext* Gameplay = NewObject<UInputMappingContext>(Config, TEXT("IslandGameplay"));
    Config->Gameplay = Gameplay;

    Config->Move = NewObject<UInputAction>(Config, TEXT("Move"));
    Config->Move->ValueType = EInputActionValueType::Axis2D;
    for (const FKey& Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D})
    {
        FEnhancedActionKeyMapping& Mapping = Character->MapKey(Config->Move, Key);
        if (Key == EKeys::A || Key == EKeys::S)
            Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Character));
        if (Key == EKeys::W || Key == EKeys::S)
        {
            auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(Character);
            Swizzle->Order = EInputAxisSwizzle::YXZ;
            Mapping.Modifiers.Add(Swizzle);
        }
    }

    Config->Look = NewObject<UInputAction>(Config, TEXT("Look"));
    Config->Look->ValueType = EInputActionValueType::Axis2D;
    Character->MapKey(Config->Look, EKeys::Mouse2D);
    Config->Jump = NewObject<UInputAction>(Config, TEXT("Jump"));
    Character->MapKey(Config->Jump, EKeys::SpaceBar);

    auto BindButton = [&](const TCHAR* Name, const FKey& Key)
    {
        UInputAction* Button = NewObject<UInputAction>(Config, FName(Name));
        Gameplay->MapKey(Button, Key);
        return Button;
    };
    Config->Dig = BindButton(TEXT("Dig"), EKeys::LeftMouseButton);
    Config->SecondaryTool = BindButton(TEXT("SecondaryTool"), EKeys::RightMouseButton);
    Config->Fill = BindButton(TEXT("Fill"), EKeys::Q);
    Config->Climb = BindButton(TEXT("Climb"), EKeys::LeftShift);
    Config->Recall = BindButton(TEXT("Recall"), EKeys::R);
    Config->Recall->bTriggerWhenPaused = true;
    return Config;
}

#include "IslandPassageMaterial.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

bool IslandPassageMaterial::Save(bool bRebuild)
{
    const FString Path = TEXT("/Game/TreasureIsland/FX/M_IslandPassage");
    const bool bExists = FPackageName::DoesPackageExist(Path);
    if (bExists && !bRebuild) return true;
    FString Source;
    if (!FFileHelper::LoadFileToString(Source, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders/Private/IslandPassage.hlsl")))) return false;
    UPackage* Package = CreatePackage(*Path);
    UMaterial* Mat = bExists ? LoadObject<UMaterial>(nullptr, *(Path+TEXT(".M_IslandPassage"))) :
        NewObject<UMaterial>(Package, TEXT("M_IslandPassage"), RF_Public | RF_Standalone);
    if (!Mat) return false;
    Mat->PreEditChange(nullptr);
    Mat->GetExpressionCollection().Empty();
    Mat->MaterialDomain = MD_UI;
    Mat->BlendMode = BLEND_Translucent;
    auto* Warp = NewObject<UMaterialExpressionCustom>(Mat);
    Warp->Description = TEXT("Expedition ink currents");
    Warp->Code = MoveTemp(Source);
    Warp->OutputType = CMOT_Float4;
    Warp->Inputs.Reset();
    Mat->GetExpressionCollection().AddExpression(Warp);
    const auto Connect = [&](FName Name, UMaterialExpression* Node)
    {
        Mat->GetExpressionCollection().AddExpression(Node);
        FCustomInput& Input = Warp->Inputs.AddDefaulted_GetRef();
        Input.InputName = Name;
        Input.Input.Connect(0, Node);
    };
    Connect(TEXT("UV"), NewObject<UMaterialExpressionTextureCoordinate>(Mat));
    const auto Scalar = [&](FName Name, float Value)
    {
        auto* Node = NewObject<UMaterialExpressionScalarParameter>(Mat);
        Node->ParameterName = Name;
        Node->DefaultValue = Value;
        Node->UpdateParameterGuid(true, true);
        Connect(Name, Node);
    };
    Scalar(TEXT("Progress"), .5f);
    Scalar(TEXT("Aspect"), 16.f/9.f);
    Scalar(TEXT("Motion"), 1.f);
    Scalar(TEXT("Returning"), 0.f);
    auto* Alpha = NewObject<UMaterialExpressionComponentMask>(Mat);
    Alpha->R = Alpha->G = Alpha->B = false;
    Alpha->A = true;
    Alpha->Input.Connect(0, Warp);
    Mat->GetExpressionCollection().AddExpression(Alpha);
    Mat->GetEditorOnlyData()->EmissiveColor.Connect(0, Warp);
    Mat->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
    Mat->PostEditChange();
    if (!bExists) FAssetRegistryModule::AssetCreated(Mat);
    const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Mat, *File, Args);
}

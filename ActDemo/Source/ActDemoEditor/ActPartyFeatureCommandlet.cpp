#include "ActPartyFeatureCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "PartyMinigame.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

int32 UActPartyFeatureCommandlet::Main(const FString& Params)
{
    const FString PluginName = TEXT("PartyButtonChallenge");
    IPluginManager::Get().MountExplicitlyLoadedPlugin(PluginName);
    const TCHAR* AssetPath = TEXT("/PartyButtonChallenge/PartyButtonChallenge");
    const TSoftClassPtr<APartySession> SessionClass(FSoftObjectPath(TEXT("/Script/PartyButtonChallenge.PartyButtonSession")));
    if (!SessionClass.LoadSynchronous())
    {
        UE_LOG(LogTemp, Error, TEXT("Party feature session class is missing"));
        return 1;
    }

    UPackage* Package = CreatePackage(AssetPath);
    auto* Data = NewObject<UPartyMinigameFeatureData>(Package, *PluginName, RF_Public | RF_Standalone);
    Data->SessionClass = SessionClass;
    FAssetRegistryModule::AssetCreated(Data);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const FString File = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
    if (!UPackage::SavePackage(Package, Data, *File, Args))
    {
        return 1;
    }
    UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_ASSET_READY %s"), AssetPath);
    return 0;
}

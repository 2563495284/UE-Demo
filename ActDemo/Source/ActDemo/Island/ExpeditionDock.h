#pragma once

#include "Island/IslandBreakable.h"
#include "ExpeditionDock.generated.h"

// Local X points inland. Moving this actor also moves the crew, chart pin and preview camera.
UCLASS()
class ACTDEMO_API AExpeditionDock : public AIslandBreakable
{
    GENERATED_BODY()
public:
    AExpeditionDock();
    void RefreshLandingNumber();
    void OnConstruction(const FTransform& Transform) override;
    void BeginPlay() override;
    UPROPERTY(EditInstanceOnly, Category="Expedition") int32 DockIndex = 0;
    UPROPERTY(EditInstanceOnly, Category="Expedition", meta=(MakeEditWidget=true)) TArray<FVector> Berths;
    UPROPERTY(EditInstanceOnly, Category="Expedition", meta=(MakeEditWidget=true)) FVector PreviewEye = FVector(-670, -530, 370);
    UPROPERTY(EditInstanceOnly, Category="Expedition", meta=(MakeEditWidget=true)) FVector PreviewTarget = FVector(80, 0, 105);
    FTransform GetPreviewView() const;
    static AExpeditionDock* FindForTeam(UWorld* World, const TArray<int32>& OccupiedTeamIds, int32 TeamId);
    static bool FindStart(UWorld* World, const TArray<int32>& OccupiedTeamIds, int32 TeamId, int32 MemberIndex, FTransform& Out);
};

UCLASS()
class ACTDEMO_API AExpeditionLandmark : public AActor
{
    GENERATED_BODY()
public:
    AExpeditionLandmark();
    UPROPERTY(EditInstanceOnly, Category="Expedition", meta=(ClampMin="0", ClampMax="3")) int32 ClueIndex = 0;
};

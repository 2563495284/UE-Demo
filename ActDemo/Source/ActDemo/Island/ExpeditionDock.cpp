#include "Island/ExpeditionDock.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"

AExpeditionDock::AExpeditionDock()
{
    for (int32 Row = 0; Row < 2; ++Row)
        for (int32 Column = 0; Column < 4; ++Column)
            Berths.Add(FVector(-100 + Row * 140, (Column - 1.5) * 124, 123));
}

void AExpeditionDock::RefreshLandingNumber()
{
    UTextRenderComponent* DockSign = GetSign();
    if (!IsValid(DockSign)) DockSign = FindComponentByClass<UTextRenderComponent>();
    // Construction may run before the native render component is restored during map load.
    if (!IsValid(DockSign)) return;
    DockSign->SetRelativeLocation(FVector(270.,-308.,115.));
    DockSign->SetRelativeRotation(FRotator(0,180,0));
    DockSign->SetHorizontalAlignment(EHTA_Center);
    DockSign->SetWorldSize(69.f);
    DockSign->SetText(FText::FromString(FString::Printf(TEXT("%02d"),DockIndex+1)));
    DockSign->SetTextRenderColor(FColor(54,68,75));
}

void AExpeditionDock::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshLandingNumber();
}

void AExpeditionDock::BeginPlay()
{
    Super::BeginPlay();
    RefreshLandingNumber();
}

FTransform AExpeditionDock::GetPreviewView() const
{
    const FVector Eye = GetActorTransform().TransformPositionNoScale(PreviewEye);
    const FVector Target = GetActorTransform().TransformPositionNoScale(PreviewTarget);
    return FTransform((Target - Eye).Rotation(), Eye);
}

AExpeditionDock* AExpeditionDock::FindForTeam(UWorld* World, const TArray<int32>& OccupiedTeamIds, int32 TeamId)
{
    if (!World || TeamId < 0) return nullptr;
    TArray<int32> Ordered;
    for (const int32 Id : OccupiedTeamIds) if (Id >= 0) Ordered.AddUnique(Id);
    Ordered.Sort();
    const int32 Rank = Ordered.IndexOfByKey(TeamId);
    if (Rank == INDEX_NONE) return nullptr;
    TArray<AExpeditionDock*> Docks;
    for (TActorIterator<AExpeditionDock> It(World); It; ++It) Docks.Add(*It);
    Docks.Sort([](const AExpeditionDock& A, const AExpeditionDock& B){ return A.DockIndex < B.DockIndex; });
    if (Ordered.Num() > Docks.Num()) return nullptr;
    // Even angular spacing uses opposing coasts for two crews and distinct landings for all eight.
    return Docks[FMath::RoundToInt(double(Rank) * Docks.Num() / Ordered.Num()) % Docks.Num()];
}

bool AExpeditionDock::FindStart(UWorld* World, const TArray<int32>& OccupiedTeamIds, int32 TeamId, int32 MemberIndex, FTransform& Out)
{
    const AExpeditionDock* Dock = FindForTeam(World, OccupiedTeamIds, TeamId);
    if (!Dock || !Dock->Berths.IsValidIndex(MemberIndex)) return false;
    Out = FTransform(Dock->GetActorQuat(), Dock->GetActorTransform().TransformPositionNoScale(Dock->Berths[MemberIndex]));
    return true;
}

AExpeditionLandmark::AExpeditionLandmark()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Origin")));
}

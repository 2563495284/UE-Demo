#include "Island/IslandGameMode.h"

#include "Island/ExpeditionBodyMud.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/IslandAttachedMud.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

void AIslandGameMode::ThrowExpeditionMud(AIslandPlayerController& Player, const FVector& Aim)
{
    if (!bExpeditionRoom || !CanPlayerAct(Player) || Aim.ContainsNaN() || !Aim.IsUnit(.05)) return;
    AVoxelCharacter* Hero = Player.GetHero();
    FActorSpawnParameters Params;
    Params.Owner = &Player;
    Params.Instigator = Hero;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Ball = GetWorld()->SpawnActor<AExpeditionMudBall>(AExpeditionMudBall::StaticClass(), Hero->GetAimOrigin(), Aim.Rotation(), Params);
    if (!Ball) return;
    Ball->Launch(Aim.GetSafeNormal());
    Hero->MulticastExpeditionThrow();
}

void AIslandGameMode::ResolveMudImpact(AIslandPlayerController& Thrower, const FHitResult& Hit)
{
    const auto* Round = GetGameState<AIslandGameState>();
    if (!bExpeditionRoom || !IsPlaying() || !Round || Round->Phase != EIslandPhase::Hunting
        || !GetPlayers().Contains(&Thrower) || !Thrower.GetHero()) return;

    if (auto* Body = Cast<AVoxelCharacter>(Hit.GetActor()))
    {
        const auto* Target = Cast<AIslandPlayerController>(Body->GetController());
        if (Target && Target != &Thrower && Target->GetTeam() != Thrower.GetTeam()
            && CanPlayerPunch(*Target) && !Round->IsDecoyEventActive(EIslandDecoyEvent::Ceasefire))
        {
            Body->GetBodyMud()->AddHit(Hit);
        }
        return;
    }

    AVoxelTerrain* IslandTerrain = GetTerrain();
    if (!IslandTerrain || !Hit.GetActor()) return;
    const bool bWide = Round->HasDecoyToolBoost(Thrower.GetTeam());
    if (Hit.GetActor() == IslandTerrain)
    {
        QueueMudLanding(Hit.ImpactPoint, Hit.ImpactNormal, bWide);
        return;
    }
    FHitResult MechanismHit = Hit;
    if (const auto* Mud = Cast<AIslandAttachedMud>(Hit.GetActor()))
    {
        if (auto* Beam = Cast<UPrimitiveComponent>(Mud->GetRootComponent()->GetAttachParent()))
            MechanismHit = FHitResult(Beam->GetOwner(), Beam, Hit.ImpactPoint, Hit.ImpactNormal);
    }
    if (AIslandAttachedMud::CanReceive(Hit.GetComponent()))
    {
        FVoxelEdit Stamp;
        Stamp.Action = EVoxelAction::Fill;
        Stamp.BrushId = VoxelPlayground::MudPad;
        Stamp.Center = VoxelPlayground::QuantizeCenter(Hit.ImpactPoint);
        Stamp.SurfaceNormal = VoxelPlayground::QuantizeNormal(Hit.ImpactNormal);
        Stamp.bWideFootprint = bWide;
        ClipProtectedMud(Stamp);
        TMap<AVoxelCharacter*, FVector> Lifts;
        if (CheckMudOccupants(Stamp, &Lifts) && AIslandAttachedMud::Place(Hit, Stamp))
            LiftMudOccupants(Lifts);
    }
    else
    {
        FHitResult Ground;
        const FVector Probe = Hit.ImpactPoint + Hit.ImpactNormal * 20.;
        if (IslandTerrain->TraceRock(Probe + FVector::UpVector * 38., Probe - FVector::UpVector * 60., Thrower.GetHero(), Ground))
            QueueMudLanding(Ground.ImpactPoint, Ground.ImpactNormal, bWide);
    }
    if (auto* Prop = Cast<AIslandNeutralProp>(MechanismHit.GetActor())) Prop->ReceiveMudImpact(MechanismHit);
}

bool AIslandGameMode::TraceCrewMud(const AVoxelCharacter& Digger, FVector Start, FVector End, FHitResult& OutHit)
{
    const auto* Member = Digger.GetPlayerState<AIslandPlayerState>();
    if (!Member) return false;
    bool bFound = false;
    for (TActorIterator<AVoxelCharacter> It(Digger.GetWorld()); It; ++It)
    {
        const auto* Other = It->GetPlayerState<AIslandPlayerState>();
        if (*It == &Digger || !Other || Other->Team != Member->Team || !It->GetBodyMud()->Num()) continue;
        FHitResult Hit;
        if (It->GetBodyMud()->Trace(Start, End, 0.f, Hit))
        {
            FHitResult Obstruction;
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(MudRescue), false, &Digger);
            if (AIslandNeutralProp::TraceShovel(*Digger.GetWorld(), Obstruction, Start, Hit.ImpactPoint, Query)) continue;
            // A fist aimed straight through a teammate cannot peel the far side of their coat.
            if (It->GetCapsuleComponent()->LineTraceComponent(Obstruction, Start, Hit.ImpactPoint, Query)
                && FVector::Dist(Obstruction.ImpactPoint, Hit.ImpactPoint) > 12.) continue;
            bFound = true;
            OutHit = Hit;
            End = Hit.ImpactPoint;
        }
    }
    return bFound;
}

void AIslandGameMode::RefreshMudChestLocks()
{
    if (!ChestLocks.IsEmpty() && GetGameState<AIslandGameState>()->Phase == EIslandPhase::Hunting)
    {
        UpdateChestLocks(GetGameState<AIslandGameState>()->GetServerWorldTimeSeconds());
        PublishLockViews();
    }
}

void AIslandGameMode::GetProtectedMudVolumes(TArray<FBox>& OutVolumes) const
{
    for (const AIslandChest* Chest : Exposed)
    {
        if (!Chest) continue;
        // Preserve the lid, the space it opens into and a reachable working face on every side.
        FBox Access = FBox::BuildAABB(Chest->GetActorLocation(), IslandRules::ChestExtent + FVector(24.,24.,4.));
        Access.Max.Z += 54.;
        OutVolumes.Add(Access);
    }
}

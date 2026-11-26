#include "Island/IslandGameMode.h"

#include "Abilities/ExpeditionMudEffect.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

void FExpeditionBurialSite::Begin(const FVoxelDensity& Density, const FVector& Position)
{
    Foot = Position;
    Layers = 0;
    bPending = false;
    bAwaitingLanding = false;
    ++Generation;
    // Save what already existed around the shell. A nearby pre-existing wall intercepts a ball
    // as ordinary terrain even when the far surface of that wall touches the burial site.
    Origin = Density.Grid.Origin;
    const FVector GridPosition = (Position - FVector(Origin)) / VoxelPlayground::Spacing;
    First = FIntVector(FMath::FloorToInt(GridPosition.X) - Across/2,
        FMath::FloorToInt(GridPosition.Y) - Across/2, FMath::FloorToInt(GridPosition.Z) - 2);
    Before.SetNumUninitialized(Across * Across * High);
    for (int32 Z = 0; Z < High; ++Z)
        for (int32 Y = 0; Y < Across; ++Y)
            for (int32 X = 0; X < Across; ++X)
                Before[X + Across * (Y + Across * Z)] = Density.Sample(First + FIntVector(X,Y,Z));
}

bool FExpeditionBurialSite::WasAir(const FVector& Position) const
{
    const FVector Local = (Position - FVector(Origin)) / VoxelPlayground::Spacing - FVector(First);
    const int32 X = FMath::FloorToInt(Local.X), Y = FMath::FloorToInt(Local.Y), Z = FMath::FloorToInt(Local.Z);
    if (X < 0 || X + 1 >= Across || Y < 0 || Y + 1 >= Across || Z < 0 || Z + 1 >= High || Before.IsEmpty())
        return false;
    const auto At = [&](int32 DX, int32 DY, int32 DZ) -> double
    {
        return Before[X + DX + Across * (Y + DY + Across * (Z + DZ))];
    };
    const auto Row = [&](int32 DY, int32 DZ) { return FMath::Lerp(At(0,DY,DZ), At(1,DY,DZ), Local.X - X); };
    const double Bottom = FMath::Lerp(Row(0,0), Row(1,0), Local.Y - Y);
    const double Top = FMath::Lerp(Row(0,1), Row(1,1), Local.Y - Y);
    return FMath::Lerp(Bottom, Top, Local.Z - Z) > 0.;
}

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
}

void AIslandGameMode::AddMudShellLayer(AIslandPlayerController& Target)
{
    FExpeditionBurialSite* Active = BurialSites.Find(&Target);
    if (!Active || !Target.GetHero() || !Target.GetHero()->IsMudStunned()) return;
    FExpeditionBurialSite& Site = *Active;
    if (Site.bAwaitingLanding || Site.bPending || Site.Layers >= 3) return;
    Site.bPending = true;
    const int32 Layer = Site.Layers + 1;
    const uint32 Generation = Site.Generation;
    const TWeakObjectPtr<AIslandPlayerController> Member(&Target);
    const TWeakObjectPtr<AIslandGameMode> WeakMode(this);
    QueueBurialLayer(*Target.GetHero(), Site.Foot, Layer, [WeakMode, Member, Generation, Layer](bool bCommitted)
    {
        AIslandGameMode* Mode = WeakMode.Get();
        if (!Mode || !Member.IsValid()) return;
        FExpeditionBurialSite* Current = Mode->BurialSites.Find(Member.Get());
        if (!Current || Current->Generation != Generation) return;
        Current->bPending = false;
        if (bCommitted) Current->Layers = Layer;
        UE_LOG(LogTemp, Display, TEXT("ISLAND_MUD_SHELL target=%s layer=%d committed=%d"),
            *Member->GetName(), Layer, bCommitted);
    });
}

AVoxelCharacter* AIslandGameMode::ResolveMudImpact(AIslandPlayerController& Thrower, const FHitResult& Hit, uint8& Marks)
{
    const auto* Round = GetGameState<AIslandGameState>();
    if (!bExpeditionRoom || !IsPlaying() || !Round || Round->Phase != EIslandPhase::Hunting
        || !GetPlayers().Contains(&Thrower) || !Thrower.GetHero()) return nullptr;

    AVoxelCharacter* Body = Cast<AVoxelCharacter>(Hit.GetActor());
    if (!Body)
    {
        AVoxelTerrain* IslandTerrain = GetTerrain();
        if (!IslandTerrain || Hit.GetActor() == nullptr) return nullptr;
        if (Hit.GetActor() == IslandTerrain)
        {
            for (AVoxelPlayerController* Other : GetPlayers())
            {
                auto* Target = CastChecked<AIslandPlayerController>(Other);
                if (Target->GetTeam() == Thrower.GetTeam() || !Target->GetHero() || !Target->GetHero()->IsMudStunned()) continue;
                const FExpeditionBurialSite* Active = BurialSites.Find(Target);
                if (!Active) continue;
                const FExpeditionBurialSite& Site = *Active;
                if (Site.Layers < 1 || Site.Layers >= 3
                    || !VoxelPlayground::IsBurialSurface(Site.Foot, Site.Layers, Hit.ImpactPoint)
                    || !Site.WasAir(Hit.ImpactPoint - Hit.ImpactNormal * 6.)) continue;
                AddMudShellLayer(*Target);
                return nullptr;
            }
        }
        else
        {
            // Static walls may support a foothold at the soil line; upper decks and props cannot grow floating mud.
            FHitResult Ground;
            const FVector Probe = Hit.ImpactPoint + Hit.ImpactNormal * 20.;
            if (!IslandTerrain->TraceRock(Probe + FVector::UpVector * 38., Probe - FVector::UpVector * 60., Thrower.GetHero(), Ground))
                return nullptr;
            QueueMudLanding(Ground.ImpactPoint, Ground.ImpactNormal, Round->HasDecoyToolBoost(Thrower.GetTeam()));
            return nullptr;
        }
        QueueMudLanding(Hit.ImpactPoint, Hit.ImpactNormal, Round->HasDecoyToolBoost(Thrower.GetTeam()));
        return nullptr;
    }

    auto* Target = Cast<AIslandPlayerController>(Body->GetController());
    if (!Target || Target == &Thrower || Target->GetTeam() == Thrower.GetTeam() || !GetPlayers().Contains(Target)) return nullptr;
    if (Round->IsDecoyEventActive(EIslandDecoyEvent::Ceasefire)) return nullptr;
    if (Body->IsMudStunned())
    {
        AddMudShellLayer(*Target);
        return nullptr;
    }
    const double Now = Round->GetServerWorldTimeSeconds();
#if !UE_BUILD_SHIPPING
    const auto TraceContact = [&](const TCHAR* Result, int32 Stack)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
            UE_LOG(LogTemp, Display, TEXT("MUD_QA_CONTACT source=%s target=%s result=%s stack=%d time=%.4f"),
                *Thrower.GetName(), *Target->GetName(), Result, Stack, Now);
    };
#endif
    if (Body->IsMudImmune())
    {
#if !UE_BUILD_SHIPPING
        TraceContact(TEXT("immune"), GetMudMarks(*Body));
#endif
        return nullptr;
    }
    if (!CanPlayerAct(*Target)) return nullptr;
    Marks = UExpeditionMudMarkEffect::TryHit(*Thrower.GetHero(), *Body);
#if !UE_BUILD_SHIPPING
    if (!Marks)
    {
        static const FGameplayTag Window = FGameplayTag::RequestGameplayTag(TEXT("Expedition.Status.MudImpactWindow"));
        TraceContact(Body->GetAbilitySystemComponent()->HasMatchingGameplayTag(Window) ? TEXT("window") : TEXT("rejected"), GetMudMarks(*Body));
    }
    else
        TraceContact(TEXT("accepted"), Marks);
#endif
    if (!Marks) return nullptr;
    UE_LOG(LogTemp, Display, TEXT("ISLAND_MUD_HIT target=%s stage=%d time=%.3f"), *Target->GetName(), Marks, Now);
    if (Marks < 3) return Body;

    Marks = 0;
    if (!ChestLocks.IsEmpty())
    {
        // Resolve already-earned work before changing the leader's ability to hold the lock.
        UpdateChestLocks(Now);
        if (Round->Phase != EIslandPhase::Hunting) return nullptr;
    }
    if (!Body->ApplyMudStun()) return nullptr;
    // An airborne victim follows normal gravity. Their shell begins at the actual landing.
    BurialSites.FindOrAdd(Target).bAwaitingLanding = true;
    CheckMudLandings();
    if (!ChestLocks.IsEmpty())
    {
        UpdateChestLocks(Now);
        PublishLockViews();
    }
    return Body;
}

void AIslandGameMode::CheckMudLandings()
{
    for (auto It = BurialSites.CreateIterator(); It; ++It)
    {
        AIslandPlayerController* Target = It.Key();
        if (!It.Value().bAwaitingLanding) continue;
        AVoxelCharacter* Body = Target->GetHero();
        if (!Body || !Body->IsMudStunned())
        {
            It.RemoveCurrent();
            continue;
        }
        if (!Body->GetCharacterMovement()->IsMovingOnGround()) continue;
        const FVector Foot = Body->GetActorLocation() - FVector::UpVector * Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        It.Value().Begin(*GetTerrain()->GetDensity(), Foot);
        AddMudShellLayer(*Target);
    }
}

int32 AIslandGameMode::GetMudMarks(const AVoxelCharacter& Target) const
{
    return UExpeditionMudMarkEffect::Count(Target);
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

#include "Tests/ExpeditionDecoyToolProof.h"

#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Tests/IslandProbe.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr int32 ShoreX = 6600;
    constexpr int32 FirstY = -2100;
    constexpr int32 LaneSpacing = 1100;
    constexpr int32 StrokeCount = 4;

    bool FloorDelta(const AVoxelTerrain& Terrain, int32 X, int32 Y, int32& Out)
    {
        FHitResult Floor;
        if (!Terrain.TraceRock({double(X),double(Y),1500.},{double(X),double(Y),-600.},nullptr,Floor)) return false;
        Out = FMath::RoundToInt(Floor.ImpactPoint.Z) - VoxelPlayground::BaseHeight(EVoxelLayout::Expedition,X,Y);
        return true;
    }
}

bool FExpeditionDecoyToolProof::Tick(AIslandGameMode& Mode, const AIslandGameState& Round)
{
    if (Step == EStep::Done) return true;
    const bool bWide = Stroke % 2 != 0;
    const bool bThrow = Stroke >= 2;
    auto* Terrain = Mode.GetTerrain();
    const double Now = FPlatformTime::Seconds();
    if (Step == EStep::Place)
    {
        // The two strokes of each kind use identical starting geometry on separate untouched lanes.
        const int32 Y = FirstY + LaneSpacing * Stroke;
        for (int32 ProbeY = Y - 200; ProbeY <= Y + 550; ProbeY += 25)
        {
            int32 Delta = 0;
            if (!FloorDelta(*Terrain,ShoreX,ProbeY,Delta) || FMath::Abs(Delta) > 2)
            {
                UE_LOG(LogTemp,Error,TEXT("DECOY_TOOL_BAD_FIXTURE stroke=%d y=%d delta=%d"),Stroke,ProbeY,Delta);
                bPassed = false;
                Step = EStep::Done;
                return true;
            }
        }
        Shooter.Reset();
        for (TActorIterator<AIslandProbePeer> It(Mode.GetWorld()); It; ++It)
        {
            auto* Player = Cast<AIslandPlayerController>(It->GetOwner());
            if (!Player || !Player->GetHero() || Round.HasDecoyToolBoost(Player->GetTeam()) != bWide) continue;
            // For a co-op win, let a different winner perform the second tool check.
            if (!Shooter.IsValid() || (bThrow ? It->Seat > Shooter->Seat : It->Seat < Shooter->Seat)) Shooter = *It;
        }
        if (!Shooter.IsValid()) { bPassed = false; Step = EStep::Done; return true; }
        auto& Player = *CastChecked<AIslandPlayerController>(Shooter->GetOwner());
        auto& Hero = *Player.GetHero();
        OriginalPose = Hero.GetActorTransform();
        OriginalView = Player.GetControlRotation();
        Stand = {double(ShoreX),double(Y),double(VoxelPlayground::BaseHeight(EVoxelLayout::Expedition,ShoreX,Y))};
        Hero.ReturnToStart(FTransform(FRotator(0.,90.,0.),Stand+FVector(0.,0.,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.)));
        Player.ClientResetView({0.,90.,0.});
        StartSequence = Terrain->GetAppliedSequence();
        bImpactMeasured = false;
        StepAt = Now;
        Step = EStep::Settle;
        return false;
    }
    auto& Player = *CastChecked<AIslandPlayerController>(Shooter->GetOwner());
    auto& Hero = *Player.GetHero();
    if (Step == EStep::Settle || Step == EStep::CheckReach)
    {
        if (Now-StepAt < .9) return false;
        if (!bThrow && Step == EStep::Settle)
        {
            // The same 3.4 m target is outside the actual tool range for both crews.
            Shooter->ClientMudStroke(false,(Stand+FVector(0.,340.,0.)-Hero.GetAimOrigin()).GetSafeNormal());
            Step = EStep::CheckReach;
            StepAt = Now;
            return false;
        }
        if (!bThrow)
        {
            const bool bRejected = StartSequence == Terrain->GetAppliedSequence() && !Terrain->IsBusy();
            UE_LOG(LogTemp,Display,TEXT("DECOY_TOOL_OUT_OF_REACH team=%d wide=%d rejected=%d"),Player.GetTeam(),bWide,bRejected);
            bPassed &= bRejected;
        }
        const FVector Aim = bThrow ? FVector(0.,.90,-.435).GetSafeNormal() :
            (Stand+FVector(0.,100.,0.)-Hero.GetAimOrigin()).GetSafeNormal();
        UE_LOG(LogTemp,Display,TEXT("DECOY_TOOL_USE seq=%u action=%s team=%d wide=%d"),StartSequence+1,
            bThrow ? TEXT("mud") : TEXT("dig"),Player.GetTeam(),bWide);
        Step = EStep::WaitImpact;
        StepAt = Now;
        Shooter->ClientMudStroke(bThrow,Aim);
        return false;
    }
    if (!bImpactMeasured && Now-StepAt < 12.) return false;
    if (!bImpactMeasured) UE_LOG(LogTemp,Error,TEXT("DECOY_TOOL_NO_IMPACT stroke=%d seq=%u"),Stroke,Terrain->GetAppliedSequence());
    bPassed &= bImpactMeasured;
    Hero.ReturnToStart(OriginalPose);
    Player.ClientResetView(OriginalView);
    Step = ++Stroke == StrokeCount || !bPassed ? EStep::Done : EStep::Place;
    return Step == EStep::Done;
}

void FExpeditionDecoyToolProof::ObserveCommitted(AVoxelTerrain& Terrain, const FVoxelEdit& Edit)
{
    const bool bThrow = Edit.BrushId == VoxelPlayground::MudPad;
    if ((!bThrow && Edit.BrushId != VoxelPlayground::ShallowCut) || FMath::Abs(Edit.Center.X-ShoreX)>35) return;
    const int32 Lane = FMath::FloorToInt((Edit.Center.Y-FirstY)/double(LaneSpacing));
    if (Lane < 0 || Lane >= StrokeCount) return;
    const int32 Travel = Edit.Center.Y - (FirstY + Lane * LaneSpacing);
    if (Travel < 40 || Travel > 500) return;

    // Trace the committed collision rather than inferring width from the requested brush ID.
    int32 Center = 0, Side = 0, Outer = 0, Width = 0;
    bool bValid = true;
    for (int32 Offset = -180; Offset <= 180; Offset += 5)
    {
        int32 Delta = 0;
        bValid &= FloorDelta(Terrain,Edit.Center.X,Edit.Center.Y+Offset,Delta);
        if (Offset == 0) Center = Delta;
        if (FMath::Abs(Offset) == 100) Side += Delta;
        if (FMath::Abs(Offset) == 180) Outer = FMath::Max(Outer,FMath::Abs(Delta));
        if ((bThrow ? Delta : -Delta) >= 12) Width += 5;
    }
    Side /= 2;
    UE_LOG(LogTemp,Display,TEXT("DECOY_TOOL_REPLAY seq=%u action=%s wide=%d crc=%08x center_cm=%d side_cm=%d outer_cm=%d width_cm=%d travel_cm=%d"),
        Edit.Sequence,bThrow ? TEXT("mud") : TEXT("dig"),Edit.bWideFootprint,Terrain.GetChecksum(),Center,Side,Outer,Width,Travel);
    if (!Terrain.HasAuthority()) return;
    for (TActorIterator<AIslandProbe> It(Terrain.GetWorld()); It; ++It)
    {
        auto& Proof = It->DecoyTools;
        if (Proof.Step != EStep::WaitImpact || Proof.Stroke != Lane) continue;
        Proof.bImpactMeasured = true;
        const int32 Sign = bThrow ? 1 : -1;
        Proof.bPassed &= bValid && Edit.Sequence == Proof.StartSequence+1 && (Lane>=2)==bThrow &&
            Edit.bWideFootprint == (Lane%2 != 0) && Sign*Center >= 26 && Sign*Center <= 53 && Outer<=4;
        if (Edit.bWideFootprint)
        {
            Proof.bPassed &= Sign*Side>=14 && Width>=1.65*Proof.NormalWidth && Width<=2.5*Proof.NormalWidth;
            if (bThrow) Proof.bPassed &= FMath::Abs(Travel-Proof.NormalThrowTravel)<=12.;
        }
        else
        {
            Proof.NormalWidth = Width;
            Proof.bPassed &= FMath::Abs(Side)<=6 && Width>=70 && Width<=170;
            if (bThrow) Proof.NormalThrowTravel = Travel;
        }
    }
}

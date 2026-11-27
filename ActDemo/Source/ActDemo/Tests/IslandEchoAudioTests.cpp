#if WITH_DEV_AUTOMATION_TESTS
#include "Island/IslandEchoAudio.h"
#include "Island/IslandExcavationEcho.h"
#include "Island/IslandPlayerController.h"
#include "Audio.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandEchoNotesTest, "ActDemo.Island.Echo.Notes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandEchoNotesTest::RunTest(const FString&)
{
    const FVector Ear(-600, 50, 190), Front(1,0,0);
    for (const FVector Direction : {Front, -Front, FVector::RightVector, -FVector::RightVector,
        FVector::UpVector, -FVector::UpVector})
    {
        const auto Cue = FIslandEchoAudio::ForListener(Ear, Front, Direction, .5f);
        TestTrue(TEXT("The source is on the strike bearing relative to the third-person listener"),
            (Cue.Position - Ear).GetSafeNormal().Equals(Direction));
        TestEqual(TEXT("The second note rises above and falls below"), FMath::Sign(Cue.AnswerPitch - Cue.Pitch), float(Direction.Z));
    }
    TestTrue(TEXT("Rear and front have distinguishable inter-onset times on two speakers"),
        FIslandEchoAudio::ForListener(Ear,Front,-Front,.5f).AnswerDelay -
        FIslandEchoAudio::ForListener(Ear,Front,Front,.5f).AnswerDelay > .1f);
    float LastGain = 0.f;
    for (float Cm : {990.f,830.f,600.f,400.f,249.f,150.f,20.f,0.f})
    {
        const auto Cue = FIslandEchoAudio::ForListener(Ear,Front,Front,FIslandExcavationEcho::Proximity(Cm));
        TestTrue(TEXT("Gain increases within and across the former bands"), Cue.Volume > LastGain);
        LastGain = Cue.Volume;
    }
    return !HasAnyErrors();
}

namespace
{
    struct FEchoTake
    {
        const TCHAR* Name;
        float Cm;
        FVector RelativeDirection;
        double LeftPower = 0., RightPower = 0.;
    };

    class FCaptureEchoMix final : public IAutomationLatentCommand
    {
    public:
        FCaptureEchoMix(FAutomationTestBase& InTest,AIslandPlayerController& Owner)
            : Test(InTest), Player(&Owner)
        {
            FParse::Value(FCommandLine::Get(), TEXT("EchoAudioQAOutput="), OutputDir);
            if (!OutputDir.IsEmpty()) IFileManager::Get().MakeDirectory(*OutputDir,true);
        }

        bool Update() override
        {
            if (!Player.IsValid()) { Test.AddError(TEXT("The local player left during audio capture")); return true; }
            auto* Mixer = Player->GetWorld()->GetAudioDeviceRaw();
            if (!Mixer) { Test.AddError(TEXT("Echo mix capture requires an enabled audio mixer")); return true; }
            const double Now = FPlatformTime::Seconds();
            auto& Take = Takes[Index];
            if (!bRecording)
            {
                Mixer->StartRecording(nullptr,1.2f);
                StartedAt = Now;
                bRecording = true;
                return false;
            }
            if (!bPlayed && Now - StartedAt >= .12)
            {
                FVector Ear, Front, Right;
                Player->GetAudioListenerPosition(Ear,Front,Right);
                const FVector Direction = Front.GetSafeNormal2D() * Take.RelativeDirection.X +
                    Right.GetSafeNormal2D() * Take.RelativeDirection.Y + FVector::UpVector * Take.RelativeDirection.Z;
                Player->ClientExcavationEcho(Take.Cm,Direction);
                bPlayed = true;
                PlayedAt = Now;
                return false;
            }
            if (!bPlayed || Now - PlayedAt < .92) return false;
            float ChannelCount = 0.f, Rate = 0.f;
            const auto& Samples = Mixer->StopRecording(nullptr,ChannelCount,Rate);
            if (Samples.IsEmpty() || ChannelCount < 2 || Rate <= 0.f)
            {
                Test.AddError(TEXT("The master submix did not return a stereo recording"));
                return true;
            }
            const int32 Channels = int32(ChannelCount);
            for (int32 I = 0; I+1 < Samples.Num(); I += Channels)
            {
                Take.LeftPower += FMath::Square(Samples[I]);
                Take.RightPower += FMath::Square(Samples[I+1]);
            }
            Test.TestTrue(FString::Printf(TEXT("%s audible in the master submix"), Take.Name),
                Take.LeftPower + Take.RightPower > .01);
            if (!OutputDir.IsEmpty())
            {
                TArray<int16> PCM;
                PCM.Reserve(Samples.Num());
                for (float Value : Samples) PCM.Add(int16(FMath::Clamp(Value,-1.f,1.f) * 32767.f));
                TArray<uint8> Bytes;
                SerializeWaveFile(Bytes,reinterpret_cast<const uint8*>(PCM.GetData()),PCM.Num()*sizeof(int16),Channels,int32(Rate));
                const FString File = FPaths::Combine(OutputDir,FString(Take.Name)+TEXT(".wav"));
                Test.TestTrue(FString::Printf(TEXT("Saved %s"),*File),FFileHelper::SaveArrayToFile(Bytes,*File));
            }
            bRecording = bPlayed = false;
            if (++Index < UE_ARRAY_COUNT(Takes)) return false;
            Test.TestTrue(TEXT("Near echo has more energy than the far echo"),
                Takes[0].LeftPower + Takes[0].RightPower > 3. * (Takes[1].LeftPower + Takes[1].RightPower));
            Test.TestTrue(TEXT("Left echo is louder on the left output"), Takes[2].LeftPower > 2. * Takes[2].RightPower);
            Test.TestTrue(TEXT("Right echo is louder on the right output"), Takes[3].RightPower > 2. * Takes[3].LeftPower);
            return true;
        }
    private:
        FAutomationTestBase& Test;
        TWeakObjectPtr<AIslandPlayerController> Player;
        FString OutputDir;
        int32 Index = 0;
        double StartedAt = 0., PlayedAt = 0.;
        bool bRecording = false, bPlayed = false;
        FEchoTake Takes[8] = {
            {TEXT("Near"),60.f,{1,0,0}}, {TEXT("Far"),900.f,{1,0,0}},
            {TEXT("Left"),450.f,{0,-1,0}}, {TEXT("Right"),450.f,{0,1,0}},
            {TEXT("Front"),450.f,{1,0,0}}, {TEXT("Rear"),450.f,{-1,0,0}},
            {TEXT("Above"),450.f,{0,0,1}}, {TEXT("Below"),450.f,{0,0,-1}}
        };
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandEchoMixedOutputTest, "ActDemo.Island.Echo.MixedOutput",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FIslandEchoMixedOutputTest::RunTest(const FString&)
{
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType != EWorldType::Game || !Context.World()) continue;
        auto* Player = Cast<AIslandPlayerController>(Context.World()->GetFirstPlayerController());
        if (Player && Player->IsLocalController())
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCaptureEchoMix(*this,*Player));
            return true;
        }
    }
    AddError(TEXT("Run MixedOutput in the expedition front end with -game and audio enabled"));
    return false;
}
#endif

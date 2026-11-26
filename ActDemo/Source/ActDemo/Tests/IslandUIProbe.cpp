#include "Tests/IslandUIProbe.h"
#if !UE_BUILD_SHIPPING
#include "Island/ExpeditionDock.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomPanel.h"
#include "Island/IslandClientSettings.h"
#include "ExpeditionText.h"
#include "Island/IslandRoomSession.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "PartyCrewRig.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputKeyEventArgs.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "UnrealClient.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
    bool HasLabel(const TSharedRef<SWidget>& Widget, const FString& Label)
    {
        if (Widget->GetType() == FName(TEXT("STextBlock")))
            return StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Label;
        FChildren* Children = Widget->GetChildren();
        for (int32 Index=0; Children && Index<Children->Num(); ++Index)
            if (HasLabel(Children->GetChildAt(Index), Label)) return true;
        return false;
    }

    TSharedPtr<SWidget> FindButton(const TSharedRef<SWidget>& Parent, FName Tag, const FString& Label={})
    {
        if (!Parent->GetVisibility().IsVisible()) return {};
        if (Parent->GetTag()==Tag && (Label.IsEmpty() || HasLabel(Parent, Label))) return Parent;
        if (FChildren* Children=Parent->GetChildren())
        {
            for (int32 I=0;I<Children->Num();++I)
                if (TSharedPtr<SWidget> Found=FindButton(Children->GetChildAt(I),Tag,Label)) return Found;
        }
        return {};
    }

    void InputDiagnostic(FName Tag,const TCHAR* Reason,const FString& Detail={})
    {
        static TSet<FString> Reported;
        const FString Key=Tag.ToString()+TEXT("/")+Reason;
        if (Reported.Contains(Key)) return;
        Reported.Add(Key);
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_INPUT tag=%s reason=%s %s"),*Tag.ToString(),Reason,*Detail);
    }

    bool Press(const TSharedRef<SWidget>& Panel,FName Tag,bool bPointer,const FString& Label={})
    {
        // Internet probes can run independently on background monitors while the user keeps playing.
        // Enter invokes the focused button's normal Slate callback without moving the system cursor.
        static const bool bInternetQA=FParse::Param(FCommandLine::Get(),TEXT("IslandUIInternet"));
        if (bInternetQA) bPointer=false;
        auto Button=FindButton(Panel,Tag,Label);
        if (!Button || !Button->IsEnabled()) { InputDiagnostic(Tag,TEXT("missing_or_disabled")); return false; }
        auto& Slate=FSlateApplication::Get();
        const auto Room=StaticCastSharedRef<SIslandRoomPanel>(Panel);
        const uint32 PreviousActions=Room->ActionCount();
        if (bPointer)
        {
            const auto Window=Slate.FindWidgetWindow(Button.ToSharedRef());
            if (!Window) { InputDiagnostic(Tag,TEXT("no_window")); return false; }
            const auto Geometry=Button->GetCachedGeometry();
            if (Geometry.GetLocalSize().IsNearlyZero()) { InputDiagnostic(Tag,TEXT("no_layout")); return false; }
            const FVector2D Position=Geometry.GetAbsolutePositionAtCoordinates(FVector2D(.5f,.5f));
            static TSet<FName> AlignCursor;
            const bool bAlign=AlignCursor.Contains(Tag);
            if (bAlign) Slate.SetCursorPos(Position);
            const FWidgetPath Path=Slate.LocateWindowUnderMouse(Position,{Window.ToSharedRef()},false,0);
            if (!Path.IsValid() || !Path.ContainsWidget(Button.Get()))
            {
                const FString Leaf=Path.IsValid()?Path.GetLastWidget()->GetTypeAsString():TEXT("none");
                InputDiagnostic(Tag,TEXT("outside_hit_grid"),FString::Printf(TEXT("leaf=%s position=%s"),*Leaf,*Position.ToString()));
                AlignCursor.Add(Tag);
                return false;
            }
            const TSet<FKey> Down{EKeys::LeftMouseButton}, Up;
            const FPointerEvent DownEvent(FSlateApplication::CursorPointerIndex, Position, Position, Down, EKeys::LeftMouseButton,0,FModifierKeysState());
            const FPointerEvent UpEvent(FSlateApplication::CursorPointerIndex, Position, Position, Up, EKeys::LeftMouseButton,0,FModifierKeysState());
            const FVector2D CursorPosition=Slate.GetCursorPos();
            const FPointerEvent MoveEvent(FSlateApplication::CursorPointerIndex,Position,CursorPosition,Up,EKeys::Invalid,0,FModifierKeysState());
            const auto Captor=Slate.GetCursorUser()->GetCursorCaptor();
            const bool bMoved=Slate.ProcessMouseMoveEvent(MoveEvent,false);
            const bool bHovered=Button->IsHovered();
            const bool bDown=Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),DownEvent);
            const bool bButtonCaptured=Button->HasMouseCapture();
            const bool bUp=Slate.ProcessMouseButtonUpEvent(UpEvent);
            if (Room->ActionCount()==PreviousActions)
            {
                InputDiagnostic(Tag,bAlign?TEXT("aligned_pointer_no_callback"):TEXT("pointer_no_callback"),
                    FString::Printf(TEXT("active=%d physical=%s target=%s previous_captor=%s move=%d hover=%d down=%d captured=%d up=%d"),
                        Slate.IsActive(),*CursorPosition.ToString(),*Position.ToString(),Captor?*Captor->GetTypeAsString():TEXT("none"),
                        bMoved,bHovered,bDown,bButtonCaptured,bUp));
                AlignCursor.Add(Tag);
                return false;
            }
        }
        else
        {
            if (!Slate.SetKeyboardFocus(Button,EFocusCause::Navigation) && !Button->HasKeyboardFocus()) return false;
            const FKeyEvent Event(EKeys::Enter,FModifierKeysState(),0,false,13,13);
            Slate.ProcessKeyDownEvent(Event);
            Slate.ProcessKeyUpEvent(Event);
        }
        if (Room->ActionCount()==PreviousActions) { InputDiagnostic(Tag,TEXT("enter_no_callback")); return false; }
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_PRESS tag=%s input=%s"),*Tag.ToString(),bPointer?TEXT("pointer"):TEXT("key"));
        return true;
    }

    bool EnterText(const TSharedRef<SWidget>& Panel,FName Tag,const FString& Value,bool bNumeric=false)
    {
        const auto Field=FindButton(Panel,Tag);
        if (!Field || !Field->IsEnabled()) return false;
        auto& Slate=FSlateApplication::Get();
        Slate.SetKeyboardFocus(Field,EFocusCause::Navigation);
        if (!Field->HasKeyboardFocus() && !Field->HasFocusedDescendants()) return false;
        const FKeyEvent End(EKeys::End,FModifierKeysState(),0,false,0,0);
        const FModifierKeysState Shift(true,false,false,false,false,false,false,false,false);
        const FKeyEvent Select(EKeys::Home,Shift,0,false,0,0);
        Slate.ProcessKeyDownEvent(End);
        Slate.ProcessKeyUpEvent(End);
        Slate.ProcessKeyDownEvent(Select);
        Slate.ProcessKeyUpEvent(Select);
        for (TCHAR C:Value)
            Slate.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
        if (bNumeric)
        {
            const FKeyEvent Enter(EKeys::Enter,FModifierKeysState(),0,false,13,13);
            Slate.ProcessKeyDownEvent(Enter);
            Slate.ProcessKeyUpEvent(Enter);
        }
        const FString Actual=bNumeric ? FString::FromInt(StaticCastSharedPtr<SSpinBox<int32>>(Field)->GetValue())
            : StaticCastSharedPtr<SEditableTextBox>(Field)->GetText().ToString();
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_TEXT tag=%s value=%s matches=%d"),*Tag.ToString(),*Actual,Actual==Value);
        return Actual==Value;
    }

    bool CancelThenSearch(const TSharedRef<SWidget>& Panel,UIslandRoomSession& Session)
    {
        static int32 Step=0;
        if (Step==0 && !Session.IsBusy()) { Press(Panel,"Island.Refresh",false); return false; }
        if (Step==0 && Session.IsBusy() && Press(Panel,"Island.Refresh",false)) { ++Step; return false; }
        if (Step==1 && !Session.IsBusy() && Press(Panel,"Island.Refresh",false))
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE search_cancelled=1 searched_again=1"));
            ++Step;
        }
        return Step==2;
    }

    bool RoomEdits(AIslandPlayerController& PC,const TSharedRef<SIslandRoomPanel>& Panel,AIslandGameState& GS,bool bGuest,int32 Want)
    {
        static int32 Stage=0;
        const FString Name=TEXT("白港联测");
        const int32 Capacity=FMath::Max(3,Want);
        const bool Saved=GS.RoomSettings.RoomName==Name && GS.RoomSettings.MaxPlayers==Capacity && GS.RoomSettings.DecoyCount==0;
        if (!bGuest)
        {
            if (Stage==0 && Panel->Current()==EIslandPanel::Lobby && Press(Panel,"Island.Settings",false)) { ++Stage; return false; }
            if (Stage==1 && Panel->Current()==EIslandPanel::Settings && EnterText(Panel,"Island.RoomName",Name)) { ++Stage; return false; }
            if (Stage==2 && EnterText(Panel,"Island.MaxPlayers",FString::FromInt(Capacity),true)) { ++Stage; return false; }
            if (Stage==3 && EnterText(Panel,"Island.DecoyCount",TEXT("0"),true)) { ++Stage; return false; }
            if (Stage==4 && Press(Panel,"Island.SubmitRoom",false)) { ++Stage; return false; }
            if (Stage==5 && !Saved)
            {
                static FString Last;
                const FString Actual=FString::Printf(TEXT("%s/%d"),*GS.RoomSettings.RoomName,GS.RoomSettings.MaxPlayers);
                if (Actual!=Last) UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE awaiting_saved_room current=%s page=%d"),*Actual,int32(Panel->Current()));
                Last=Actual;
            }
            if (Stage==5 && Saved)
            {
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE room_edit=1 name=%s max=%d decoys=%d"),*Name,Capacity,GS.RoomSettings.DecoyCount);
                ++Stage;
            }
            if (Stage<6) return false;
            // The last ready action follows the guest's explicit switch into this crew.
            for (const auto& Entry:GS.PlayerArray)
                if (const auto* P=Cast<AIslandPlayerState>(Entry.Get());P && P->Team!=PC.GetTeam()) return false;
            return true;
        }

        const auto* Self=PC.GetPlayerState<AIslandPlayerState>();
        const AIslandPlayerState* Host=nullptr;
        for (const auto& Entry:GS.PlayerArray)
            if (const auto* P=Cast<AIslandPlayerState>(Entry.Get());P && P->bHost) Host=P;
        if (!Saved || !Self || !Host || Panel->Current()!=EIslandPanel::Lobby) return false;
        const auto ChestRules=StaticCastSharedPtr<STextBlock>(FindButton(Panel,"Island.LobbyChestRules"));
        if (!ChestRules || !ChestRules->GetText().ToString().EndsWith(EXPEDITION_TEXT("假箱 0","Decoys 0"))) return false;
        if (Stage==0 && !Self->bRoomReady && Press(Panel,"Island.Ready",false)) { ++Stage; return false; }
        if (Stage==1 && Self->bRoomReady && Press(Panel,"Island.Ready",false)) { ++Stage; return false; }
        if (Stage==2 && !Self->bRoomReady && Self->Team!=Host->Team)
        {
            if (Press(Panel,FName(*FString::Printf(TEXT("Island.Team.%d"),Host->Team)),true)) ++Stage;
            return false;
        }
        if (Stage==3 && !Self->bRoomReady && Self->Team==Host->Team)
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE ready_cancelled=1 team_switch=1 settings_received=1 visible_decoys=0"));
            ++Stage;
        }
        return Stage==4;
    }

    void Key(AIslandPlayerController& PC,FKey Button,bool Down)
    {
        PC.InputKey(FInputKeyEventArgs::CreateSimulated(Button,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
    }

    struct FChartMouseCheck
    {
        TOptional<FRotator> StartView;
        FRotator StartControl;
        double SentAt = 0.;
        bool bPassed = false;

        bool Check(AIslandPlayerController& PC, const TCHAR* Stage, int32 Direction, double Now)
        {
            if (bPassed) return true;
            FVector Eye;
            FRotator View;
            PC.GetPlayerViewPoint(Eye, View);
            const bool bRawMouse = !FParse::Param(FCommandLine::Get(), TEXT("unattended"));
            auto& Slate = FSlateApplication::Get();
            auto* Client = PC.GetWorld()->GetGameViewport();
            const FViewport* Viewport = Client ? Client->Viewport : nullptr;
            if (!StartView.IsSet())
            {
                const auto Focus = Slate.GetKeyboardFocusedWidget();
                const auto Captor = Slate.GetCursorUser()->GetCursorCaptor();
                UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_MOUSE_ROUTING stage=%s foreground=%d focus=%d capture=%d cursor=%d ignore_look=%d slate_active=%d focus_widget=%s captor=%s"),
                    Stage, Viewport && Viewport->IsForegroundWindow(), Viewport && Viewport->HasFocus(),
                    Viewport && Viewport->HasMouseCapture(), int32(PC.bShowMouseCursor), PC.IsLookInputIgnored(), Slate.IsActive(),
                    Focus ? *Focus->GetTypeAsString() : TEXT("none"), Captor ? *Captor->GetTypeAsString() : TEXT("none"));
                StartView = View;
                StartControl = PC.GetControlRotation();
                SentAt = Now;
                if (bRawMouse)
                {
                    Slate.OnRawMouseMove(160 * Direction, 120 * Direction);
                }
                else
                {
                    // Background clients on one Mac cannot simultaneously own native mouse capture.
                    PC.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 160.f * Direction, 1));
                    PC.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, -120.f * Direction, 1));
                }
                return false;
            }
            const FRotator CameraDelta = (View - StartView.GetValue()).GetNormalized();
            const FRotator ControlDelta = (PC.GetControlRotation() - StartControl).GetNormalized();
            if (FMath::Abs(CameraDelta.Yaw) < .1 || FMath::Abs(CameraDelta.Pitch) < .1 ||
                FMath::Abs(ControlDelta.Yaw) < .1 || FMath::Abs(ControlDelta.Pitch) < .1)
            {
                if (Now - SentAt > 2.)
                    UE_LOG(LogTemp, Error, TEXT("ISLAND_UI_FAIL mouse look with Tab %s: camera=%s control=%s"),
                        Stage, *CameraDelta.ToString(), *ControlDelta.ToString());
                return false;
            }
            UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_CHART_LOOK stage=%s input=%s yaw=%.3f pitch=%.3f"),
                Stage, bRawMouse ? TEXT("raw") : TEXT("axis"), CameraDelta.Yaw, CameraDelta.Pitch);
            bPassed = true;
            return true;
        }
    };

    bool CheckViews(AIslandPlayerController& PC, int32 Seats, TFunctionRef<bool(const TCHAR*)> Shot)
    {
        static int32 Stage=0;
        static FRotator InitialAim;
        static double InitialGap=0.;
        if (Stage>=4) return true;
        auto* Hero=Cast<AVoxelCharacter>(PC.GetPawn());
        if (!Hero) return false;
        // The two-team spawn docks are outside pawn relevancy. Set up this camera-only
        // check at adjacent berths so both clients continuously observe the other pawn.
        static bool bAtOtherDock=false;
        const auto* State=PC.GetWorld()->GetGameState<AIslandGameState>();
        if (PC.HasAuthority() && !bAtOtherDock && State && State->LaunchedTeams.Num()==2)
        {
            const int32 FirstTeam=State->LaunchedTeams[0];
            const int32 GuestTeam=FirstTeam==PC.GetTeam() ? State->LaunchedTeams[1] : FirstTeam;
            FTransform Berth;
            if (!AExpeditionDock::FindStart(PC.GetWorld(),State->LaunchedTeams,GuestTeam,1,Berth)
                || !Hero->TeleportTo(Berth.GetLocation(),Berth.Rotator())) return false;
            PC.SetControlRotation(Berth.Rotator());
            Hero->ForceNetUpdate();
            bAtOtherDock=true;
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_VIEW_SITE opposing_team=%d"),GuestTeam);
            return false;
        }
        int32 Body=0, Paws=0, Others=0;
        bool bOwnBodyComplete=false;
        for (TActorIterator<AVoxelCharacter> It(PC.GetWorld()); It; ++It)
        {
            if (!It->GetPlayerState()) continue;
            int32 BodyParts=0, EyeParts=0;
            TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
            for (auto* Part:Parts)
                if (Part->IsVisible())
                {
                    if (Part->GetAttachParent()==It->GetEyeCamera()) ++EyeParts;
                    else ++BodyParts;
                }
            const auto* Rig=It->FindComponentByClass<UPartyCrewRig>();
            const bool HasRig=Rig && Rig->IsVisible() && Rig->GetSkinnedAsset();
            const bool HasBody=HasRig || BodyParts>=6;
            if (*It==Hero) { Body=BodyParts+int32(HasRig); bOwnBodyComplete=HasBody; Paws=EyeParts; }
            else if (HasBody && EyeParts==0) ++Others;
        }
        if (Others<Seats-1) return false;

        FVector Eye;
        FRotator View;
        PC.GetPlayerViewPoint(Eye,View);
        const double Gap=FVector::Dist(Eye,Hero->GetAimOrigin());
        const double Dig=View.Vector()|PC.GetExcavationAim(*Hero);
        const double Throw=View.Vector()|PC.GetExpeditionThrowAim(*Hero);
        const bool First=Stage==1 || Stage==2;
        const TCHAR* Mode=Stage==0?TEXT("third"):Stage==1?TEXT("first"):Stage==2?TEXT("steep"):TEXT("restored");
        const bool Valid=Hero->IsInFirstPersonView()==First && (First
            ? Gap<3. && Dig>.998 && Throw>.998 && Paws==2 && Body==0
            : Gap>80. && bOwnBodyComplete && Paws==0);
        if (!Valid || (Stage==2 && !FMath::IsNearlyEqual(FRotator::NormalizeAxis(View.Pitch),-78.,1.))
            || (Stage==3 && FMath::Abs(Gap-InitialGap)>12.))
        {
            UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL perspective=%s gap=%.2f dig=%.5f throw=%.5f pitch=%.2f first=%d body=%d paws=%d"),
                Mode,Gap,Dig,Throw,View.Pitch,Hero->IsInFirstPersonView(),Body,Paws);
            return false;
        }
        if (Stage==0 && Shot(TEXT("ThirdPerson"))) return false;
        if (Stage==1 && Shot(TEXT("FirstPerson"))) return false;
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_PERSPECTIVE mode=%s gap_cm=%.2f pitch=%.2f dig_dot=%.5f throw_dot=%.5f body=%d paws=%d remote_bodies=%d"),
            Mode,Gap,View.Pitch,Dig,Throw,Body,Paws,Others);
        if (Stage==0)
        {
            InitialGap=Gap; InitialAim=PC.GetControlRotation();
            Key(PC,EKeys::V,true);
        }
        else if (Stage==1)
        {
            Key(PC,EKeys::V,false);
            PC.SetControlRotation(FRotator(-78.,InitialAim.Yaw,0.));
        }
        else if (Stage==2)
        {
            PC.SetControlRotation(InitialAim); Key(PC,EKeys::V,true);
        }
        else Key(PC,EKeys::V,false);
        ++Stage;
        return false;
    }
}

void TickIslandUIProbe(AIslandPlayerController& PC,const TSharedRef<SIslandRoomPanel>& Panel)
{
    static const FString Role=[] {FString R;FParse::Value(FCommandLine::Get(),TEXT("IslandUIProbe="),R);return R;}();
    if (Role.IsEmpty()) return;
    static double PreviousFrame=0.0;
    static double StartClicked=0.0;
    static double Playable=0.0;
    static TArray<double> FrameMs;
    static TSet<FString> Shots;
    static const bool bShots=FParse::Param(FCommandLine::Get(),TEXT("IslandUIShots"));
    static int32 Step=0;
    static double Next=0;
    static double LastChange=FPlatformTime::Seconds();
    static bool Done=false;
    if (Done) return;
    const double Now=FPlatformTime::Seconds();
    if (PreviousFrame>0.0) FrameMs.Add((Now-PreviousFrame)*1000.);
    PreviousFrame=Now;
    if (Now<Next) return;
    Next=Now+.30;
    int32 Want=1;
    FParse::Value(FCommandLine::Get(),TEXT("IslandUISeats="),Want);
    const auto Page=Panel->Current();
    // A real recording needs rendered, readable pages before the next input is dispatched.
    static const bool bRecording=FParse::Param(FCommandLine::Get(),TEXT("IslandUIRecord"));
    if ((bRecording || bShots) && Page!=EIslandPanel::Hidden)
    {
        static TWeakObjectPtr<UWorld> SeenWorld;
        static EIslandPanel SeenPage=EIslandPanel::Hidden;
        static double PageSince=0;
        static int32 PageFrame=0;
        if (SeenWorld.Get()!=PC.GetWorld() || SeenPage!=Page)
        {
            SeenWorld=PC.GetWorld();
            SeenPage=Page;
            PageSince=Now;
            PageFrame=FrameMs.Num();
        }
        if (Now-PageSince<(bRecording?2.:.45) || FrameMs.Num()-PageFrame<(bRecording?6:3)) return;
    }
    auto* GS=PC.GetWorld()->GetGameState<AIslandGameState>();
    auto* Session=PC.GetGameInstance()->GetSubsystem<UIslandRoomSession>();
    const bool bInternetPair=Role==TEXT("InternetHost") || Role==TEXT("InternetGuest");
    const bool Guest=Role==TEXT("Guest") || Role==TEXT("Join") || Role==TEXT("InternetGuest");
    static const FString TargetRoom=[] {
        FString Name;
        FParse::Value(FCommandLine::Get(),TEXT("IslandUIRoom="),Name);
        return Name;
    }();
    static const bool bEdges=FParse::Param(FCommandLine::Get(),TEXT("IslandUIEdges"));
    static const bool bViews=FParse::Param(FCommandLine::Get(),TEXT("IslandUIPerspective"));
    static const bool bChartWalk=FParse::Param(FCommandLine::Get(),TEXT("IslandUIChartMove"));
    static TOptional<FVector2D> ChartWalkStart;
    static FVector2D ChartWalkEnd;
    static FChartMouseCheck ChartBaselineMouse, ChartHeldMouse, ChartReleasedMouse;
    auto Advance=[&]{ ++Step; LastChange=Now; };
    const auto Shot=[&](const TCHAR* Label)
    {
        if (!bShots || Shots.Contains(Label)) return false;
        FString Directory;
        if (!FParse::Value(FCommandLine::Get(),TEXT("IslandUIOutput="),Directory)) Directory=FPaths::ProjectSavedDir()/TEXT("IslandUIEvidence");
        Directory=FPaths::ConvertRelativePathToFull(Directory);
        IFileManager::Get().MakeDirectory(*Directory,true);
        const FString Path=Directory/(Role+TEXT("_")+Label+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(Path,true,false);
        Shots.Add(Label);
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_CAPTURE role=%s shot=%s path=%s"),*Role,Label,*Path);
        return true;
    };
    if (Step==0 && Page==EIslandPanel::Home && Shot(TEXT("Home"))) return;
    if (Step==1 && Page==EIslandPanel::Create && Shot(TEXT("Create"))) return;
    if (Step==1 && Page==EIslandPanel::Search && Session && !Session->Rooms.IsEmpty() && Shot(TEXT("Search"))) return;
    if (Step==2 && Page==EIslandPanel::Lobby && GS && GS->JoinedCount>=Want && PC.GetViewTarget()!=PC.GetPawn() && Shot(TEXT("Lobby"))) return;
    if (Step==4 && Page==EIslandPanel::Hidden && Shot(TEXT("Hunt"))) return;
    if (Step==5 && PC.IsMapOpen() && Shot(TEXT("Chart"))) return;
    if (Step==7 && Page==EIslandPanel::Pause && Shot(TEXT("Pause"))) return;
    // The other executable may still be booting. The external runner owns the overall deadline.
    if (Step==2 && !Guest && Page==EIslandPanel::Lobby && GS && GS->JoinedCount<Want) LastChange=Now;
    const bool bChartProof=Role==TEXT("Chart");
    const bool bWalkRole=bChartProof || Role==TEXT("Walkthrough") || bInternetPair;
    const bool bWalking=bWalkRole && (Step==3 || Step==4) && GS && GS->Phase==EIslandPhase::Hunting;
    if (bWalking || (bInternetPair && Step==4 && GS && GS->Phase!=EIslandPhase::Finished)) LastChange=Now;
    if (bInternetPair && Guest && Step==1 && Page==EIslandPanel::Search) LastChange=Now;
    if (Now-LastChange>90)
    {
        UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL role=%s step=%d page=%d"),*Role,Step,int32(Page));
        Done=true;
        return;
    }
    if (Role == TEXT("Character"))
    {
        static const TCHAR* MeshNames[] = {TEXT("SK_ExpeditionPig"), TEXT("SK_ExpeditionMonkey"), TEXT("SK_ExpeditionFox"), TEXT("SK_ExpeditionCalf")};
        static const TCHAR* Views[] = {TEXT("Pig"), TEXT("Monkey"), TEXT("Fox"), TEXT("Calf")};
        if (Step == 0 && Page == EIslandPanel::Home && Press(Panel, "Island.Create", false)) Advance();
        else if (Step == 1 && Page == EIslandPanel::Create && Press(Panel, "Island.SubmitRoom", false)) Advance();
        else if (Step == 2 && Page == EIslandPanel::Lobby && Press(Panel, "Island.Character", false)) Advance();
        else if (Step >= 3 && Step <= 10 && Page == EIslandPanel::Character)
        {
            const int32 Model = (Step - 3) / 2;
            const FName Choice(*FString::Printf(TEXT("Island.Character.%d"), Model));
            if ((Step - 3) % 2 == 0)
            {
                if (Press(Panel, Choice, Model == 2)) Advance();
            }
            else
            {
                const auto* Self = PC.GetPlayerState<AIslandPlayerState>();
                const auto* Hero = PC.GetHero();
                if (!Self || Self->GetCharacterModel() != Model || !Hero || PC.GetViewTarget() == Hero) return;
                const auto* Rig=Hero->FindComponentByClass<UPartyCrewRig>();
                if (!Rig || !Rig->IsVisible() || !Rig->GetSkinnedAsset() || Rig->GetSkinnedAsset()->GetFName()!=MeshNames[Model]) return;
                if (bShots)
                {
#if WITH_EDITOR
                    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { LastChange=Now; return; }
#endif
                    if (Rig->GetSkinnedAsset()->HasPendingInitOrStreaming() || !Rig->IsRenderStateCreated() || !Rig->WasRecentlyRendered())
                        return;
                    // Give the newly selected outfit time to render after the model change.
                    if (Now-LastChange<.45) return;
                }
                for (int32 I = 0; I < UE_ARRAY_COUNT(Views); ++I)
                    if (FindButton(Panel, FName(*FString::Printf(TEXT("Island.Character.Current.%d"), I))).IsValid() != (I == Model)) return;
                if (Shot(Views[Model])) return;
                UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_CHARACTER mesh=%s selected=%d"), MeshNames[Model], Model);
                Advance();
            }
        }
        else if (Step == 11 && Page == EIslandPanel::Character)
        {
            auto& Slate = FSlateApplication::Get();
            const FKeyEvent Escape(EKeys::Escape,FModifierKeysState(),0,false,27,27);
            Slate.ProcessKeyDownEvent(Escape);
            Slate.ProcessKeyUpEvent(Escape);
            if (Panel->Current() == EIslandPanel::Lobby) Advance();
        }
        else if (Step == 12 && Page == EIslandPanel::Lobby)
        {
            const auto Entry = FindButton(Panel, "Island.Character");
            if (!Entry || !Entry->HasKeyboardFocus() || !PC.GetPlayerState<AIslandPlayerState>() ||
                PC.GetPlayerState<AIslandPlayerState>()->GetCharacterModel() != 3) return;
            if (Shot(TEXT("Room"))) return;
            if (Press(Panel, "Island.Character", false)) Advance();
        }
        else if (Step == 13 && Page == EIslandPanel::Character)
        {
            const auto Selected = FindButton(Panel, "Island.Character.3");
            if (Selected && Selected->HasKeyboardFocus() && Press(Panel, "Island.Character.Back", false)) Advance();
        }
        else if (Step == 14 && Page == EIslandPanel::Lobby)
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=Character mesh_count=4 restore_focus=1"));
            Done = true;
        }
        return;
    }
    if (Role==TEXT("DirectoryHost") || Role==TEXT("DirectoryObserver"))
    {
        static const FString Directory=[] {
            FString Output;
            FParse::Value(FCommandLine::Get(),TEXT("IslandUIOutput="),Output);
            return Output;
        }();
        auto Signalled=[&](const TCHAR* Name){ return IFileManager::Get().FileExists(*(Directory/Name)); };
        if (!Session) return;
        if (Role==TEXT("DirectoryHost"))
        {
            if (Step==0 && Page==EIslandPanel::Home && Press(Panel,"Island.Create",false)) Advance();
            else if (Step==1 && Page==EIslandPanel::Create && EnterText(Panel,"Island.RoomName",TargetRoom)) Advance();
            else if (Step==2 && Page==EIslandPanel::Create && Press(Panel,"Island.SubmitRoom",false)) Advance();
            else if (Step==3 && Page==EIslandPanel::Lobby && GS && GS->CanLaunch() && Signalled(TEXT("observed-open"))
                && Press(Panel,"Island.Start",false)) Advance();
            else if (Step==4 && GS && GS->Phase==EIslandPhase::Hunting && PC.IsVoxelInputEnabled())
            {
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DIRECTORY_HUNTING"));
                Advance();
            }
            else if (Step==5 && Signalled(TEXT("observed-hidden")))
            {
                Key(PC,EKeys::Escape,true); Key(PC,EKeys::Escape,false); Advance();
            }
            else if (Step==6 && Page==EIslandPanel::Pause && Press(Panel,"Island.ConfirmLeave",false)) Advance();
            else if (Step==7 && Page==EIslandPanel::Home)
            {
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=DirectoryHost home_after_close=1"));
                Done=true;
            }
            return;
        }
        if (Step==0)
        {
            if (Page==EIslandPanel::Home && Press(Panel,"Island.Search",false)) Advance();
            return;
        }
        if (Page!=EIslandPanel::Search || Session->IsBusy()) return;
        if (Step==2 || Step==4)
        {
            const TCHAR* Marker=Step==2 ? TEXT("host-hunting") : TEXT("host-closed");
            // Begin a new search after the host has confirmed the transition.
            if (Signalled(Marker) && Press(Panel,"Island.Refresh",false)) Advance();
            return;
        }
        static double NextSearch=0;
        const auto* Row=Session->Rooms.FindByPredicate([](const FIslandRoomListing& Entry) {
            return Entry.RoomName==TargetRoom;
        });
        const bool bQueryOK=Session->Error.IsEmpty() && Session->GetRoomNetwork()==EIslandRoomNetwork::Internet;
        if (Step==1 && bQueryOK && Row && Row->Occupied==1 && Row->MaxPlayers==8)
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DIRECTORY_OPEN occupied=1 max=8"));
            Advance();
        }
        else if (Step==3 && bQueryOK && !Row)
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DIRECTORY_HIDDEN"));
            Advance();
        }
        else if (Step==5 && bQueryOK && !Row)
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=DirectoryObserver absent_after_close=1"));
            Done=true;
        }
        else if (Now>=NextSearch && Press(Panel,"Island.Refresh",false)) NextSearch=Now+3.;
        return;
    }
    if (Role == TEXT("Settings") || Role == TEXT("SettingsReload"))
    {
        const auto Window = FSlateApplication::Get().FindWidgetWindow(Panel);
        auto* Settings = UIslandClientSettings::Get();
        if (!Window || !Settings) return;
        static FIntPoint InitialSize;
        const FVector2D ClientSize = Window->GetClientSizeInScreen();
        const FIntPoint Size(FMath::RoundToInt(ClientSize.X), FMath::RoundToInt(ClientSize.Y));
        auto Activate = [&](FName Tag){ if (Press(Panel, Tag, false)) Advance(); };
        auto KeyOnWidget = [&](FName Tag, FKey Key)
        {
            const auto Widget = FindButton(Panel, Tag);
            if (!Widget) return;
            auto& Slate = FSlateApplication::Get();
            Slate.SetKeyboardFocus(Widget, EFocusCause::Navigation);
            const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
            Slate.ProcessKeyDownEvent(Event);
            Slate.ProcessKeyUpEvent(Event);
            Advance();
        };
        if (Role == TEXT("SettingsReload"))
        {
            FIntPoint Expected(0, 0);
            FParse::Value(FCommandLine::Get(), TEXT("IslandExpectedX="), Expected.X);
            FParse::Value(FCommandLine::Get(), TEXT("IslandExpectedY="), Expected.Y);
            const auto Restored = Settings->ReadDraft();
            switch (Step)
            {
            case 0:
                if (Page == EIslandPanel::Home && Window->GetWindowMode() == EWindowMode::Windowed && Size == Expected
                    && ExpeditionText::IsEnglish() && Restored.bInvertY && !Restored.bCameraShake
                    && FMath::IsNearlyEqual(Restored.LookSensitivity, 1.05f)
                    && FMath::IsNearlyEqual(Restored.MasterVolume, .95f) && Restored.FrameLimit == 120 && Restored.bVSync)
                    Activate("Island.ClientSettings");
                break;
            case 1: if (!Shot(TEXT("EnglishDisplay"))) Activate("Island.Client.General"); break;
            case 2: if (!Shot(TEXT("EnglishGeneral"))) Activate("Island.Client.Display"); break;
            case 3: Activate("Island.Client.WindowMode"); break;
            case 4: Activate("Island.Client.Apply"); break;
            case 5:
                if (Window->GetWindowMode() != EWindowMode::Windowed)
                    KeyOnWidget("Island.Client.Revert", EKeys::Escape);
                break;
            case 6:
                if (!FindButton(Panel, "Island.Client.Revert") && Size == Expected && Window->GetWindowMode() == EWindowMode::Windowed)
                    KeyOnWidget("Island.Client.Back", EKeys::Escape);
                break;
            case 7:
                if (Page == EIslandPanel::Home)
                {
                    UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=SettingsReload window=%dx%d preferences_loaded=1 fullscreen_reverted=1"),Size.X,Size.Y);
                    Done=true;
                }
                break;
            }
            return;
        }
        switch (Step)
        {
        case 0:
            if (Page == EIslandPanel::Home && Window->GetWindowMode() == EWindowMode::Windowed)
            {
                InitialSize = Size;
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_SETTINGS startup_window=%dx%d"),Size.X,Size.Y);
                Activate("Island.ClientSettings");
            }
            break;
        case 1: if (!Shot(TEXT("Display"))) Activate("Island.Client.Resolution"); break;
        case 2: Activate("Island.Client.Apply"); break;
        case 3:
            if (FindButton(Panel, "Island.Client.Revert") && Size != InitialSize && !Shot(TEXT("Confirmation"))) Advance();
            break;
        case 4:
            if (!FindButton(Panel, "Island.Client.Revert") && Size == InitialSize)
            {
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_SETTINGS timeout_reverted=1"));
                Activate("Island.Client.Resolution.Previous");
            }
            break;
        case 5: Activate("Island.Client.FrameLimit"); break;
        case 6: Activate("Island.Client.VSync"); break;
        case 7: Activate("Island.Client.General"); break;
        case 8: if (!Shot(TEXT("General"))) Activate("Island.Client.Language"); break;
        case 9: KeyOnWidget("Island.Client.MasterVolume", EKeys::Left); break;
        case 10: KeyOnWidget("Island.Client.LookSensitivity", EKeys::Right); break;
        case 11: Activate("Island.Client.InvertY"); break;
        case 12: Activate("Island.Client.CameraShake"); break;
        case 13: Activate("Island.Client.Apply"); break;
        case 14: if (Size != InitialSize) Activate("Island.Client.Keep"); break;
        case 15:
            if (Page == EIslandPanel::Home && Size != InitialSize && Settings->IsVerticalLookInverted()
                && !Settings->IsCameraShakeEnabled() && ExpeditionText::IsEnglish())
            {
                if (Shot(TEXT("EnglishHome"))) break;
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=Settings confirmed_window=%dx%d"), Size.X, Size.Y);
                Done=true;
            }
            break;
        }
        return;
    }
    if (bWalkRole && Step>=3)
    {
        static FString RoomName;
        static double ResultShown=0;
        static double ChartOpened=0;
        static uint32 StartSequence=0;
        static bool bSawEdits=false, bSawParty=false, bGuestReadied=false;
        if (bInternetPair && Step==4 && GS)
        {
            if (!bSawParty && GS->Phase==EIslandPhase::PartyPlaying)
            {
                bSawParty=true;
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EOS_PARTY role=%s"),*Role);
            }
            if (!bSawEdits && GS->Phase==EIslandPhase::Hunting)
            {
                const auto* Terrain=AVoxelTerrain::Find(PC.GetWorld());
                if (Terrain && Terrain->GetAppliedSequence()>StartSequence)
                {
                    bSawEdits=true;
                    UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EOS_EDIT role=%s sequence=%u"),*Role,Terrain->GetAppliedSequence());
                }
            }
        }
        if (!PC.IsMapOpen()) ChartOpened=0;
        else if (GS && GS->Phase==EIslandPhase::Hunting)
        {
            if (ChartOpened==0.) ChartOpened=Now;
            if (Now-ChartOpened>.2)
            {
                const int32 Clues=PC.GetLocalClues().Count();
                if (Clues==0 && Shot(TEXT("ChartUnsearched")))
                {
                    UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_CHART clues=0"));
                }
                if (Clues==1 && Shot(TEXT("ChartOneClue")))
                {
                    UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_CHART clues=1"));
                }
                else if (bChartProof && Shots.Contains(TEXT("ChartUnsearched")) && Shots.Contains(TEXT("ChartOneClue")))
                {
                    UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=Chart acquired_clues=%d"),Clues);
                    Done=true;
                }
            }
        }
        if (Step==3 && GS && GS->Phase==EIslandPhase::Hunting && Page==EIslandPanel::Hidden && PC.IsVoxelInputEnabled()
            && PC.GetPawn() && PC.GetViewTarget()==PC.GetPawn())
        {
            RoomName=GS->RoomSettings.RoomName;
            if (bInternetPair && RoomName!=TargetRoom)
            {
                UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL incorrect public room"));
                Done=true;
                return;
            }
            if (const auto* Terrain=AVoxelTerrain::Find(PC.GetWorld())) StartSequence=Terrain->GetAppliedSequence();
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_WALK_CONTROL room=%s"),*RoomName);
            Advance();
        }
        else if (Step==4 && Page==EIslandPanel::Result && GS && GS->Phase==EIslandPhase::Finished && GS->Winner>=0)
        {
            ResultShown=Now;
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_WALK_RESULT team=%d clues=%d"),GS->Winner,PC.GetLocalClues().Count());
            Advance();
        }
        else if (Step==5 && Page==EIslandPanel::Result && Now-ResultShown>=2.)
        {
            if (Shot(TEXT("Result"))) return;
            if (Guest || Press(Panel,"Island.ReturnRoom",true)) Advance();
        }
        else if (Step==6 && GS && GS->Phase==EIslandPhase::Waiting && Page==EIslandPanel::Lobby && !GS->bRoomBlocked
            && GS->RoomSettings.RoomName==RoomName && PC.GetLocalClues().Count()==0 && GS->Winner==INDEX_NONE
            && GS->JoinedCount==Want)
        {
            if (bInternetPair)
            {
                if (!bSawEdits || !bSawParty)
                {
                    UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL missing replicated dig or minigame on %s"),*Role);
                    Done=true;
                    return;
                }
                if (Guest && !bGuestReadied) bGuestReadied=Press(Panel,"Island.Ready",false);
                if (Guest ? !bGuestReadied : !GS->CanLaunch()) return;
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EOS_RETURN role=%s room=%s players=%d"),*Role,*RoomName,int32(GS->JoinedCount));
                if (Guest || Press(Panel,"Island.Leave",false)) Advance();
                return;
            }
            if (!GS->CanLaunch()) return;
            if (Shot(TEXT("ReturnedLobby"))) return;
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=Walkthrough returned_to_room=1 cleared_clues=1 players=%d"),int32(GS->JoinedCount));
            Done=true;
        }
        else if (bInternetPair && Step==7)
        {
            if (!Guest && Page==EIslandPanel::Pause && Press(Panel,"Island.ConfirmLeave",false)) Advance();
            else if (Guest && (Page==EIslandPanel::Search || Page==EIslandPanel::Home)) Advance();
        }
        else if (bInternetPair && Step==8 && Session && !Session->IsBusy())
        {
            if (Page==EIslandPanel::Home) Press(Panel,"Island.Search",false);
            else if (Page==EIslandPanel::Search && Press(Panel,"Island.Refresh",false)) Advance();
        }
        else if (bInternetPair && Step==9 && Session && !Session->IsBusy() && Page==EIslandPanel::Search)
        {
            const bool bOldRoom=Session->Rooms.ContainsByPredicate([](const FIslandRoomListing& Entry) {
                return Entry.RoomName==TargetRoom;
            });
            if (Session->Error.IsEmpty() && !bOldRoom && Session->GetRoomNetwork()==EIslandRoomNetwork::Internet)
            {
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=%s returned_to_room=1 host_closed=1 listing_removed=1"),*Role);
                Done=true;
            }
            else if (Now-LastChange>3.) Press(Panel,"Island.Refresh",false);
        }
        return;
    }
    if (Role==TEXT("Recovery"))
    {
        if (!Session) return;
        if (Step==0 && Page==EIslandPanel::Home && Press(Panel,"Island.Search",true)) Advance();
        else if (Step==1 && Page==EIslandPanel::Search && CancelThenSearch(Panel,*Session)) Advance();
        else if (Step==2 && !Session->IsBusy())
        {
            FString Address=TEXT("127.0.0.1:65531");
            FParse::Value(FCommandLine::Get(),TEXT("IslandUIAddress="),Address);
            if (EnterText(Panel,"Island.Address",Address)) Advance();
        }
        else if (Step==3 && Press(Panel,"Island.JoinAddress",false)) Advance();
        else if (Step==4 && Page==EIslandPanel::Search && !Session->IsBusy() && !Session->Error.IsEmpty())
        {
            const auto Refresh=FindButton(Panel,"Island.Refresh");
            const bool bFocus=Refresh && Refresh->HasKeyboardFocus();
            if (bShots && Shot(TEXT("Recovery"))) return;
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE join_failed=1 search_focus=%d reason=%s"),bFocus,*Session->Error);
            if (bFocus && Press(Panel,"Island.Refresh",false)) Advance();
        }
        else if (Step==5 && !Session->IsBusy() && Session->Error.IsEmpty())
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=Recovery search_cancel=1 join_failed=1 retry_ok=1"));
            Done=true;
        }
        return;
    }
    if (bEdges && Step==1 && Guest && Page==EIslandPanel::Search && Session && !CancelThenSearch(Panel,*Session)) return;
    if (bEdges && Step==2 && GS && !RoomEdits(PC,Panel,*GS,Guest,Want)) return;
    if (Role == TEXT("Solo") && (Step == 2 || Step == 7))
    {
        // A room member and a player in a live hunt must be able to inspect local settings and return.
        static int32 CheckedStep = -1;
        if (CheckedStep != Step)
        {
            if (Page == EIslandPanel::ClientSettings)
            {
                if (PC.IsVoxelInputEnabled())
                {
                    UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL world input active in settings"));
                }
                else if (Press(Panel, "Island.Client.Back", false)) CheckedStep = Step;
                return;
            }
            if (Page == EIslandPanel::Lobby || Page == EIslandPanel::Pause)
            {
                Press(Panel, "Island.ClientSettings", false);
                return;
            }
        }
    }
    if (Step==0 && Page==EIslandPanel::Home && Press(Panel,Guest?TEXT("Island.Search"):TEXT("Island.Create"),true)) Advance();
    else if (Step==1 && Guest && Page==EIslandPanel::Search)
    {
        if (!Session || Session->IsBusy()) return;
        const FIslandRoomListing* Row=Session->Rooms.FindByPredicate([](const FIslandRoomListing& Entry) {
            return TargetRoom.IsEmpty() || Entry.RoomName==TargetRoom;
        });
        if (Row)
        {
            const FString Title=FString::Printf(TEXT("%s   %d / %d"),*Row->RoomName,Row->Occupied,Row->MaxPlayers);
            if (Press(Panel,"Island.JoinListing",false,Title)) Advance();
        }
        else
        {
            static double NextRefresh=0;
            if (Now>=NextRefresh && Press(Panel,"Island.Refresh",false)) NextRefresh=Now+3.;
        }
    }
    else if (Step==1 && !Guest && Page==EIslandPanel::Create)
    {
        static bool Named=false;
        static bool CapacitySet=false;
        if (!TargetRoom.IsEmpty() && !Named) { Named=EnterText(Panel,"Island.RoomName",TargetRoom); return; }
        if (bInternetPair && !CapacitySet)
        {
            CapacitySet=EnterText(Panel,"Island.MaxPlayers",FString::FromInt(Want),true);
            return;
        }
        if (Press(Panel,"Island.SubmitRoom",false)) Advance();
    }
    else if (Step==2 && Page==EIslandPanel::Lobby && GS)
    {
        if (bInternetPair && Guest && PC.GetTeam()!=0)
        {
            Press(Panel,"Island.Team.0",false);
            return;
        }
        if (Guest && Press(Panel,"Island.Ready",true)) { StartClicked=Now; Advance(); }
        else if (!Guest && GS->JoinedCount>=Want && GS->CanLaunch() && Press(Panel,"Island.Start",false)) { StartClicked=Now; Advance(); }
    }
    else if (Step==3 && GS && GS->Phase==EIslandPhase::Hunting && PC.IsVoxelInputEnabled() && PC.GetViewTarget()==PC.GetPawn() && Page==EIslandPanel::Hidden)
    {
        if (bChartWalk) UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_MOUSE_BASELINE_READY"));
        Playable=Now; Next=Now+.75; Advance();
    }
    else if (Step==4)
    {
        if (bViews && !CheckViews(PC,Want,Shot)) return;
        if (bChartWalk)
        {
            if (!ChartBaselineMouse.Check(PC,TEXT("before"),-1,Now)) return;
            PC.SetControlRotation((-PC.GetPawn()->GetActorLocation()).GetSafeNormal2D().Rotation());
            Key(PC,EKeys::W,true);
        }
        Key(PC,EKeys::Tab,true); Advance();
    }
    else if (Step==5 && PC.IsMapOpen())
    {
        if (bChartWalk)
        {
            const FVector2D Position(PC.GetPawn()->GetActorLocation());
            if (!ChartWalkStart.IsSet())
            {
                ChartWalkStart=Position;
                Shot(TEXT("ChartWalkStart"));
                return;
            }
            if (!ChartHeldMouse.Check(PC,TEXT("held"),1,Now)) return;
            if (FVector2D::Distance(Position,ChartWalkStart.GetValue())<350.)
            {
                if (Now-LastChange>8.) UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL W did not move the pawn while Tab was held"));
                return;
            }
            if (Shot(TEXT("ChartWalkEnd")))
            {
                const auto* Terrain=AVoxelTerrain::Find(PC.GetWorld());
                if (!Terrain) { UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL missing chart terrain")); return; }
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_CHART_WALK start=%.2f,%.2f end=%.2f,%.2f shore=%d"),
                    ChartWalkStart->X,ChartWalkStart->Y,Position.X,Position.Y,Terrain->GetShoreExtent());
                return;
            }
            ChartWalkEnd=Position;
        }
        if (bViews)
        {
            static bool Tried=false;
            if (!Tried) { Key(PC,EKeys::V,true); Tried=true; return; }
            Key(PC,EKeys::V,false);
            if (PC.WantsFirstPersonView()) { UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL perspective=chart")); return; }
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_PERSPECTIVE mode=chart_blocked"));
        }
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_CHART role=%s opened=1"),*Role);
        Key(PC,EKeys::Tab,false); Advance();
    }
    else if (Step==6 && !PC.IsMapOpen() && PC.IsVoxelInputEnabled())
    {
        if (bChartWalk)
        {
            if (!ChartReleasedMouse.Check(PC,TEXT("released"),-1,Now)) return;
            const double Continued=FVector2D::Distance(FVector2D(PC.GetPawn()->GetActorLocation()),ChartWalkEnd);
            if (Continued<100.)
            {
                if (Now-LastChange>5.) UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL held W stopped after releasing Tab"));
                return;
            }
            Key(PC,EKeys::W,false);
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_CHART_RELEASE continued_cm=%.2f"),Continued);
        }
        Key(PC,EKeys::Escape,true); Key(PC,EKeys::Escape,false); Advance();
    }
    else if (Step==7 && Page==EIslandPanel::Pause)
    {
        if (PC.IsVoxelInputEnabled()) InputDiagnostic("Island.Resume",TEXT("world_input_still_enabled"));
        else
        {
            // Two rendered clients on one Mac share one native cursor. Keep guest navigation on Enter.
            static const double FirstResumeAttempt = Now;
            const bool bTryPointer=!Guest && Now-FirstResumeAttempt<2.;
            if (Press(Panel,"Island.Resume",bTryPointer)) Advance();
        }
    }
    else if (Step==8 && Page==EIslandPanel::Hidden && PC.IsVoxelInputEnabled())
    {
        FrameMs.Sort();
        const double P95=FrameMs.IsEmpty()?0.:FrameMs[FMath::Clamp(FMath::CeilToInt(.95*FrameMs.Num())-1,0,FrameMs.Num()-1)];
        UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=%s players=%d teams=%d chart_release=1 pause_resume=1 frames=%d p95_ms=%.3f max_ms=%.3f to_control_ms=%.3f"),
            *Role,GS?int32(GS->JoinedCount):0,GS?GS->ActiveTeams:0,FrameMs.Num(),P95,FrameMs.IsEmpty()?0.:FrameMs.Last(),(Playable-StartClicked)*1000.);
        Done=true;
    }
}
#endif

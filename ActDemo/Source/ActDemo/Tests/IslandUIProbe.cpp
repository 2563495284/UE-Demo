#include "Tests/IslandUIProbe.h"
#if !UE_BUILD_SHIPPING
#include "Island/ExpeditionDock.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomPanel.h"
#include "Island/IslandRoomSession.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
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

namespace
{
    TSharedPtr<SWidget> FindButton(const TSharedRef<SWidget>& Parent, FName Tag)
    {
        if (!Parent->GetVisibility().IsVisible()) return {};
        if (Parent->GetTag()==Tag) return Parent;
        if (FChildren* Children=Parent->GetChildren())
        {
            for (int32 I=0;I<Children->Num();++I)
                if (TSharedPtr<SWidget> Found=FindButton(Children->GetChildAt(I),Tag)) return Found;
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

    bool Press(const TSharedRef<SWidget>& Panel,FName Tag,bool bPointer)
    {
        auto Button=FindButton(Panel,Tag);
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
        const bool Saved=GS.RoomSettings.RoomName==Name && GS.RoomSettings.MaxPlayers==Capacity;
        if (!bGuest)
        {
            if (Stage==0 && Panel->Current()==EIslandPanel::Lobby && Press(Panel,"Island.Settings",false)) { ++Stage; return false; }
            if (Stage==1 && Panel->Current()==EIslandPanel::Settings && EnterText(Panel,"Island.RoomName",Name)) { ++Stage; return false; }
            if (Stage==2 && EnterText(Panel,"Island.MaxPlayers",FString::FromInt(Capacity),true)) { ++Stage; return false; }
            if (Stage==3 && Press(Panel,"Island.SubmitRoom",false)) { ++Stage; return false; }
            if (Stage==4 && !Saved)
            {
                static FString Last;
                const FString Actual=FString::Printf(TEXT("%s/%d"),*GS.RoomSettings.RoomName,GS.RoomSettings.MaxPlayers);
                if (Actual!=Last) UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE awaiting_saved_room current=%s page=%d"),*Actual,int32(Panel->Current()));
                Last=Actual;
            }
            if (Stage==4 && Saved)
            {
                UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE room_edit=1 name=%s max=%d"),*Name,Capacity);
                ++Stage;
            }
            if (Stage<5) return false;
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
        if (Stage==0 && !Self->bRoomReady && Press(Panel,"Island.Ready",false)) { ++Stage; return false; }
        if (Stage==1 && Self->bRoomReady && Press(Panel,"Island.Ready",false)) { ++Stage; return false; }
        if (Stage==2 && !Self->bRoomReady && Self->Team!=Host->Team)
        {
            if (Press(Panel,FName(*FString::Printf(TEXT("Island.Team.%d"),Host->Team)),true)) ++Stage;
            return false;
        }
        if (Stage==3 && !Self->bRoomReady && Self->Team==Host->Team)
        {
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_EDGE ready_cancelled=1 team_switch=1 settings_received=1"));
            ++Stage;
        }
        return Stage==4;
    }

    void Key(AIslandPlayerController& PC,FKey Button,bool Down)
    {
        PC.InputKey(FInputKeyEventArgs::CreateSimulated(Button,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
    }

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
            if (*It==Hero) { Body=BodyParts; Paws=EyeParts; }
            else if (BodyParts>=6 && EyeParts==0) ++Others;
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
            : Gap>80. && Body>=6 && Paws==0);
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
    const bool Guest=Role==TEXT("Guest") || Role==TEXT("Join");
    static const bool bEdges=FParse::Param(FCommandLine::Get(),TEXT("IslandUIEdges"));
    static const bool bViews=FParse::Param(FCommandLine::Get(),TEXT("IslandUIPerspective"));
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
    const bool bWalkRole=bChartProof || Role==TEXT("Walkthrough");
    const bool bWalking=bWalkRole && (Step==3 || Step==4) && GS && GS->Phase==EIslandPhase::Hunting;
    if (bWalking) LastChange=Now;
    if (Now-LastChange>90)
    {
        UE_LOG(LogTemp,Error,TEXT("ISLAND_UI_FAIL role=%s step=%d page=%d"),*Role,Step,int32(Page));
        Done=true;
        return;
    }
    if (bWalkRole && Step>=3)
    {
        static FString RoomName;
        static double ResultShown=0;
        static double ChartOpened=0;
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
            if (Press(Panel,"Island.ReturnRoom",true)) Advance();
        }
        else if (Step==6 && GS && GS->Phase==EIslandPhase::Waiting && Page==EIslandPanel::Lobby && !GS->bRoomBlocked
            && GS->RoomSettings.RoomName==RoomName && PC.GetLocalClues().Count()==0 && GS->Winner==INDEX_NONE && GS->CanLaunch())
        {
            if (Shot(TEXT("ReturnedLobby"))) return;
            UE_LOG(LogTemp,Display,TEXT("ISLAND_UI_DONE role=Walkthrough returned_to_room=1 cleared_clues=1 players=%d"),int32(GS->JoinedCount));
            Done=true;
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
    if (Step==0 && Page==EIslandPanel::Home && Press(Panel,Guest?TEXT("Island.Search"):TEXT("Island.Create"),true)) Advance();
    else if (Step==1 && Guest && Page==EIslandPanel::Search)
    {
        if (Session && !Session->IsBusy() && !Session->Rooms.IsEmpty() && Press(Panel,"Island.JoinListing",false)) Advance();
        else if (Session && !Session->IsBusy() && Now-LastChange>2) Press(Panel,"Island.Refresh",false);
    }
    else if (Step==1 && !Guest && Page==EIslandPanel::Create && Press(Panel,"Island.SubmitRoom",false)) Advance();
    else if (Step==2 && Page==EIslandPanel::Lobby && GS)
    {
        if (Guest && Press(Panel,"Island.Ready",true)) { StartClicked=Now; Advance(); }
        else if (!Guest && GS->JoinedCount>=Want && GS->CanLaunch() && Press(Panel,"Island.Start",false)) { StartClicked=Now; Advance(); }
    }
    else if (Step==3 && GS && GS->Phase==EIslandPhase::Hunting && PC.IsVoxelInputEnabled() && PC.GetViewTarget()==PC.GetPawn() && Page==EIslandPanel::Hidden)
    {
        Playable=Now; Next=Now+.75; Advance();
    }
    else if (Step==4)
    {
        if (bViews && !CheckViews(PC,Want,Shot)) return;
        Key(PC,EKeys::Tab,true); Advance();
    }
    else if (Step==5 && PC.IsMapOpen())
    {
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
        Key(PC,EKeys::Escape,true); Key(PC,EKeys::Escape,false); Advance();
    }
    else if (Step==7 && Page==EIslandPanel::Pause)
    {
        if (PC.IsVoxelInputEnabled()) InputDiagnostic("Island.Resume",TEXT("world_input_still_enabled"));
        else
        {
            // Two rendered clients on one Mac share one native cursor. Keep guest navigation on Enter.
            const bool bTryPointer=!Guest && Now-LastChange<2.;
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

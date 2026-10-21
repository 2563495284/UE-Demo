#include "ActDemoBuildCommandlet.h"
#include "AI/ActAIController.h"
#include "Character/ActCharacter.h"
#include "Combat/ActCombatTypes.h"
#include "Combat/ActCombatNotify.h"
#include "UI/ActHUDWidget.h"
#include "World/ActGameMode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Blueprint/WidgetTree.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"
#include "Factories/SoundFactory.h"
#include "Misc/FileHelper.h"

namespace
{
 template<class T> T* Asset(const FString& Path)
 {
  UPackage* Package=CreatePackage(*Path); Package->FullyLoad();
  T* Object=FindObject<T>(Package,*FPackageName::GetLongPackageAssetName(Path));
  if (!Object) { Object=NewObject<T>(Package,*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Object); }
  return Object;
 }
 bool Save(UObject* Object)
 {
  Object->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
  const FString Filename=FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
  return UPackage::SavePackage(Object->GetOutermost(),Object,*Filename,Args);
 }
 UAnimMontage* Montage(FString Name,FString SequencePath)
 {
  UAnimSequence* Sequence=LoadObject<UAnimSequence>(nullptr,*SequencePath); check(Sequence);
  auto* M=Asset<UAnimMontage>(TEXT("/Game/ActDemo/Animation/")+Name);
  M->SetSkeleton(Sequence->GetSkeleton()); M->SlotAnimTracks.Reset(); M->CompositeSections.Reset(); M->Notifies.Reset();
  FSlotAnimationTrack& Track=M->SlotAnimTracks.AddDefaulted_GetRef(); Track.SlotName=TEXT("DefaultSlot");
  FAnimSegment Segment; Segment.SetAnimReference(Sequence); Segment.AnimStartTime=0; Segment.AnimEndTime=Sequence->GetPlayLength(); Segment.AnimPlayRate=1; Segment.LoopingCount=1; Segment.StartPos=0;
  Track.AnimTrack.AnimSegments.Add(Segment); M->SetCompositeLength(Sequence->GetPlayLength()); M->AddAnimCompositeSection(TEXT("Action"),0);
  M->BlendIn.SetBlendTime(.08f); M->BlendOut.SetBlendTime(.12f); M->bEnableAutoBlendOut=true;
  return M;
 }
 void Window(UAnimMontage* Montage,uint8 Type,float Start,float End,float Duration)
 {
  FAnimNotifyEvent& E=Montage->Notifies.AddDefaulted_GetRef();
  E.NotifyStateClass=NewObject<UActCombatNotify>(Montage); CastChecked<UActCombatNotify>(E.NotifyStateClass)->Window=Type;
  const float Scale=Montage->GetPlayLength()/Duration;
  E.Link(Montage,Start*Scale); E.SetDuration((End-Start)*Scale); E.EndLink.Link(Montage,End*Scale); E.NotifyName=TEXT("CombatWindow");
 }
 UMaterial* Material(FString Name,FLinearColor Color,float Emission=0.f)
 {
  auto* M=Asset<UMaterial>(TEXT("/Game/ActDemo/Materials/")+Name);
  if (Name==TEXT("M_Player") || Name==TEXT("M_Scout") || Name==TEXT("M_Brute") || Name==TEXT("M_Boss"))
  { M->SetUsageByFlag(MATUSAGE_SkeletalMesh,true); }
  M->GetExpressionCollection().Empty();
  auto* C=NewObject<UMaterialExpressionConstant3Vector>(M); C->Constant=Color;
  M->GetExpressionCollection().AddExpression(C); M->GetEditorOnlyData()->BaseColor.Connect(0,C);
  auto* R=NewObject<UMaterialExpressionConstant>(M); R->R=.8f; M->GetExpressionCollection().AddExpression(R); M->GetEditorOnlyData()->Roughness.Connect(0,R);
  if (Emission>0) { auto* E=NewObject<UMaterialExpressionConstant3Vector>(M); E->Constant=Color*Emission; M->GetExpressionCollection().AddExpression(E); M->GetEditorOnlyData()->EmissiveColor.Connect(0,E); }
  M->PostEditChange(); Save(M); return M;
 }
 void BuildInput()
 {
  auto* Config=Asset<UActInputConfig>(TEXT("/Game/ActDemo/Input/DA_Input"));
  auto* Game=Asset<UInputMappingContext>(TEXT("/Game/ActDemo/Input/IMC_Gameplay"));
  auto* Menu=Asset<UInputMappingContext>(TEXT("/Game/ActDemo/Input/IMC_Menu"));
  Game->UnmapAll(); Menu->UnmapAll(); Config->Gameplay=Game; Config->Menu=Menu;
  auto Action=[&](FString Name,EInputActionValueType Type=EInputActionValueType::Boolean)
  { auto* A=Asset<UInputAction>(TEXT("/Game/ActDemo/Input/IA_")+Name); A->ValueType=Type; Save(A); return A; };
  Config->Move=Action(TEXT("Move"),EInputActionValueType::Axis2D); Config->Look=Action(TEXT("Look"),EInputActionValueType::Axis2D);
  for (auto Key : {EKeys::W,EKeys::A,EKeys::S,EKeys::D})
  {
   auto& Map=Game->MapKey(Config->Move,Key);
   if (Key==EKeys::S || Key==EKeys::A) Map.Modifiers.Add(NewObject<UInputModifierNegate>(Game));
   if (Key==EKeys::W || Key==EKeys::S) { auto* Swizzle=NewObject<UInputModifierSwizzleAxis>(Game); Swizzle->Order=EInputAxisSwizzle::YXZ; Map.Modifiers.Add(Swizzle); }
  }
  Game->MapKey(Config->Look,EKeys::Mouse2D);
  auto Bind=[&](TObjectPtr<UInputAction>& Field,FString Name,FKey Key)
  { Field=Action(Name); Game->MapKey(Field,Key); };
  Bind(Config->Sprint,TEXT("Sprint"),EKeys::LeftShift); Bind(Config->Light,TEXT("Light"),EKeys::LeftMouseButton);
  Bind(Config->Heavy,TEXT("Heavy"),EKeys::RightMouseButton); Bind(Config->Dodge,TEXT("Dodge"),EKeys::SpaceBar);
  Bind(Config->Heal,TEXT("Heal"),EKeys::Q); Bind(Config->Lock,TEXT("Lock"),EKeys::Tab); Bind(Config->Switch,TEXT("Switch"),EKeys::E);
  Bind(Config->Pause,TEXT("Pause"),EKeys::Escape); Bind(Config->Restart,TEXT("Restart"),EKeys::R); Bind(Config->Debug,TEXT("Debug"),EKeys::F1);
  Config->Pause->bTriggerWhenPaused=true; Config->Restart->bTriggerWhenPaused=true; Save(Config->Pause); Save(Config->Restart);
  Menu->MapKey(Config->Pause,EKeys::Escape); Menu->MapKey(Config->Restart,EKeys::R);
  Save(Game); Save(Menu); Save(Config);
 }
 void BuildCombat()
 {
  for (const FString Name : {FString(TEXT("S_Hit")),FString(TEXT("S_Swing"))})
  {
   const FString Path=TEXT("/Game/ActDemo/Audio/")+Name;
   if (!FPackageName::DoesPackageExist(Path))
   {
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*(FPaths::ProjectDir()/TEXT("SourceAudio")/(Name+TEXT(".wav"))));
    if (Bytes.Num())
    {
     auto* Factory=NewObject<USoundFactory>(); Factory->SuppressImportDialogs(); const uint8* Data=Bytes.GetData();
     UObject* Sound=Factory->FactoryCreateBinary(USoundWave::StaticClass(),CreatePackage(*Path),FName(Name),RF_Public|RF_Standalone,nullptr,TEXT("wav"),Data,Data+Bytes.Num(),GWarn);
     if (Sound) { FAssetRegistryModule::AssetCreated(Sound); Save(Sound); }
    }
   }
  }
  auto* Config=Asset<UActCombatConfig>(TEXT("/Game/ActDemo/Data/DA_Combat"));
  const FString Base=TEXT("/Game/Characters/Mannequins/Anims/");
  for (int32 I=0; I<3; ++I)
  {
   auto& A=Config->LightAttacks[I]; A.Montage=Montage(FString::Printf(TEXT("AM_Light%d"),I+1),Base+FString::Printf(TEXT("Unarmed/Attack/MM_Attack_0%d"),I+1));
   Window(A.Montage,0,A.ActiveStart,A.ActiveEnd,A.Duration); Window(A.Montage,2,A.ComboStart,A.Duration-.02f,A.Duration); Save(A.Montage);
  }
  auto& Heavy=Config->HeavyAttack; Heavy.Montage=Montage(TEXT("AM_Heavy"),Base+TEXT("Unarmed/Attack/MM_ChargedAttack"));
  Window(Heavy.Montage,0,Heavy.ActiveStart,Heavy.ActiveEnd,Heavy.Duration); Save(Heavy.Montage);
  Config->DodgeMontage=Montage(TEXT("AM_Dodge"),Base+TEXT("Unarmed/Jump/MM_Dash"));
  Window(Config->DodgeMontage,1,Config->InvulnerableStart,Config->InvulnerableEnd,Config->DodgeDuration); Save(Config->DodgeMontage);
  Config->HitMontage=Montage(TEXT("AM_Hit"),Base+TEXT("Rifle/HitReact/MM_HitReact_Front_Lgt_01")); Save(Config->HitMontage);
  Config->DeathMontage=Montage(TEXT("AM_Death"),Base+TEXT("Death/MM_Death_Front_01")); Config->DeathMontage->bEnableAutoBlendOut=false; Save(Config->DeathMontage);
  Config->HitEffect=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"));
  Config->HitSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/ActDemo/Audio/S_Hit.S_Hit"));
  Config->AttackSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/ActDemo/Audio/S_Swing.S_Swing"));
  Save(Config);
  auto Profile=[&](const TCHAR* Name,const TCHAR* Label,float Health,float Damage,float Speed)
  {
   auto* P=Asset<UActCharacterProfile>(FString(TEXT("/Game/ActDemo/Data/DA_"))+Name);
   P->DisplayName=FText::FromString(Label); P->Health=Health; P->DamageScale=Damage; P->MoveSpeed=Speed;
   if (!P->Mesh) P->Combat=Config; // Keep an adapted mesh paired with its skeleton-compatible combat set.
   Save(P);
  };
  Profile(TEXT("Player"),TEXT("WAYFARER"),150.f,1.f,420.f);
  Profile(TEXT("Scout"),TEXT("ASH SCOUT"),100.f,.65f,340.f);
  Profile(TEXT("Brute"),TEXT("IRON GUARD"),200.f,.85f,240.f);
  Profile(TEXT("Boss"),TEXT("WARDEN OF ASH"),850.f,1.f,340.f);
  Profile(TEXT("Dummy"),TEXT("TRAINING EFFIGY"),10000.f,0.f,0.f);
 }
 bool BuildAI()
 {
  auto* Tree=Asset<UStateTree>(TEXT("/Game/ActDemo/AI/ST_Enemy"));
  auto* Data=NewObject<UStateTreeEditorData>(Tree,NAME_None,RF_Transactional); Tree->EditorData=Data;
  Data->Schema=NewObject<UStateTreeAIComponentSchema>(Data);
  UStateTreeState& Root=Data->AddRootState();
  for (EActAIState State : {EActAIState::Dead,EActAIState::Attack,EActAIState::Recover,EActAIState::Chase,EActAIState::Investigate,EActAIState::Search,EActAIState::Return,EActAIState::Patrol})
  {
   auto& Child=Root.AddChildState(FName(UEnum::GetValueAsString(State)));
   Child.AddEnterCondition<FActAICondition>().GetInstanceData().State=State;
   Child.AddTask<FActAIStateTask>().GetInstanceData().State=State;
   Child.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted,EStateTreeTransitionType::GotoState,&Root);
  }
  FStateTreeCompilerLog Log; FStateTreeCompiler Compiler(Log);
  if (!Compiler.Compile(*Tree)) { UE_LOG(LogTemp,Error,TEXT("StateTree compilation failed")); return false; }
  return Save(Tree);
 }
 void BuildHUD()
 {
  UPackage* Package=CreatePackage(TEXT("/Game/ActDemo/UI/WBP_HUD"));
  auto* BP=FindObject<UWidgetBlueprint>(Package,TEXT("WBP_HUD"));
  if (!BP) BP=CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UActHUDWidget::StaticClass(),Package,TEXT("WBP_HUD"),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
  UActHUDWidget::CreateLayout(BP->WidgetTree); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
  FKismetEditorUtilities::CompileBlueprint(BP); FAssetRegistryModule::AssetCreated(BP); Save(BP);
 }
 bool BuildMap()
 {
  UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false); check(World);
  World->GetWorldSettings()->DefaultGameMode=AActGameMode::StaticClass();
  auto* Stone=Material(TEXT("M_Stone"),FLinearColor(.16,.19,.23));
  auto* LightStone=Material(TEXT("M_LightStone"),FLinearColor(.32,.36,.38));
  auto* Dark=Material(TEXT("M_Basalt"),FLinearColor(.055,.065,.08));
  auto* Amber=Material(TEXT("M_Amber"),FLinearColor(.9,.43,.08),3.f);
  auto* Teal=Material(TEXT("M_Teal"),FLinearColor(.07,.45,.48),1.5f);
  Material(TEXT("M_Player"),FLinearColor(.62,.69,.7)); Material(TEXT("M_Scout"),FLinearColor(.08,.3,.35)); Material(TEXT("M_Brute"),FLinearColor(.43,.17,.09)); Material(TEXT("M_Boss"),FLinearColor(.18,.12,.08));
  UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube"));
  auto Block=[&](FString Name,FVector Pos,FVector Size,UMaterial* Mat)
  {
   auto* A=World->SpawnActor<AStaticMeshActor>(Pos,FRotator::ZeroRotator); A->SetActorLabel(Name);
   A->GetStaticMeshComponent()->SetStaticMesh(Cube); A->GetStaticMeshComponent()->SetMaterial(0,Mat); A->SetActorScale3D(Size/100.f); return A;
  };
  Block(TEXT("Court Foundation"),FVector(0,0,-70),FVector(11000,3000,140),Stone);
  for (int I=-5; I<=5; ++I) { Block(TEXT("Walkway Joint"),FVector(I*950,0,1),FVector(12,2950,2),Dark); }
  for (float Side : {-1.f,1.f})
  {
   Block(TEXT("Outer Wall"),FVector(0,Side*1500,240),FVector(11000,100,480),Dark);
   for (int I=-5;I<=5;++I)
   {
    Block(TEXT("Buttress"),FVector(I*1000,Side*1430,310),FVector(170,210,620),LightStone);
    Block(TEXT("Amber Sconce"),FVector(I*1000,Side*1315,280),FVector(30,30,130),Amber);
   }
  }
  for (float End:{-5500.f,5500.f}) Block(TEXT("Boundary"),FVector(End,0,230),FVector(100,3000,460),Dark);
  for (float X:{-500.f,1800.f})
  {
   for (float Side:{-1.f,1.f}) Block(TEXT("Gate Wall"),FVector(X,Side*1000,250),FVector(140,1000,500),LightStone);
   Block(TEXT("Gate Lintel"),FVector(X,0,560),FVector(220,1300,150),Dark);
  }
  for (FVector Pos:{FVector(-2800,650,140),FVector(-1800,-600,140),FVector(1000,600,140),FVector(3300,850,140),FVector(4200,-850,140)}) Block(TEXT("Cover Plinth"),Pos,FVector(260,300,280),LightStone);
  Block(TEXT("Beacon Base"),FVector(0,0,15),FVector(360,360,30),Dark);
  Block(TEXT("Beacon"),FVector(0,0,65),FVector(120,120,100),Amber);
  Block(TEXT("Approach Mark"),FVector(-4200,0,2),FVector(500,12,4),Teal);
  auto* Beacon=World->SpawnActor<AActCheckpoint>(FVector(0,0,100),FRotator::ZeroRotator); Beacon->SetActorLabel(TEXT("Checkpoint - Amber Beacon"));
  auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,1500),FRotator(-45,-25,0)); Sun->GetLightComponent()->SetIntensity(4.f); Sun->SetMobility(EComponentMobility::Movable);
  CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight=true;
  World->SpawnActor<ASkyAtmosphere>();
  auto* Sky=World->SpawnActor<ASkyLight>(); Sky->GetLightComponent()->SetIntensity(.8f); Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
  for (float X:{-4000.f,0.f,3800.f}) { auto* Light=World->SpawnActor<APointLight>(FVector(X,0,500),FRotator::ZeroRotator); Light->GetLightComponent()->SetIntensity(9000.f); Light->GetLightComponent()->SetLightColor(FLinearColor(1,.65,.3)); CastChecked<UPointLightComponent>(Light->GetLightComponent())->SetAttenuationRadius(1700.f); }
  World->SpawnActor<APlayerStart>(FVector(-4700,0,100),FRotator::ZeroRotator);
  auto Spawn=[&](FVector Pos,EActEnemyKind Kind,int Group)
  {
   auto* A=World->SpawnActor<AActSpawnPoint>(Pos,FRotator(0,180,0)); A->Kind=Kind; A->Encounter=Group; A->SetActorLabel(UEnum::GetValueAsString(Kind));
   const FString Name=StaticEnum<EActEnemyKind>()->GetNameStringByValue(static_cast<int64>(Kind));
   A->Profile=LoadObject<UActCharacterProfile>(nullptr,*(FString(TEXT("/Game/ActDemo/Data/DA_"))+Name));
  };
  Spawn(FVector(-4100,600,100),EActEnemyKind::Dummy,0);
  Spawn(FVector(-3000,-250,100),EActEnemyKind::Scout,0); Spawn(FVector(-2200,450,100),EActEnemyKind::Scout,0); Spawn(FVector(-1300,0,100),EActEnemyKind::Brute,0);
  Spawn(FVector(900,-450,100),EActEnemyKind::Scout,1); Spawn(FVector(1400,450,100),EActEnemyKind::Brute,1); Spawn(FVector(3900,0,110),EActEnemyKind::Boss,2);
  auto Sign=[&](FVector Pos,FString Value)
  { auto* A=World->SpawnActor<ATextRenderActor>(Pos,FRotator(0,180,0)); A->GetTextRender()->SetText(FText::FromString(Value)); A->GetTextRender()->SetWorldSize(35); A->GetTextRender()->SetTextRenderColor(FColor(235,190,110)); A->GetTextRender()->SetHorizontalAlignment(EHTA_Center); };
  Sign(FVector(-3600,0,310),TEXT("I  /  THE TRAINING COURT")); Sign(FVector(100,0,310),TEXT("II  /  THE BEACON")); Sign(FVector(2100,0,420),TEXT("III  /  WARDEN OF ASH"));
  auto* Bounds=World->SpawnActor<ANavMeshBoundsVolume>(FVector(0,0,300),FRotator::ZeroRotator);
  Bounds->Brush=NewObject<UModel>(Bounds,NAME_None,RF_Transactional); Bounds->Brush->Initialize(nullptr,true);
  Bounds->GetBrushComponent()->Brush=Bounds->Brush;
  auto* Builder=NewObject<UCubeBuilder>(); Builder->X=11500; Builder->Y=3300; Builder->Z=1500; Builder->Build(World,Bounds); Bounds->PostEditChange();
  // Cook strips editor brush polygons; runtime bounds come from the body setup.
  FBSPOps::csgPrepMovingBrush(Bounds);
  Bounds->GetBrushComponent()->UpdateBounds();
  const UBodySetup* Body=Bounds->GetBrushComponent()->BrushBodySetup;
  if (!Body || Body->AggGeom.GetElementCount()==0)
  { UE_LOG(LogTemp,Error,TEXT("Navigation bounds have no runtime collision hull")); return false; }
  FNavigationSystem::AddNavigationSystemToWorld(*World,FNavigationSystemRunMode::EditorMode);
  if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
  {
   Nav->OnNavigationBoundsUpdated(Bounds);
   Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
   Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock | ENavigationBuildLock::InitialLock | ENavigationBuildLock::NoUpdateInEditor);
   Nav->Build();
  }
  return UEditorLoadingAndSavingUtils::SaveMap(World,TEXT("/Game/ActDemo/Maps/L_AshenCourt"));
 }
}
UActDemoBuildCommandlet::UActDemoBuildCommandlet() { IsClient=false; IsEditor=true; LogToConsole=true; }
int32 UActDemoBuildCommandlet::Main(const FString& Params)
{
 if (FPackageName::DoesPackageExist(TEXT("/Game/ActDemo/Maps/L_AshenCourt")) && !FParse::Param(*Params,TEXT("Force")))
 { UE_LOG(LogTemp,Display,TEXT("Demo assets already exist; use -Force only to regenerate owned assets.")); return 0; }
 BuildInput(); BuildCombat(); if (!BuildAI()) return 1; BuildHUD(); if (!BuildMap()) return 1;
 UE_LOG(LogTemp,Display,TEXT("ACTDEMO_ASSETS_READY")); return 0;
}

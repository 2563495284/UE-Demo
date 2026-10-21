#include "ActKwangCommandlet.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Character/ActAnimInstance.h"
#include "Combat/ActCombatTypes.h"
#include "Combat/ActCombatNotify.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Animation/BlendSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "K2Node_VariableGet.h"
#include "UObject/SavePackage.h"

namespace
{
 const FString Source=TEXT("/Game/ParagonKwang/Characters/Heroes/Kwang/Animations/");
 const FString Destination=TEXT("/Game/ActDemo/Kwang/");
 bool Save(UObject* Object)
 {
  Object->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
  return UPackage::SavePackage(Object->GetPackage(),Object,*FPackageName::LongPackageNameToFilename(Object->GetPackage()->GetName(),TEXT(".uasset")),Args);
 }
 template<class T> T* Asset(const FString& Name)
 {
  UPackage* Package=CreatePackage(*(Destination+Name)); Package->FullyLoad();
  T* Object=FindObject<T>(Package,*Name);
  if(!Object) { Object=NewObject<T>(Package,*Name,RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Object); }
  return Object;
 }
 UAnimMontage* Montage(const FString& Name,const TCHAR* SequenceName,bool bAbsolute=false)
 {
  auto* Sequence=LoadObject<UAnimSequence>(nullptr,*(Source+SequenceName)); check(Sequence);
  if(bAbsolute && Sequence->IsValidAdditive())
  {
   // Raw keys contain the authored pose; remove additive extraction on our own copy.
   const FString CopyName=TEXT("A_")+FString(SequenceName);
   UPackage* Package=CreatePackage(*(Destination+CopyName)); Package->FullyLoad();
   auto* Copy=FindObject<UAnimSequence>(Package,*CopyName);
   if(!Copy) { Copy=DuplicateObject<UAnimSequence>(Sequence,Package,*CopyName); FAssetRegistryModule::AssetCreated(Copy); }
   Copy->AdditiveAnimType=AAT_None; Copy->PostEditChange(); check(Save(Copy)); Sequence=Copy;
  }
  auto* M=Asset<UAnimMontage>(Name); M->SetSkeleton(Sequence->GetSkeleton());
  M->SlotAnimTracks.Reset(); M->CompositeSections.Reset(); M->Notifies.Reset();
  auto& Track=M->SlotAnimTracks.AddDefaulted_GetRef(); Track.SlotName=TEXT("DefaultSlot");
  FAnimSegment Segment; Segment.SetAnimReference(Sequence); Segment.AnimStartTime=0; Segment.AnimEndTime=Sequence->GetPlayLength(); Segment.AnimPlayRate=1; Segment.LoopingCount=1;
  Track.AnimTrack.AnimSegments.Add(Segment); M->SetCompositeLength(Sequence->GetPlayLength()); M->AddAnimCompositeSection(TEXT("Action"),0);
  M->BlendIn.SetBlendTime(.08f); M->BlendOut.SetBlendTime(.12f); M->bEnableAutoBlendOut=true;
  return M;
 }
 void Window(UAnimMontage* M,uint8 Type,float Start,float End,float Duration)
 {
  auto& E=M->Notifies.AddDefaulted_GetRef(); E.NotifyStateClass=NewObject<UActCombatNotify>(M); CastChecked<UActCombatNotify>(E.NotifyStateClass)->Window=Type;
  const float Scale=M->GetPlayLength()/Duration;
  E.Link(M,Start*Scale); E.SetDuration((End-Start)*Scale); E.EndLink.Link(M,End*Scale); E.NotifyName=TEXT("CombatWindow");
 }
 void FitAttack(FActAttack& Attack)
 {
  UAnimMontage* M=Attack.Montage;
  auto& Segments=M->SlotAnimTracks[0].AnimTrack.AnimSegments;
  auto* Sequence=CastChecked<UAnimSequence>(Segments[0].GetAnimReference());
  // The source blade crosses the front at 15-20% of the clip, followed by a long hold.
  // Fit windup / strike / recovery independently instead of speeding the entire clip up.
  const float SourceCuts[]={0.f,.12f,.24f,1.f};
  const float Times[]={0.f,Attack.ActiveStart,Attack.ActiveEnd,Attack.Duration};
  Segments.Reset();
  for(int32 I=0;I<3;++I)
  {
   auto& S=Segments.AddDefaulted_GetRef(); S.SetAnimReference(Sequence); S.StartPos=Times[I];
   S.AnimStartTime=SourceCuts[I]*Sequence->GetPlayLength(); S.AnimEndTime=SourceCuts[I+1]*Sequence->GetPlayLength();
   S.AnimPlayRate=(S.AnimEndTime-S.AnimStartTime)/(Times[I+1]-Times[I]); S.LoopingCount=1;
  }
  M->SetCompositeLength(Attack.Duration);
 }
 template<class T> T* Node(UEdGraph* Graph,int32 X)
 {
  auto* N=NewObject<T>(Graph); Graph->AddNode(N,false,false); N->CreateNewGuid(); N->PostPlacedNewNode(); N->AllocateDefaultPins(); N->NodePosX=X; return N;
 }
 UAnimBlueprint* BuildAnimation(USkeletalMesh* Mesh)
 {
  auto* Blend=Asset<UBlendSpace>(TEXT("BS_KwangMove")); Blend->SetSkeleton(Mesh->GetSkeleton());
  // Configure through the reflected data used by the editor details panel.
  FStructProperty* Parameters=FindFProperty<FStructProperty>(UBlendSpace::StaticClass(),TEXT("BlendParameters")); check(Parameters);
  auto* Direction=Parameters->ContainerPtrToValuePtr<FBlendParameter>(Blend,0);
  Direction->DisplayName=TEXT("Direction"); Direction->Min=-180; Direction->Max=180; Direction->GridNum=4; Direction->bWrapInput=true;
  auto* Speed=Parameters->ContainerPtrToValuePtr<FBlendParameter>(Blend,1);
  Speed->DisplayName=TEXT("Speed"); Speed->Min=0; Speed->Max=660; Speed->GridNum=4;
  while(Blend->GetNumberOfBlendSamples()) Blend->DeleteSample(0);
  const TCHAR* Jog[]={TEXT("Jog_Bwd"),TEXT("Jog_Left"),TEXT("Jog_Fwd"),TEXT("Jog_Right"),TEXT("Jog_Bwd")};
  for(int32 I=0;I<5;++I)
  {
   const float Angle=-180.f+I*90.f;
   Blend->AddSample(LoadObject<UAnimSequence>(nullptr,*(Source+TEXT("Idle"))),FVector(Angle,0,0));
   Blend->AddSample(LoadObject<UAnimSequence>(nullptr,*(Source+Jog[I])),FVector(Angle,420,0));
   Blend->AddSample(LoadObject<UAnimSequence>(nullptr,*(Source+(I==2?TEXT("Sprint_Fwd"):Jog[I]))),FVector(Angle,660,0));
  }
  Blend->ValidateSampleData(); Blend->ResampleData(); Blend->PostEditChange(); check(Save(Blend));
  UPackage* Package=CreatePackage(*(Destination+TEXT("ABP_Kwang"))); Package->FullyLoad();
  auto* BP=FindObject<UAnimBlueprint>(Package,TEXT("ABP_Kwang"));
  if(!BP)
  {
   auto* Factory=NewObject<UAnimBlueprintFactory>(); Factory->ParentClass=UActAnimInstance::StaticClass(); Factory->TargetSkeleton=Mesh->GetSkeleton(); Factory->PreviewSkeletalMesh=Mesh;
   BP=CastChecked<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),Package,TEXT("ABP_Kwang"),RF_Public|RF_Standalone,nullptr,GWarn)); FAssetRegistryModule::AssetCreated(BP);
  }
  UEdGraph* Graph=nullptr; for(auto G:BP->FunctionGraphs) if(G->GetFName()==TEXT("AnimGraph")) Graph=G;
  check(Graph);
  TArray<TObjectPtr<UEdGraphNode>> OldNodes=Graph->Nodes; for(auto N:OldNodes) Graph->RemoveNode(N);
  auto* Root=Node<UAnimGraphNode_Root>(Graph,600);
  auto* Slot=Node<UAnimGraphNode_Slot>(Graph,350); Slot->Node.SlotName=TEXT("DefaultSlot");
  auto* Player=Node<UAnimGraphNode_BlendSpacePlayer>(Graph,0); Player->Node.SetBlendSpace(Blend); Player->ReconstructNode();
  for(auto& Pin:Player->ShowPinForProperties) if(Pin.PropertyName==TEXT("X")||Pin.PropertyName==TEXT("Y")) Pin.bShowPin=true;
  Player->ReconstructNode();
  const UEdGraphSchema* Schema=Graph->GetSchema();
  auto Link=[&](UEdGraphPin* A,UEdGraphPin* B) { check(A && B); check(Schema->TryCreateConnection(A,B)); };
  Link(Player->FindPin(TEXT("Pose")),Slot->FindPin(TEXT("Source")));
  Link(Slot->FindPin(TEXT("Pose")),Root->FindPin(TEXT("Result")));
  for(const TCHAR* Variable:{TEXT("Direction"),TEXT("Speed")})
  {
   auto* Getter=NewObject<UK2Node_VariableGet>(Graph); Graph->AddNode(Getter,false,false); Getter->CreateNewGuid(); Getter->VariableReference.SetSelfMember(Variable); Getter->AllocateDefaultPins(); Getter->NodePosX=-250;
   Link(Getter->GetValuePin(),Player->FindPin(FString(Variable)==TEXT("Direction")?TEXT("X"):TEXT("Y")));
  }
  FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
  check(BP->Status!=BS_Error); check(Save(BP)); return BP;
 }
 bool Build(USkeletalMesh* Mesh)
 {
  UAnimBlueprint* BP=BuildAnimation(Mesh);
  auto* Config=Asset<UActCombatConfig>(TEXT("DA_KwangCombat"));
  // Preserve combat balance; windows are fitted to the original sword arcs.
  const TCHAR* Attacks[]={TEXT("PrimaryAttack_A_Slow"),TEXT("PrimaryAttack_B_Slow"),TEXT("PrimaryAttack_C_Slow")};
  for(int32 I=0;I<3;++I)
  {
   auto& A=Config->LightAttacks[I]; A.Montage=Montage(FString::Printf(TEXT("AM_KwangLight%d"),I+1),Attacks[I]);
   A.ActiveStart=.28f; A.ActiveEnd=.5f; A.ComboStart=.52f; A.Reach=230.f; FitAttack(A);
   Window(A.Montage,0,A.ActiveStart,A.ActiveEnd,A.Duration); Window(A.Montage,2,A.ComboStart,A.Duration-.02f,A.Duration); check(Save(A.Montage));
  }
  auto& Heavy=Config->HeavyAttack; Heavy.Montage=Montage(TEXT("AM_KwangHeavy"),TEXT("PrimaryAttack_D_Slow")); Heavy.ActiveStart=.7f; Heavy.ActiveEnd=.95f; Heavy.Reach=250.f; FitAttack(Heavy);
  Window(Heavy.Montage,0,Heavy.ActiveStart,Heavy.ActiveEnd,Heavy.Duration); check(Save(Heavy.Montage));
  Config->DodgeMontage=Montage(TEXT("AM_KwangDodge"),TEXT("Jump_Apex"));
  Window(Config->DodgeMontage,1,Config->InvulnerableStart,Config->InvulnerableEnd,Config->DodgeDuration); check(Save(Config->DodgeMontage));
  Config->HealMontage=Montage(TEXT("AM_KwangHeal"),TEXT("Cast")); check(Save(Config->HealMontage));
  Config->HitMontage=Montage(TEXT("AM_KwangHit"),TEXT("Hitreact_Fwd"),true); check(Save(Config->HitMontage));
  Config->DeathMontage=Montage(TEXT("AM_KwangDeath"),TEXT("Death_Bwd")); Config->DeathMontage->bEnableAutoBlendOut=false; check(Save(Config->DeathMontage));
  auto* Fallback=LoadObject<UActCombatConfig>(nullptr,TEXT("/Game/ActDemo/Data/DA_Combat")); check(Fallback);
  Config->HitEffect=Fallback->HitEffect; Config->HitSound=Fallback->HitSound; Config->AttackSound=Fallback->AttackSound; check(Save(Config));
  auto* Profile=LoadObject<UActCharacterProfile>(nullptr,TEXT("/Game/ActDemo/Data/DA_Player")); check(Profile);
  Profile->Mesh=Mesh; Profile->AnimClass=BP->GeneratedClass; Profile->Combat=Config;
  Profile->WeaponBaseSocket=TEXT("FX_weapon_base"); Profile->WeaponTipSocket=TEXT("FX_weapon_tip"); Profile->WeaponTraceRadius=18.f;
  check(Save(Profile)); UE_LOG(LogTemp,Display,TEXT("ACTDEMO_KWANG_READY")); return true;
 }
}

UActKwangCommandlet::UActKwangCommandlet() { IsClient=false; IsEditor=true; LogToConsole=true; }
int32 UActKwangCommandlet::Main(const FString& Params)
{
 const FString Base=TEXT("/Game/ParagonKwang/Characters/Heroes/Kwang/");
 auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*(Base+TEXT("Meshes/Kwang_GDC")));
 if (!Mesh) return 1;
 if(FParse::Param(*Params,TEXT("Build"))) return Build(Mesh)?0:1;
 auto* Skeleton=Mesh->GetSkeleton();
 UE_LOG(LogTemp,Display,TEXT("KWANG mesh=%s skeleton=%s bounds=%s"),*Mesh->GetName(),*Skeleton->GetPathName(),*Mesh->GetBounds().BoxExtent.ToString());
 const auto& Ref=Skeleton->GetReferenceSkeleton();
 for (int32 I=0;I<Ref.GetNum();++I)
 {
  FString Name=Ref.GetBoneName(I).ToString();
  if(Name.Contains(TEXT("sword"))||Name.Contains(TEXT("weapon"))||Name.Contains(TEXT("hand_r")))
   UE_LOG(LogTemp,Display,TEXT("KWANG bone[%d]=%s parent=%s ref=%s"),I,*Name,*Ref.GetBoneName(Ref.GetParentIndex(I)).ToString(),*Ref.GetRefBonePose()[I].ToString());
 }
 for(auto S:Skeleton->Sockets) UE_LOG(LogTemp,Display,TEXT("KWANG socket=%s bone=%s transform=%s"),*S->SocketName.ToString(),*S->BoneName.ToString(),*S->GetSocketLocalTransform().ToString());
 for (const TCHAR* Name:{TEXT("Idle"),TEXT("Jog_Fwd"),TEXT("Sprint_Fwd"),TEXT("PrimaryAttack_A_Slow"),TEXT("PrimaryAttack_B_Slow"),TEXT("PrimaryAttack_C_Slow"),TEXT("PrimaryAttack_D_Slow"),TEXT("Hitreact_Fwd"),TEXT("Death_Bwd"),TEXT("Jump_Start"),TEXT("Emote_DeepBreath"),TEXT("Cast")})
 {
  auto* A=LoadObject<UAnimSequence>(nullptr,*(Base+TEXT("Animations/")+Name));
  if(A) UE_LOG(LogTemp,Display,TEXT("KWANG anim=%s duration=%.3f additive=%d root=%d notifies=%d"),Name,A->GetPlayLength(),A->IsValidAdditive(),A->bEnableRootMotion,A->Notifies.Num());
  if(A && FString(Name).StartsWith(TEXT("PrimaryAttack")))
  {
   const auto* BaseSocket=Skeleton->FindSocket(TEXT("FX_weapon_base")); const auto* TipSocket=Skeleton->FindSocket(TEXT("FX_weapon_tip"));
   for(int32 Frame=0;Frame<=20;++Frame)
   {
    const double Time=A->GetPlayLength()*Frame/20.; FTransform Weapon=FTransform::Identity;
    for(int32 Bone=Ref.FindBoneIndex(TEXT("weapon_r"));Bone!=INDEX_NONE;Bone=Ref.GetParentIndex(Bone))
    { FTransform Local; A->GetBoneTransform(Local,FSkeletonPoseBoneIndex(Bone),FAnimExtractContext(Time),true); Weapon=Weapon*Local; }
    const FTransform MeshTransform(FRotator(0,-90,0),FVector(0,0,-90));
    const FVector B=MeshTransform.TransformPosition(Weapon.TransformPosition(BaseSocket->RelativeLocation));
    const FVector T=MeshTransform.TransformPosition(Weapon.TransformPosition(TipSocket->RelativeLocation));
    UE_LOG(LogTemp,Display,TEXT("KWANG_ARC %s fraction=%.2f frontDistance=%.1f tip=%s"),Name,Frame/20.f,FMath::PointDistToSegment(FVector(140,0,0),B,T),*T.ToString());
   }
  }
 }
 auto* BP=FParse::Param(*Params,TEXT("InspectBlueprint")) ? LoadObject<UAnimBlueprint>(nullptr,*(Base+TEXT("Animations/Kwang_AnimBlueprint"))) : nullptr;
 if (BP)
 {
  UE_LOG(LogTemp,Display,TEXT("KWANG animBP parent=%s"),*GetNameSafe(BP->ParentClass));
  TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
  for(auto* G:Graphs) for(auto N:G->Nodes)
   if(G->GetName()==TEXT("EventGraph")||N->GetClass()->GetName().Contains(TEXT("Slot")))
    UE_LOG(LogTemp,Display,TEXT("KWANG graph=%s node=%s title=%s"),*G->GetName(),*N->GetClass()->GetName(),*N->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"),TEXT(" ")));
 }
 return 0;
}

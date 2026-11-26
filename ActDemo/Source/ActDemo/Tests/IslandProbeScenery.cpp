#include "Tests/IslandProbe.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandScenerySupport.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelTerrain.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    // Two trail markers, the four clue landmarks, a landing, four working mechanisms and four route shapes.
    constexpr int32 NumCases = 16, FirstRoute = 11, ChestCase = 15;
    const TCHAR* CaseNames[] = {TEXT("Cairn"),TEXT("TrailBeacon"),TEXT("Arch"),TEXT("SignalNeedles"),
        TEXT("Quarry"),TEXT("Fin"),TEXT("Dock"),TEXT("Bridge"),TEXT("Dam"),TEXT("SandDepot"),TEXT("WindTower"),
        TEXT("FanPalm"),TEXT("Saltbush"),TEXT("SplitReef"),TEXT("CoralSpur"),TEXT("Chest")};

    int32 CaseFor(AActor& Owner)
    {
        if (const auto* Route=Cast<AIslandRouteVoxel>(&Owner))
            return int32(Route->GetKind()) < 4 ? FirstRoute+int32(Route->GetKind()) : INDEX_NONE;
        if (Owner.IsA<AIslandChest>()) return ChestCase;
        if (const auto* Prop=Cast<AIslandNeutralProp>(&Owner)) return Prop->GetKind() >= EIslandNeutralKind::Bridge ? 7+int32(Prop->GetKind())-int32(EIslandNeutralKind::Bridge) : INDEX_NONE;
        if (Owner.IsA<AExpeditionDock>()) return 6;
        if (const auto* Mark=Cast<AExpeditionLandmark>(&Owner)) return 2+Mark->ClueIndex;
        if (const auto* Marker=Cast<AStaticMeshActor>(&Owner))
        {
            const UStaticMesh* Mesh=Marker->GetStaticMeshComponent()->GetStaticMesh();
            if (Mesh && Mesh->GetFName()==TEXT("SM_Cairn")) return 0;
            if (Mesh && Mesh->GetFName()==TEXT("SM_TrailBeacon")) return 1;
        }
        return INDEX_NONE;
    }

    int32 Count(const UProceduralMeshComponent& Field)
    {
        if (auto* Sculpt=Cast<UIslandRouteVoxelComponent>(&Field)) return Sculpt->RemainingCount();
        return CastChecked<UIslandMeshVoxels>(&Field)->RemainingCount();
    }

    FName SourceName(const UProceduralMeshComponent& Field)
    {
        if (auto* Skin=Cast<UIslandMeshVoxels>(&Field)) return Skin->SourcePart()->GetFName();
        return NAME_None;
    }

    UProceduralMeshComponent* MatchField(AActor* Subject,FName Part)
    {
        if (!Subject) return nullptr;
        if (auto* Route=Cast<AIslandRouteVoxel>(Subject)) return Route->Field();
        TInlineComponentArray<UIslandMeshVoxels*> Skins(Subject);
        for (auto* Field : Skins)
            if (Field->SourcePart() && Field->SourcePart()->GetFName()==Part) return Field;
        return nullptr;
    }

    int32 TraceDepth(UProceduralMeshComponent& Field,FVector Eye,FVector Aim)
    {
        FHitResult Hit;
        return Field.LineTraceComponent(Hit,Eye,Eye+Aim*260.,FCollisionQueryParams::DefaultQueryParam)
            ? FMath::RoundToInt(Hit.Distance) : -1;
    }

    FString Snapshot(UProceduralMeshComponent& Field,FVector Eye,FVector Aim)
    {
        auto* Route=Cast<UIslandRouteVoxelComponent>(&Field);
        const uint32 Cells=Route ? Route->Digest() : CastChecked<UIslandMeshVoxels>(&Field)->Digest();
        int32 Triangles=0;
        for (int32 I=0; I<Field.GetNumSections(); ++I)
            if (const auto* Sec=Field.GetProcMeshSection(I); Sec && Sec->bSectionVisible)
                Triangles+=Sec->ProcIndexBuffer.Num()/3;
        return FString::Printf(TEXT("%d:%08x:%d:%d:%d"),Count(Field),Cells,Triangles,
            int32(Field.GetCollisionResponseToChannel(ECC_Pawn)),TraceDepth(Field,Eye,Aim));
    }

    bool Reaches(UWorld& World,AVoxelCharacter& Hero,UProceduralMeshComponent& Field,FVector Eye,FVector Aim,FVector& Impact)
    {
        FHitResult Hit;
        if (!AIslandNeutralProp::TraceShovel(World,Hit,Eye,Eye+Aim*235.,
            FCollisionQueryParams(SCENE_QUERY_STAT(SceneryToolFace),false,&Hero))) return false;
        const auto* Skin=Cast<UIslandMeshVoxels>(&Field);
        if (Hit.GetComponent()!=&Field && !(Skin && Hit.GetComponent()==Skin->SourcePart())) return false;
        Impact=Hit.ImpactPoint;
        return true;
    }

    bool PickFace(UWorld& World,AVoxelCharacter& Hero,UProceduralMeshComponent& Field,int32 Offset,
        FVector& Eye,FVector& Aim,FVector& Impact,const FVector* Away=nullptr,bool bAimAtFrond=false)
    {
        const auto Try=[&](FVector P,FVector N)
        {
            // Show a surviving crown after a frond hit, before testing the loss of a whole unsupported trunk.
            const auto IsFrond=[&](FVector Point)
            {
                const FVector Local=Field.GetComponentTransform().InverseTransformPosition(Point);
                return Local.Z>72. && Local.SizeSquared2D()>FMath::Square(86.);
            };
            if (bAimAtFrond && !IsFrond(P)) return false;
            if (N.IsNearlyZero() || (Away && FVector::DistSquared(P,*Away)<FMath::Square(68.))) return false;
            const FVector Origin=P+N*46.;
            if (!Reaches(World,Hero,Field,Origin,-N,Impact) ||
                (Away && FVector::DistSquared(Impact,*Away)<FMath::Square(55.))) return false;
            if (bAimAtFrond && !IsFrond(Impact)) return false;
            Eye=Origin;
            Aim=-N;
            return true;
        };
        const FTransform& Transform=Field.GetComponentTransform();
        for (int32 I=0; I<Field.GetNumSections(); ++I)
        {
            const auto* Section=Field.GetProcMeshSection(I);
            if (!Section || !Section->bEnableCollision || !Section->bSectionVisible) continue;
            const int32 Faces=Section->ProcIndexBuffer.Num()/3;
            const int32 Tries=FMath::Min(Faces,240);
            for (int32 Sample=0; Sample<Tries; ++Sample)
            {
                const int32 T=((Sample*Faces/Tries+Offset)%Faces)*3;
                const auto Vertex=[&](int32 K){ return Transform.TransformPosition(Section->ProcVertexBuffer[Section->ProcIndexBuffer[T+K]].Position); };
                const FVector A=Vertex(0), B=Vertex(1), C=Vertex(2);
                const FVector Normal=FVector::CrossProduct(C-A,B-A).GetSafeNormal();
                const FVector Center=(A+B+C)/3.;
                if (Try(Center,Normal) || Try(Center,-Normal)) return true;
            }
        }
        if (auto* Skin=Cast<UIslandMeshVoxels>(&Field))
        {
            FVector P,N;
            if (Skin->GetSurfaceTarget(P,N) && Try(P,N)) return true;
            auto* Original=Skin->SourcePart();
            if (!Original) return false;
            for (const FVector Side : {FVector::UpVector,FVector::RightVector,-FVector::RightVector,FVector::ForwardVector,-FVector::ForwardVector})
            {
                FHitResult Hit;
                if (Original->LineTraceComponent(Hit,Original->Bounds.Origin+Side*(Original->Bounds.SphereRadius+70.),
                    Original->Bounds.Origin,FCollisionQueryParams::DefaultQueryParam) && Try(Hit.ImpactPoint,Hit.ImpactNormal)) return true;
            }
        }
        return false;
    }

    TArray<UProceduralMeshComponent*> PartsFor(AActor& Owner)
    {
        TArray<UProceduralMeshComponent*> Parts;
        if (auto* Route=Cast<AIslandRouteVoxel>(&Owner)) { Parts.Add(Route->Field()); return Parts; }
        TInlineComponentArray<UIslandMeshVoxels*> Skins(&Owner);
        for (auto* Field : Skins) if (Field->SourcePart() && Field->InitialCount()>0) Parts.Add(Field);
        return Parts;
    }
}

void AIslandProbePeer::ClientSceneryInspect_Implementation(int32 Stage,AActor* Subject,FName Part,FVector Eye,FVector Aim)
{
    if (auto* Field=MatchField(Subject,Part))
    {
        const FString Result=Snapshot(*Field,Eye,Aim);
        UE_LOG(LogTemp,Display,TEXT("SCENERY_VOXEL_VIEW stage=%d observed=%s"),Stage,*Result);
        ServerNeutralReport(Stage,Result);
    }
}

void AIslandProbe::TickSceneryCheck(double Now)
{
    if (Now-StepAt>35.)
    {
        UE_LOG(LogTemp,Error,TEXT("SCENERY_VOXEL_STALLED case=%d cuts=%d attempts=%d stage=%d expected=%s reports=%d"),
            SceneryCase,SceneryCuts,SceneryAttempts,NeutralStep,*NeutralExpected,Reports.Num());
        Require(false,TEXT("the last voxel edit or its observed collision failed to converge"));
        return;
    }
    if (Now-LastAction<.25) return;
    LastAction=Now;
    auto& World=*GetWorld();
    auto& Player=*Mode->GetPlayers()[0];
    auto& Hero=*Player.GetHero();

    if (!bSceneryInventory)
    {
        int32 Actors[NumCases]={},Parts[NumCases]={};
        TArray<FString> Detached;
        for (TActorIterator<AActor> It(&World); It; ++It)
        {
            const int32 Case=CaseFor(**It);
            if (Case<0 || Case>=NumCases) continue;
            ++Actors[Case];
            const auto Fields=PartsFor(**It);
            Parts[Case]+=Fields.Num();
            for (const auto* Field:Fields)
                if (Count(*Field)<=0) { Require(false,TEXT("initial voxel field must contain material")); return; }
            // Two strikes on the largest section would miss an unregistered wheel, yard or canopy.
            const auto* Neutral = Cast<AIslandNeutralProp>(*It);
            if (Case <= 6 || Neutral)
            {
                TInlineComponentArray<UPrimitiveComponent*> Sources(*It);
                for (auto* Source : Sources)
                {
                    if (Source->IsA<UIslandMeshVoxels>()) continue;
                    if (Neutral && Source->ComponentHasTag(TEXT("IslandMechanismEffect"))) continue;
                    const auto* Static = Cast<UStaticMeshComponent>(Source);
                    const auto* Procedural = Cast<UProceduralMeshComponent>(Source);
                    if (!(Static && Static->GetStaticMesh()) && !(Procedural && Procedural->GetNumSections() > 0)) continue;
                    const auto* Field = Cast<UIslandMeshVoxels>(IslandSceneryVoxels::SurfaceFor(Source));
                    if (!Field || Field->SourcePart() != Source)
                    {
                        Require(false, *FString::Printf(TEXT("%s is missing its own voxel surface"), *Source->GetPathName()));
                        return;
                    }
                }
            }
            if (Case <= 6 || Neutral)
            {
                const auto* Terrain = AVoxelTerrain::Find(&World);
                if (!Terrain) { Require(false,TEXT("mechanism support check requires live island terrain")); return; }
                TMap<UIslandMeshVoxels*,TArray<int32>> Unsupported;
                IslandScenerySupport::InspectActor(**It, *Terrain, Unsupported);
                for (const auto& Loss : Unsupported)
                {
                    UE_LOG(LogTemp,Display,TEXT("SCENERY_SUPPORT_LOOSE class=%s owner=%s part=%s cells=%d total=%d"),
                        CaseNames[Case], *It->GetName(), *Loss.Key->SourcePart()->GetName(), Loss.Value.Num(), Loss.Key->RemainingCount());
                    if (Loss.Value.Num() == Loss.Key->RemainingCount())
                        Detached.Add(FString::Printf(TEXT("%s/%s"), CaseNames[Case], *Loss.Key->SourcePart()->GetName()));
                }
            }
        }
        if (!Detached.IsEmpty())
        {
            Require(false, *FString::Printf(TEXT("mechanism parts would disappear on the first nearby cut: %s"),
                *FString::Join(Detached, TEXT(", "))));
            return;
        }
        for (int32 I=0; I<NumCases; ++I)
        {
            UE_LOG(LogTemp,Display,TEXT("SCENERY_VOXEL_INVENTORY class=%s actors=%d fields=%d"),CaseNames[I],Actors[I],Parts[I]);
            if (!Actors[I] || Parts[I]<Actors[I]) { Require(false,TEXT("every scenery class has an occupied voxel representation")); return; }
        }
        bSceneryInventory=true;
    }

    auto* Field=SceneryField.Get();
    if (!Field)
    {
        TArray<UProceduralMeshComponent*> Choices;
        for (TActorIterator<AActor> It(&World); It; ++It)
            if (CaseFor(**It)==SceneryCase) Choices.Append(PartsFor(**It));
        Choices.Sort([](const UProceduralMeshComponent& A,const UProceduralMeshComponent& B){ return Count(A)>Count(B); });
        if (SceneryCase==ChestCase)
            Choices.StableSort([](const UProceduralMeshComponent& A,const UProceduralMeshComponent& B)
            {
                // Strike exposed metal as well as the timber, stone, cloth, sand and foliage sampled by the other cases.
                return (SourceName(A)==TEXT("PortHoop")) > (SourceName(B)==TEXT("PortHoop"));
            });
        FVector Impact;
        for (auto* Part:Choices)
        {
            if (Count(*Part)>0 && PickFace(World,Hero,*Part,0,SceneryEye,SceneryAim,Impact,nullptr,SceneryCase==FirstRoute))
            { Field=Part; break; }
        }
        if (!Field) { Require(false,TEXT("unable to aim a shovel at an occupied frame of this category")); return; }
        SceneryField=Field;
        SceneryInitial=Count(*Field);
        SceneryCuts=SceneryAttempts=0;
        bSceneryPartial=bSceneryDifferent=bSceneryRepeated=false;
        SceneryFirstPoint=Impact;
        SetNeutralStep(1000+SceneryCase*100,Now);
        NeutralExpected=Snapshot(*Field,SceneryEye,SceneryAim);
    }
    if (!NeutralExpected.IsEmpty())
    {
        for (int32 Seat=0; Seat<Peers.Num(); ++Seat)
            if (!Reports.Contains(Seat))
                Peers[Seat]->ClientSceneryInspect(NeutralStep,Field->GetOwner(),SourceName(*Field),SceneryEye,SceneryAim);
        if (Reports.Num()!=Peers.Num()) return;
        UE_LOG(LogTemp,Display,TEXT("SCENERY_VOXEL_SHARED class=%s stage=%d observed=%s peers=%d"),
            CaseNames[SceneryCase],NeutralStep,*NeutralExpected,Reports.Num());
        NeutralExpected.Reset();
    }
    const bool bExhaust=(SceneryCase>=FirstRoute && SceneryCase<ChestCase) || SceneryCase==0;
    if (Count(*Field)==0 || (!bExhaust && SceneryCuts>=2))
    {
        UE_LOG(LogTemp,Display,TEXT("SCENERY_VOXEL_CASE class=%s initial=%d final=%d cuts=%d partial=%d different=%d repeated=%d"),
            CaseNames[SceneryCase],SceneryInitial,Count(*Field),SceneryCuts,bSceneryPartial,bSceneryDifferent,bSceneryRepeated);
        SceneryField.Reset();
        ++SceneryCase;
        if (SceneryCase==NumCases)
        {
            UE_LOG(LogTemp,Display,TEXT("SCENERY_QA_DONE classes=%d strikes=%d peers=%d"),NumCases,SceneryHits,Peers.Num());
            Advance(EStep::Done);
        }
        return;
    }

    FVector Impact;
    const bool Again = SceneryCuts==1 && Reaches(World,Hero,*Field,SceneryEye,SceneryAim,Impact);
    if (!Again)
    {
        const FVector* Away = SceneryCuts>0 && !bSceneryDifferent ? &SceneryFirstPoint : nullptr;
        if (!PickFace(World,Hero,*Field,(SceneryAttempts*269)%7919,SceneryEye,SceneryAim,Impact,Away,SceneryCase==FirstRoute && !SceneryCuts) &&
            !PickFace(World,Hero,*Field,(SceneryAttempts*389)%7907,SceneryEye,SceneryAim,Impact,nullptr,SceneryCase==FirstRoute && !SceneryCuts))
        {
            UE_LOG(LogTemp,Error,TEXT("SCENERY_VOXEL_UNREACHABLE class=%s owner=%s part=%s remaining=%d"),
                CaseNames[SceneryCase],*Field->GetOwner()->GetName(),*SourceName(*Field).ToString(),Count(*Field));
            Require(false,TEXT("remaining scenery does not expose a diggable face")); return;
        }
    }
    const int32 Before=Count(*Field);
    const int32 BeforeDepth=TraceDepth(*Field,SceneryEye,SceneryAim);
    Hero.SetActorLocation(SceneryEye-(Hero.GetAimOrigin()-Hero.GetActorLocation()),false,nullptr,ETeleportType::TeleportPhysics);
    Player.SubmitEdit(EVoxelAction::Punch,SceneryAim);
    ++SceneryAttempts;
    const int32 After=Count(*Field);
    if (After>=Before) return; // A rejected or cooldown-limited input never counts as a cut.
    if (!SceneryCuts) SceneryFirstPoint=Impact;
    bSceneryPartial |= After>0;
    bSceneryRepeated |= Again;
    bSceneryDifferent |= FVector::DistSquared(Impact,SceneryFirstPoint)>FMath::Square(50.);
    ++SceneryCuts;
    ++SceneryHits;
    SetNeutralStep(1000+SceneryCase*100+SceneryCuts,Now);
    NeutralExpected=Snapshot(*Field,SceneryEye,SceneryAim);
    UE_LOG(LogTemp,Display,TEXT("SCENERY_VOXEL_CUT class=%s from=%d to=%d depth_before=%d depth_after=%d xyz=%s"),
        CaseNames[SceneryCase],Before,After,BeforeDepth,TraceDepth(*Field,SceneryEye,SceneryAim),*Impact.ToCompactString());
}

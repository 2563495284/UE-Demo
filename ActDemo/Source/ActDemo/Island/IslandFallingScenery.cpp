#include "Island/IslandFallingScenery.h"

#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandSceneryVoxels.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "RHIGlobals.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/ExpeditionImpact.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr double Gravity = 980.;
    constexpr double FadeTime = .26;
    constexpr double DustTime = 1.20;
    constexpr int32 PuffCount = 9;
    constexpr double MaxDrop = 3500.;

    double ServerNow(const UWorld& World)
    {
        const auto* State = World.GetGameState();
        return State ? State->GetServerWorldTimeSeconds() : World.GetTimeSeconds();
    }

    double FlightTime(double Drop)
    {
        // Let a very short drop read at character scale. High ledges retain their real flight time.
        return FMath::Max(.46, FMath::Sqrt(2.*Drop/Gravity));
    }

    void PutUInt(TArray<uint8>& Data, uint32 Value)
    {
        while (Value >= 128) { Data.Add(uint8(Value)|128); Value >>= 7; }
        Data.Add(uint8(Value));
    }

    bool GetUInt(TConstArrayView<uint8> Data, int32& At, uint32& Value)
    {
        Value = 0;
        for (int32 Shift = 0; Shift <= 28 && At < Data.Num(); Shift+=7)
        {
            const uint8 Byte = Data[At++];
            if (Shift==28 && Byte>15) return false;
            Value |= uint32(Byte & 127) << Shift;
            if (!(Byte & 128)) return true;
        }
        return false;
    }

    TArray<FVector> SampleFeet(const TArray<FVector>& Feet)
    {
        FBox Box(ForceInit);
        for (const FVector P : Feet) Box+=P;
        const FVector Span=Box.GetSize().ComponentMax(FVector(1.));
        TOptional<FVector> Sole[25];
        for (const FVector P : Feet)
        {
            const FVector UV=(P-Box.Min)/Span;
            auto& Foot=Sole[FMath::Clamp(int32(UV.X*5.),0,4)+5*FMath::Clamp(int32(UV.Y*5.),0,4)];
            if (!Foot || P.Z<Foot->Z) Foot=P;
        }
        TArray<FVector> Result;
        for (const auto& Foot : Sole) if (Foot) Result.Add(*Foot);
        return Result;
    }

    FCollisionQueryParams FallQuery(UWorld& World, AActor* Subject)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FallingIslandScenery), true);
        for (auto It=World.GetPlayerControllerIterator(); It; ++It)
            if (const auto* PC=It->Get()) Query.AddIgnoredActor(PC->GetPawn());
        TArray<AActor*> Children;
        if (Subject) Subject->GetAttachedActors(Children,true,true);
        Query.AddIgnoredActors(Children);
        return Query;
    }

    FIslandFallContact FindContact(UWorld& World, AActor* Subject, const TArray<FVector>& Sole, double Fallen=0.)
    {
        const auto* Terrain=AVoxelTerrain::Find(&World);
        const auto* Ground=Terrain ? Terrain->GetDensity() : nullptr;
        const auto Query=FallQuery(World,Subject);
        FIslandFallContact Contact;
        Contact.Drop=MaxDrop;
        for (const FVector Foot : Sole)
        {
            FHitResult Hit;
            if (World.LineTraceSingleByChannel(Hit, Foot+FVector::UpVector*(2.-Fallen), Foot-FVector::UpVector*Contact.Drop,
                ECC_Visibility, Query))
            {
                const double Drop=FMath::Max(0.,Foot.Z-Hit.ImpactPoint.Z);
                if (Drop <= Contact.Drop)
                {
                    Contact.Drop=Drop;
                    Contact.Point=Hit.ImpactPoint;
                    Contact.Normal=Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
                    Contact.bWater=false;
                    Contact.bHasContact=true;
                }
            }
            // IslandWater draws the sea at Z=0 outside the authored coastline.
            if (Ground && Ground->ShoreDistance(FMath::RoundToInt(Foot.X),FMath::RoundToInt(Foot.Y)) >= 0 &&
                Foot.Z>=Fallen-3. && Foot.Z<Contact.Drop)
            {
                Contact.Drop=FMath::Max(0.,Foot.Z);
                Contact.Point={Foot.X,Foot.Y,0.};
                Contact.Normal=FVector::UpVector;
                Contact.bWater=true;
                Contact.bHasContact=true;
            }
        }
        return Contact;
    }

    // One shared soft-edged texture, sampled by a single unlit translucent card per impact.
    UTexture2D* DustMask()
    {
        static TWeakObjectPtr<UTexture2D> Cached;
        if (Cached.IsValid()) return Cached.Get();
        constexpr int32 Width=64;
        TArray<FColor> Pixels;
        Pixels.SetNumUninitialized(Width*Width);
        const FVector Puffs[]={{-.48,.06,.27},{-.27,-.13,.29},{-.06,-.07,.33},{.05,.17,.26},
            {.28,-.12,.30},{.48,.04,.23},{.63,-.10,.14},{-.67,-.10,.15}};
        for (int32 Y=0; Y<Width; ++Y)
            for (int32 X=0; X<Width; ++X)
            {
                const FVector2D UV((2.*X+1.)/Width-1.,1.-(2.*Y+1.)/Width);
                const float Ripple=FMath::PerlinNoise2D(UV*6.7f)*.16f+FMath::PerlinNoise2D(UV*14.f)*.05f;
                double Opacity=0.;
                for (const FVector Puff : Puffs)
                {
                    const FVector2D D=(UV-FVector2D(Puff))/Puff.Z;
                    Opacity=FMath::Max(Opacity,FMath::SmoothStep(0.,1.,1.-D.Size()+Ripple));
                }
                Pixels[Y*Width+X]=FColor(255,255,255,FMath::RoundToInt(255.*Opacity));
            }
        auto* Texture=UTexture2D::CreateTransient(Width,Width,PF_B8G8R8A8,NAME_None,
            MakeArrayView(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num()*int32(sizeof(FColor))));
        if (Texture)
        {
            Texture->AddressX=Texture->AddressY=TA_Clamp;
            Texture->UpdateResource();
            Cached=Texture;
        }
        return Texture;
    }
}

AIslandFallingScenery::AIslandFallingScenery()
{
    // Remote copies are started by the existing player's ordered reliable channel.
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FallOrigin")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Drop(TEXT("/Game/TreasureIsland/Art/Neutral/SM_Spray"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Reef(TEXT("/Game/TreasureIsland/Art/Neutral/SM_ReefStone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Crest(TEXT("/Game/TreasureIsland/Art/Neutral/SM_FoamCrest"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/TreasureIsland/Art/Neutral/M_WreckFinish"));
    DropShape=Drop.Object;
    ReefShape=Reef.Object;
    FoamShape=Crest.Object;
    WaterPaint=Paint.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DustSheet(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    PuffShape=Sphere.Object;
    DustBase=DustSheet.Object;
    DustPlane=Plane.Object;
}

void AIslandPlayerController::ClientSceneryFallStart_Implementation(const FIslandSceneryFall& Fall, const FIslandFallContact& InitialContact)
{
    if (GetNetMode()!=NM_Client || !Fall.Id || Fall.Parts.IsEmpty()) return;
    const FTransform Frame(FVector(Fall.Origin));
    auto* FX=GetWorld()->SpawnActorDeferred<AIslandFallingScenery>(AIslandFallingScenery::StaticClass(),Frame,nullptr,nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!FX) return;
    // Only the peers connected at detachment receive this event. There is no transient-event replay on join.
    FX->Fall=Fall;
    FX->Contact=InitialContact;
    FX->FinishSpawning(Frame);
    FX->SetLifeSpan(FlightTime(MaxDrop)+DustTime+6.);
    if (FParse::Param(FCommandLine::Get(),TEXT("IslandQA")))
        UE_LOG(LogTemp,Display,TEXT("SCENERY_FALL_RECEIVED id=%u subject=%s parts=%d drop_cm=%.1f"),Fall.Id,
            *Fall.SubjectName.ToString(),Fall.Parts.Num(),InitialContact.Drop);
    FX->Tick(0.f);
}

void AIslandPlayerController::ClientSceneryFallContact_Implementation(uint32 Id, const FIslandFallContact& Contact)
{
    if (GetNetMode()!=NM_Client) return;
    for (TActorIterator<AIslandFallingScenery> It(GetWorld()); It; ++It)
        if (It->Fall.Id==Id) { It->Contact=Contact; return; }
}

void AIslandFallingScenery::PackCells(TArray<int32> Cells, TArray<uint8>& Out)
{
    Out.Reset();
    Cells.Sort();
    int32 End=0;
    for (int32 I=0; I<Cells.Num();)
    {
        const int32 First=Cells[I++];
        int32 Next=First+1;
        while (I<Cells.Num() && Cells[I]==Next) { ++Next; ++I; }
        PutUInt(Out, First-End);
        PutUInt(Out, Next-First);
        End=Next;
    }
}

bool AIslandFallingScenery::UnpackCells(TConstArrayView<uint8> Data, int32 Limit, TArray<int32>& Out)
{
    Out.Reset();
    int32 At=0, End=0;
    while (At<Data.Num())
    {
        uint32 Gap, Count;
        if (!GetUInt(Data,At,Gap) || !GetUInt(Data,At,Count) || !Count || int64(End)+Gap+Count>Limit) return false;
        const int32 First=End+Gap;
        End=First+Count;
        for (int32 I=First; I<End; ++I) Out.Add(I);
    }
    return !Out.IsEmpty();
}

void AIslandFallingScenery::Release(AActor& Subject, const TArray<IslandScenerySupport::FCellGroup>& Groups)
{
    for (const auto& Group : Groups)
    {
        TArray<FIslandFallingPart> Parts;
        TArray<FVector> Feet;
        auto Surface=EIslandChipSurface::Rock;
        int32 Largest=0;
        for (const auto& Pair : Group)
        {
            const auto* Field=Pair.Key;
            const auto* Original=Field->SourcePart();
            if (!Original) continue;
            auto& Part=Parts.AddDefaulted_GetRef();
            Part.Source=Original->GetFName();
            Part.Frame=Field->GetComponentTransform();
            PackCells(Pair.Value,Part.Shape);
            for (const int32 Cell : Pair.Value) Feet.Add(Field->CellFoot(Cell));
            if (Pair.Value.Num()<=Largest) continue;
            Largest=Pair.Value.Num();
            Surface=Field->CellSurface(Pair.Value[0]);
        }
        Spawn(Subject,MoveTemp(Parts),Feet,Surface);
    }
}

void AIslandFallingScenery::Release(UIslandRouteVoxelComponent& Field, const TArray<FIslandLooseSamples>& Groups, EIslandChipSurface Surface)
{
    for (const auto& Group : Groups)
    {
        if (Group.Shape.IsEmpty() || Group.Feet.IsEmpty()) continue;
        TArray<FIslandFallingPart> Parts;
        auto& Part=Parts.AddDefaulted_GetRef();
        Part.Source=Field.GetFName();
        Part.Frame=Field.GetComponentTransform();
        Part.Shape=Group.Shape;
        Spawn(*Field.GetOwner(),MoveTemp(Parts),Group.Feet,Surface);
    }
}

void AIslandFallingScenery::Spawn(AActor& Subject, TArray<FIslandFallingPart>&& Parts, const TArray<FVector>& Feet, EIslandChipSurface Surface)
{
    if (!Subject.HasAuthority() || Feet.IsEmpty() || Parts.IsEmpty()) return;
    UWorld& World=*Subject.GetWorld();
    FBox Bounds(ForceInit);
    for (const FVector P : Feet) Bounds+=P;
    const FTransform Frame(Bounds.GetCenter());
    auto* FX=World.SpawnActorDeferred<AIslandFallingScenery>(StaticClass(),Frame,nullptr,nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!FX) return;
    static uint32 NextId=0;
    if (!++NextId) ++NextId;
    FX->Fall.Id=NextId;
    FX->Fall.Subject=&Subject;
    FX->Fall.SubjectName=Subject.GetFName();
    FX->Fall.Origin=Bounds.GetCenter();
    FX->Fall.Parts=MoveTemp(Parts);
    FX->Fall.Start=ServerNow(World);
    FX->Fall.Surface=Surface;
    FX->ContactFeet=SampleFeet(Feet);
    FX->Contact=FindContact(World,&Subject,FX->ContactFeet);
    FX->FinishSpawning(Frame);
    FX->SetLifeSpan(FlightTime(FX->Contact.Drop)+DustTime+.65);
    AIslandPlayerController::ForSceneryViewers(World,[&](AIslandPlayerController& Player)
    {
        Player.ClientSceneryFallStart(FX->Fall,FX->Contact);
    });
    // Show the replacement in the same game-thread update as the disappearing static surface.
    FX->Tick(0.f);
}

bool AIslandFallingScenery::CreatePieces()
{
    if (!Fall.Subject && !Fall.SubjectName.IsNone())
        Fall.Subject=FindObjectFast<AActor>(GetWorld()->PersistentLevel,Fall.SubjectName);
    if (!Fall.Subject || Fall.Parts.IsEmpty()) return false;
    TInlineComponentArray<UProceduralMeshComponent*> Fields(Fall.Subject.Get());
    TArray<UProceduralMeshComponent*> Sources;
    for (const auto& Part : Fall.Parts)
    {
        UProceduralMeshComponent* Found=nullptr;
        for (auto* Candidate : Fields)
        {
            if (auto* Mesh=Cast<UIslandMeshVoxels>(Candidate); Mesh && Mesh->SourcePart() && Mesh->SourcePart()->GetFName()==Part.Source)
                Found=Mesh;
            if (Candidate->IsA<UIslandRouteVoxelComponent>() && Candidate->GetFName()==Part.Source) Found=Candidate;
            if (Found) break;
        }
        if (!Found) return false; // source and transient actor can arrive on different actor channels
        Sources.Add(Found);
    }
    for (int32 I=0; I<Sources.Num(); ++I)
    {
        auto* Source=Sources[I];
        const auto& Part=Fall.Parts[I];
        TArray<FProcMeshSection> Sections;
        if (auto* Mesh=Cast<UIslandMeshVoxels>(Source))
        {
            if (!Mesh->FallingSections(Part.Shape,Sections)) continue;
        }
        else if (!CastChecked<UIslandRouteVoxelComponent>(Source)->FallingSection(Part.Shape,Sections.AddDefaulted_GetRef())) continue;
        auto* Piece=NewObject<UProceduralMeshComponent>(this);
        AddInstanceComponent(Piece);
        Piece->SetMobility(EComponentMobility::Movable);
        Piece->SetupAttachment(GetRootComponent());
        Piece->SetRelativeTransform(Part.Frame.GetRelativeTransform(GetActorTransform()));
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Piece->SetCanEverAffectNavigation(false);
        Piece->SetGenerateOverlapEvents(false);
        for (int32 Slot=0; Slot<Sections.Num(); ++Slot)
        {
            Sections[Slot].bEnableCollision=false;
            if (Sections[Slot].ProcIndexBuffer.IsEmpty()) continue;
            Piece->SetMaterial(Slot,Source->GetMaterial(Slot));
            Piece->SetProcMeshSection(Slot,Sections[Slot]);
        }
        Piece->RegisterComponent();
        Pieces.Add(Piece);
        PieceIndices.Add(I);
    }
    return !Pieces.IsEmpty();
}

void AIslandFallingScenery::Impact()
{
    FLinearColor Tint(.32,.24,.16);
    double Largest=-1.;
    for (const auto& Piece : Pieces)
    {
        if (!Piece || Piece->Bounds.SphereRadius<=Largest) continue;
        for (int32 Slot=0; Slot<Piece->GetNumSections(); ++Slot)
        {
            const auto* Section=Piece->GetProcMeshSection(Slot);
            if (!Section || Section->ProcVertexBuffer.IsEmpty()) continue;
            Tint=Section->ProcVertexBuffer[0].Color.ReinterpretAsLinear();
            const auto* Source=Piece->GetMaterial(Slot);
            FLinearColor MaterialTint;
            if (Source && Source->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")),MaterialTint)) Tint*=MaterialTint;
            Tint.A=1.f;
            Largest=Piece->Bounds.SphereRadius;
            break;
        }
    }
    auto* Finish=WaterPaint ? UMaterialInstanceDynamic::Create(WaterPaint,this) : nullptr;
    if (Finish)
    {
        Finish->SetVectorParameterValue(TEXT("Color"), Contact.bWater ? FLinearColor(.63,.92,.94) : Tint);
        Finish->SetScalarParameterValue(TEXT("Roughness"),Contact.bWater ? .23f : .84f);
    }
    if (Contact.bWater)
    {
        if (FoamShape)
        {
            Foam=NewObject<UStaticMeshComponent>(this);
            AddInstanceComponent(Foam);
            Foam->SetStaticMesh(FoamShape);
            Foam->SetMaterial(0,Finish);
            Foam->SetMobility(EComponentMobility::Movable);
            Foam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Foam->SetCanEverAffectNavigation(false);
            Foam->SetCastShadow(false);
            Foam->SetWorldLocation(FVector(Contact.Point)+FVector(0.,0.,2.));
            Foam->RegisterComponent();
        }
    }
    const bool bDusty=!Contact.bWater && (Fall.Surface==EIslandChipSurface::Rock || Fall.Surface==EIslandChipSurface::Sand);
    if (bDusty && PuffShape && WaterPaint)
    {
        auto* Pigment=UMaterialInstanceDynamic::Create(WaterPaint,this);
        DustTint=FMath::Lerp(Tint,FLinearColor(.68,.52,.34),.66f);
        Pigment->SetVectorParameterValue(TEXT("Color"),DustTint);
        Pigment->SetScalarParameterValue(TEXT("Roughness"),1.f);
        DustPuffs=NewObject<UInstancedStaticMeshComponent>(this);
        DustPuffs->SetStaticMesh(PuffShape);
        DustPuffs->SetMaterial(0,Pigment);
        DustPuffs->SetMobility(EComponentMobility::Movable);
        DustPuffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DustPuffs->SetGenerateOverlapEvents(false);
        DustPuffs->SetCanEverAffectNavigation(false);
        DustPuffs->SetCastShadow(false);
        DustPuffs->SetReceivesDecals(false);
        DustPuffs->SetWorldLocation(Contact.Point);
        PuffPoses.SetNum(PuffCount);
        DrawDust(0.);
        AddInstanceComponent(DustPuffs);
        DustPuffs->RegisterComponent();
    }
    if (bDusty && DustBase && DustPlane)
    {
        if (UTexture2D* Mask=DustMask())
        {
            DustFinish=UMaterialInstanceDynamic::Create(DustBase,this);
            DustFinish->SetTextureParameterValue(TEXT("SlateUI"),Mask);
            DustFinish->SetScalarParameterValue(TEXT("OpacityFromTexture"),1.f);
            DustTint=FMath::Lerp(Tint,FLinearColor(.68,.52,.34),.66f);
            Dust=NewObject<UStaticMeshComponent>(this);
            AddInstanceComponent(Dust);
            Dust->SetStaticMesh(DustPlane);
            Dust->SetMaterial(0,DustFinish);
            Dust->SetMobility(EComponentMobility::Movable);
            Dust->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Dust->SetCanEverAffectNavigation(false);
            Dust->SetCastShadow(false);
            Dust->SetReceivesDecals(false);
            DrawDust(0.);
            Dust->RegisterComponent();
        }
    }
    const bool bSoft=Fall.Surface==EIslandChipSurface::Sand || Fall.Surface==EIslandChipSurface::Leaf || Fall.Surface==EIslandChipSurface::Canvas;
    const bool bDrops=Contact.bWater || Fall.Surface==EIslandChipSurface::Timber || Fall.Surface==EIslandChipSurface::Leaf;
    AIslandSceneryDamage::PlayDebris(*GetWorld(),Contact.Point,Contact.Normal,Finish,bDrops ? DropShape.Get() : ReefShape.Get(),
        bDrops ? FVector(.085,.075,.21) : FVector(.16,.14,.15),nullptr,bSoft && !Contact.bWater);
    AExpeditionImpactCamera::PlayAt(*GetWorld(),Contact.Point,bSoft ? .58f : .95f,Contact.bWater ? 1.18f : .68f,bSoft ? .25f : .65f);
}

void AIslandFallingScenery::DrawDust(double After)
{
    const double T=FMath::Clamp(After/DustTime,0.,1.);
    const double Scale=FMath::Clamp(1.1+FMath::Sqrt(Contact.Drop)/34.,1.15,1.95);
    if (DustPuffs)
    {
        // The low cloud rolls out along the ground. The three inner lobes rise through its center.
        for (int32 I=0; I<PuffCount; ++I)
        {
            const bool bInner=I>=6;
            const double Angle=I*2.399963+Fall.Id*.72;
            const double Bias=FMath::Sin(Angle*2.);
            const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0.);
            const double Spread=(bInner ? 8.+17.*T : 16.+59.*T)*(1.+.12*Bias);
            const double Rise=(bInner ? 15.+67.*T : 9.+31.*T)*(1.+.15*Bias);
            const double Size=(.45+.28*FMath::SmoothStep(0.,.30,T))*(1.+.18*Bias)*
                (1.-FMath::SmoothStep(.34+.08*Bias,1.,T));
            PuffPoses[I]=FTransform(FRotator(0.,FMath::RadiansToDegrees(Angle),0.),
                (Direction*Spread+FVector::UpVector*Rise)*Scale, FVector(1.,.79,bInner ? .86 : .61)*(Size*Scale));
        }
        if (!DustPuffs->GetInstanceCount()) DustPuffs->AddInstances(PuffPoses,false,false,false);
        else DustPuffs->BatchUpdateInstancesTransforms(0,PuffPoses,false,true,true);
    }
    if (!Dust || !DustFinish) return;
    const auto* Player=GetWorld()->GetFirstPlayerController();
    if (!Player || !Player->PlayerCameraManager) return;
    const FVector Eye=Player->PlayerCameraManager->GetCameraLocation();
    const FVector TowardsViewer=(Eye-FVector(Contact.Point)).GetSafeNormal2D();
    const FVector Center=FVector(Contact.Point)+TowardsViewer*(14.+12.*T)+FVector::UpVector*(29.+67.*T);
    const FVector TowardEye=(Player->PlayerCameraManager->GetCameraLocation()-Center).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
    const FVector Side=FVector::CrossProduct(FVector::UpVector,TowardEye).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
    Dust->SetWorldLocationAndRotation(Center,FRotationMatrix::MakeFromZX(TowardEye,Side).Rotator());
    Dust->SetWorldScale3D(FVector((1.25+1.65*T)*Scale,(.72+.68*T)*Scale,1.));
    DustTint.A=.82*(1.-T)*(1.-.3*T);
    DustFinish->SetVectorParameterValue(TEXT("TintColorAndOpacity"),DustTint);
}

void AIslandFallingScenery::CheckLanding(double Age)
{
    if (Contact.bConfirmed) return;
    const auto PublishContact=[&]()
    {
        AIslandPlayerController::ForSceneryViewers(*GetWorld(),[&](AIslandPlayerController& Player)
        {
            Player.ClientSceneryFallContact(Fall.Id,Contact);
        });
    };
    const double Flight=FlightTime(Contact.Drop);
    if (Age<Flight && Age<NextContactCheck) return;
    NextContactCheck=Age+.10;
    if (Contact.bHasContact && !Contact.bWater)
    {
        FHitResult Hit;
        const FVector Landing=Contact.Point;
        const auto Query=FallQuery(*GetWorld(),Fall.Subject.Get());
        // Do not smash against a ledge a second player has just cut out.
        if (!GetWorld()->LineTraceSingleByChannel(Hit,Landing+FVector::UpVector*4.,
            Landing-FVector::UpVector*4.,ECC_Visibility,Query))
        {
            const double T=FMath::Clamp(Age/Flight,0.,1.);
            Contact=FindContact(*GetWorld(), Fall.Subject.Get(), ContactFeet, Contact.Drop*T*T);
            PublishContact();
            SetLifeSpan(FMath::Max(.1,FlightTime(Contact.Drop)+DustTime+.65-Age));
        }
    }
    if (Age>=FlightTime(Contact.Drop))
    {
        Contact.bConfirmed=true;
        PublishContact();
    }
}

void AIslandFallingScenery::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Fall.Parts.IsEmpty()) return;
    const double WorldTime=GetWorld()->GetTimeSeconds();
    const double ServerAge=FMath::Max(0.,ServerNow(*GetWorld())-Fall.Start);
    const bool bRemote=GetNetMode()==NM_Client;
    if (!bRemote) CheckLanding(ServerAge);
    if (GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender())
    {
        if (Contact.bConfirmed) SetActorTickEnabled(false);
        return;
    }
    const double Flight=FlightTime(Contact.Drop);
    if (!bShown)
    {
        bShown=CreatePieces();
        if (!bShown)
        {
            if (!bReportedSourceWait && FParse::Param(FCommandLine::Get(),TEXT("IslandQA")))
            {
                bReportedSourceWait=true;
                UE_LOG(LogTemp,Display,TEXT("SCENERY_FALL_WAITING id=%u subject=%s resolved=%d"),Fall.Id,
                    *Fall.SubjectName.ToString(),int32(Fall.Subject!=nullptr));
            }
            return;
        }
        VisibleAt=WorldTime;
    }
    const double Age=bRemote ? FMath::Max(0.,WorldTime-VisibleAt) : ServerAge;
    const double T=FMath::Clamp(Age/Flight,0.,1.);
    const bool bJustHit=Age>=Flight && Contact.bConfirmed && !bHit;
    if (bJustHit)
    {
        bHit=true;
        ImpactAt=WorldTime;
    }
    const double After=bHit ? WorldTime-ImpactAt : 0.;
    for (int32 I=0; I<Pieces.Num(); ++I)
    {
        auto Frame=Fall.Parts[PieceIndices[I]].Frame;
        Frame.AddToTranslation(FVector::DownVector*(Contact.Drop*T*T));
        // Hold the full-sized ruin at its real contact through the first impact frames.
        // Its debris and dust continue after this mesh clears.
        if (After>=.13) Pieces[I]->SetVisibility(false);
        Pieces[I]->SetWorldTransform(Frame);
    }
    static const bool TraceFalls=FParse::Param(FCommandLine::Get(),TEXT("IslandQA"));
    if (TraceFalls && !bTracedMotion && T>0.12 && T<1. && !Pieces.IsEmpty())
    {
        const int32 PartIndex=PieceIndices[0];
        const double Displacement=Fall.Parts[PartIndex].Frame.GetLocation().Z-Pieces[0]->GetComponentLocation().Z;
        if (Displacement>0.)
        {
            bTracedMotion=true;
            UE_LOG(LogTemp,Display,TEXT("SCENERY_FALL_MOVING actor=%s subject=%s parts=%d displacement_cm=%.2f drop_cm=%.2f"),
                *GetName(),*GetNameSafe(Fall.Subject.Get()),Pieces.Num(),Displacement,Contact.Drop);
        }
    }
    if (bJustHit)
    {
        if (Contact.bHasContact)
        {
            Impact();
            if (TraceFalls)
                UE_LOG(LogTemp,Display,TEXT("SCENERY_FALL_CONTACT actor=%s subject=%s water=%d point=%s drop_cm=%.2f dust=%d foam=%d"),
                    *GetName(),*GetNameSafe(Fall.Subject.Get()),int32(Contact.bWater),*Contact.Point.ToString(),Contact.Drop,
                    int32(DustPuffs!=nullptr),int32(Foam!=nullptr));
        }
    }
    if (DustPuffs || Dust)
    {
        DrawDust(After);
        if (TraceFalls && !GUsingNullRHI && !bTracedDust && DustPuffs && After>.025 && After<DustTime &&
            DustPuffs->GetLastRenderTimeOnScreen()>=ImpactAt)
        {
            bTracedDust=true;
            UE_LOG(LogTemp,Display,TEXT("SCENERY_DUST_VISIBLE actor=%s age=%.3f puffs=%d rise_cm=%.1f"),
                *GetName(),After,DustPuffs->GetInstanceCount(),PuffPoses.Last().GetLocation().Z);
        }
    }
    if (Foam)
    {
        const double Wave=FMath::Clamp(After/FadeTime,0.,1.);
        Foam->SetWorldScale3D(FVector(.25+.92*Wave,.25+.92*Wave,.10*(1.-Wave)));
    }
    if (After>=((DustPuffs || Dust) ? DustTime : FadeTime))
    {
        // Actor bHidden replicates. A remote viewer can still be finishing a delayed landing.
        for (const auto& Piece : Pieces) Piece->SetVisibility(false);
        if (DustPuffs) DustPuffs->SetVisibility(false);
        if (Dust) Dust->SetVisibility(false);
        if (Foam) Foam->SetVisibility(false);
        SetActorTickEnabled(false);
        if (bRemote) Destroy();
    }
}

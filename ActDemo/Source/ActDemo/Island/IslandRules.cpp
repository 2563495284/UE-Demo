#include "Island/IslandRules.h"

#include "Voxel/VoxelDensity.h"
#include "Island/ExpeditionDock.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"

namespace
{
    constexpr double DecoyRadius = 180.0;
    constexpr double DecoyClearance = 175.0;
    constexpr double ClueOffsetWithDecoys = 110.0;

    double Cross(const FVector2D& A, const FVector2D& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    bool FindCrossing(FVector2D A, FVector2D B, FVector2D C, FVector2D D, FVector2D& Crossing)
    {
        const FVector2D AC = C - A;
        const FVector2D BD = D - B;
        const double Denominator = Cross(AC, BD);
        if (FMath::Abs(Denominator) < 1.0)
        {
            return false;
        }
        const double T = Cross(B - A, BD) / Denominator;
        const double U = Cross(B - A, AC) / Denominator;
        Crossing = A + AC * T;
        return T > 0.0 && T < 1.0 && U > 0.0 && U < 1.0;
    }

    bool FindCrossing(const FIslandTreasureLayout& Layout, FVector2D& Crossing)
    {
        return FindCrossing(FVector2D(Layout.Centers[0]), FVector2D(Layout.Centers[1]),
            FVector2D(Layout.Centers[2]), FVector2D(Layout.Centers[3]), Crossing);
    }

    FString CircleError(int32 Index)
    {
        return FString::Printf(TEXT("Search circle %c needs more buried rock or clearance"), 'A' + Index);
    }

}

int32 FIslandClues::Count() const
{
    return FPlatformMath::CountBits(static_cast<uint32>(Mask));
}

TArray<FIslandSearchCircle> IslandRules::DefaultSearchCircles()
{
    return {{{0, -1150}, 300}, {{1150, 0}, 300}, {{0, 1150}, 300}, {{-1150, 0}, 300}};
}

FLinearColor IslandRules::TeamColor(int32 Team)
{
    static const FLinearColor Colors[] = {{.93f, .29f, .28f}, {.25f, .59f, 1.f}, {1.f, .79f, .24f},
        {.22f, .82f, .55f}, {.72f, .49f, 1.f}, {1.f, .58f, .22f}, {.24f, .83f, .85f}, {.97f, .55f, .74f}};
    return Team >= 0 && Team < UE_ARRAY_COUNT(Colors) ? Colors[Team] : FLinearColor::White;
}

const TCHAR* IslandRules::TeamName(int32 Team)
{
    static const TCHAR* Names[] = {TEXT("Red"), TEXT("Blue"), TEXT("Yellow"), TEXT("Green"),
        TEXT("Violet"), TEXT("Orange"), TEXT("Teal"), TEXT("Rose")};
    return Team >= 0 && Team < UE_ARRAY_COUNT(Names) ? Names[Team] : TEXT("Waiting");
}

bool IslandRules::InSea(const FVector& Head)
{
    return Head.Z < 0 && VoxelPlayground::ShoreDistance(FMath::RoundToInt(Head.X), FMath::RoundToInt(Head.Y)) >= 0;
}

TArray<FBox2D> IslandRules::CollectSearchObstacles(UWorld& World)
{
    TArray<FBox2D> Obstacles;
    for (TActorIterator<AActor> It(&World); It; ++It)
    {
        if (!It->IsA<AExpeditionLandmark>() && !It->IsA<AStaticMeshActor>() && !It->IsA<AExpeditionDock>()) continue;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
        for (const UStaticMeshComponent* Mesh : Meshes)
        {
            if (!Mesh->GetStaticMesh() || !Mesh->IsQueryCollisionEnabled()
                || Mesh->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;
            const FBox Bounds = Mesh->Bounds.GetBox();
            // Reserve an approach for a standing player as well as the buried chest itself.
            Obstacles.Add(FBox2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max)).ExpandBy(100.));
        }
    }
    return Obstacles;
}

bool IslandRules::IsValidLayout(const FIslandTreasureLayout& Layout, TConstArrayView<FIslandSearchCircle> Circles,
    const FVoxelDensity& Density, FString* Error, TConstArrayView<FBox2D> Obstacles)
{
    const auto Reject = [Error](const FString& Message)
    {
        if (Error)
        {
            *Error = Message;
        }
        return false;
    };
    FVector2D Crossing;
    if (!FindCrossing(Layout, Crossing) || !Crossing.Equals(Layout.Crossing, .01)
        || !Crossing.Equals(FVector2D(Layout.Centers[PrizeIndex]), .01))
    {
        return Reject(TEXT("A-C and B-D need an intersection inside both segments"));
    }
    if (Circles.Num() != ClueCount || Layout.Decoys.Num() > MaxDecoys)
    {
        return Reject(TEXT("Expected four search circles and at most 24 decoys"));
    }
    if (Density.Layout == EVoxelLayout::Expedition)
    {
        FVector2D CenterGuess;
        const bool bHasGuess = FindCrossing(FVector2D(Circles[0].Center), FVector2D(Circles[1].Center),
            FVector2D(Circles[2].Center), FVector2D(Circles[3].Center), CenterGuess);
        int32 SmallestRadius = Circles[0].Radius;
        for (const auto& Circle : Circles) SmallestRadius = FMath::Min(SmallestRadius, Circle.Radius);
        if (bHasGuess && FVector2D::Distance(CenterGuess, Crossing) < FMath::Min(1000., .60 * SmallestRadius))
            return Reject(TEXT("The public circle centers give away the prize intersection"));
    }
    TStaticArray<int32, ClueCount> PerCircle(InPlace, 0);
    const int32 BaseCount = Layout.Decoys.Num() / ClueCount;
    for (const FIslandDecoy& Decoy : Layout.Decoys)
    {
        if (Decoy.ClueIndex < 0 || Decoy.ClueIndex >= ClueCount)
        {
            return Reject(TEXT("Invalid decoy search circle"));
        }
        ++PerCircle[Decoy.ClueIndex];
    }
    for (int32 Index = 0; Index < ClueCount; ++Index)
    {
        if (PerCircle[Index] < BaseCount || PerCircle[Index] > BaseCount + 1)
        {
            return Reject(CircleError(Index));
        }
    }

    for (int32 Index = 0; Index < Layout.Num(); ++Index)
    {
        const bool bDecoy = Layout.IsDecoy(Index);
        const bool bPrize = Index == PrizeIndex;
        const FVector& Center = Layout.GetCenter(Index);
        const FVector2D Point(Center);
        const TOptional<double> Surface = Density.SurfaceAt(Point);
        const double Depth = Surface.Get(0.0) - Center.Z;
        const int32 Circle = bDecoy ? Layout.Decoys[Index - Layout.Centers.Num()].ClueIndex : Index;
        const FString Message = bPrize ? TEXT("The prize intersection needs deeper solid rock") : CircleError(Circle);
        const bool Expedition = Density.Layout == EVoxelLayout::Expedition;
        const double MinDepth = bPrize ? (Expedition ? 250. : 600.) : (Expedition ? 125. : 200.);
        const double MaxDepth = bPrize ? (Expedition ? 300. : 800.) : (Expedition ? 175. : 400.);
        if (!Surface || Depth < MinDepth - .01 || Depth > MaxDepth + .01
            || Center.Z - ChestExtent.Z < -700
            || (!bPrize && FVector2D::DistSquared(Point, FVector2D(Circles[Circle].Center)) > FMath::Square(Circles[Circle].Radius))
            || Density.ShoreDistance(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y)) > -550
            || Obstacles.ContainsByPredicate([Point](const FBox2D& Area) { return Area.IsInside(Point); })
            || !Density.IsVolumeSolid(FBox::BuildAABB(Center, ChestExtent)))
        {
            return Reject(Message);
        }
        const double Clearance = bDecoy ? DecoyClearance : 300.0;
        for (int32 Other = 0; Other < Index; ++Other)
        {
            const FVector& OtherCenter = Layout.GetCenter(Other);
            const double Distance = bDecoy ? FVector::DistSquaredXY(Center, OtherCenter) : FVector::DistSquared(Center, OtherCenter);
            if (Distance < FMath::Square(Clearance))
            {
                return Reject(Message);
            }
        }
    }
    return true;
}

bool IslandRules::GenerateLayout(int32 Seed, int32 DecoyCount, TConstArrayView<FIslandSearchCircle> Circles,
    const FVoxelDensity& Density, FIslandTreasureLayout& Layout, FString* Error, bool bFixedOnly, TConstArrayView<FBox2D> Obstacles)
{
    if (Circles.Num() != ClueCount)
    {
        if (Error)
        {
            *Error = TEXT("Expected exactly four search circles");
        }
        return false;
    }
    const bool bExpedition = Density.Layout == EVoxelLayout::Expedition;
    const int32 MaxRadius = bExpedition ? MaxExpeditionSearchRadius : MaxSearchRadius;
    for (int32 Index = 0; Index < ClueCount; ++Index)
    {
        const FIslandSearchCircle& Circle = Circles[Index];
        const FIntVector Max = Density.Grid.Origin + Density.Grid.Cells * VoxelPlayground::Spacing;
        if (Circle.Radius < MinSearchRadius || Circle.Radius > MaxRadius
            || Circle.Center.X < Density.Grid.Origin.X || Circle.Center.Y < Density.Grid.Origin.Y
            || Circle.Center.X > Max.X || Circle.Center.Y > Max.Y)
        {
            if (Error)
            {
                *Error = FString::Printf(TEXT("Search circle %c is outside the editable bounds"), 'A' + Index);
            }
            return false;
        }
    }

    DecoyCount = FMath::Clamp(DecoyCount, 0, MaxDecoys);
    FRandomStream Random(Seed);
    FRandomStream Fixed(7319);
    constexpr int32 RandomAttempts = 64;
    const int32 TotalAttempts = bExpedition ? 512 : RandomAttempts + 64;
    const auto Place = [&Density](FVector2D Point, int32 Depth)
    {
        // Remap the original draw so the random sequence and planar coordinates remain stable.
        if (Density.Layout == EVoxelLayout::Expedition)
            Depth = Depth >= 600 ? 250 + (Depth-600)/4 : 125 + (Depth-200)/4;
        return FVector(Point, Density.SurfaceAt(Point).Get(-10000.0) - Depth);
    };

    for (int32 Attempt = bFixedOnly && !bExpedition ? RandomAttempts : 0; Attempt < TotalAttempts; ++Attempt)
    {
        const bool bFixed = bExpedition ? bFixedOnly : Attempt >= RandomAttempts;
        const bool bCentered = !bExpedition && Attempt == RandomAttempts;
        FRandomStream& Stream = bFixed ? Fixed : Random;
        TStaticArray<int32, ClueCount> PerCircle(InPlace, DecoyCount / ClueCount);
        int32 Order[ClueCount] = {0, 1, 2, 3};
        for (int32 Index = 0; Index < DecoyCount % ClueCount; ++Index)
        {
            if (!bCentered)
            {
                Swap(Order[Index], Order[Stream.RandRange(Index, ClueCount - 1)]);
            }
            ++PerCircle[Order[Index]];
        }

        Layout.Decoys.Reset(DecoyCount);
        for (int32 Index = 0; Index < ClueCount; ++Index)
        {
            const FIslandSearchCircle& Circle = Circles[Index];
            FVector2D Point(Circle.Center);
            if (!bCentered)
            {
                const double Radius = bExpedition
                    ? FMath::Max(0., Circle.Radius - ChestExtent.Size2D() - (PerCircle[Index] ? DecoyRadius : 0.))
                    : PerCircle[Index] > 0
                    ? FMath::Min(ClueOffsetWithDecoys, Circle.Radius - DecoyRadius - 1.0)
                    : Circle.Radius - 1.0;
                const double Angle = Stream.FRand() * UE_TWO_PI;
                const double Distance = FMath::Sqrt(Stream.FRand()) * Radius;
                Point += FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
            }
            Layout.Centers[Index] = Place(Point, bCentered ? 300 : Stream.RandRange(200, 400));
        }
        if (!FindCrossing(Layout, Layout.Crossing))
        {
            if (Error)
            {
                *Error = TEXT("A-C and B-D need an intersection inside both segments");
            }
            continue;
        }
        Layout.Centers[PrizeIndex] = Place(Layout.Crossing, bCentered ? 700 : Stream.RandRange(600, 800));
        for (int32 Index = 0; Index < ClueCount; ++Index)
        {
            const double Rotation = PerCircle[Index] > 0 && !bCentered ? Stream.FRand() * UE_TWO_PI : 0.0;
            for (int32 Slot = 0; Slot < PerCircle[Index]; ++Slot)
            {
                const double Angle = Rotation + Slot * UE_TWO_PI / PerCircle[Index];
                const FVector2D Point = FVector2D(Layout.Centers[Index]) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * DecoyRadius;
                Layout.Decoys.Add({Place(Point, bCentered ? 300 : Stream.RandRange(200, 400)), Index});
            }
        }
        if (IsValidLayout(Layout, Circles, Density, Error, Obstacles))
        {
            if (Error)
            {
                Error->Reset();
            }
            return true;
        }
    }
    return false;
}

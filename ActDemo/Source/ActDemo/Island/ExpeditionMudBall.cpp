#include "Island/ExpeditionMudBall.h"

#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/ExpeditionBodyMud.h"
#include "Island/IslandAttachedMud.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    bool TraceMudSurface(UWorld& World, const FVector& Start, const FVector& End, float SweepRadius,
        const FCollisionQueryParams& Query, FHitResult& Hit)
    {
        TArray<FHitResult> Contacts;
        if (SweepRadius > 0.f)
            World.SweepMultiByChannel(Contacts, Start, End, FQuat::Identity, ECC_Visibility,
                FCollisionShape::MakeSphere(SweepRadius), Query);
        else
            World.LineTraceMultiByChannel(Contacts, Start, End, ECC_Visibility, Query);
        for (const FHitResult& Contact : Contacts)
        {
            if (!Contact.bBlockingHit && !Cast<AIslandNeutralProp>(Contact.GetActor())
                && !AIslandAttachedMud::CanReceive(Contact.GetComponent())) continue;
            Hit = Contact;
            // Initial sphere overlaps report the sweep origin as the impact. Recover
            // the surface position, otherwise the pad's short support trace can miss.
            if (Hit.bStartPenetrating && SweepRadius > 0.f && !Hit.ImpactNormal.IsNearlyZero())
                Hit.ImpactPoint = Start - Hit.ImpactNormal * (SweepRadius - Hit.PenetrationDepth);
            return true;
        }
        return false;
    }
}

bool AExpeditionMudBall::TraceContact(UWorld& World, const AVoxelCharacter& Thrower, const FVector& Start,
    const FVector& End, float SweepRadius, FHitResult& Hit)
{
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionMud), false, &Thrower);
    FCollisionQueryParams SceneryQuery = Query;
    FHitResult Body;
    bool bBody = false;
    const FBox FlightBounds = FBox(Start.ComponentMin(End),Start.ComponentMax(End)).ExpandBy(SweepRadius + 1.);
    // Only a character's physical capsule and its actual clods can catch a throw.
    // Cosmetic and interaction components on the Pawn channel can extend far outside it.
    for (TActorIterator<AVoxelCharacter> It(&World); It; ++It)
    {
        if (*It == &Thrower) continue;
        SceneryQuery.AddIgnoredActor(*It);
        auto* Capsule = It->GetCapsuleComponent();
        FHitResult Contact;
        if (Capsule->IsQueryCollisionEnabled() && FlightBounds.Intersect(Capsule->Bounds.GetBox())
            && (SweepRadius > 0.f
                ? Capsule->SweepComponent(Contact,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(SweepRadius))
                : Capsule->LineTraceComponent(Contact,Start,End,Query))
            && (!bBody || Contact.Time < Body.Time))
        {
            if (Contact.bStartPenetrating)
            {
                const double BodyRadius = Capsule->GetScaledCapsuleRadius();
                const FVector Offset = Capsule->GetUpVector() * (Capsule->GetScaledCapsuleHalfHeight() - BodyRadius);
                const FVector Center = Capsule->GetComponentLocation();
                const FVector Axis = FMath::ClosestPointOnSegment(Start, Center - Offset, Center + Offset);
                Contact.ImpactNormal = (Start - Axis).GetSafeNormal(UE_SMALL_NUMBER,
                    (Start - End).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
                Contact.ImpactPoint = Axis + Contact.ImpactNormal * BodyRadius;
            }
            Contact.Item = INDEX_NONE;
            Body = Contact;
            bBody = true;
        }
        const auto* Mud = It->GetBodyMud();
        FHitResult Clay;
        if (Mud && Mud->Trace(Start, End, SweepRadius, Clay) && (!bBody || Clay.Time <= Body.Time))
        {
            Body = Clay;
            bBody = true;
        }
    }
    FHitResult Surface;
    const bool bSurface = TraceMudSurface(World, Start, End, SweepRadius, SceneryQuery, Surface);
    if (bBody && bSurface)
    {
        // Physics pulls a sweep hit back a small distance. For grazing both surfaces,
        // compare at centimetre precision and check the exposed side at the actual
        // contact, so a floor-level ankle wins but a capsule behind a wall never does.
        const double Along = (Body.Time - Surface.Time) * FVector::Dist(Start, End);
        if (Along > .8) bBody = false;
        else if (Along >= -.8)
        {
            FHitResult Occluder;
            const bool bBehind = FVector::DotProduct(Body.ImpactPoint - Surface.ImpactPoint, Surface.ImpactNormal) < -.5;
            const bool bOccluded = TraceMudSurface(World, Start, Body.ImpactPoint, 0.f, SceneryQuery, Occluder)
                && FVector::DistSquared(Occluder.ImpactPoint, Body.ImpactPoint) > .25;
            bBody = !bBehind && !bOccluded;
        }
    }
    if (bBody) Hit = Body;
    else if (bSurface) Hit = Surface;
    return bBody || bSurface;
}

bool AExpeditionMudBall::SweepFlight(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
    const auto* Thrower = Cast<AVoxelCharacter>(GetInstigator());
    return Thrower && TraceContact(*GetWorld(), *Thrower, Start, End, Radius, Hit);
}

void AExpeditionMudBall::ApplyImpact(const FHitResult& Hit)
{
    auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
    auto* Thrower = Cast<AIslandPlayerController>(GetOwner());
    if (Mode && Thrower) Mode->ResolveMudImpact(*Thrower, Hit);
}

bool AExpeditionMudBall::GetThrowHandLocation(FVector& OutLocation) const
{
    const auto* Thrower = GetInstigator();
    const auto* Animal = Thrower ? Thrower->FindComponentByClass<UExpeditionAnimalComponent>() : nullptr;
    return Animal && Animal->GetThrowHandLocation(OutLocation);
}

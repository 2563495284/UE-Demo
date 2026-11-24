#include "ActVoxelEditorProps.h"

#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Island/IslandBreakable.h"

AIslandBreakable* ActVoxelEditorProps::Convert(AStaticMeshActor& Source)
{
    const UStaticMeshComponent& Mesh = *Source.GetStaticMeshComponent();
    if (!Mesh.GetStaticMesh())
    {
        return nullptr;
    }
    UWorld* World = Source.GetWorld();
    FActorSpawnParameters Params;
    Params.OverrideLevel = Source.GetLevel();
    Params.ObjectFlags |= RF_Transactional;
    Source.GetLevel()->Modify();
    AIslandBreakable* Prop = World->SpawnActor<AIslandBreakable>(AIslandBreakable::StaticClass(), Mesh.GetComponentTransform(), Params);
    if (!Prop)
    {
        return nullptr;
    }
    Prop->SetActorLabel(Source.GetActorLabel());
    Prop->SetFolderPath(Source.GetFolderPath());
    Prop->Tags = Source.Tags;
    Prop->GetMesh().SetStaticMesh(Mesh.GetStaticMesh());
    for (int32 Index = 0; Index < Mesh.GetNumMaterials(); ++Index)
    {
        Prop->GetMesh().SetMaterial(Index, Mesh.GetMaterial(Index));
    }
    Source.Modify();
    World->EditorDestroyActor(&Source, true);
    Prop->MarkPackageDirty();
    return Prop;
}

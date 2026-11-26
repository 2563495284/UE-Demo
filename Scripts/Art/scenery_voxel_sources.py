"""Rebuild the CPU triangle archive used by the cooked scenery voxelizer.

Run this in the UE editor after authoring meshes, or invoke the dedicated
UnrealEditor-Cmd commandlet with -run=ActIslandVoxelSources to rebuild the archive.
"""
import unreal


def retain_source(mesh):
    if mesh.get_editor_property("allow_cpu_access"):
        return
    mesh.set_editor_property("allow_cpu_access", True)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
        raise RuntimeError(f"Could not save voxel source {mesh.get_path_name()}")


if __name__ == "__main__":
    assets = []
    for folder in ("Harbour", "Neutral", "Crew"):
        root = "/Game/TreasureIsland/Art/" + folder
        for path in unreal.EditorAssetLibrary.list_assets(root, recursive=False):
            name = path.split(".")[-1]
            if name.startswith("SM_") and (folder != "Crew" or name.startswith("SM_Coffer")):
                assets.append(path)
    for path in assets:
        retain_source(unreal.load_asset(path))
    if not unreal.ActIslandVoxelSourcesCommandlet.rebuild_source_archive():
        raise RuntimeError("Could not rebuild the island scenery triangle archive")
    unreal.log(f"SCENERY_VOXEL_SOURCES count={len(assets)}")

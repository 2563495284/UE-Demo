"""Install artist-authored distant meshes, retaining the sculpted face at each distance."""
from pathlib import Path
import unreal as ue


def bake(model, source_dir=Path(__file__).resolve().parent):
    editor=ue.get_editor_subsystem(ue.SkeletalMeshEditorSubsystem)
    for tier in (1,2):
        path=source_dir/f'{model.get_name()}_LOD{tier}.glb'
        if editor.import_lod(model,tier,str(path))!=tier:
            raise RuntimeError(f'Failed to import pig LOD {tier}')
    if editor.get_lod_count(model)!=3:
        raise RuntimeError('Expected three pig levels of detail')
    ue.EditorAssetLibrary.save_loaded_asset(model,False)
    species=model.get_name().removeprefix('SK_Expedition').upper()
    print(f'{species}_LODS_READY 3 '+model.get_path_name())


if __name__=='__main__':
    bake(ue.load_asset('/Game/TreasureIsland/Art/Crew/Pig/SK_ExpeditionPig'))

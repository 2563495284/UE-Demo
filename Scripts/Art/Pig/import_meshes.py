"""Shared Interchange settings for island skeletal characters and their near-camera hands."""
import unreal as ue
from lods import bake as bake_distance_lods


def import_character(source, species, paint):
    assets=ue.EditorAssetLibrary
    folder=f'/Game/TreasureIsland/Art/Crew/{species}'
    name=f'SK_Expedition{species}'
    fist_name=f'SM_{species}Fist'
    assets.make_directory(folder)
    pipeline=ue.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials',False)
    pipeline.get_editor_property('mesh_pipeline').set_editor_property('create_physics_asset',False)
    pipeline.get_editor_property('animation_pipeline').set_editor_property('import_animations',False)
    pipeline.set_editor_property('use_source_name_for_asset',True)
    params=ue.ImportAssetParameters()
    params.set_editor_property('is_automated',True)
    params.set_editor_property('override_pipelines',[ue.SoftObjectPath(pipeline.get_path_name())])
    manager=ue.InterchangeManager.get_interchange_manager_scripted()
    for filename in (source,source.with_name(fist_name+".glb")):
        args=[folder,ue.InterchangeManager.create_source_data(str(filename)),params]
        if not manager.import_asset(*args):
            raise RuntimeError('glTF import failed for '+str(filename))
    found=assets.list_assets(folder,recursive=True,include_folder=False)
    print('CREW_IMPORTED '+str(found))
    meshes=[x for x in found if isinstance(assets.load_asset(x),ue.SkeletalMesh)]
    if len(meshes)!=1:
        raise RuntimeError('Expected one skinned model: '+str(meshes))
    model=assets.load_asset(meshes[0])
    if model.get_name()!=name:
        if not assets.rename_asset(meshes[0],folder+'/'+name):
            raise RuntimeError('Could not install the character at its runtime package')
        model=assets.load_asset(folder+'/'+name)
    slots=model.get_editor_property('materials')
    assert len(slots)==1, slots
    slot=slots[0]
    slot.set_editor_property('material_interface',paint)
    slots[0]=slot
    model.set_editor_property('materials',slots)
    hand=assets.load_asset(folder+'/'+fist_name)
    if not isinstance(hand,ue.StaticMesh):
        raise RuntimeError('Missing imported first-person hand')
    hand.set_material(0,paint)
    for path in assets.list_assets(folder,recursive=True,include_folder=False):
        obj=assets.load_asset(path)
        if obj: assets.save_loaded_asset(obj,False)
    bake_distance_lods(model,source.parent)
    probe=ue.SkeletalMeshComponent()
    probe.set_skeletal_mesh_asset(model)
    if probe.get_material(0)!=paint or hand.get_material(0)!=paint:
        raise RuntimeError(f"{name}: missing shared vertex material")
    bones=[str(probe.get_bone_name(i)) for i in range(probe.get_num_bones())]
    for bone in ('root','pelvis','spine','head','ear_l','tail_02','hand_l','hand_r','foot_l','foot_r','brow_l','eyelid_r','jaw','hat_brim'):
        if bone not in bones:
            raise RuntimeError(f"{name}: missing {bone}")
    return model,bones

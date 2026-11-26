"""Create or refresh the skinned and first-person pig's shared vertex-paint material."""
import unreal as ue

FOLDER='/Game/TreasureIsland/Art/Crew/Pig'


def bake():
    assets=ue.EditorAssetLibrary
    assets.make_directory(FOLDER)
    path=FOLDER+'/M_PigPaint'
    paint=assets.load_asset(path) if assets.does_asset_exist(path) else ue.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_PigPaint',FOLDER,ue.Material,ue.MaterialFactoryNew())
    ed=ue.MaterialEditingLibrary
    ed.delete_all_material_expressions(paint)
    vertex=ed.create_material_expression(paint,ue.MaterialExpressionVertexColor,-250,0)
    # UE's composite vertex color pin has an empty name; only its four single channels are named.
    assert ed.connect_material_property(vertex,'',ue.MaterialProperty.MP_BASE_COLOR)
    assert ed.connect_material_property(vertex,'A',ue.MaterialProperty.MP_ROUGHNESS)
    metal_uv=ed.create_material_expression(paint,ue.MaterialExpressionTextureCoordinate,-400,150)
    metal_uv.set_editor_property('coordinate_index',1)
    mask=ed.create_material_expression(paint,ue.MaterialExpressionComponentMask,-180,150)
    mask.set_editor_property('r',True)
    for channel in ('g','b','a'):
        mask.set_editor_property(channel,False)
    assert ed.connect_material_expressions(metal_uv,'',mask,'')
    assert ed.connect_material_property(mask,'',ue.MaterialProperty.MP_METALLIC)
    assert ed.get_material_property_input_node(paint,ue.MaterialProperty.MP_BASE_COLOR)==vertex
    assert ed.get_material_property_input_node_output_name(paint,ue.MaterialProperty.MP_ROUGHNESS)=='A'
    paint.set_editor_property('used_with_skeletal_mesh',True)
    paint.set_editor_property('used_with_nanite',True)
    ed.recompile_material(paint)
    assert assets.save_loaded_asset(paint,False)
    print('PIG_PAINT_VERIFIED base=vertex_rgb roughness=vertex_alpha '+paint.get_path_name())
    return paint


if __name__=='__main__':
    bake()

"""Author the island ground, arena ground and diggable coastal vegetation materials."""
import unreal as ue

LIB = ue.MaterialEditingLibrary
DEST = "/Game/TreasureIsland/FX"


def expression(material, node_type, x, y):
    return LIB.create_material_expression(material, node_type, x, y)


def custom(material, title, code, inputs, y):
    node = expression(material, ue.MaterialExpressionCustom, -340, y)
    node.set_editor_property("description", title)
    node.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("code", code)
    input_names = []
    for name in inputs:
        pin = ue.CustomInput()
        pin.set_editor_property("input_name", name)
        input_names.append(pin)
    node.set_editor_property("inputs", input_names)
    for name, source in inputs.items():
        assert LIB.connect_material_expressions(source, "", node, name)
    return node


ISLAND_COLOR = """
// One geometric palette across baked terrain, restored chunks and freshly cut surfaces.
// Signed excavation depth comes from the same base height as the voxel generator (centimetres).
float2 q = P.xy * .0037;
q += .16 * sin(q.yx * .67 + float2(.4, 2.1));
float2 lattice = float2(q.x - q.y*.57735, q.y*1.1547);
float2 cell = floor(lattice);
float upperFacet = step(1., dot(frac(lattice),float2(1,1)));
float face = frac(sin(dot(cell,float2(51.71,127.53)) + upperFacet*37.19)*4375.83) - .5;
float drift = sin(P.x*.00095 + 1.3*sin(P.y*.00137)) + .42*sin(P.y*.0019 + P.x*.00042);
float slope = 1.-abs(N.z);
float tideHeight = P.z + 9.*drift;
float wet = 1.-smoothstep(7., 62., tideHeight);
float swale = sin(P.x*.00048 - P.y*.00091 + .87*drift);
float grass = smoothstep(292.,394.,P.z+drift*91.+swale*42.+face*21.) * (1.-smoothstep(.10,.31,slope));
float3 shellSand = lerp(float3(.61,.395,.235),float3(.84,.665,.435),saturate(.55+.23*drift));
float3 beach = lerp(shellSand,float3(.36,.30,.245),wet);
float shellLine = 1.-smoothstep(5.,17.,abs(tideHeight+face*5.-54.));
beach += float3(.060,.052,.035)*shellLine;
float wind = smoothstep(-.55,.65,swale+.30*drift);
float3 turf = lerp(float3(.18,.345,.25),float3(.40,.485,.225),wind);
float dryTips = smoothstep(.62,.93,swale+face*.21) * smoothstep(365.,590.,P.z);
turf = lerp(turf,float3(.505,.465,.235),dryTips*.58);
float3 topsoil = lerp(beach,turf,grass);
float tiltedZ = P.z+.06*P.x-.035*P.y+drift*10.;
float bedding = smoothstep(.43,.59,frac(tiltedZ/104.));
float3 shale = lerp(float3(.24,.305,.31),float3(.395,.405,.35),bedding*.68+.12);
float ledge = smoothstep(490.,725.,P.z+drift*72.) * smoothstep(.055,.20,slope);
float3 untouched = lerp(topsoil,shale,ledge);
float depth = Cut.x;
float3 bank = lerp(float3(.355,.205,.135),float3(.515,.345,.215),bedding*.72+.08);
float mineral = 1.-smoothstep(3.,10.,abs(frac(tiltedZ/115.)-.84)*115.);
float3 deepRock = lerp(shale*.84,float3(.515,.49,.365),mineral*.50);
float3 exposed = lerp(bank,deepRock,smoothstep(150.,320.,depth));
float3 top = lerp(untouched,exposed,smoothstep(12.,55.,depth));
top = lerp(top,lerp(float3(.49,.295,.15),shellSand,.46),smoothstep(12.,50.,-depth));
return top*(1.+face*.085);
"""
ARENA_COLOR = """
float2 q = (P.xy + float2(.27,.47)*P.z)*.0065;
float2 lattice = float2(q.x-q.y*.57735,q.y*1.1547);
float2 cell = floor(lattice);
float face = frac(sin(dot(cell,float2(51.71,127.53)) + step(1.,frac(lattice.x)+frac(lattice.y))*37.19)*4375.83);
return Tint.rgb*(.965+face*.07);
"""


def build_ground(name, is_island):
    path = f"{DEST}/{name}"
    if ue.EditorAssetLibrary.does_asset_exist(path):
        material = ue.load_asset(path)
    else:
        material = ue.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DEST, ue.Material, ue.MaterialFactoryNew())
    material.set_editor_property("tangent_space_normal", False)
    prop = ue.MaterialProperty
    # These graphs are referenced by the terrain CDO. Edit existing nodes in place.
    normal = LIB.get_material_property_input_node(material, prop.MP_NORMAL)
    if normal:
        world, eye = LIB.get_inputs_for_material_expression(material, normal)
    else:
        world = expression(material, ue.MaterialExpressionWorldPosition, -1050, 0)
        eye = expression(material, ue.MaterialExpressionCameraVectorWS, -1050, -200)
        normal = custom(material, "Geometric face normal",
                        "float3 f=cross(ddy(P),ddx(P)); return f * rsqrt(max(dot(f,f),1.e-12)) * (dot(f,Eye)<0.?-1.:1.);",
                        {"P": world, "Eye": eye}, -220)
        assert LIB.connect_material_property(normal, "", prop.MP_NORMAL)
    color = LIB.get_material_property_input_node(material, prop.MP_BASE_COLOR)
    if color:
        color.set_editor_property("code", ISLAND_COLOR if is_island else ARENA_COLOR)
    elif is_island:
        depth = expression(material, ue.MaterialExpressionTextureCoordinate, -760, 280)
        depth.set_editor_property("coordinate_index", 1)
        color = custom(material, "Coast, upland and exposed strata", ISLAND_COLOR,
                       {"P": world, "N": normal, "Cut": depth}, 80)
    else:
        pigment = expression(material, ue.MaterialExpressionVertexColor, -810, 270)
        color = custom(material, "Faceted arena pigments", ARENA_COLOR, {"P": world, "Tint": pigment}, 80)
    assert LIB.connect_material_property(color, "", prop.MP_BASE_COLOR)
    rough = LIB.get_material_property_input_node(material, prop.MP_ROUGHNESS)
    if is_island:
        code = "return lerp(.54,.92,smoothstep(7.,72.,P.z));"
        if isinstance(rough, ue.MaterialExpressionCustom):
            rough.set_editor_property("code", code)
        else:
            rough = custom(material, "Damp foreshore to dry upland", code, {"P": world}, 500)
            rough.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT1)
    elif not rough:
        rough = expression(material, ue.MaterialExpressionConstant, -340, 500)
        rough.set_editor_property("r", .91)
    assert LIB.connect_material_property(rough, "", prop.MP_ROUGHNESS)
    spec = LIB.get_material_property_input_node(material, prop.MP_SPECULAR)
    if not spec:
        spec = expression(material, ue.MaterialExpressionConstant, -340, 650)
    spec.set_editor_property("r", .27 if is_island else .18)
    assert LIB.connect_material_property(spec, "", prop.MP_SPECULAR)
    LIB.recompile_material(material)
    assert ue.EditorAssetLibrary.save_loaded_asset(material, False)


def build_route_pigment():
    path = f"{DEST}/M_RoutePigment"
    material = ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else (
        ue.AssetToolsHelpers.get_asset_tools().create_asset("M_RoutePigment", DEST, ue.Material, ue.MaterialFactoryNew()))
    prop = ue.MaterialProperty
    pigment = LIB.get_material_property_input_node(material, prop.MP_BASE_COLOR)
    if not pigment:
        pigment = expression(material, ue.MaterialExpressionVertexColor, -350, 80)
    assert LIB.connect_material_property(pigment, "", prop.MP_BASE_COLOR)
    assert LIB.connect_material_property(pigment, "A", prop.MP_ROUGHNESS)
    material.set_editor_property("tangent_space_normal", False)
    normal = LIB.get_material_property_input_node(material, prop.MP_NORMAL)
    if not normal:
        world = expression(material, ue.MaterialExpressionWorldPosition, -700, -180)
        eye = expression(material, ue.MaterialExpressionCameraVectorWS, -700, -300)
        normal = custom(material, "Carved coastal facets",
                        "float3 f=cross(ddy(P),ddx(P)); return f * rsqrt(max(dot(f,f),1.e-12)) * (dot(f,Eye)<0.?-1.:1.);",
                        {"P": world, "Eye": eye}, -180)
        assert LIB.connect_material_property(normal, "", prop.MP_NORMAL)
    LIB.recompile_material(material)
    assert ue.EditorAssetLibrary.save_loaded_asset(material, False)


if __name__ == "__main__":
    build_ground("M_IslandGround", True)
    build_ground("M_ArenaGround", False)
    build_route_pigment()

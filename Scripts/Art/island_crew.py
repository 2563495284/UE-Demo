"""Build the island crew, their common hand shapes, chest lid and lagoon material in UE Editor."""
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).parent))
from static_mesh import Facets, ue

ART = "/Game/TreasureIsland/Art/Crew"
TAU = math.tau


def paint(hex_code):
    rgb = [int(hex_code[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return ue.LinearColor(*[c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4 for c in rgb], 1.0)


class Carving(Facets):
    def __init__(self):
        super().__init__()
        self.pigment = None

    def face(self, *points):
        super().face(*points)
        if self.pigment is not None:
            self.colors.extend([self.pigment] * len(points))

    def solid(self, tone, center, size, spin=0, power=.88, rings=5, sides=12):
        """A squat carved ellipsoid; spin is rotation around the character's fore-aft axis."""
        self.pigment = paint(tone) if tone else None
        cx, cy, cz = center
        rx, ry, rz = [v * .5 for v in size]
        cs, sn = math.cos(math.radians(spin)), math.sin(math.radians(spin))

        def point(a, h):
            r = max(0, 1 - h * h) ** (power * .5)
            def rounded(v):
                return math.copysign(abs(v) ** power, v)
            x, y, z = rx * r * rounded(math.cos(a)), ry * r * rounded(math.sin(a)), rz * rounded(h)
            return cx + x, cy + cs * y - sn * z, cz + sn * y + cs * z

        belts = [[point(TAU * j / sides, -math.cos(math.pi * i / rings)) for j in range(sides)] for i in range(1, rings)]
        for j in range(sides):
            k = (j + 1) % sides
            self.face(point(0, -1), belts[0][k], belts[0][j])
            for lo, hi in zip(belts, belts[1:]):
                self.face(lo[j], lo[k], hi[k], hi[j])
            self.face(belts[-1][j], belts[-1][k], point(0, 1))

    def spar(self, tone, a, b, bottom, top, sides=8):
        """A tapered piece following the two points, including its caps."""
        self.pigment = paint(tone)
        av, bv = ue.Vector(*a), ue.Vector(*b)
        axis = (bv - av).normal()
        axis_y = axis.cross(ue.Vector(0, 0, 1))
        if axis_y.length() < .01:
            axis_y = ue.Vector(1, 0, 0)
        axis_y = axis_y.normal()
        axis_z = axis.cross(axis_y).normal()
        low, high = [], []
        for j in range(sides):
            v = axis_y * math.cos(TAU * j / sides) + axis_z * math.sin(TAU * j / sides)
            bottom_point = av + v * bottom
            top_point = bv + v * top
            low.append((bottom_point.x, bottom_point.y, bottom_point.z))
            high.append((top_point.x, top_point.y, top_point.z))
        self.face(*reversed(low))
        self.face(*high)
        for j in range(sides):
            k = (j + 1) % sides
            self.face(low[j], low[k], high[k], high[j])

    def ear(self, root, tip, width, thickness, palette):
        """An angular ear with a recessed colored mask on the front (+X) face."""
        x, y, z = root
        tx, ty, tz = tip
        lower = (x + thickness * .45, y, z - 3)
        left = (x, y - width * .5, z + 3)
        right = (x, y + width * .5, z + 3)
        back = (x - thickness, y, z + 5)
        point = (tx, ty, tz)
        self.pigment = paint(palette[0])
        self.face(lower, right, point)
        self.face(lower, point, left)
        self.face(left, point, back)
        self.face(point, right, back)
        self.face(lower, left, back, right)
        # A smaller triangular inset sits on the sloping ear face.
        self.pigment = paint(palette[1])
        self.face((x + thickness * .35 + .8, y - width * .25, z + 5),
                  (x + thickness * .35 + .8, y + width * .25, z + 5),
                  (tx * .75 + x * .25 + 1.7, ty * .75 + y * .25, tz * .75 + z * .25))


def character(species, skin):
    mesh = Carving()
    cream, ink, brass = "F7DBAD", "302E36", "E9AD58"
    widths = ((73, 77, 68), (57, 62, 70), (59, 67, 65), (72, 82, 72))
    heads = ((78, 91, 73), (76, 77, 74), (74, 81, 70), (79, 91, 70))
    mesh.solid(skin, (0, 0, 58), widths[species], power=.70)
    mesh.solid(skin, (4, 0, 112), heads[species], power=.70 if species in (0, 3) else .87)
    if species == 0:
        mesh.solid("356E87", (-1, 0, 42), (75, 78, 45), power=.65)
        mesh.solid("478399", (28, 0, 63), (17, 51, 37), power=.64)
        for s in (-1, 1):
            mesh.spar("478399", (24, s * 17, 65), (1, s * 19, 88), 4, 4, 6)
            mesh.spar("478399", (1, s * 19, 88), (-26, s * 17, 65), 4, 4, 6)
            mesh.solid(brass, (35, s * 15, 73), (3, 6, 6), rings=4, sides=8)
            mesh.ear((-1, s * 26, 139), (-4, s * (46 if s < 0 else 31), 151 if s < 0 else 166),
                     22, 12, (skin, "C16B67"))
        mesh.solid("D8877F", (42, 0, 104), (28, 48, 27), power=.60)
        for s in (-1, 1):
            mesh.solid("764845", (55.5, s * 10, 106), (3, 6, 8), rings=4, sides=8)
        mesh.spar(skin, (-32, 0, 47), (-42, 4, 50), 4.5, 4)
        mesh.spar(skin, (-42, 4, 50), (-44, 9, 57), 4, 3)
        mesh.spar(skin, (-44, 9, 57), (-39, 13, 57), 3, 1.3)
    elif species == 1:
        mesh.solid("B96E43", (-1, 0, 59), (60, 65, 68))
        for s in (-1, 1):
            mesh.solid(skin, (0, s * 39, 119), (20, 34, 35), power=.85)
            mesh.solid("DFAC7B", (8, s * 43, 119), (9, 22, 21), power=.83)
            mesh.solid("E9BD8E", (31, s * 15, 117), (26, 38, 41))
        mesh.solid("E9BD8E", (40, 0, 102), (24, 39, 25))
        mesh.solid(ink, (51, 0, 107), (7, 13, 8), rings=4, sides=8)
        curve = [(-22, 0, 42), (-38, 3, 42), (-50, 8, 51), (-54, 14, 66), (-49, 17, 76), (-42, 15, 74)]
        for i, (a, b) in enumerate(zip(curve, curve[1:])):
            mesh.spar(skin, a, b, 7 - .7 * i, 6.3 - .7 * i, 7)
        mesh.solid("408B86", (0, 0, 47), (60, 66, 11), power=.8)
    elif species == 2:
        for s in (-1, 1):
            mesh.ear((-2, s * 26, 138), (-4, s * 39, 169), 30, 15, ("683C32", "EFAC8D"))
            mesh.solid(cream, (28, s * 20, 103), (33, 35, 23), spin=s * 13)
        mesh.solid(cream, (43, 0, 103), (35, 34, 20))
        mesh.solid(ink, (59, 0, 108), (8, 14, 9), rings=4, sides=8)
        mesh.solid("407E82", (0, 0, 83), (65, 71, 16), power=.75)
        mesh.ear((-24, 22, 83), (-40, 37, 66), 20, 6, ("407E82", "61A4A0"))
        tail = [(-20, -7, 46), (-40, -11, 42), (-58, -15, 49), (-67, -17, 63), (-72, -16, 74)]
        for a, b, cr, tr, tone in zip(tail, tail[1:], (9, 15, 15, 10), (15, 16, 10, 1), (skin, skin, cream, cream)):
            mesh.spar(tone, a, b, cr, tr, 9)
    else:
        mesh.solid("52505A", (31, -19, 122), (26, 34, 39), power=.86)
        mesh.solid("CE9C8B", (42, 0, 101), (29, 54, 29), power=.7)
        for s in (-1, 1):
            mesh.solid(skin, (-2, s * 41, 131), (18, 31, 18), spin=s * -14)
            mesh.spar("E9CFA2", (0, s * 23, 140), (-2, s * 33, 154), 8.5, 5, 7)
            mesh.spar(cream, (-2, s * 33, 154), (1, s * 37, 161), 5, 1.4, 7)
            mesh.solid("79574E", (55, s * 12, 103), (3, 7, 6), rings=4, sides=8)
        mesh.solid("915B48", (0, 0, 81), (73, 78, 14), power=.75)
        mesh.spar(brass, (36, 0, 66), (35, 0, 76), 8, 4, 6)
        mesh.solid("52505A", (-4, -32, 64), (34, 14, 27))
        mesh.spar(skin, (-28, 0, 51), (-39, 5, 31), 3.6, 2.5)
        mesh.solid("52505A", (-40, 5, 28), (10, 10, 15), rings=4, sides=8)
    for s in (-1, 1):
        mesh.solid(ink, (40 if species != 3 else 41, s * 19, 121), (9, 10, 15), rings=5, sides=9)
        mesh.solid("FFF3DA", (44 if species != 3 else 45, s * 19 - 1, 124), (3, 3.6, 4), rings=4, sides=7)
    return mesh


def material(name, path, configure, *, reuse_nodes=False):
    dest = path + "/" + name
    exists = ue.EditorAssetLibrary.does_asset_exist(dest)
    obj = ue.load_asset(dest) if exists else ue.AssetToolsHelpers.get_asset_tools().create_asset(name, path, ue.Material, ue.MaterialFactoryNew())
    ed = ue.MaterialEditingLibrary
    if not reuse_nodes:
        ed.delete_all_material_expressions(obj)
    configure(obj, ed)
    ed.recompile_material(obj)
    ue.EditorAssetLibrary.save_loaded_asset(obj, False)
    return obj


def crew_material(obj, ed):
    pigment = ed.create_material_expression(obj, ue.MaterialExpressionVertexColor, -280, 0)
    tint = ed.create_material_expression(obj, ue.MaterialExpressionVectorParameter, -280, -140)
    tint.set_editor_property("parameter_name", "Color")
    tint.set_editor_property("default_value", ue.LinearColor(1,1,1,1))
    mix = ed.create_material_expression(obj, ue.MaterialExpressionMultiply, -80, 0)
    ed.connect_material_expressions(tint, "", mix, "A")
    ed.connect_material_expressions(pigment, "", mix, "B")
    ed.connect_material_property(mix, "", ue.MaterialProperty.MP_BASE_COLOR)
    rough = ed.create_material_expression(obj, ue.MaterialExpressionConstant, -280, 130)
    rough.set_editor_property("r", .87)
    ed.connect_material_property(rough, "", ue.MaterialProperty.MP_ROUGHNESS)


def lagoon_material(obj, ed):
    obj.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_DEFAULT_LIT)
    obj.set_editor_property("two_sided", True)
    # The harbour uses a scaled cube; the island uses concentric procedural rings.
    # Describe the same up-facing wave facets on both surfaces in world space.
    obj.set_editor_property("tangent_space_normal", False)
    # The game loads the lagoon from a CDO. Upgrade its existing graph in place so
    # the editor never tries to delete expressions rooted at startup.
    base_color = ue.MaterialProperty.MP_BASE_COLOR
    emissive = ue.MaterialProperty.MP_EMISSIVE_COLOR
    shader = ed.get_material_property_input_node(obj, base_color) or ed.get_material_property_input_node(obj, emissive)
    inputs = ed.get_inputs_for_material_expression(obj, shader) if shader else []
    if shader:
        xy, cols, tint, mask = inputs[:4]
    else:
        xy = ed.create_material_expression(obj, ue.MaterialExpressionWorldPosition, -700, -160)
        cols = ed.create_material_expression(obj, ue.MaterialExpressionVertexColor, -700, 0)
        tint = ed.create_material_expression(obj, ue.MaterialExpressionVectorParameter, -700, 160)
        mask = ed.create_material_expression(obj, ue.MaterialExpressionScalarParameter, -700, 280)
        shader = ed.create_material_expression(obj, ue.MaterialExpressionCustom, -250, 0)
    time = inputs[4] if len(inputs) > 4 else ed.create_material_expression(obj, ue.MaterialExpressionTime, -700, 400)
    coast_uv = inputs[5] if len(inputs) > 5 else ed.create_material_expression(obj, ue.MaterialExpressionTextureCoordinate, -700, 500)
    tint.set_editor_property("parameter_name", "WaterTint")
    tint.set_editor_property("default_value", paint("378D91"))
    mask.set_editor_property("parameter_name", "UseCoastColors")
    shader.set_editor_property("description", "Broad tidal facets and sparse broken wave crests")
    shader.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT3)
    args = []
    for label in ("Position", "Coast", "Tint", "UseCoast", "Seconds", "ShoreUV"):
        inp = ue.CustomInput()
        inp.set_editor_property("input_name", label)
        args.append(inp)
    shader.set_editor_property("inputs", args)
    wave_normal = ue.CustomOutput()
    wave_normal.set_editor_property("output_name", "WaveNormal")
    wave_normal.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT3)
    shader.set_editor_property("additional_outputs", [wave_normal])
    shader.set_editor_property("code", """
// Work in centimetres so the same wind facets cross adjacent mesh sections.
float2 p = Position.xy * float2(.0010,.0021);
p.x += .28*p.y;
p += float2(-.029,.017)*Seconds;
float2 cell = floor(p);
float2 f = frac(p);
float seed = frac(sin(dot(cell,float2(23.5,57.1)))*12514.);
float facet = step(f.y + .12*(seed-.5), f.x) - .5;
float aa = max(fwidth(p.y),.001);
float visible = 1.-smoothstep(.10,.44,aa);
WaveNormal = normalize(float3(.062*facet*visible,.074*(seed-.5)*visible,1.));
float3 water = lerp(Tint.rgb,Coast.rgb,UseCoast) * (1.+visible*(.065*facet+.06*(seed-.5)));
float crestY = f.y-.44+.14*abs(f.x-.42);
float crest = (1.-smoothstep(.011,.026+aa,abs(crestY))) * visible
    * smoothstep(.05,.15,f.x)*(1.-smoothstep(.49,.74,f.x))*smoothstep(.72,.85,seed);
// Only the island supplies signed shoreline distance in UV0.x. The shallow
// band follows the island perimeter, breaking into short pieces between headlands.
float drift = dot(Position.xy,float2(.0013,.0019));
float arrival = 23.*sin(drift*1.4+Seconds*.35)+14.*sin(drift*2.3-Seconds*.44);
float shore = ShoreUV.x;
float edgeAA = max(1.,fwidth(shore));
float lip = (1.-smoothstep(2.,8.+edgeAA,abs(shore-43.-arrival)));
float backwash = (1.-smoothstep(1.,6.+edgeAA,abs(shore-125.-arrival*.75)));
float openEdge = smoothstep(-.25,.60,sin(drift+Seconds*.12)+.45*sin(drift*2.9));
float wash = (lip*.78 + backwash*.28) * openEdge * UseCoast * visible;
return lerp(water,float3(.68,.83,.72),saturate(.28*crest+wash));
""")
    for source, name in ((xy,"Position"), (cols,"Coast"), (tint,"Tint"), (mask,"UseCoast"), (time,"Seconds"), (coast_uv,"ShoreUV")):
        if not ed.connect_material_expressions(source, "", shader, name):
            raise RuntimeError("Unable to connect " + name)
    assert ed.connect_material_property(shader, "", base_color)
    assert ed.connect_material_property(shader, "WaveNormal", ue.MaterialProperty.MP_NORMAL)
    for prop, value, y in ((ue.MaterialProperty.MP_ROUGHNESS, .46, 180),
                           (ue.MaterialProperty.MP_SPECULAR, .28, 280),
                           (emissive, 0., 380)):
        node = ed.get_material_property_input_node(obj, prop)
        if not isinstance(node, ue.MaterialExpressionConstant):
            node = ed.create_material_expression(obj, ue.MaterialExpressionConstant, -250, y)
        node.set_editor_property("r", value)
        assert ed.connect_material_property(node, "", prop)


def build():
    pigment = material("M_CrewPigment", ART, crew_material)
    for name, tone in (("SalvageWood","A96643"), ("Brass","DCAF68"), ("Iron","48565B"), ("LagoonTrim","457D81"), ("Canvas","E9D2A7")):
        path = ART + "/MI_" + name
        instance = ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else ue.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_" + name, ART, ue.MaterialInstanceConstant, ue.MaterialInstanceConstantFactoryNew())
        ue.MaterialEditingLibrary.set_material_instance_parent(instance, pigment)
        ue.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, "Color", paint(tone))
        ue.MaterialEditingLibrary.update_material_instance(instance)
        ue.EditorAssetLibrary.save_loaded_asset(instance, False)
    material("M_Lagoon", "/Game/TreasureIsland/Art", lagoon_material, reuse_nodes=True)
    for species, (name, color) in enumerate((("Pig","EDAE9C"), ("Monkey","A96845"), ("Fox","D88043"), ("Calf","E7DCC2"))):
        asset = character(species, color).write(ART + "/SM_" + name, "Sphere")
        asset.set_material(0, pigment)
        ue.EditorAssetLibrary.save_loaded_asset(asset, False)
    stone = Carving()
    stone.solid(None, (0, 0, 0), (100, 100, 100), power=.78, sides=12, rings=6)
    stone.write(ART + "/SM_CarvedRound", "Sphere")
    paw = Carving()
    paw.solid(None, (-2, 2, 0), (87, 80, 100), power=.70, sides=10)
    paw.solid(None, (19,-29,7), (56,42,48), spin=-18, power=.82, sides=8)
    paw.write(ART + "/SM_Mitten", "Sphere")
    from expedition_salvage import bake as bake_salvage
    bake_salvage()
    ue.log("ISLAND_CREW_ART_READY")


if __name__ == "__main__":
    build()

"""Generate the parameterized finish used by the island wreck and its chipped surfaces."""
import os
import sys

import unreal as ue

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from island_crew import material


def wreck_finish(asset, edit):
    base = ue.MaterialProperty.MP_BASE_COLOR
    mix = edit.get_material_property_input_node(asset, base)
    if mix:
        tint, pigment = edit.get_inputs_for_material_expression(asset, mix)
    else:
        tint = edit.create_material_expression(asset, ue.MaterialExpressionVectorParameter, -520, -100)
        pigment = edit.create_material_expression(asset, ue.MaterialExpressionVertexColor, -520, 30)
        mix = edit.create_material_expression(asset, ue.MaterialExpressionMultiply, -270, -20)
        assert edit.connect_material_expressions(tint, "", mix, "A")
        assert edit.connect_material_expressions(pigment, "", mix, "B")
        assert edit.connect_material_property(mix, "", base)
    tint.set_editor_property("parameter_name", "Color")
    tint.set_editor_property("default_value", ue.LinearColor(1, 1, 1, 1))
    for row, (name, value, prop) in enumerate((
        ("Roughness", 0.84, ue.MaterialProperty.MP_ROUGHNESS),
        ("Metallic", 0.0, ue.MaterialProperty.MP_METALLIC),
    )):
        channel = edit.get_material_property_input_node(asset, prop)
        if not channel:
            channel = edit.create_material_expression(asset, ue.MaterialExpressionScalarParameter, -310, 170 + 120 * row)
            assert edit.connect_material_property(channel, "", prop)
        channel.set_editor_property("parameter_name", name)
        channel.set_editor_property("default_value", value)

    # Inset only the shadow caster on bevelled planks. At grazing angles the
    # Mac shadow map otherwise leaves a comb pattern on the receiving surface.
    offset = ue.MaterialProperty.MP_WORLD_POSITION_OFFSET
    shadow = edit.get_material_property_input_node(asset, offset)
    if shadow:
        zero, inset = edit.get_inputs_for_material_expression(asset, shadow)
    else:
        normal = edit.create_material_expression(asset, ue.MaterialExpressionVertexNormalWS, -680, 490)
        inset = edit.create_material_expression(asset, ue.MaterialExpressionMultiply, -450, 490)
        assert edit.connect_material_expressions(normal, "", inset, "A")
        zero = edit.create_material_expression(asset, ue.MaterialExpressionConstant, -450, 620)
        shadow = edit.create_material_expression(asset, ue.MaterialExpressionShadowReplace, -210, 490)
        assert edit.connect_material_expressions(zero, "", shadow, "Default")
        assert edit.connect_material_expressions(inset, "", shadow, "Shadow")
        assert edit.connect_material_property(shadow, "", offset)
    inset.set_editor_property("const_b", -2.0)


if __name__ == "__main__":
    material("M_WreckFinish", "/Game/TreasureIsland/Art/Neutral", wreck_finish, reuse_nodes=True)

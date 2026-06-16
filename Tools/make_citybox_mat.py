# M_CityBox: opaque lit materiaal dat z'n BaseColor uit de PER-INSTANCE custom data (RGB) leest, zodat
# alle stad-boxen (muren/vloeren/daken) als instances in een HISM kunnen met elk hun eigen kleur -> 1
# draw-call per shape i.p.v. duizenden losse componenten. Vervangt BasicShapeMaterial+MID-per-box.
import unreal

PKG = "/Game/_Project/Materials"
NAME = "M_CityBox"
FULL = PKG + "/" + NAME

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
mel = unreal.MaterialEditingLibrary

# Drie PerInstanceCustomData-floats (index 0,1,2) = R,G,B.
def pic(idx, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -600, y)
    n.set_editor_property("data_index", idx)
    return n

r = pic(0, -150)
g = pic(1, 0)
b = pic(2, 150)

# AppendVector(AppendVector(R,G), B) -> float3
ap1 = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -380, -75)
mel.connect_material_expressions(r, "", ap1, "A")
mel.connect_material_expressions(g, "", ap1, "B")
ap2 = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -200, 0)
mel.connect_material_expressions(ap1, "", ap2, "A")
mel.connect_material_expressions(b, "", ap2, "B")
mel.connect_material_property(ap2, "", unreal.MaterialProperty.MP_BASE_COLOR)

# Roughness vast (matte stad-look, zoals BasicShapeMaterial ongeveer).
rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, 200)
rough.set_editor_property("r", 0.65)
mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("M_CityBox gemaakt op " + FULL)

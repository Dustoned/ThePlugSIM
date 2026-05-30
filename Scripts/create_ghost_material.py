import unreal

PKG_PATH = "/Game/_Project/Materials"
NAME = "M_PlacementGhost"
FULL = PKG_PATH + "/" + NAME

atools = unreal.AssetToolsHelpers.get_asset_tools()

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)

mat = atools.create_asset(NAME, PKG_PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)

mle = unreal.MaterialEditingLibrary

# Kleur-parameter (door C++ gezet: blauw = ok, rood = niet plaatsbaar).
color = mle.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
color.set_editor_property("parameter_name", "GhostColor")
color.set_editor_property("default_value", unreal.LinearColor(0.15, 0.5, 1.0, 1.0))

# Constante opacity zodat je er een beetje doorheen kijkt.
opac = mle.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 250)
opac.set_editor_property("r", 0.45)

mle.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mle.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
mle.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)

mle.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)

with open("C:/TPS_tmp/ghost_mat_result.txt", "w") as f:
    f.write("created %s\n" % FULL)

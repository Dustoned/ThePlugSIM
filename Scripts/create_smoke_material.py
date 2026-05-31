import unreal

PKG_PATH = "/Game/_Project/Materials"
NAME = "M_Smoke"
FULL = PKG_PATH + "/" + NAME

atools = unreal.AssetToolsHelpers.get_asset_tools()

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)

mat = atools.create_asset(NAME, PKG_PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)
# Unlit zodat het zacht grijs oplicht als een rookwolkje, ongeacht licht.
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

mle = unreal.MaterialEditingLibrary

# Kleur (lichtgrijs) als emissive.
color = mle.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
color.set_editor_property("parameter_name", "SmokeColor")
color.set_editor_property("default_value", unreal.LinearColor(0.72, 0.74, 0.78, 1.0))

# Opacity als scalar-parameter (door de puff-actor uitgefade).
opac = mle.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 250)
opac.set_editor_property("parameter_name", "Opacity")
opac.set_editor_property("default_value", 0.5)

mle.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mle.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)

mle.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
print("created %s" % FULL)

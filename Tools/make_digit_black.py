# Maakt M_DigitBlack: een unlit, (bijna) zwart materiaal voor het lift-cijfer-VLAK (de 'screen'),
# zodat het cijfer-vlakje niet meer wit oplicht. Het cijfer zelf wordt er wit (TextRender) overheen
# gezet -> zwart scherm + wit oplichtend nummer (binnenste-buiten t.o.v. eerst).
import unreal

PKG = "/Game/_Project/Materials"
NAME = "M_DigitBlack"
FULL = PKG + "/" + NAME

# Fris (her)maken zodat de kleur klopt: NEUTRAAL pikzwart (geen blauwzweem).
if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)
tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
mel = unreal.MaterialEditingLibrary
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
# Echt zwart, neutraal (r=g=b) zodat het geen blauw trekt. Unlit -> geen reflecties/licht-zweem.
emis = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -350, 0)
emis.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
mel.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("M_DigitBlack gemaakt op " + FULL)

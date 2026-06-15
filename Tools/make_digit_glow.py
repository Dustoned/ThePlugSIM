# Maakt M_DigitGlow: een unlit materiaal dat zacht wit emissive licht geeft, voor de lift-cijfer-
# meshes (in/boven de lift + op de knoppen) zodat de cijfers altijd leesbaar oplichten zonder dat
# er een echte lichtbron op staat.
import unreal

PKG = "/Game/_Project/Materials"
NAME = "M_DigitGlow"
FULL = PKG + "/" + NAME

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.log("M_DigitGlow bestaat al - overslaan")
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    # Unlit: geen belichting, alleen de emissive-kleur telt -> het cijfer 'gloeit' zelf.
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Zacht wit, net iets boven 1 zodat het rustig oplicht (geen felle bloom).
    emis = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -350, 0)
    emis.set_editor_property("constant", unreal.LinearColor(1.45, 1.5, 1.65, 1.0))
    mel.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FULL)
    unreal.log("M_DigitGlow gemaakt op " + FULL)

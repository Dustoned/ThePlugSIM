import unreal

# Surface/Unlit materiaal dat een render-target toont (de wardrobe-spiegel). Param "Tex" zet ik
# runtime via een MID op de scene-capture render-target.
pkg = "/Game/_Project/Materials"
name = "M_MirrorDisplay"
full = pkg + "/" + name

if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.log_warning("M_MirrorDisplay bestaat al - overslaan")
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", True)

    mel = unreal.MaterialEditingLibrary
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property("parameter_name", "Tex")
    # Default-texture zodat 't compileert; runtime vervang ik 'm door de render-target.
    default_tex = unreal.load_asset("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid")
    if default_tex:
        tex.set_editor_property("texture", default_tex)
    mel.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    unreal.log("M_MirrorDisplay aangemaakt + opgeslagen")

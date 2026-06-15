import unreal, traceback
OUT = "C:/temp/textglow.txt"
lines = []
def w(s): lines.append(str(s))
try:
    SRC = "/Engine/EngineMaterials/DefaultTextMaterialOpaque"  # LIT, BLEND_MASKED, font-opacity intact
    DST = "/Game/_Project/Materials/M_DigitTextGlow"
    mel = unreal.MaterialEditingLibrary

    # Fris opnieuw maken (idempotent): de oude weg, dupliceer, en zet UNLIT + emissive wit.
    if unreal.EditorAssetLibrary.does_asset_exist(DST):
        unreal.EditorAssetLibrary.delete_asset(DST)
    unreal.EditorAssetLibrary.duplicate_asset(SRC, DST)
    dm = unreal.EditorAssetLibrary.load_asset(DST)
    if not dm:
        w("DST niet geladen"); raise Exception("no DST")

    dm.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Unlit -> de EMISSIVE-input bepaalt de eindkleur. De opacity-mask (font-glyph) blijft uit het
    # gedupliceerde materiaal staan, dus alleen de cijfer-vorm rendert. Emissive = helder wit => het
    # cijfer licht zelf op, ongeacht omgevingslicht.
    white = mel.create_material_expression(dm, unreal.MaterialExpressionConstant3Vector, -350, -200)
    white.set_editor_property("constant", unreal.LinearColor(1.7, 1.7, 1.8, 1.0))
    mel.connect_material_property(white, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(dm)
    unreal.EditorAssetLibrary.save_asset(DST)
    w("M_DigitTextGlow klaar: shading=%s blend=%s" % (dm.get_editor_property("shading_model"), dm.get_editor_property("blend_mode")))
except Exception:
    w("EXC:\n" + traceback.format_exc())
with open(OUT, "w") as f: f.write("\n".join(lines))

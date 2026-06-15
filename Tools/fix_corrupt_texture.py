# Fix: T_ElectricalBoxes01_N heeft corrupte bron-mipdata (cook faalt: "decompressed buffer 0 != 64MB").
# Oplossing: laat MI_ElectricalBoxes01 z'n normal-map verwijzen naar de wel-werkende sibling
# T_ElectricalBoxes02_N. De corrupte texture raakt zo ongerefereerd -> wordt niet gecookt -> geen build-fout.
import unreal

MI   = "/Game/CityBeachStrip/Materials/ElectricalBoxes/MI_ElectricalBoxes01"
GOOD = "/Game/CityBeachStrip/Textures/ElectricalBoxes/T_ElectricalBoxes02_N"
BAD_SUBSTR = "T_ElectricalBoxes01_N"

mi   = unreal.EditorAssetLibrary.load_asset(MI)
good = unreal.EditorAssetLibrary.load_asset(GOOD)
mel  = unreal.MaterialEditingLibrary

if mi is None or good is None:
    unreal.log_error("FIX: kon MI of GOOD-texture niet laden")
else:
    changed = 0
    # Haal de exacte parameter-naam uit de bestaande override die naar de corrupte texture wijst,
    # en zet 'm via de canonieke MEL-API (die markeert + persisteert correct).
    tpvs = mi.get_editor_property("texture_parameter_values")
    for tpv in tpvs:
        val = tpv.get_editor_property("parameter_value")
        if val is not None and BAD_SUBSTR in val.get_name():
            pinfo = tpv.get_editor_property("parameter_info")
            pname = pinfo.get_editor_property("name")
            unreal.log("FIX: override-param '%s' wijst naar corrupt -> repoint" % pname)
            mel.set_material_instance_texture_parameter_value(mi, pname, good)
            changed += 1

    # Fallback: als de param geen override is, scan alle texture-params van het basismateriaal.
    if changed == 0:
        base = mi
        while isinstance(base, unreal.MaterialInstance):
            base = base.get_editor_property("parent")
        names = mel.get_texture_parameter_names(base) if base else []
        for n in names:
            cur = mel.get_material_instance_texture_parameter_value(mi, n)
            if cur is not None and BAD_SUBSTR in cur.get_name():
                unreal.log("FIX: basis-param '%s' wijst naar corrupt -> repoint" % n)
                mel.set_material_instance_texture_parameter_value(mi, n, good)
                changed += 1

    mel.update_material_instance(mi)
    saved = unreal.EditorAssetLibrary.save_asset(MI, only_if_is_dirty=False)
    unreal.log("FIX: repointed %d param(s) naar T_ElectricalBoxes02_N | saved=%s" % (changed, saved))

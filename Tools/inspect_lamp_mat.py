import unreal

for mp in ["/Game/CityBeachStrip/Meshes/CeilingProps/SM_CeilingLight02.SM_CeilingLight02",
           "/Game/CityBeachStrip/Meshes/CeilingProps/SM_CeilingLight.SM_CeilingLight"]:
    m = unreal.EditorAssetLibrary.load_asset(mp)
    if not m:
        # probeer zonder submap
        for alt in ["/Game/CityBeachStrip/Meshes/SM_CeilingLight02.SM_CeilingLight02"]:
            m = unreal.EditorAssetLibrary.load_asset(alt)
            if m: break
    if not m:
        unreal.log("MESH niet gevonden: " + mp); continue
    unreal.log("=== MESH: " + m.get_name())
    mats = m.get_editor_property("static_materials")
    mel = unreal.MaterialEditingLibrary
    for sm in mats:
        mi = sm.get_editor_property("material_interface")
        if not mi: continue
        base = mi.get_base_material() if hasattr(mi, "get_base_material") else mi
        unreal.log("  MATERIAL: " + mi.get_name() + " (base " + (base.get_name() if base else "?") + ")")
        try:
            sn = mel.get_scalar_parameter_names(base)
            vn = mel.get_vector_parameter_names(base)
            unreal.log("    scalars: " + ", ".join([str(x) for x in sn]))
            unreal.log("    vectors: " + ", ".join([str(x) for x in vn]))
        except Exception as e:
            unreal.log("    params dump faalde: " + str(e))

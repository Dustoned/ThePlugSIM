import unreal, traceback
OUT = "C:/temp/elev_out.txt"
lines = []
def w(s): lines.append(str(s))
try:
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    mel = unreal.MaterialEditingLibrary
    base_dir = "/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/"
    for d in ["7", "0", "1", "8"]:
        mp = base_dir + ("SM_ElevatorNumber_%s.SM_ElevatorNumber_%s" % (d, d))
        m = unreal.EditorAssetLibrary.load_asset(mp)
        if not m: w("NOT FOUND " + mp); continue
        try: nv = sub.get_number_verts(m, 0)
        except Exception as e: nv = "err:" + str(e)
        w("MESH %s  verts(LOD0)=%s" % (m.get_name(), nv))
    # MI_ElevatorEmmsive: de pack-emissive voor deze cijfers.
    em = unreal.EditorAssetLibrary.load_asset("/Game/CityBeachStrip/Materials/Architecture/Interiors/Elevator/MI_ElevatorEmmsive.MI_ElevatorEmmsive")
    if em:
        base = em.get_base_material()
        w("MI_ElevatorEmmsive base=%s shading=%s" % (base.get_name() if base else "?", base.get_editor_property("shading_model") if base else "?"))
        w("  scalars: " + ", ".join(str(x) for x in mel.get_scalar_parameter_names(base)))
        w("  vectors: " + ", ".join(str(x) for x in mel.get_vector_parameter_names(base)))
        w("  tex: " + ", ".join(str(x) for x in mel.get_texture_parameter_names(base)))
        # huidige instance-waarden van emissive-achtige params
        for s in mel.get_scalar_parameter_names(base):
            try: w("    scalar[%s]=%s" % (s, mel.get_material_instance_scalar_parameter_value(em, s)))
            except Exception: pass
        for v in mel.get_vector_parameter_names(base):
            try: w("    vector[%s]=%s" % (v, mel.get_material_instance_vector_parameter_value(em, v)))
            except Exception: pass
except Exception:
    w("EXC:\n" + traceback.format_exc())
with open(OUT, "w") as f: f.write("\n".join(lines))

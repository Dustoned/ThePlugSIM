import unreal, traceback
OUT = "C:/temp/clothparams.txt"
lines = []
def w(s): lines.append(str(s))
mel = unreal.MaterialEditingLibrary
SKINS = [
    "/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Karl_A",
    "/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Tony_A",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_3/SK_Casual_3",
]
CLOTH = ["cloth","top","jean","pant","short","hoodie","shirt","sweater","jacket","set","wear","suit","sneak","shoe","panama","hat","cap"]
try:
    for sp in SKINS:
        m = unreal.EditorAssetLibrary.load_asset(sp)
        if not m: w("MISS "+sp); continue
        w("=== "+m.get_name())
        mats = m.get_editor_property("materials")
        for i, s in enumerate(mats):
            mi = s.material_interface
            if not mi: continue
            nm = mi.get_name()
            if not any(c in nm.lower() for c in CLOTH): continue
            base = mi.get_base_material() if hasattr(mi,"get_base_material") else mi
            try:
                vec = [str(x) for x in mel.get_vector_parameter_names(base)]
                sca = [str(x) for x in mel.get_scalar_parameter_names(base)]
            except Exception as e:
                vec=["err"]; sca=[str(e)]
            w("  slot %d  %s  (base %s)  vectors=%s" % (i, nm, base.get_name() if base else "?", vec))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

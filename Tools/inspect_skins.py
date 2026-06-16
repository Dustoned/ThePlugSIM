import unreal, traceback
OUT = "C:/temp/skins.txt"
lines = []
def w(s): lines.append(str(s))
CANDIDATES = [
    "/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_2/SK_Casual_2",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_3/SK_Casual_3",
    "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1",
    "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_2",
    "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_3",
    "/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Karl_A",
    "/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Tony_A",
]
try:
    for p in CANDIDATES:
        m = unreal.EditorAssetLibrary.load_asset(p)
        if not m:
            w("MISS " + p); continue
        sk = m.get_editor_property("skeleton")
        skn = sk.get_name() if sk else "?"
        mats = m.get_editor_property("materials")
        mnames = ", ".join([ (s.material_interface.get_name() if s.material_interface else "None") for s in mats ][:6])
        w("%-26s skel=%-26s mats=[%s]" % (m.get_name(), skn, mnames))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

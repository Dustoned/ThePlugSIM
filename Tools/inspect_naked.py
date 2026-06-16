import unreal, traceback
OUT = "C:/temp/naked.txt"
lines = []
def w(s): lines.append(str(s))
POOL = [
    "/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Karl_A",
    "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1",
    "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_2",
    "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_3",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_2/SK_Casual_2",
    "/Game/Casual_Wear_Pack1/Mesh/Casual_3/SK_Casual_3",
]
CLOTH = ["cloth","top","jean","pant","short","hoodie","shirt","sweater","jacket","dress","skirt","shoe","sneak","sock","set","wear","suit","underwear"]
try:
    for p in POOL:
        m = unreal.EditorAssetLibrary.load_asset(p)
        if not m: w("MISS "+p); continue
        mats = m.get_editor_property("materials")
        names = [ (s.material_interface.get_name() if s.material_interface else "None") for s in mats ]
        has_cloth = any(any(c in n.lower() for c in CLOTH) for n in names)
        w("%-24s CLOTH=%s  mats=%s" % (m.get_name(), has_cloth, names))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

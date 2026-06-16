import unreal, traceback
OUT = "C:/temp/tony.txt"
lines = []
def w(s): lines.append(str(s))
B = "/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Tony/SK_Citizens_Pack_Tony_"
PARTS = ["Body","Body_Cloth","Tshirt","Shirt","Pants","Shorts","Shoes","Sneakers","Cap","Hat","Panama","Glasses","Watch","Handkerchief"]
try:
    for p in PARTS:
        m = unreal.EditorAssetLibrary.load_asset(B+p)
        if not m: w("MISS "+p); continue
        sk = m.get_editor_property("skeleton")
        mats = m.get_editor_property("materials")
        mn = ", ".join([(s.material_interface.get_name() if s.material_interface else "None") for s in mats])
        w("%-14s skel=%-28s mats=%d [%s]" % (p, sk.get_name() if sk else "?", len(mats), mn))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

import unreal, traceback
OUT = "C:/temp/more.txt"
lines = []
def w(s): lines.append(str(s))
mel = unreal.MaterialEditingLibrary
B = "/Game/Casual_Wear_Pack1/Mesh/Parts/"
# Haar-materiaal-params (voor random haarkleur)
HAIR = [B+"Hairs/SK_HairShort", B+"Hairs/SK_Hair_Medium_1", B+"Hairs/SK_Hair_Braid"]
# Kandidaat extra tops
TOPS = [B+"Cloth/Torso/SK_Top_1_Optimized_Outerwear", B+"Cloth/Torso/SK_Top_2_Optimized_Outerwear",
        B+"Cloth/Torso/SK_Top_1_Optimized_Shirt", B+"Cloth/Torso/SK_Top_1_Optimized"]
try:
    w("--- HAAR materiaal-params ---")
    for p in HAIR:
        m = unreal.EditorAssetLibrary.load_asset(p)
        if not m: w("MISS "+p); continue
        for s in m.get_editor_property("materials"):
            mi = s.material_interface
            if not mi: continue
            base = mi.get_base_material() if hasattr(mi,"get_base_material") else mi
            try: vec=[str(x) for x in mel.get_vector_parameter_names(base)]
            except: vec=["?"]
            w("%-18s mat=%-18s base=%-14s vectors=%s" % (m.get_name(), mi.get_name(), base.get_name() if base else "?", vec))
    w("--- KANDIDAAT TOPS ---")
    for p in TOPS:
        m = unreal.EditorAssetLibrary.load_asset(p)
        if not m: w("MISS "+p); continue
        sk = m.get_editor_property("skeleton")
        mats = m.get_editor_property("materials")
        mn = ", ".join([(s.material_interface.get_name() if s.material_interface else "None") for s in mats])
        w("%-32s skel=%-26s mats=[%s]" % (m.get_name(), sk.get_name() if sk else "?", mn))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

import unreal, traceback
OUT = "C:/temp/modular.txt"
lines = []
def w(s): lines.append(str(s))
B = "/Game/Casual_Wear_Pack1/Mesh/Parts/"
PARTS = {
 "BODY": [B+"Bodys/SK_Body"],
 "HEAD": [B+"Heads/SK_Head_Casual_1", B+"Heads/SK_Head_Casual_2", B+"Heads/SK_Head_Casual_3"],
 "TOP":  [B+"Cloth/Torso/SK_Top_1", B+"Cloth/Torso/SK_Top_2", B+"Cloth/Torso/SK_Hoodies_Mini"],
 "LEGS": [B+"Cloth/Legs/SK_Baggy_Jeans", B+"Cloth/Legs/SK_Wide_Leg_Jeans", B+"Cloth/Legs/SK_Shorts_1"],
 "SHOE": [B+"Cloth/Shoes/SK_Sneakers_2", B+"Cloth/Shoes/SK_Sneakers_4", B+"Cloth/Shoes/SK_Sneakers_5"],
 "HAIR": [B+"Hairs/SK_HairShort", B+"Hairs/SK_Hair_Braid", B+"Hairs/SK_Hair_Medium_1"],
}
try:
    for cat, paths in PARTS.items():
        for p in paths:
            m = unreal.EditorAssetLibrary.load_asset(p)
            if not m:
                w("%-5s MISS %s" % (cat, p)); continue
            sk = m.get_editor_property("skeleton")
            mats = m.get_editor_property("materials")
            w("%-5s %-22s skel=%-26s mats=%d" % (cat, m.get_name(), sk.get_name() if sk else "?", len(mats)))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

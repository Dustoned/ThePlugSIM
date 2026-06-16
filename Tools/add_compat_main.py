import unreal, traceback
OUT="C:/temp/compatmain.txt"; L=[]
def w(s): L.append(str(s))
try:
    man = unreal.EditorAssetLibrary.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple").get_editor_property("skeleton")
    casual = unreal.EditorAssetLibrary.load_asset("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1").get_editor_property("skeleton")
    w("man=%s casual=%s" % (man.get_name(), casual.get_name()))
    def add_compat(sk, other):
        cur = list(sk.get_editor_property("compatible_skeletons"))
        names = [c.get_name() for c in cur if c]
        if other.get_name() not in names:
            cur.append(other)
            sk.set_editor_property("compatible_skeletons", cur)
            unreal.EditorAssetLibrary.save_loaded_asset(sk, only_if_is_dirty=False)
            w("ADDED %s -> %s; now %s" % (other.get_name(), sk.get_name(), [c.get_name() for c in cur if c]))
        else:
            w("already: %s in %s" % (other.get_name(), sk.get_name()))
    # beide richtingen voor de zekerheid
    add_compat(man, casual)     # SK_Mannequin krijgt UE4_Mannequin_Skeleton_Main
    add_compat(casual, man)     # en andersom
except Exception:
    w("EXC\n"+traceback.format_exc())
open(OUT,"w").write("\n".join(L))

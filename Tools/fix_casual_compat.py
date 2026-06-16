import unreal, traceback
OUT = "C:/temp/compat.txt"
lines = []
def w(s): lines.append(str(s))

# Het animatie-skelet dat de NPC's gebruiken (single-node MM_Idle/MF_Walk).
ANIM = unreal.EditorAssetLibrary.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")
ANIM_SKEL = ANIM.get_editor_property("skeleton") if ANIM else None

# Het skelet dat de WERKENDE Casual-fullbody gebruikt (animeert al -> referentie).
MAIN = unreal.EditorAssetLibrary.load_asset("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1")
MAIN_SKEL = MAIN.get_editor_property("skeleton") if MAIN else None

def compat_names(skel):
    try:
        cs = skel.get_editor_property("compatible_skeletons")
        return [ (c.get_name() if c else "?") for c in (cs or []) ]
    except Exception as e:
        return ["err:" + str(e)]

try:
    w("ANIM skelet = %s" % (ANIM_SKEL.get_name() if ANIM_SKEL else "?"))
    w("MAIN (werkend) skelet = %s  compat=%s" % (MAIN_SKEL.get_name() if MAIN_SKEL else "?", compat_names(MAIN_SKEL) if MAIN_SKEL else "?"))

    targets = [
        "/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1",
        "/Game/Casual_Wear_Pack1/Mesh/Casual_2/SK_Casual_2",
        "/Game/Casual_Wear_Pack1/Mesh/Casual_3/SK_Casual_3",
    ]
    done = set()
    for p in targets:
        m = unreal.EditorAssetLibrary.load_asset(p)
        if not m: w("MISS " + p); continue
        sk = m.get_editor_property("skeleton")
        if not sk or sk.get_name() in done: continue
        done.add(sk.get_name())
        before = compat_names(sk)
        # Voeg het ANIM-skelet toe als compatible (zodat de single-node anims spelen), idempotent.
        cs = list(sk.get_editor_property("compatible_skeletons") or [])
        names = [c.get_name() for c in cs if c]
        if ANIM_SKEL and ANIM_SKEL.get_name() not in names:
            cs.append(ANIM_SKEL)
        if MAIN_SKEL and MAIN_SKEL.get_name() not in [c.get_name() for c in cs if c]:
            cs.append(MAIN_SKEL)
        sk.set_editor_property("compatible_skeletons", cs)
        unreal.EditorAssetLibrary.save_loaded_asset(sk)
        w("%s compat: %s -> %s" % (sk.get_name(), before, compat_names(sk)))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

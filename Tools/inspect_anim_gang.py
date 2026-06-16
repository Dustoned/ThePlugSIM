import unreal, traceback
OUT = "C:/temp/animgang.txt"
lines = []
def w(s): lines.append(str(s))
try:
    # --- Anim-pack: skelet + compat ---
    w("=== GenericNPCAnimPack2 idle anims ===")
    idles = ["Anim_Idle","Anim_Idle_Hands_Crossed","Anim_Idle_Hands_On_Waist",
             "Anim_Idle_Lean_Forward_1","Anim_Idle_Wall_1","Anim_Check_Cellphone","Anim_Phone_Talking_Big"]
    base = "/Game/GenericNPCAnimPack2/Demo/Character/"
    # vind de echte anim-map
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = ar.get_assets_by_path("/Game/GenericNPCAnimPack2", recursive=True)
    anim_paths = {}
    for a in assets:
        cls = str(a.asset_class_path.asset_name) if hasattr(a,"asset_class_path") else str(a.asset_class)
        if cls == "AnimSequence":
            anim_paths[str(a.asset_name)] = str(a.package_name)
    w("AnimSequence count in pack: %d" % len(anim_paths))
    sk_seen = set()
    for nm in idles:
        p = anim_paths.get(nm)
        if not p: w("  MISS %s" % nm); continue
        a = unreal.EditorAssetLibrary.load_asset(p)
        if not a: w("  LOADFAIL %s" % nm); continue
        sk = a.get_editor_property("skeleton")
        sk_seen.add(sk.get_name() if sk else "?")
        w("  %-26s skel=%s" % (nm, sk.get_name() if sk else "?"))
    # compat-skeletons van de pack-skelet + onze NPC-skeletten
    w("\n=== Skeletons + compatible lists ===")
    SKELS = ["/Game/GenericNPCAnimPack2/Demo/Character/Mesh/UE4_Mannequin_Skeleton",
             "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/SK_Body",
             "/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Tony/SK_Citizens_Pack_Tony_Body",
             "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"]
    for sp in SKELS:
        a = unreal.EditorAssetLibrary.load_asset(sp)
        if not a: w("  MISS %s" % sp); continue
        sk = a if isinstance(a, unreal.Skeleton) else a.get_editor_property("skeleton")
        nm = sk.get_name() if sk else "?"
        comp = []
        try:
            cs = sk.get_editor_property("compatible_skeletons")
            comp = [str(c.get_name()) for c in cs]
        except Exception as e:
            comp = ["<no compatible_skeletons prop: %s>" % e]
        w("  %-40s skel=%-30s compat=%s" % (sp.split('/')[-1], nm, comp))

    # --- M_Gang ---
    w("\n=== GangSkin M_Gang ===")
    mg = unreal.EditorAssetLibrary.load_asset("/Game/GangSkin/M_Gang")
    if mg:
        w("class=%s" % type(mg).__name__)
        base_m = mg.get_base_material() if hasattr(mg,"get_base_material") else mg
        w("base=%s" % (base_m.get_name() if base_m else "?"))
        try:
            tex = unreal.MaterialEditingLibrary.get_texture_parameter_names(base_m)
            w("texture params=%s" % [str(t) for t in tex])
        except Exception as e:
            w("texparam err %s" % e)
        try:
            vec = unreal.MaterialEditingLibrary.get_vector_parameter_names(base_m)
            w("vector params=%s" % [str(t) for t in vec])
        except Exception as e:
            w("vecparam err %s" % e)
    else:
        w("M_Gang load fail")
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

import unreal, traceback
OUT = "C:/temp/compat.txt"
lines = []
def w(s): lines.append(str(s))
try:
    ue4 = unreal.EditorAssetLibrary.load_asset("/Game/GenericNPCAnimPack2/Demo/Character/Mesh/UE4_Mannequin_Skeleton")
    w("ue4 skel loaded: %s" % (ue4.get_name() if ue4 else None))
    targets = [
        "/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Tony/SK_Citizens_Pack_Tony_Body",  # -> skeleton
        "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/SK_Body",
        "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    ]
    skels = []
    for t in targets:
        a = unreal.EditorAssetLibrary.load_asset(t)
        sk = a if isinstance(a, unreal.Skeleton) else a.get_editor_property("skeleton")
        if sk and sk not in skels: skels.append(sk)
    # ook de Casual head/citizens karl skelet voor de zekerheid
    for extra in ["/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Karl/SK_Citizens_Pack_Karl_Body",
                  "/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1"]:
        a = unreal.EditorAssetLibrary.load_asset(extra)
        if a:
            sk = a.get_editor_property("skeleton")
            if sk and sk not in skels: skels.append(sk)

    for sk in skels:
        try:
            cur = list(sk.get_editor_property("compatible_skeletons"))
        except Exception as e:
            cur = []
            w("  read compat fail %s: %s" % (sk.get_name(), e))
        names = [c.get_name() for c in cur if c]
        if "UE4_Mannequin_Skeleton" not in names:
            cur.append(ue4)
            try:
                sk.set_editor_property("compatible_skeletons", cur)
                unreal.EditorAssetLibrary.save_loaded_asset(sk, only_if_is_dirty=False)
                w("  ADDED compat to %s -> now %s" % (sk.get_name(), [c.get_name() for c in cur if c]))
            except Exception as e:
                w("  SET fail %s: %s" % (sk.get_name(), e))
        else:
            w("  already compat: %s" % sk.get_name())

    # idle-anim package paths (standing idles)
    w("\n=== standing idle package paths ===")
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    want = ["Anim_Idle","Anim_Idle_Hands_Crossed","Anim_Idle_Hands_On_Waist",
            "Anim_Idle_Hand_On_Waist","Anim_Idle_Lean_Forward_1","Anim_Idle_Lean_Forward_2",
            "Anim_Idle_Lean_Forward_3"]
    assets = ar.get_assets_by_path("/Game/GenericNPCAnimPack2", recursive=True)
    for a in assets:
        nm = str(a.asset_name)
        if nm in want:
            w('  TEXT("%s.%s"),' % (str(a.package_name), nm))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

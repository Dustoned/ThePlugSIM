import unreal, traceback
OUT = "C:/temp/playeranim.txt"
L=[]
def w(s): L.append(str(s))
mel = unreal.MaterialEditingLibrary
def skel_of(p):
    a = unreal.EditorAssetLibrary.load_asset(p)
    if not a: return None, "MISS"
    sk = a.get_editor_property("skeleton")
    return sk, (sk.get_name() if sk else "?")
try:
    # 1) Skeletten van de skins
    w("=== skin meshes -> skeleton ===")
    for p in ["/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1",
              "/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_2",
              "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
              "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple",
              "/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Tony/SK_Citizens_Pack_Tony_Body"]:
        sk,nm = skel_of(p)
        w("%-58s %s" % (p.split('/')[-1], nm))
    # 2) ABP_Unarmed -> welk skelet
    w("\n=== ABP's -> target skeleton ===")
    for p in ["/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed",
              "/Game/Sample/Demo/ThirdPersonTemplate/Characters/Mannequins/Animations/ABP_Manny"]:
        a = unreal.EditorAssetLibrary.load_asset(p)
        if not a: w("%-30s MISS" % p.split('/')[-1]); continue
        sk = a.get_editor_property("target_skeleton") if hasattr(a,"get_editor_property") else None
        try: skn = sk.get_name() if sk else "?"
        except: skn="?"
        w("%-30s target=%s class=%s" % (p.split('/')[-1], skn, type(a).__name__))
    # 3) compatible-skeletons van de Casual-skelet + Mannequin-skelet
    w("\n=== compatible skeletons ===")
    for p in ["/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/FullBody/SK_FullBody_Casual_1",
              "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"]:
        sk,nm = skel_of(p)
        if not sk: w("%s: no skel" % p); continue
        try:
            cs = [c.get_name() for c in sk.get_editor_property("compatible_skeletons")]
        except Exception as e:
            cs = ["err %s"%e]
        w("%-26s compat=%s" % (nm, cs))
    # 4) zoek of de Casual-pack een eigen ABP heeft
    w("\n=== ABP/AnimBlueprint assets in Casual_Wear_Pack1 ===")
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in ar.get_assets_by_path("/Game/Casual_Wear_Pack1", recursive=True):
        cls = str(a.asset_class_path.asset_name) if hasattr(a,"asset_class_path") else ""
        if "AnimBlueprint" in cls or "ABP" in str(a.asset_name):
            w("  %s  (%s)" % (a.asset_name, cls))
except Exception:
    w("EXC\n"+traceback.format_exc())
open(OUT,"w").write("\n".join(L))

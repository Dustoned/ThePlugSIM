# strip_npc_packs.py - integratie-pass voor de Citizens Pack (4.22) + Generic NPC Anim Pack (4.27).
# Zelfde aanpak als strip_casual_pack.py:
#  1) Resave Mesh/Materials/Animations (oude engine -> huidige, nodig voor runtime -game load)
#  2) used_with_skeletal_mesh op de master-materials (anders grijze default material)
#  3) Skeletons compatible maken met SK_Mannequin (beide kanten) zodat:
#       - onze speler-AnimBP de Citizens (Tony) animeert
#       - de 55 generic anims op de speler/Casual-girls/Tony draaien
import unreal

MANNEQUIN = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"

# Mappen om te resaven (scoped: meshes/materials/animations + de skelet-meshes).
RESAVE_PATHS = [
    "/Game/Citizens_Pack/Meshes",
    "/Game/Citizens_Pack/Materials",
    "/Game/GenericNPCAnimPack2/Animations",
    "/Game/GenericNPCAnimPack2/Demo/Character/Mesh",
]

# Skeletons die compatible moeten worden met SK_Mannequin.
SKELETONS = [
    "/Game/Citizens_Pack/Meshes/SKEL_Citizens_Pack_Skeleton",
    "/Game/GenericNPCAnimPack2/Demo/Character/Mesh/UE4_Mannequin_Skeleton",
]

ar = unreal.AssetRegistryHelpers.get_asset_registry()

pkgs = []
for path in RESAVE_PATHS:
    for a in ar.get_assets_by_path(path, recursive=True):
        pkgs.append(str(a.package_name))
pkgs = sorted(set(pkgs))
unreal.log_warning("NPCPACKS: %d packages om te resaven" % len(pkgs))

ok = fail = mats = 0
for p in pkgs:
    obj = unreal.load_asset(p)
    if not obj:
        unreal.log_warning("load FAIL: %s" % p)
        fail += 1
        continue
    if isinstance(obj, unreal.Material):
        try:
            obj.set_editor_property("used_with_skeletal_mesh", True)
            unreal.MaterialEditingLibrary.recompile_material(obj)
            mats += 1
        except Exception as e:
            unreal.log_warning("usage FAIL %s: %s" % (p, e))
    if unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False):
        ok += 1
    else:
        unreal.log_warning("save FAIL: %s" % p)
        fail += 1
unreal.log_warning("NPCPACKS resave klaar: ok=%d fail=%d masterMats=%d" % (ok, fail, mats))

mann = unreal.load_asset(MANNEQUIN)
for skp in SKELETONS:
    sk = unreal.load_asset(skp)
    if not sk or not mann:
        unreal.log_warning("skeleton FAIL: %s (sk=%s mann=%s)" % (skp, bool(sk), bool(mann)))
        continue
    try:
        sk.add_compatible_skeleton(mann)
        mann.add_compatible_skeleton(sk)
        unreal.EditorAssetLibrary.save_asset(skp, only_if_is_dirty=False)
        unreal.log_warning("compatible OK: %s <-> SK_Mannequin" % skp)
    except Exception as e:
        unreal.log_warning("compatible FAIL %s: %s" % (skp, e))
unreal.EditorAssetLibrary.save_asset(MANNEQUIN, only_if_is_dirty=False)
unreal.log_warning("NPCPACKS ALLES KLAAR")

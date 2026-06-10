# strip_casual_pack.py - "asset freaks stripper" pass voor de Casual Wear Girls Pack 1.
# 1) Resave alle Mesh/Materials-assets (4.27 -> huidige engine-versie, nodig voor runtime -game load)
# 2) bUsedWithSkeletalMesh op alle master-materials (anders grijze default material, Lola-les)
# 3) Skeletons compatible maken met onze SK_Mannequin (beide kanten) zodat onze AnimBP ze animeert
import unreal

PACK = "/Game/Casual_Wear_Pack1"
MANNEQUIN = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"

ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = list(ar.get_assets_by_path(PACK + "/Mesh", recursive=True))
assets += list(ar.get_assets_by_path(PACK + "/Materials", recursive=True))
pkgs = sorted(set(str(a.package_name) for a in assets))
unreal.log_warning("STRIP: %d packages onder Mesh/Materials" % len(pkgs))

ok = fail = 0
mats = 0
for p in pkgs:
    obj = unreal.load_asset(p)
    if not obj:
        unreal.log_warning("STRIP load FAIL: %s" % p)
        fail += 1
        continue
    if isinstance(obj, unreal.Material):
        try:
            obj.set_editor_property("used_with_skeletal_mesh", True)
            unreal.MaterialEditingLibrary.recompile_material(obj)
            mats += 1
        except Exception as e:
            unreal.log_warning("STRIP usage FAIL %s: %s" % (p, e))
    if unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False):
        ok += 1
    else:
        unreal.log_warning("STRIP save FAIL: %s" % p)
        fail += 1
unreal.log_warning("STRIP resave klaar: ok=%d fail=%d masterMats=%d" % (ok, fail, mats))

mann = unreal.load_asset(MANNEQUIN)
for i in (1, 2, 3):
    skp = "%s/Mesh/Casual_%d/UE4_Mannequin_Skeleton_Casual_%d" % (PACK, i, i)
    sk = unreal.load_asset(skp)
    if not sk or not mann:
        unreal.log_warning("STRIP skeleton FAIL: %s (sk=%s mann=%s)" % (skp, bool(sk), bool(mann)))
        continue
    try:
        sk.add_compatible_skeleton(mann)
        mann.add_compatible_skeleton(sk)
        unreal.EditorAssetLibrary.save_asset(skp, only_if_is_dirty=False)
        unreal.log_warning("STRIP compatible OK: Casual_%d <-> SK_Mannequin" % i)
    except Exception as e:
        unreal.log_warning("STRIP compatible FAIL %s: %s" % (skp, e))
unreal.EditorAssetLibrary.save_asset(MANNEQUIN, only_if_is_dirty=False)
unreal.log_warning("STRIP ALLES KLAAR")

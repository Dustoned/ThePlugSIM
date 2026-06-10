# strip_citybeach.py - asset-pass voor de CityBeachStrip-pack: resave Meshes/Materials/Blueprints
# naar de huidige engine-versie (runtime -game load). Textures/Maps slaan we over (groot; laden on-demand).
import unreal

PACK = "/Game/CityBeachStrip"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = []
for Sub in ("/Meshes", "/Materials", "/Blueprints", "/LandscapeLayers"):
    assets += list(ar.get_assets_by_path(PACK + Sub, recursive=True))
pkgs = sorted(set(str(a.package_name) for a in assets))
# .umap-level-instances in Meshes-mappen overslaan (alleen .uasset packages resaven)
unreal.log_warning("CITYSTRIP: %d packages" % len(pkgs))
ok = fail = 0
for p in pkgs:
    obj = unreal.load_asset(p)
    if not obj:
        fail += 1
        continue
    if unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False):
        ok += 1
    else:
        unreal.log_warning("CITYSTRIP save FAIL: %s" % p)
        fail += 1
unreal.log_warning("CITYSTRIP klaar: ok=%d fail=%d" % (ok, fail))

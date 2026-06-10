# fix_glass_collision.py - CityBeachStrip: glas-/raam-meshes hebben vaak geen simple collision (platte
# vlakken) waardoor je er dwars doorheen loopt. Voeg BOX-collision toe aan elke Glass/Window-mesh
# zonder collision-primitives en resave.
import unreal

sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game/CityBeachStrip/Meshes", recursive=True)

checked = fixed = 0
for a in assets:
    name = str(a.asset_name)
    low = name.lower()
    if "glass" not in low and "window" not in low:
        continue
    sm = unreal.load_asset(str(a.package_name))
    if not isinstance(sm, unreal.StaticMesh):
        continue
    checked += 1
    try:
        n = sub.get_simple_collision_count(sm)
    except Exception:
        n = -1
    if n == 0:
        try:
            sub.add_simple_collisions(sm, unreal.ScriptingCollisionShapeType.BOX)
            unreal.EditorAssetLibrary.save_asset(str(a.package_name), only_if_is_dirty=False)
            fixed += 1
        except Exception as e:
            unreal.log_warning("GLASSFIX FAIL %s: %s" % (name, e))
unreal.log_warning("GLASSFIX klaar: %d glas/raam-meshes gecheckt, %d zonder collision -> BOX toegevoegd" % (checked, fixed))

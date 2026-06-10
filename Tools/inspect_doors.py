# inspect_doors.py - analyseer alle deur-meshes in de CityBeachStrip-pack: afmetingen + pivot-positie,
# zodat de runtime deur-converter weet welke meshes deur-BLADEN zijn en waar het scharnier zit.
import unreal

ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game/CityBeachStrip/Meshes", recursive=True)
for a in assets:
    name = str(a.asset_name)
    if "door" not in name.lower():
        continue
    sm = unreal.load_asset(str(a.package_name))
    if not isinstance(sm, unreal.StaticMesh):
        continue
    bb = sm.get_bounding_box()
    size = bb.max - bb.min
    unreal.log_warning("DOOR %s size=(%.0f,%.0f,%.0f) min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f)" % (
        name, size.x, size.y, size.z, bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z))
unreal.log_warning("DOOR-INSPECT KLAAR")

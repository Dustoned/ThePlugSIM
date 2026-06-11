# inspect_roomparts.py - pivots/maten van de kamer-bouwstenen (voor de RoomBuilder)
import unreal

NAMES = (
    "SM_Floor_4x4m", "SM_Floor_4x3m", "SM_Floor_4x1m", "SM_Floor_1x1m",
    "SM_Ceiling_4x4m", "SM_Ceiling_4x3m", "SM_Ceiling_4x1m", "SM_Ceiling_1x1m",
    "SM_InteriorWall_01_4m", "SM_InteriorWall_01_3m", "SM_InteriorWall_01_2m", "SM_InteriorWall_01_1m",
    "SM_InteriorWall_01_0_5m", "SM_InteriorWall_01_4m_Door01", "SM_InteriorWall_01_3m_Door01",
    "SM_InteriorWall_01_Filler01", "SM_CeilingLight02",
)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game/CityBeachStrip/Meshes", recursive=True)
paths = {str(a.asset_name): str(a.package_name) for a in assets}
for name in NAMES:
    pkg = paths.get(name)
    if not pkg:
        unreal.log_warning("PART %s NIET GEVONDEN" % name)
        continue
    sm = unreal.load_asset(pkg)
    if not isinstance(sm, unreal.StaticMesh):
        continue
    bb = sm.get_bounding_box()
    size = bb.max - bb.min
    unreal.log_warning("PART %s pad=%s size=(%.0f,%.0f,%.0f) min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f)" % (
        name, pkg, size.x, size.y, size.z, bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z))
unreal.log_warning("PARTS KLAAR")

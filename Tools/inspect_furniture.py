import unreal, traceback
OUT = "C:/temp/furn.txt"
lines = []
def w(s): lines.append(str(s))
MESHES = [
    "/Game/CityBeachStrip/Meshes/PlasticChair/SM_PlasticChair",
    "/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_GardenChair",
    "/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_OutsideChair",
    "/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_GardenTable",
    "/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_OutsideTable",
    "/Game/CityBeachStrip/Meshes/Bench/SM_Bench",
    "/Game/CityBeachStrip/Meshes/Plants/SM_DragonTree1",
    "/Game/CityBeachStrip/Meshes/Planters/SM_WoodPlanter01",
    "/Game/CityBeachStrip/Meshes/Pots/SM_Pot03",
    "/Game/CityBeachStrip/Meshes/PlasticCrate/SM_PlasticCrate",
    "/Game/CityBeachStrip/Meshes/Umbrella/SM_Umbrella_Closed",
]
try:
    for p in MESHES:
        m = unreal.EditorAssetLibrary.load_asset(p)
        if not m:
            w("MISS " + p); continue
        bb = m.get_bounding_box()
        # min.z ~ 0 => base-pivot; min.z ~ -height/2 => center-pivot
        w("%-22s min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f)  -> %s" % (
            m.get_name(), bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z,
            "BASE" if abs(bb.min.z) < max(2.0, (bb.max.z-bb.min.z)*0.15) else "CENTER/other"))
except Exception:
    w("EXC:\n"+traceback.format_exc())
with open(OUT,"w") as f: f.write("\n".join(lines))

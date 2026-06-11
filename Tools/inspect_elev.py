# inspect_elev.py - exacte bounds/pivots van de lift-meshes (voor frame-gebaseerde uitlijning)
import unreal

for name in ("SM_ElevatorCabin", "SM_ElevatorDoorFrame01", "SM_ElevatorDoor",
             "SM_ElevatorCallButton01", "SM_ElevatorCallButton02", "SM_ElevatorNumber_5"):
    path = "/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/%s.%s" % (name, name)
    sm = unreal.load_asset(path)
    if not sm:
        unreal.log_warning("ELEV %s NIET GEVONDEN" % name)
        continue
    bb = sm.get_bounding_box()
    size = bb.max - bb.min
    unreal.log_warning("ELEV %s size=(%.0f,%.0f,%.0f) min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f)" % (
        name, size.x, size.y, size.z, bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z))
unreal.log_warning("ELEV-INSPECT KLAAR")

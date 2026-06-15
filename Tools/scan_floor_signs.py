import unreal, traceback
OUT = "C:/temp/signscan.txt"
lines = []
def w(s): lines.append(str(s))
try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level("/Game/CityBeachStrip/Maps/CityBeachStrip")
    acts = unreal.EditorActorSubsystem().get_all_level_actors()
    w("total actors: %d" % len(acts))
    hits = 0
    for a in acts:
        comps = a.get_components_by_class(unreal.StaticMeshComponent)
        for c in comps:
            m = c.get_editor_property("static_mesh")
            if not m: continue
            nm = m.get_name()
            if "ElevatorNumber" in nm or "Number" in nm or ("Elevator" in nm and "Sign" in nm):
                hits += 1
                mats = c.get_materials()
                mnames = ", ".join([(mm.get_name() if mm else "None") for mm in mats])
                loc = a.get_actor_location()
                w("HIT actor=%s mesh=%s mats=[%s] loc=(%.0f,%.0f,%.0f)" % (a.get_name(), nm, mnames, loc.x, loc.y, loc.z))
                if hits >= 40: break
        if hits >= 40: break
    w("ElevatorNumber-mesh hits: %d" % hits)
except Exception:
    w("EXC:\n" + traceback.format_exc())
with open(OUT, "w") as f: f.write("\n".join(lines))

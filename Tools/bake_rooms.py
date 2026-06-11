# bake_rooms.py - bakt de runtime-gebouwde kamers PERMANENT in de CityBeachStrip-map.
# Leest Saved/RoomBake.txt (geexporteerd door de game: SPAWN-regels met mesh/transform/materialen)
# en spawnt die als echte StaticMeshActors in de persistent map, daarna save. HIDE-regels worden
# overgeslagen (parallax-glas verbergen blijft runtime: die comps zitten in gedeelde level-instances
# die we niet veilig kunnen bewerken). Idempotent: bestaat er al een actor met dezelfde mesh op
# dezelfde plek, dan wordt 'ie overgeslagen.
import unreal, os

MAP_PATH = "/Game/CityBeachStrip/Maps/CityBeachStrip"
BAKE_FILE = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "RoomBake.txt")

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP_PATH)

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Bestaande actors indexeren (mesh-naam + positie op 10cm) voor idempotentie.
existing = set()
for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
    c = a.static_mesh_component
    if not c or not c.static_mesh:
        continue
    l = a.get_actor_location()
    existing.add((c.static_mesh.get_name(), round(l.x / 10), round(l.y / 10), round(l.z / 10)))

spawned = 0
skipped = 0
with open(BAKE_FILE, encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line.startswith("SPAWN|"):
            continue
        parts = line.split("|")
        if len(parts) < 6:
            continue
        mesh_path, pos_s, rot_s, scale_s, mats_s = parts[1], parts[2], parts[3], parts[4], parts[5]
        px, py, pz = [float(v) for v in pos_s.split(",")]
        rp, ry, rr = [float(v) for v in rot_s.split(",")]
        sx, sy, sz = [float(v) for v in scale_s.split(",")]
        mesh = unreal.load_asset(mesh_path.split(".")[0])
        if not mesh:
            continue
        key = (mesh.get_name(), round(px / 10), round(py / 10), round(pz / 10))
        if key in existing:
            skipped += 1
            continue
        existing.add(key)
        actor = actor_sub.spawn_actor_from_object(mesh, unreal.Vector(px, py, pz), unreal.Rotator(rp, ry, rr))
        if not actor:
            continue
        actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))
        actor.set_folder_path("BakedRooms")
        comp = actor.static_mesh_component
        if comp:
            for mi, mp in enumerate(mats_s.split(";")):
                if mp and mp != "-":
                    mat = unreal.load_asset(mp.split(".")[0]) if "." in mp else unreal.load_asset(mp)
                    if mat:
                        comp.set_material(mi, mat)
        spawned += 1

unreal.log_warning("BAKE: %d actors gespawnd, %d bestonden al" % (spawned, skipped))
les.save_current_level()
unreal.log_warning("BAKE: map opgeslagen")

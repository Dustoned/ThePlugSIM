# bake_rooms.py - bakt de runtime-gebouwde kamers in ONZE EIGEN level-file
# /Game/_Project/Maps/BakedRooms.umap (licht, geen world partition, ships mee in packaged builds).
# De game laadt die runtime als level-instance over de beach-map heen. De grote pack-map zelf
# blijft onaangeraakt (headless bewerken daarvan crasht op sublevel-streaming).
# Idempotent: bestaande gebakken actors worden overgeslagen, dus re-baken na nieuwe kamers kan altijd.
import unreal, os

BAKED_MAP = "/Game/_Project/Maps/BakedRooms"
BAKE_FILE = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "RoomBake.txt")

if unreal.EditorAssetLibrary.does_asset_exist(BAKED_MAP):
    world = unreal.EditorLoadingAndSavingUtils.load_map(BAKED_MAP)
else:
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
if not world:
    raise RuntimeError("kon geen bake-wereld openen")

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

existing = set()
for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
    c = a.static_mesh_component
    if not c or not c.static_mesh:
        continue
    l = a.get_actor_location()
    existing.add((c.static_mesh.get_name(), round(l.x / 10), round(l.y / 10), round(l.z / 10)))

spawned = 0
skipped = 0
current_stamp = None  # stamp-blokken: JOB|STAMP_... staat VOOR zijn SPAWN-regels
with open(BAKE_FILE, encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if line.startswith("JOB|"):
            jid = line.split("|", 1)[1]
            current_stamp = jid if jid.startswith("STAMP_") else None
            continue
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
        # LET OP: unreal.Rotator(...) is (ROLL, PITCH, YAW) - positioneel (pitch, yaw, roll) doorgeven
        # kantelde elk gedraaid stuk in de bake (plafonds rechtop, muren plat). Keyword-args = veilig.
        actor = actor_sub.spawn_actor_from_object(mesh, unreal.Vector(px, py, pz), unreal.Rotator(roll=rr, pitch=rp, yaw=ry))
        if not actor:
            continue
        actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))
        if current_stamp:
            actor.tags = [unreal.Name(current_stamp)]  # window-fix herkent eigen (gebakken) ramen
        comp = actor.static_mesh_component
        if comp:
            for mi, mp in enumerate(mats_s.split(";")):
                if mp and mp != "-":
                    mat = unreal.load_asset(mp.split(".")[0])
                    if mat:
                        comp.set_material(mi, mat)
        spawned += 1

unreal.log_warning("BAKE: %d actors gespawnd, %d bestonden al" % (spawned, skipped))
# Job-ids als GEBAKKEN registreren: de game mag die jobs nooit meer spawnen (alleen raam-verbergen).
baked_jobs_path = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "BakedJobs.txt")
done = set()
if os.path.exists(baked_jobs_path):
    with open(baked_jobs_path, encoding="utf-8") as bf:
        done = {l.strip() for l in bf if l.strip()}
with open(BAKE_FILE, encoding="utf-8") as f:
    for line in f:
        if line.startswith("JOB|"):
            done.add(line.strip().split("|", 1)[1])
with open(baked_jobs_path, "w", encoding="utf-8") as bf:
    bf.write("\n".join(sorted(done)) + "\n")

unreal.EditorLoadingAndSavingUtils.save_map(world, BAKED_MAP)
unreal.log_warning("BAKE: opgeslagen als %s" % BAKED_MAP)

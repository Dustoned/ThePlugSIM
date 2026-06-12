# add_navvolume.py - zet een NavMeshBoundsVolume in BakedRooms.umap (onze overlay op de beach-map).
# Runtime-navmesh (Dynamic + invokers) bouwt dan tiles rond de invokers binnen dit volume.
# Run via volle editor: UnrealEditor.exe <proj> -ExecutePythonScript="Tools\add_navvolume.py"
import unreal

BAKED = "/Game/_Project/Maps/BakedRooms"
world = unreal.EditorLoadingAndSavingUtils.load_map(BAKED)
if not world:
    raise RuntimeError("kon BakedRooms niet openen")

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.NavMeshBoundsVolume)
if existing:
    unreal.log_warning("NavMeshBoundsVolume bestaat al (%d) - niks te doen" % len(existing))
else:
    v = actor_sub.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(-6000, 17000, 800))
    # brush-basis is een 200-kubus: scale (200, 300, 40) = ~40km x 60km x 8km - dekt de hele strip.
    # Met invoker-only generatie is een groot volume gratis (tiles ontstaan alleen rond invokers).
    v.set_actor_scale3d(unreal.Vector(200.0, 300.0, 40.0))
    unreal.log_warning("NavMeshBoundsVolume toegevoegd aan BakedRooms")

unreal.EditorLoadingAndSavingUtils.save_map(world, BAKED)
unreal.log_warning("BakedRooms opgeslagen")

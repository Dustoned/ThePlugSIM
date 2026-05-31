import unreal

MAP = "/Game/_Project/Maps/Map_Apartment"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP)
asub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Centreer rond de PlayerStart (zit zeker in het speelbare gebied).
center = unreal.Vector(0.0, 0.0, 100.0)
for a in asub.get_all_level_actors():
    if isinstance(a, unreal.PlayerStart):
        center = a.get_actor_location()
        break

# Verwijder oude exemplaren zodat herhaald draaien geen dubbele oplevert.
for a in list(asub.get_all_level_actors()):
    lbl = a.get_actor_label()
    if lbl in ("NavBounds", "CustomerSpawner"):
        asub.destroy_actor(a)

# NavMesh-bounds (groot genoeg voor het appartement).
nav = asub.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(center.x, center.y, center.z - 60.0))
if nav:
    nav.set_actor_scale3d(unreal.Vector(55.0, 55.0, 14.0))
    nav.set_actor_label("NavBounds")

# Spawner op het spawn-punt (bij de speler-start).
sp = asub.spawn_actor_from_class(unreal.CustomerSpawner, center)
if sp:
    sp.set_actor_label("CustomerSpawner")

les.save_current_level()
print("nav+spawner placed at (%.0f,%.0f,%.0f)" % (center.x, center.y, center.z))

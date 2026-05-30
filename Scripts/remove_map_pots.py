import unreal

MAP = "/Game/_Project/Maps/Map_Apartment"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP)

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_sub.get_all_level_actors()

removed = 0
for a in all_actors:
    if isinstance(a, unreal.GrowPlant):
        unreal.log("Removing GrowPlant: %s" % a.get_actor_label())
        actor_sub.destroy_actor(a)
        removed += 1

unreal.log("Removed %d GrowPlant actors." % removed)
les.save_current_level()
unreal.log("Map saved.")

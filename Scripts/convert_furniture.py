import unreal

MAP = "/Game/_Project/Maps/Map_Apartment"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP)
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# label -> (item_id, base_z)  (base_z = loc.z - halfZ zodat de onderkant op de vloer staat)
convert = {
    "Koelkast": ("Fridge", 0.0),
    "Matras":   ("Mattress", 5.0),
    "Tafel":    ("Table", 5.0),
}
# Weg uit de wereld: upgrade-stations (upgrades koop je via de telefoon) + testblok.
remove = {"UpgradeStation", "UpgradeStation2", "MoneyTestPickup"}

log = []
to_delete = []
to_spawn = []

for a in actor_sub.get_all_level_actors():
    label = a.get_actor_label()
    if label in remove:
        to_delete.append(a)
        log.append("remove %s" % label)
    elif label in convert:
        item_id, base_z = convert[label]
        loc = a.get_actor_location()
        to_spawn.append((item_id, unreal.Vector(loc.x, loc.y, base_z), label))
        to_delete.append(a)

for item_id, loc, label in to_spawn:
    prop = actor_sub.spawn_actor_from_class(unreal.PlaceableProp, loc, unreal.Rotator(0, 0, 0))
    if prop:
        prop.set_editor_property("item_id", item_id)
        prop.set_actor_label(item_id)
        log.append("spawned PlaceableProp %s at (%.0f,%.0f,%.0f)" % (item_id, loc.x, loc.y, loc.z))

for a in to_delete:
    actor_sub.destroy_actor(a)

les.save_current_level()
with open("C:/TPS_tmp/convert_result.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(log) + ("\n-- done --" if log else "nothing"))

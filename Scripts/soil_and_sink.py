import unreal

# 1) Bruin opaak soil-materiaal.
PKG = "/Game/_Project/Materials"
NAME = "M_Soil"
FULL = PKG + "/" + NAME
atools = unreal.AssetToolsHelpers.get_asset_tools()
if not unreal.EditorAssetLibrary.does_asset_exist(FULL):
    mat = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mle = unreal.MaterialEditingLibrary
    col = mle.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    col.set_editor_property("constant", unreal.LinearColor(0.20, 0.12, 0.05, 1.0))  # donkerbruine aarde
    mle.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mle.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", 0.95)
    mle.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mle.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FULL)

# 2) Gootsteen in huis plaatsen (bij de kweekhoek, tegen de noordmuur y=250).
MAP = "/Game/_Project/Maps/Map_Apartment"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP)
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# verwijder bestaande sinks (idempotent)
for a in actor_sub.get_all_level_actors():
    if isinstance(a, unreal.WaterSink):
        actor_sub.destroy_actor(a)

sink = actor_sub.spawn_actor_from_class(unreal.WaterSink, unreal.Vector(210, 215, 0), unreal.Rotator(0, 0, 0))
if sink:
    sink.set_actor_label("WaterSink")

les.save_current_level()
with open("C:/TPS_tmp/soilsink_result.txt", "w", encoding="utf-8") as f:
    f.write("soil material + sink done")

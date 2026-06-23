# Laatste poging: importeer de maker-idle-FBX MET een nieuw skelet (import als skeletal mesh). Dan bevat het
# nieuwe skelet ALLE botten van de FBX (incl. HeadTop_End) -> geen mismatch. De resulterende AnimSequence
# staat op een Manny-achtig skelet en kan via de montage op de speler-Manny spelen.
import unreal

FBX  = r"C:\Users\Dustoned\Downloads\Standard Idle.fbx"
DEST = "/Game/Characters/StandardIdle"

task = unreal.AssetImportTask()
task.filename = FBX
task.destination_path = DEST
task.replace_existing = True
task.automated = True
task.save = True
opts = unreal.FbxImportUI()
opts.import_mesh = True
opts.import_as_skeletal = True
opts.import_animations = True
opts.create_physics_asset = False
opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
task.options = opts

try:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
except Exception as e:
    unreal.log_error("[idle] import-exceptie: " + str(e))

# Toon ALLES wat er in de dest-map staat (mesh + skelet + de anim - naam onbekend).
assets = unreal.EditorAssetLibrary.list_assets(DEST, recursive=True) if unreal.EditorAssetLibrary.does_directory_exist(DEST) else []
for a in assets:
    obj = unreal.load_asset(a)
    unreal.log("[idle] -> %s  (%s)" % (a, type(obj).__name__ if obj else "?"))
if not assets:
    unreal.log_error("[idle] niks geimporteerd.")
unreal.log("[idle] KLAAR")

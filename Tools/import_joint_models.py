# Importeert de twee joint-modellen (B.9) als StaticMesh + textures naar /Game/_Project/Models/Joints.
# Draaien in de OPEN editor via Tools/ue_run.ps1 (UnrealClaude execute_script) - saves persisteren dan.
# Na afloop logt 'ie de bounds per mesh (nodig voor de C++-schaal/orientatie-normalisatie).
import unreal, os

SRC = r"C:\Users\Dustoned\Documents\Unreal Projects\ThePlugSIM - Claude\_IncomingKits\joints"
DEST = "/Game/_Project/Models/Joints"

def import_fbx(fbx_path, dest_name):
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.import_animations = False
    ui.import_materials = True
    ui.import_textures = True
    ui.static_mesh_import_data.combine_meshes = True
    ui.static_mesh_import_data.generate_lightmap_u_vs = False
    ui.static_mesh_import_data.auto_generate_collision = False  # pickup-Body draagt de collision

    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = DEST
    task.destination_name = dest_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return task.imported_object_paths

out = []
out += list(import_fbx(os.path.join(SRC, r"joint-scan-v2\source\Joint_6\jspp_LOD1.fbx"), "SM_JointSmall"))
out += list(import_fbx(os.path.join(SRC, r"fat-joint\source\FatJoint\Fat Joint.fbx"), "SM_JointFat"))
print("IMPORTED: " + ", ".join([str(p) for p in out]))

# Bounds + materiaal-info loggen voor de C++-normalisatie (lange as + lengte in cm).
for name in ("SM_JointSmall", "SM_JointFat"):
    path = DEST + "/" + name + "." + name
    sm = unreal.EditorAssetLibrary.load_asset(path)
    if not sm:
        print("MISSING: " + path)
        continue
    b = sm.get_bounds()
    e = b.box_extent
    print("%s: extent x=%.2f y=%.2f z=%.2f (volle maat %.1f x %.1f x %.1f cm)" % (name, e.x, e.y, e.z, e.x*2, e.y*2, e.z*2))

# Alles onder de doel-map opslaan (zeker weten dat textures/materials mee-persisteren).
unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=True, recursive=True)
print("SAVED " + DEST)

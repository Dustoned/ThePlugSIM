# Sync de CSV-bronnen in Data/ naar de .uasset DataTables in Content/_Project/Data.
# BELANGRIJK: de game laadt de .uasset (zie GrowPlant.cpp / StoreComponent.cpp), NIET de CSV.
# Een CSV-edit landt dus pas in de game NADAT dit script de DataTable opnieuw vult + opslaat.
#
# Draaien (headless):
#   UnrealEditor-Cmd.exe "ThePlugSIM.uproject" -ExecutePythonScript="Tools/reimport_datatables.py" -unattended -nosplash
import unreal
import os

PROJECT_DIR = unreal.Paths.project_dir()
DATA_DIR = os.path.join(PROJECT_DIR, "Data")

# (CSV-bestandsnaam zonder extensie, asset-pad in /Game)
TABLES = [
    ("DT_Strains",  "/Game/_Project/Data/DT_Strains"),
    ("DT_Products", "/Game/_Project/Data/DT_Products"),
]

unreal.log("[sync] start - data-dir: " + DATA_DIR)

for csv_name, asset_path in TABLES:
    csv_path = os.path.join(DATA_DIR, csv_name + ".csv")
    if not os.path.isfile(csv_path):
        unreal.log_error("[sync] CSV ontbreekt: " + csv_path)
        continue
    dt = unreal.load_asset(asset_path)
    if dt is None:
        unreal.log_error("[sync] DataTable niet gevonden: " + asset_path)
        continue
    with open(csv_path, "r", encoding="utf-8") as f:
        csv_str = f.read()
    problems = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, csv_str)
    if problems:
        unreal.log_warning("[sync] %s - import-meldingen: %s" % (csv_name, str(problems)))
    saved = unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    unreal.log("[sync] %s -> opgeslagen=%s, rijen=%d" % (csv_name, str(saved), len(rows)))

unreal.log("[sync] KLAAR")

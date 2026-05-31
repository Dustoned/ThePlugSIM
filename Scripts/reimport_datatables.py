import unreal

# Herimporteer DataTables vanuit de CSV's (gebruikt het bestaande RowStruct van elke tabel).
PROJECT = unreal.Paths.project_dir()
DATA_DIR = PROJECT + "Data/"

TABLES = {
    "/Game/_Project/Data/DT_Strains": DATA_DIR + "DT_Strains.csv",
    "/Game/_Project/Data/DT_Products": DATA_DIR + "DT_Products.csv",
    "/Game/_Project/Data/DT_NPCs": DATA_DIR + "DT_NPCs.csv",
}

for asset_path, csv_path in TABLES.items():
    dt = unreal.load_asset(asset_path)
    if not dt:
        unreal.log_error("Kan DataTable niet laden: %s" % asset_path)
        continue
    with open(csv_path, "r", encoding="utf-8") as f:
        csv_text = f.read()
    problems = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, csv_text)
    unreal.log("Gevuld %s vanuit %s (problems=%s)" % (asset_path, csv_path, str(problems)))
    unreal.EditorAssetLibrary.save_loaded_asset(dt)

unreal.log("DataTable herimport klaar.")

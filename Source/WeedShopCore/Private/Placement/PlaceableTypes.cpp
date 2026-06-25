#include "Placement/PlaceableTypes.h"

const TArray<FPlaceableDef>& GetAllPlaceables()
{
	// Cube/Cylinder basis-mesh = 100 cm; BoxHalf = 50 * schaal (en pot is een cilinder r=25, h=40).
	static const TArray<FPlaceableDef> Defs = {
		// ItemId,        DisplayName,   MeshPath,                         MeshScale,                  BoxHalf,                    bIsPot
		{ TEXT("Pot_Broken"),  TEXT("Broken pot"),  TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.45f, 0.45f, 0.35f), FVector(23.f, 23.f, 18.f), true },
		{ TEXT("Pot_Clay"),    TEXT("Clay pot"),    TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.50f, 0.50f, 0.40f), FVector(25.f, 25.f, 20.f), true },
		{ TEXT("Pot_Plastic"), TEXT("Plastic pot"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.56f, 0.56f, 0.42f), FVector(28.f, 28.f, 21.f), true },
		{ TEXT("Pot_Fabric"),  TEXT("Fabric pot"),  TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.72f, 0.72f, 0.46f), FVector(36.f, 36.f, 23.f), true },
		{ TEXT("Fridge"),   TEXT("Fridge"),   TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(0.6f, 0.6f, 1.8f),  FVector(30.f, 30.f, 90.f), false, 12000, false, false, false, false, /*bIsShelf*/ true },
		{ TEXT("Mattress"), TEXT("Mattress"), TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(2.0f, 1.0f, 0.5f),  FVector(100.f, 50.f, 25.f), false, 4000, false, false, false, false, false, false, false, false, /*bIsBed*/ true },
		{ TEXT("Table"),    TEXT("Table"),    TEXT("/Engine/BasicShapes/Cube.Cube"),         FVector(1.2f, 0.8f, 0.8f),  FVector(60.f, 40.f, 40.f), false, 6000 },
		// ATM: spawnt een AAtm (interactief) en mag ook BUITEN geplaatst worden.
		{ TEXT("Atm"), TEXT("ATM"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.78f, 0.60f, 1.62f), FVector(39.f, 30.f, 81.f), false, 0, /*bIsAtm*/ true, /*bAllowOutdoors*/ true },
		// Kluizen: spawnen AAtm in safe-modus. Progressie: grotere kluis = meer cash veilig (cap in Atm::SafeCapacityForItem).
		{ TEXT("Safe_Small"),  TEXT("Small safe"),  TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.78f, 0.60f, 1.62f), FVector(39.f, 30.f, 81.f), false, 40000,   false, false, false, false, false, false, false, false, false, false, /*bIsSafe*/ true },
		{ TEXT("Safe_Medium"), TEXT("Medium safe"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.78f, 0.60f, 1.62f), FVector(39.f, 30.f, 81.f), false, 200000,  false, false, false, false, false, false, false, false, false, false, /*bIsSafe*/ true },
		{ TEXT("Safe_Large"),  TEXT("Large safe"),  TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.78f, 0.60f, 1.62f), FVector(39.f, 30.f, 81.f), false, 1000000, false, false, false, false, false, false, false, false, false, false, /*bIsSafe*/ true },
		{ TEXT("Safe_Vault"),  TEXT("Vault"),       TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.78f, 0.60f, 1.62f), FVector(39.f, 30.f, 81.f), false, 4000000, false, false, false, false, false, false, false, false, false, false, /*bIsSafe*/ true },
		{ TEXT("Wardrobe"), TEXT("Wardrobe"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.1f, 0.5f, 2.0f), FVector(55.f, 25.f, 100.f), false, 5000, false, false, false, false, false, false, false, false, false, false, /*bIsSafe*/ false, /*bIsWardrobe*/ true },

		// --- Building-tool (dev): muren/vloeren/plafonds van de CityBeachStrip-pack tekenen ---
		// Velden t/m bIsWardrobe positioneel, daarna bIsStructure/bIsCeilingPiece/bIsDoubleWall.
		{ TEXT("Struct_Wall4m"), TEXT("Wall 4m"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_InteriorWall_01_4m.SM_InteriorWall_01_4m"), FVector(1.f), FVector(5.f, 200.f, 160.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, true },
		{ TEXT("Struct_Wall2m"), TEXT("Wall 2m"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_InteriorWall_01_2m.SM_InteriorWall_01_2m"), FVector(1.f), FVector(5.f, 100.f, 160.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, true },
		{ TEXT("Struct_Wall1m"), TEXT("Wall 1m"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_InteriorWall_01_1m.SM_InteriorWall_01_1m"), FVector(1.f), FVector(5.f, 50.f, 160.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, true },
		{ TEXT("Struct_WallDoor4m"), TEXT("Wall 4m + doorway"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_InteriorWall_01_4m_Door01.SM_InteriorWall_01_4m_Door01"), FVector(1.f), FVector(5.f, 200.f, 160.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, true },
		{ TEXT("Struct_WallDoor3m"), TEXT("Wall 3m + doorway"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_InteriorWall_01_3m_Door01.SM_InteriorWall_01_3m_Door01"), FVector(1.f), FVector(5.f, 150.f, 160.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, true },
		{ TEXT("Struct_Floor4x4"), TEXT("Floor 4x4m (carpet)"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_Floor_4x4m.SM_Floor_4x4m"), FVector(1.f), FVector(200.f, 200.f, 3.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, false },
		{ TEXT("Struct_Floor1x1"), TEXT("Floor 1x1m (tiles)"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_Floor_1x1m.SM_Floor_1x1m"), FVector(1.f), FVector(50.f, 50.f, 3.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, false },
		{ TEXT("Struct_Ceil4x4"), TEXT("Ceiling 4x4m"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_Ceiling_4x4m.SM_Ceiling_4x4m"), FVector(1.f), FVector(200.f, 200.f, 3.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, true, false },
		{ TEXT("Struct_Ceil1x1"), TEXT("Ceiling 1x1m"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_Ceiling_1x1m.SM_Ceiling_1x1m"), FVector(1.f), FVector(50.f, 50.f, 3.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, true, false },
		{ TEXT("Struct_CeilLamp"), TEXT("Ceiling lamp (built-in)"), TEXT("/Game/CityBeachStrip/Meshes/CeilingProps/SM_CeilingLight02.SM_CeilingLight02"), FVector(1.f), FVector(20.f, 20.f, 4.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, true, false, false },
		{ TEXT("Struct_Door"), TEXT("Room door (working)"), TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/SM_Door_Apartment02.SM_Door_Apartment02"), FVector(1.f), FVector(9.f, 66.f, 115.f), false, 100, false, true, false, false, false, false, false, false, false, false, false, false, true, false, false, true },
		// Droogrekken: spawnen een ADryingRack (RackTier = item-id). Binnen plaatsen.
		{ TEXT("DryRack_Cheap"), TEXT("Cheap drying rack"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.2f, 0.3f, 1.5f), FVector(60.f, 15.f, 75.f), false, 0, false, false, /*bIsDryRack*/ true, false, false, false, false, /*bIsWallMount*/ true },
		{ TEXT("DryRack_Std"),   TEXT("Drying rack"),       TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.4f, 0.3f, 1.6f), FVector(70.f, 15.f, 80.f), false, 0, false, false, true, false, false, false, false, /*bIsWallMount*/ true },
		{ TEXT("DryRack_Pro"),   TEXT("Pro drying cabinet"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.6f, 0.4f, 1.8f), FVector(80.f, 20.f, 90.f), false, 0, false, false, true, false, false, false, false, /*bIsWallMount*/ true },
		// Verpak-tafels (tiers): spawnen een APackBench. Hogere tier = meer zakjes per keer. Binnen plaatsen.
		{ TEXT("Bench_Pack"),  TEXT("Packing bench"),       TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.3f, 0.7f, 0.9f), FVector(65.f, 35.f, 45.f), false, 0, false, false, false, /*bIsPackBench*/ true },
		{ TEXT("Bench_Pack2"), TEXT("Pro packing bench"),   TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.5f, 0.8f, 0.9f), FVector(75.f, 40.f, 45.f), false, 0, false, false, false, true },
		{ TEXT("Bench_Pack3"), TEXT("Industrial packing table"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.7f, 0.9f, 0.95f), FVector(85.f, 45.f, 47.f), false, 0, false, false, false, true },
		// Opslag-schap: voorraad-opslag in de shop (binnen). Spawnt een AStorageShelf.
		{ TEXT("Shelf"), TEXT("Storage shelf"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.5f, 0.4f, 1.7f), FVector(75.f, 20.f, 85.f), false, 0, false, false, false, false, /*bIsShelf*/ true },
		// Opslag-kist: laag/breed kistje, ook gewoon opslag (spawnt een AStorageShelf, tier "Chest").
		{ TEXT("Chest"), TEXT("Storage chest"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.0f, 0.6f, 0.55f), FVector(50.f, 30.f, 27.f), false, 0, false, false, false, false, /*bIsShelf*/ true },
		// Gootsteen: waterfles vullen. Spawnt een AWaterSink. Binnen plaatsen.
		{ TEXT("Sink"), TEXT("Sink"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.84f, 0.56f, 1.0f), FVector(42.f, 28.f, 50.f), false, 0, false, false, false, false, false, /*bIsSink*/ true },
		// Plafondlamp: warme spot. Spawnt een ACeilingLamp. Binnen plaatsen.
		{ TEXT("Lamp_Ceiling"), TEXT("Ceiling lamp"), TEXT("/Engine/BasicShapes/Cone.Cone"), FVector(0.28f, 0.28f, 0.22f), FVector(20.f, 20.f, 14.f), false, 0, false, false, false, false, false, false, /*bIsLamp*/ true },
		// Lichtschakelaar: hangt aan de muur (wall-mount). Spawnt een APackLightSwitch (tap = aan/uit, hold = dimmer;
		// claimt de plafondlampen in de buurt). Plaats er een bij de deur en een bij de badkamer.
		{ TEXT("LightSwitch"), TEXT("Light switch"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.03f, 0.085f, 0.13f), FVector(3.f, 6.f, 8.f), false, 800, false, false, false, false, false, false, false, /*bIsWallMount*/ true, false, false, false, false, false, false, false, false, /*bIsLightSwitch*/ true },

		// --- HUISKAMER-MEUBELS (om je kamers in te richten) -----------------------------------------
		// Echte pack-meshes (pivot aan de basis -> bBasePivot=true, laatste veld). Generieke props
		// (oppakken = terug in inventory). Decor, geen speciale functie.
		{ TEXT("Furn_ChairPlastic"), TEXT("Plastic chair"), TEXT("/Game/CityBeachStrip/Meshes/PlasticChair/SM_PlasticChair"), FVector(1.f), FVector(32.f, 34.f, 51.f), false, 600,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, /*bBasePivot*/ true },
		{ TEXT("Furn_ChairGarden"),  TEXT("Garden chair"),  TEXT("/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_GardenChair"), FVector(1.f), FVector(23.f, 30.f, 47.f), false, 700,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_ChairWood"),    TEXT("Wooden chair"),  TEXT("/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_OutsideChair"), FVector(1.f), FVector(40.f, 38.f, 52.f), false, 800,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_TableSmall"),   TEXT("Small table"),   TEXT("/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_GardenTable"), FVector(1.f), FVector(58.f, 37.f, 40.f), false, 1500, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_TableRound"),   TEXT("Round table"),   TEXT("/Game/CityBeachStrip/Meshes/OutdoorFurniture/SM_OutsideTable"), FVector(1.f), FVector(59.f, 59.f, 42.f), false, 1800, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_Bench"),        TEXT("Bench"),         TEXT("/Game/CityBeachStrip/Meshes/Bench/SM_Bench"), FVector(1.f), FVector(112.f, 39.f, 46.f), false, 2000, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_Plant"),        TEXT("Potted tree"),   TEXT("/Game/CityBeachStrip/Meshes/Plants/SM_DragonTree1"), FVector(0.7f), FVector(45.f, 45.f, 100.f), false, 1200, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_Planter"),      TEXT("Wood planter"),  TEXT("/Game/CityBeachStrip/Meshes/Planters/SM_WoodPlanter01"), FVector(1.f), FVector(33.f, 33.f, 31.f), false, 800,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_DecoPot"),      TEXT("Decorative pot"),TEXT("/Game/CityBeachStrip/Meshes/Pots/SM_Pot03"), FVector(1.f), FVector(15.f, 15.f, 15.f), false, 300,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
		{ TEXT("Furn_Crate"),        TEXT("Crate"),         TEXT("/Game/CityBeachStrip/Meshes/PlasticCrate/SM_PlasticCrate"), FVector(1.f), FVector(30.f, 20.f, 8.f), false, 200,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },

		// Basic-shape huiskamer-meubels (zelfde stijl als Table/Fridge; center-pivot, normale offset).
		{ TEXT("Furn_Sofa"),       TEXT("Sofa"),         TEXT("/Game/CityBeachStrip/Meshes/TrashProps/Couch/SM_Couch"), FVector(1.f), FVector(100.f, 45.f, 40.f), false, 3000, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, /*bBasePivot*/ true },
		{ TEXT("Furn_TV"),         TEXT("TV (old)"),     TEXT("/Game/CityBeachStrip/Meshes/TrashProps/OldTV/SM_OldTV"), FVector(1.f), FVector(35.f, 20.f, 28.f), false, 2500, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, /*bBasePivot*/ true },
		{ TEXT("Furn_TVStand"),    TEXT("TV cabinet"),   TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(1.6f, 0.45f, 0.5f), FVector(80.f, 22.f, 25.f), false, 2000 },
		{ TEXT("Furn_CoffeeTable"),TEXT("Coffee table"), TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(1.0f, 0.6f, 0.4f),  FVector(50.f, 30.f, 20.f), false, 1200 },
		{ TEXT("Furn_Bookshelf"),  TEXT("Bookshelf"),    TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(1.0f, 0.3f, 1.8f),  FVector(50.f, 15.f, 90.f), false, 2200 },
		{ TEXT("Furn_Dresser"),    TEXT("Dresser"),      TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(1.0f, 0.5f, 1.0f),  FVector(50.f, 25.f, 50.f), false, 1800 },
		{ TEXT("Furn_Nightstand"), TEXT("Nightstand"),   TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.45f, 0.45f, 0.5f),FVector(22.f, 22.f, 25.f), false, 700 },
		{ TEXT("Furn_Desk"),       TEXT("Desk"),         TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(1.4f, 0.7f, 0.78f), FVector(70.f, 35.f, 39.f), false, 1600 },
		{ TEXT("Furn_Rug"),        TEXT("Rug"),          TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(2.0f, 1.4f, 0.04f), FVector(100.f, 70.f, 2.f), false, 500 },
		{ TEXT("Furn_FloorLamp"),  TEXT("Floor lamp"),   TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.18f, 0.18f, 1.6f), FVector(9.f, 9.f, 80.f), false, 900 },
		// --- Pot-gear: fysieke accessoires die je NAAST/op je pot zet; de pot leest welke vlakbij staan en
		//     past die bonus toe zolang 't accessoire er staat. Generieke props (oppakken = terug in inv).
		{ TEXT("Gear_Drainage"),  TEXT("Drainage layer"),  TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.26f, 0.26f, 0.18f), FVector(13.f, 13.f, 9.f),  false, 1200 },
		{ TEXT("Gear_Insulation"),TEXT("Insulation wrap"),  TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.30f, 0.30f, 0.30f), FVector(15.f, 15.f, 15.f), false, 1600 },
		{ TEXT("Gear_Bloom"),     TEXT("Bloom booster"),    TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.24f, 0.24f, 0.30f), FVector(12.f, 12.f, 15.f), false, 2000 },
		{ TEXT("Gear_Lamp1"),     TEXT("Grow lamp I"),      TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.30f, 0.30f, 0.55f), FVector(15.f, 15.f, 28.f), false, 2400 },
		{ TEXT("Gear_Lamp2"),     TEXT("Grow lamp II"),     TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.34f, 0.34f, 0.60f), FVector(17.f, 17.f, 30.f), false, 4800 },
		{ TEXT("Gear_Lamp3"),     TEXT("Grow lamp III"),    TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.38f, 0.38f, 0.66f), FVector(19.f, 19.f, 33.f), false, 8800 },
		{ TEXT("Gear_Tent1"),     TEXT("Grow tent I"),      TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.55f, 0.55f, 0.80f), FVector(27.f, 27.f, 40.f), false, 2800 },
		{ TEXT("Gear_Tent2"),     TEXT("Grow tent II"),     TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.62f, 0.62f, 0.88f), FVector(31.f, 31.f, 44.f), false, 5200 },
		{ TEXT("Gear_Tent3"),     TEXT("Grow tent III"),    TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.70f, 0.70f, 0.96f), FVector(35.f, 35.f, 48.f), false, 9600 },
		{ TEXT("Gear_Water1"),    TEXT("Auto-water I"),     TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.30f, 0.30f, 0.55f), FVector(15.f, 15.f, 28.f), false, 5600 },
		{ TEXT("Gear_Water2"),    TEXT("Auto-water II"),    TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.34f, 0.34f, 0.62f), FVector(17.f, 17.f, 31.f), false, 10400 },
		// --- Hasj-keten: Mesh-extractor (Bud -> Crystal) + Heatpress (Crystal -> Hash). Spawnen AProcessorMachine. Binnen.
		{ TEXT("Mesh_Cheap"),  TEXT("Mesh extractor"),       TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f), FVector(35.f, 35.f, 27.5f), false, 3000,  false, false, false, false, false, false, false, false, /*bIsProcessor*/ true },
		{ TEXT("Mesh_Std"),    TEXT("Pro mesh extractor"),   TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f), FVector(35.f, 35.f, 27.5f), false, 6000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Mesh_Pro"),    TEXT("Industrial extractor"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f), FVector(35.f, 35.f, 27.5f), false, 11000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Press_Cheap"), TEXT("Heatpress"),            TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f), FVector(35.f, 35.f, 27.5f), false, 5000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Press_Std"),   TEXT("Pro heatpress"),        TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f), FVector(35.f, 35.f, 27.5f), false, 9000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Press_Pro"),   TEXT("Industrial press"),     TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f), FVector(35.f, 35.f, 27.5f), false, 16000, false, false, false, false, false, false, false, false, true },
		// --- Edibles-keten: Oven (Bud->Baked), Pan (Baked+boter->ButterMix), Koelkast (ButterMix->Edible). Spawnen AProcessorMachine. Binnen.
		{ TEXT("Oven_Std"),    TEXT("Oven / stove"),         TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.75f, 0.7f, 0.55f), FVector(37.5f, 35.f, 27.5f), false, 7000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Pan_Std"),     TEXT("Cooking pan"),          TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f),  FVector(35.f, 35.f, 27.5f), false, 11000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Fridge_Std"),  TEXT("Fridge conversion"),    TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.85f),  FVector(35.f, 35.f, 42.5f), false, 4000, false, false, false, false, false, false, false, false, true },
		// Pro-edibles (sneller + grotere capaciteit).
		{ TEXT("Oven_Pro"),    TEXT("Pro range cooker"),     TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.75f, 0.7f, 0.55f), FVector(37.5f, 35.f, 27.5f), false, 16000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Pan_Pro"),     TEXT("Pro cooktop"),          TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f),  FVector(35.f, 35.f, 27.5f), false, 18000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Fridge_Pro"),  TEXT("Walk-in fridge"),       TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.8f, 0.8f, 0.95f),  FVector(40.f, 40.f, 47.5f), false, 11000, false, false, false, false, false, false, false, false, true },
		// Concentraat-machines: Rosin-pers, Ice/Bubble extractor (Isolator), Moonrock-station (allemaal bud -> concentraat).
		{ TEXT("Rosin_Std"),   TEXT("Rosin press"),          TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f),  FVector(35.f, 35.f, 27.5f), false, 7000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Rosin_Pro"),   TEXT("Hydraulic rosin press"),TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.55f),  FVector(35.f, 35.f, 27.5f), false, 20000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Iso_Std"),     TEXT("Ice extractor"),        TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.8f),   FVector(35.f, 35.f, 40.f),  false, 9000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Iso_Pro"),     TEXT("Pro isolator"),         TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.7f, 0.7f, 0.8f),   FVector(35.f, 35.f, 40.f),  false, 24000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Oil_Std"),     TEXT("Oil press"),            TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.65f, 0.65f, 0.5f), FVector(32.5f, 32.5f, 25.f), false, 8000,  false, false, false, false, false, false, false, false, true },
		{ TEXT("Oil_Pro"),     TEXT("Pro oil press"),        TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.65f, 0.65f, 0.5f), FVector(32.5f, 32.5f, 25.f), false, 22000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Moon_Std"),    TEXT("Moonrock station"),     TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.75f, 0.7f, 0.55f), FVector(37.5f, 35.f, 27.5f),false, 12000, false, false, false, false, false, false, false, false, true },
		{ TEXT("Moon_Pro"),    TEXT("Pro moonrock station"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.75f, 0.7f, 0.55f), FVector(37.5f, 35.f, 27.5f),false, 28000, false, false, false, false, false, false, false, false, true },
		// --- Losse upgrade-gear: zet vlakbij een DROOGREK of HASJ-MACHINE om 'm sneller/beter te maken.
		{ TEXT("DryUp_FanSmall"), TEXT("Small drying fan"), TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.26f, 0.26f, 0.26f), FVector(13.f, 13.f, 13.f), false, 2200 },
		{ TEXT("DryUp_Fan"),    TEXT("Drying fan"),      TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.30f, 0.30f, 0.30f), FVector(15.f, 15.f, 15.f), false, 3000 },
		{ TEXT("DryUp_Seal"),   TEXT("Humidity sealer"), TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.30f, 0.30f, 0.24f), FVector(15.f, 15.f, 12.f), false, 4500 },
		{ TEXT("ProcUp_Motor"), TEXT("Power motor"),     TEXT("/Engine/BasicShapes/Cube.Cube"),     FVector(0.30f, 0.30f, 0.30f), FVector(15.f, 15.f, 15.f), false, 5000 },
		{ TEXT("ProcUp_Yield"), TEXT("Fine filter"),     TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.28f, 0.28f, 0.34f), FVector(14.f, 14.f, 17.f), false, 7000 },
	};
	return Defs;
}

bool GetPlaceableDef(FName ItemId, FPlaceableDef& Out)
{
	for (const FPlaceableDef& D : GetAllPlaceables())
	{
		if (D.ItemId == ItemId)
		{
			Out = D;
			return true;
		}
	}
	return false;
}

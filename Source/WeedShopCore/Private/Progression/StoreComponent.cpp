#include "Progression/StoreComponent.h"

#include "WeedShopCore.h"
#include "Data/WeedStrain.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Cultivation/SoilTypes.h"
#include "Cultivation/PotTypes.h"
#include "Cultivation/BottleTypes.h"
#include "Placement/PlaceableTypes.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"

UStoreComponent::UStoreComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UDataTable> TableFinder(
		TEXT("/Game/_Project/Data/DT_Strains.DT_Strains"));
	if (TableFinder.Succeeded())
	{
		StrainTable = TableFinder.Object;
	}
}

FName UStoreComponent::SeedItemId(FName StrainId)
{
	return FName(*FString::Printf(TEXT("Seed_%s"), *StrainId.ToString()));
}

FName UStoreComponent::StrainFromSeedItem(FName SeedId)
{
	const FString S = SeedId.ToString();
	if (S.StartsWith(TEXT("Seed_")))
	{
		return FName(*S.RightChop(5));
	}
	return NAME_None;
}

TArray<FName> UStoreComponent::GetSeedCatalog() const
{
	return StrainTable ? StrainTable->GetRowNames() : TArray<FName>();
}

bool UStoreComponent::GetSeedDisplay(FName StrainId, FText& OutName, int32& OutPriceCents) const
{
	if (!StrainTable)
	{
		return false;
	}
	const FWeedStrainRow* Row = StrainTable->FindRow<FWeedStrainRow>(StrainId, TEXT("GetSeedDisplay"), false);
	if (!Row)
	{
		return false;
	}
	OutName = Row->DisplayName;
	OutPriceCents = Row->SeedPriceCents;
	return true;
}

bool UStoreComponent::BuySeed(FName StrainId, UInventoryComponent* Buyer)
{
	if (GetOwnerRole() != ROLE_Authority || !StrainTable || !Buyer)
	{
		return false;
	}
	const FWeedStrainRow* Row = StrainTable->FindRow<FWeedStrainRow>(StrainId, TEXT("BuySeed"), false);
	if (!Row)
	{
		return false;
	}

	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	if (!Econ || !Econ->RemoveMoney(Row->SeedPriceCents))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red,
				FString::Printf(TEXT("Not enough money for seed %s"), *StrainId.ToString()));
		}
		return false;
	}

	Buyer->AddItem(SeedItemId(StrainId), 1);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Seed bought: %s"), *Row->DisplayName.ToString()));
	}
	return true;
}

// --- Supplies (vaste catalogus) ---
namespace
{
	struct FSupplyDef { const TCHAR* Id; const TCHAR* Name; const TCHAR* Desc; int32 PriceCents; int32 PackSize; };
	static const FSupplyDef GSupplies[] = {
		// Papers (max gram per joint loopt op).
		{ TEXT("Papers_Small"),     TEXT("Rolling papers"),   TEXT("Up to 2g per joint - 10 pcs"),  500, 10 },
		{ TEXT("Papers_Big"),       TEXT("King-size papers"), TEXT("Up to 5g per joint - 10 pcs"), 1500, 10 },
		{ TEXT("Papers_Blunt"),     TEXT("Blunt wraps"),      TEXT("Up to 7g per joint - 10 pcs"), 3000, 10 },
		{ TEXT("Papers_Backwoods"), TEXT("Backwoods"),        TEXT("Up to 10g per joint - 5 pcs"), 5000, 5 },
		// Pots (betere pot = betere waterretentie/kwaliteit + meer yield).
		{ TEXT("Pot_Broken"),       TEXT("Broken pot"),  TEXT("Leaks - low quality, 1 plant"),    1500, 1 },
		{ TEXT("Pot_Clay"),         TEXT("Clay pot"),    TEXT("Decent retention, 1 plant"),       4000, 1 },
		{ TEXT("Pot_Plastic"),      TEXT("Plastic pot"), TEXT("More yield, up to 2 plants"),     10000, 1 },
		{ TEXT("Pot_Fabric"),       TEXT("Fabric pot"),  TEXT("Best quality, up to 6 plants"),   35000, 1 },
		// Soil (betere soil = meer yield/kwaliteit, ontgrendelt met fase).
		{ TEXT("Soil_Basic"),       TEXT("Basic soil"),   TEXT("Lasts 3 harvests"),               1500, 1 },
		{ TEXT("Soil_Rich"),        TEXT("Rich soil"),    TEXT("More yield - 4 harvests"),        4000, 1 },
		{ TEXT("Soil_Premium"),     TEXT("Premium soil"), TEXT("Top yield - 6 harvests"),         9000, 1 },
		// Water-flessen (betere fles = meer water, minder vaak vullen).
		{ TEXT("WaterBottle_Plastic"),  TEXT("Plastic bottle"), TEXT("3 waterings per fill"),  1000,  1 },
		{ TEXT("WaterBottle_Steel"),    TEXT("Steel bottle"),   TEXT("6 waterings per fill"),  4500,  1 },
		{ TEXT("WaterBottle_Jerrycan"), TEXT("Jerry can"),      TEXT("12 waterings per fill"), 15000,  1 },
		{ TEXT("WaterBottle_Tank"),     TEXT("Water tank"),     TEXT("25 waterings per fill"), 45000,  1 },
		// Droogrekken (vers geoogste wiet eerst drogen voor verkoop). Meer/sneller = duurder.
		{ TEXT("DryRack_Cheap"), TEXT("Cheap drying rack"),  TEXT("2 batches, slow (~3 min)"),    8000, 1 },
		{ TEXT("DryRack_Std"),   TEXT("Drying rack"),        TEXT("5 batches, faster (~2 min)"), 25000, 1 },
		{ TEXT("DryRack_Pro"),   TEXT("Pro drying cabinet"), TEXT("10 batches, fast (~1 min)"),  70000, 1 },
		// Verpak-tafel + bakjes/jars (verdeel gedroogde wiet in verkoopbare verpakkingen).
		{ TEXT("Bench_Pack"),  TEXT("Packing bench"),            TEXT("1 bag at a time"),  12000, 1 },
		{ TEXT("Bench_Pack2"), TEXT("Pro packing bench"),        TEXT("3 bags at a time"),  40000, 1 },
		{ TEXT("Bench_Pack3"), TEXT("Industrial packing table"), TEXT("6 bags at a time"), 110000, 1 },
		{ TEXT("Cont_Bag2"),  TEXT("Small baggies"),  TEXT("Up to 2g each - 10 pcs"),   800, 10 },
		{ TEXT("Cont_Bag5"),  TEXT("Big baggies"),    TEXT("Up to 5g each - 10 pcs"),  1500, 10 },
		{ TEXT("Cont_Jar10"), TEXT("Small jars"),     TEXT("Up to 10g each - 5 pcs"),  2500,  5 },
		{ TEXT("Cont_Jar15"), TEXT("Jars"),           TEXT("Up to 15g each - 5 pcs"),  4000,  5 },
		{ TEXT("Cont_Block100"),  TEXT("Press blocks"), TEXT("100g each - 3 pcs (bulk)"),   9000, 3 },
		{ TEXT("Cont_Garbage500"),TEXT("Garbage bags"), TEXT("500g each - 2 pcs (bulk)"),  30000, 2 },
		// Meubels (placeables, binnen neerzetten). Puur inrichting voor nu.
		{ TEXT("Table"),    TEXT("Table"),    TEXT("A sturdy table"),       12000, 1 },
		{ TEXT("Mattress"), TEXT("Mattress"), TEXT("Somewhere to crash"),    8000, 1 },
		{ TEXT("Fridge"),   TEXT("Fridge"),   TEXT("Keeps things cold"),    30000, 1 },
	};
}

TArray<FName> UStoreComponent::GetSupplyCatalog() const
{
	TArray<FName> Out;
	for (const FSupplyDef& S : GSupplies)
	{
		Out.Add(FName(S.Id));
	}
	return Out;
}

bool UStoreComponent::GetSupplyDisplay(FName SupplyId, FText& OutName, int32& OutPriceCents, int32& OutPackSize) const
{
	for (const FSupplyDef& S : GSupplies)
	{
		if (SupplyId == FName(S.Id))
		{
			OutName = FText::FromString(S.Name);
			OutPriceCents = S.PriceCents;
			OutPackSize = S.PackSize;
			return true;
		}
	}
	return false;
}

int32 UStoreComponent::GetSellValueCents(FName ItemId) const
{
	// Wiet/joints verkoop je aan klanten, niet aan de supplier.
	const FString S = ItemId.ToString();
	if (S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("Joint_")))
	{
		return 0;
	}

	// 70% terug van de koopprijs (seeds + supplies, incl. pots/soil/papers/water).
	FText Name; int32 Buy = 0; int32 Pack = 1;
	if (!StrainFromSeedItem(ItemId).IsNone() && GetSeedDisplay(StrainFromSeedItem(ItemId), Name, Buy))
	{
		return FMath::RoundToInt(Buy * 0.70f);
	}
	if (GetSupplyDisplay(ItemId, Name, Buy, Pack))
	{
		// Pack-prijs / pack-grootte = prijs per stuk, 70% terug.
		const float PerUnit = Pack > 0 ? float(Buy) / Pack : float(Buy);
		return FMath::RoundToInt(PerUnit * 0.70f);
	}

	// Meubels (placeables zonder koopprijs) hebben een vaste verkoopwaarde.
	FPlaceableDef Pd;
	if (GetPlaceableDef(ItemId, Pd) && Pd.SellCents > 0)
	{
		return Pd.SellCents;
	}
	return 0;
}

bool UStoreComponent::SellItem(FName ItemId, UInventoryComponent* Seller)
{
	if (GetOwnerRole() != ROLE_Authority || !Seller)
	{
		return false;
	}
	const int32 Price = GetSellValueCents(ItemId);
	if (Price <= 0 || !Seller->HasItem(ItemId, 1))
	{
		return false;
	}
	if (!Seller->RemoveItem(ItemId, 1))
	{
		return false;
	}
	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	if (UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr)
	{
		Econ->AddMoneyUntracked(Price);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Green, FString::Printf(TEXT("Sold %s (+EUR %.2f)"), *ItemId.ToString(), Price / 100.f));
	}
	return true;
}

bool UStoreComponent::BuyAny(FName CatalogId, UInventoryComponent* Buyer)
{
	if (GetOwnerRole() != ROLE_Authority || !Buyer || CatalogId.IsNone())
	{
		return false;
	}
	// Supply? Dan via BuySupply; anders behandelen we het als strain-zaad.
	FText Name; int32 Price = 0; int32 Pack = 1;
	if (GetSupplyDisplay(CatalogId, Name, Price, Pack))
	{
		return BuySupply(CatalogId, Buyer);
	}
	return BuySeed(CatalogId, Buyer);
}

int32 UStoreComponent::RequiredLevelFor(FName CatalogId)
{
	const FString S = CatalogId.ToString();
	// Droogrekken
	if (S == TEXT("DryRack_Std"))     { return 5; }
	if (S == TEXT("DryRack_Pro"))     { return 12; }
	// Verpak-tafels
	if (S == TEXT("Bench_Pack2"))     { return 6; }
	if (S == TEXT("Bench_Pack3"))     { return 15; }
	// Containers
	if (S == TEXT("Cont_Jar10") || S == TEXT("Cont_Jar15")) { return 3; }
	if (S == TEXT("Cont_Block100"))   { return 8; }
	if (S == TEXT("Cont_Garbage500")) { return 15; }
	// Potten
	if (S == TEXT("Pot_Plastic"))     { return 4; }
	if (S == TEXT("Pot_Fabric"))      { return 10; }
	return 0;
}

bool UStoreComponent::GrantAny(FName CatalogId, UInventoryComponent* Buyer)
{
	if (GetOwnerRole() != ROLE_Authority || !Buyer || CatalogId.IsNone()) { return false; }
	// Supply -> voeg de pack-grootte toe; anders behandelen als strain-zaad. Geen geld, geen fase-check.
	FText Name; int32 Price = 0; int32 Pack = 1;
	if (GetSupplyDisplay(CatalogId, Name, Price, Pack))
	{
		return Buyer->AddItem(CatalogId, FMath::Max(1, Pack));
	}
	if (StrainTable && StrainTable->FindRow<FWeedStrainRow>(CatalogId, TEXT("GrantAny"), false))
	{
		return Buyer->AddItem(SeedItemId(CatalogId), 1);
	}
	return false;
}

int32 UStoreComponent::GetCatalogPriceCents(FName CatalogId) const
{
	FText Name; int32 Price = 0; int32 Pack = 1;
	if (GetSupplyDisplay(CatalogId, Name, Price, Pack)) { return Price; }
	if (GetSeedDisplay(CatalogId, Name, Price)) { return Price; }
	return 0;
}

FText UStoreComponent::GetCatalogName(FName CatalogId) const
{
	FText Name; int32 Price = 0; int32 Pack = 1;
	if (GetSupplyDisplay(CatalogId, Name, Price, Pack)) { return Name; }
	if (GetSeedDisplay(CatalogId, Name, Price)) { return Name; }
	return FText::FromName(CatalogId);
}

FText UStoreComponent::GetCatalogDesc(FName CatalogId) const
{
	for (const FSupplyDef& S : GSupplies)
	{
		if (CatalogId == FName(S.Id)) { return FText::FromString(S.Desc); }
	}
	// Zaad: korte beschrijving met de potentie van de strain.
	if (StrainTable)
	{
		if (const FWeedStrainRow* Row = StrainTable->FindRow<FWeedStrainRow>(CatalogId, TEXT("GetCatalogDesc"), false))
		{
			return FText::FromString(FString::Printf(TEXT("Seed - up to %.0f%% THC"), Row->BaseThcPercent));
		}
	}
	return FText::GetEmpty();
}

TArray<FName> UStoreComponent::GetSupplierCategory(int32 Cat) const
{
	// Categorieën: 0=Seeds, 1=Pots, 2=Drying, 3=Packing, 4=Papers, 5=Soil, 6=Water, 7=Furniture.
	// (De telefoon verdeelt deze over de Grow shop en de Supplies-app.)
	if (Cat == 0)
	{
		return GetSeedCatalog();
	}
	TArray<FName> Out;
	for (const FName& Id : GetSupplyCatalog())
	{
		const FString S = Id.ToString();
		bool bMatch = false;
		switch (Cat)
		{
		case 1: bMatch = S.StartsWith(TEXT("Pot")); break;
		case 2: bMatch = S.StartsWith(TEXT("DryRack_")); break;
		case 3: bMatch = S.StartsWith(TEXT("Bench_")) || S.StartsWith(TEXT("Cont_")); break;
		case 4: bMatch = S.StartsWith(TEXT("Papers_")); break;
		case 5: bMatch = S.StartsWith(TEXT("Soil_")); break;
		case 6: bMatch = S.StartsWith(TEXT("WaterBottle")); break;
		case 7: bMatch = (S == TEXT("Table") || S == TEXT("Mattress") || S == TEXT("Fridge")); break;
		default: break;
		}
		if (bMatch) { Out.Add(Id); }
	}
	return Out;
}

bool UStoreComponent::BuySupply(FName SupplyId, UInventoryComponent* Buyer)
{
	if (GetOwnerRole() != ROLE_Authority || !Buyer)
	{
		return false;
	}
	FText Name; int32 Price = 0; int32 Pack = 0;
	if (!GetSupplyDisplay(SupplyId, Name, Price, Pack))
	{
		return false;
	}

	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());

	// Betere soil/pot pas vanaf de juiste fase (progressie).
	uint8 ReqPhase = 0;
	FSoilDef Soil; FPotDef Pot; FBottleDef Bottle;
	if (GetSoilDef(SupplyId, Soil)) { ReqPhase = Soil.MinPhase; }
	else if (GetPotDef(SupplyId, Pot)) { ReqPhase = Pot.MinPhase; }
	else if (GetBottleDef(SupplyId, Bottle)) { ReqPhase = Bottle.MinPhase; }
	if (ReqPhase > 0)
	{
		const uint8 Phase = (GS && GS->GetMilestones()) ? static_cast<uint8>(GS->GetMilestones()->GetCurrentPhase()) : 0;
		if (Phase < ReqPhase)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange,
					FString::Printf(TEXT("%s unlocks at a later phase."), *Name.ToString()));
			}
			return false;
		}
	}

	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	if (!Econ || !Econ->RemoveMoney(Price))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red,
				FString::Printf(TEXT("Not enough money for %s"), *Name.ToString()));
		}
		return false;
	}

	Buyer->AddItem(SupplyId, Pack);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Bought: %s"), *Name.ToString()));
	}
	return true;
}

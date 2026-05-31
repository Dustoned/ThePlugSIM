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
	struct FSupplyDef { const TCHAR* Id; const TCHAR* Name; int32 PriceCents; int32 PackSize; };
	static const FSupplyDef GSupplies[] = {
		// Papers (max gram per joint loopt op).
		{ TEXT("Papers_Small"),     TEXT("Rolling papers (up to 2g) - 10 pcs"),   500, 10 },
		{ TEXT("Papers_Big"),       TEXT("King-size papers (up to 5g) - 10 pcs"), 1500, 10 },
		{ TEXT("Papers_Blunt"),     TEXT("Blunt wraps (up to 7g) - 10 pcs"),      3000, 10 },
		{ TEXT("Papers_Backwoods"), TEXT("Backwoods (up to 10g) - 5 pcs"),        5000, 5 },
		// Pots (betere pot = betere waterretentie/kwaliteit + meer yield; hogere tiers later in de progressie).
		{ TEXT("Pot_Broken"),       TEXT("Broken pot (leaks, low quality)"),      1500, 1 },
		{ TEXT("Pot_Clay"),         TEXT("Clay pot (decent)"),                    4000, 1 },
		{ TEXT("Pot_Plastic"),      TEXT("Plastic pot (+yield, 2 plants*)"),     10000, 1 },
		{ TEXT("Pot_Fabric"),       TEXT("Fabric pot (best, 6 plants*)"),        35000, 1 },
		// Soil (betere soil = meer yield/kwaliteit, ontgrendelt met fase).
		{ TEXT("Soil_Basic"),       TEXT("Basic soil (3 harvests)"),              1500, 1 },
		{ TEXT("Soil_Rich"),        TEXT("Rich soil (+yield, 4 harvests)"),       4000, 1 },
		{ TEXT("Soil_Premium"),     TEXT("Premium soil (++yield, 6 harvests)"),   9000, 1 },
		// Water-flessen (betere fles = meer water, minder vaak vullen).
		{ TEXT("WaterBottle_Plastic"),  TEXT("Plastic bottle (3 waterings)"),  1000,  1 },
		{ TEXT("WaterBottle_Steel"),    TEXT("Steel bottle (6 waterings)"),    4500,  1 },
		{ TEXT("WaterBottle_Jerrycan"), TEXT("Jerry can (12 waterings)"),     15000,  1 },
		{ TEXT("WaterBottle_Tank"),     TEXT("Water tank (25 waterings)"),    45000,  1 },
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

TArray<FName> UStoreComponent::GetSupplierCategory(int32 Cat) const
{
	if (Cat == 0)
	{
		return GetSeedCatalog();
	}
	const TCHAR* Prefix = nullptr;
	switch (Cat)
	{
	case 1: Prefix = TEXT("Papers_"); break;
	case 2: Prefix = TEXT("Pot");     break;
	case 3: Prefix = TEXT("Soil_");   break;
	case 4: Prefix = TEXT("WaterBottle"); break;
	default: break; // Sell-tab (5) e.d.: geen koop-catalogus
	}
	if (!Prefix)
	{
		return TArray<FName>();
	}
	TArray<FName> Out;
	for (const FName& Id : GetSupplyCatalog())
	{
		if (Id.ToString().StartsWith(Prefix))
		{
			Out.Add(Id);
		}
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

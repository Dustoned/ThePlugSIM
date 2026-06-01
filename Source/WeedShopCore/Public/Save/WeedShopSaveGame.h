// UWeedShopSaveGame — opslagcontainer voor de gedeelde voortgang. Bewust uitbreidbaar; nu
// alleen kas + dag-tijd. Later: voorraad, planten, klant-relaties.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WeedShopSaveGame.generated.h"

// Eén inventory-item in de save (met THC%/kwaliteit behouden).
USTRUCT()
struct FInvSaveItem
{
	GENERATED_BODY()
	UPROPERTY() FName ItemId = NAME_None;
	UPROPERTY() int32 Quantity = 0;
	UPROPERTY() float Thc = 0.f;
	UPROPERTY() float QualityPct = 0.f;
};

// Per-speler opgeslagen staat, op username gekoppeld (co-op: ieder z'n eigen).
USTRUCT()
struct FPlayerSaveData
{
	GENERATED_BODY()
	UPROPERTY() FString PlayerName;
	UPROPERTY() int64 CashCents = 0;
	UPROPERTY() int64 BankCents = 0;
	UPROPERTY() bool bBankAppUnlocked = false;
	UPROPERTY() TArray<FInvSaveItem> Items;
};

UCLASS()
class WEEDSHOPCORE_API UWeedShopSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 SaveVersion = 2;

	// --- Gedeelde (host) wereld-staat ---
	UPROPERTY(VisibleAnywhere, Category = "Save")
	float TimeOfDaySeconds = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 DayNumber = 0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 TotalEarnedCents = 0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	uint8 MilestonePhase = 0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FName> PurchasedUpgrades;

	// --- Per-speler (op username) ---
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FPlayerSaveData> Players;

	// Compat: oude saves hadden alleen deze velden (host-cash/bank).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 BalanceCents = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 BankCents = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bBankAppUnlocked = false;
};

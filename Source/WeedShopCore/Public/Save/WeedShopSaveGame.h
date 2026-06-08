// UWeedShopSaveGame — opslagcontainer voor de gedeelde voortgang. Bewust uitbreidbaar; nu
// alleen kas + dag-tijd. Later: voorraad, planten, klant-relaties.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Npc/NpcRegistryComponent.h"   // FNpcState (NPC-relaties)
#include "Phone/ContactsComponent.h"    // FPhoneContact / FPhoneMessage
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
	UPROPERTY() int32 GridCell = -1; // slot-positie in de inventory-grid (-1 = onbekend/oude save)
};

// --- Geplaatste wereld-objecten ---
USTRUCT()
struct FSaveStack // shelf/chest-inhoud
{
	GENERATED_BODY()
	UPROPERTY() FName ItemId = NAME_None;
	UPROPERTY() int32 Quantity = 0;
	UPROPERTY() float Thc = 0.f;
	UPROPERTY() float QualityPct = 0.f;
};

USTRUCT()
struct FSaveDry // drogende batch op een rek
{
	GENERATED_BODY()
	UPROPERTY() FName DryItemId = NAME_None;
	UPROPERTY() int32 Quantity = 0;
	UPROPERTY() float Thc = 0.f;
	UPROPERTY() float Quality = 0.f;
	UPROPERTY() float Elapsed = 0.f;
	UPROPERTY() bool bDone = false;
	UPROPERTY() float OverTime = 0.f;
};

USTRUCT()
struct FSavePlantSlot // één plant-plek in een pot
{
	GENERATED_BODY()
	UPROPERTY() FName Strain = NAME_None;
	UPROPERTY() float Growth = 0.f;
	UPROPERTY() uint8 Phase = 0;
	UPROPERTY() uint8 Afflict = 0;      // 0 gezond, 1 mold, 2 pest
	UPROPERTY() float AfflictTime = 0.f;
};

USTRUCT()
struct FPlacedObjectSave
{
	GENERATED_BODY()
	UPROPERTY() FName ItemId = NAME_None;   // tier/id om mee te respawnen
	UPROPERTY() uint8 Kind = 0;             // 0 prop,1 pot,2 dryrack,3 bench,4 shelf,5 atm
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY() TArray<FSaveStack> ShelfItems;   // shelf/chest
	UPROPERTY() TArray<FSaveDry> DryEntries;     // droogrek

	// Pot/plant:
	UPROPERTY() int32 PotUpgradeMask = 0;
	UPROPERTY() FName SoilId = NAME_None;
	UPROPERTY() int32 SoilUsesLeft = 0;
	UPROPERTY() float CareMultiplier = 0.6f;
	UPROPERTY() float CareAvg = 0.6f;
	UPROPERTY() float WaterLevel = 0.6f;
	UPROPERTY() float FertYieldMult = 1.f; // mest-bonus (geldt voor 1 oogst)
	UPROPERTY() TArray<FSavePlantSlot> Slots;
};

// Een nog-onderweg telefoon-bestelling (items + aantallen). Bij laden meteen geleverd (je had al betaald).
USTRUCT()
struct FSavePendingDelivery
{
	GENERATED_BODY()
	UPROPERTY() TArray<FName> Ids;
	UPROPERTY() TArray<int32> Qtys;
};

// Per-speler opgeslagen staat, op username gekoppeld (co-op: ieder z'n eigen).
USTRUCT()
struct FPlayerSaveData
{
	GENERATED_BODY()
	UPROPERTY() FString PlayerId;    // stabiele platform-id (Steam/EOS net-id); leeg = offline
	UPROPERTY() FString PlayerName;  // weergavenaam (kan wijzigen; alleen fallback-match)
	UPROPERTY() int64 CashCents = 0;
	UPROPERTY() int64 BankCents = 0;
	UPROPERTY() bool bBankAppUnlocked = false;
	UPROPERTY() TArray<FInvSaveItem> Items;
	UPROPERTY() TArray<int32> HotbarCells; // per hotbar-slot: grid-cel van de toegewezen stapel (-1 = leeg)
	UPROPERTY() int32 ActiveSlot = 0;      // geselecteerd hotbar-slot
	UPROPERTY() int32 RentDueDay = 31;     // dag waarop de volgende huur gaat
	UPROPERTY() bool bRentIntroShown = false;
	UPROPERTY() int32 WaterCharges = 0;    // (legacy) water in je fles - water zit nu in de fles-stack Quality
	UPROPERTY() TArray<FSavePendingDelivery> Pending; // nog onderweg zijnde bestellingen
	UPROPERTY() int64 DepositedTodayCents = 0; // dag-limiet stort (anti-exploit bij reload)
	UPROPERTY() int32 DepositDay = 0;
	UPROPERTY() int32 TransfersToday = 0;      // dag-limiet overboekingen
	UPROPERTY() int64 LegitIncomeCents = 0;    // totale verkoop-omzet (witwas-"schone ruimte")
	UPROPERTY() int64 LaunderedCents = 0;      // totaal gestort (voor de witwas-heat)

	// Waar de speler stond op het moment van opslaan (bij laden gaat 'ie hier weer staan).
	UPROPERTY() bool bHasTransform = false;
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() FRotator Rotation = FRotator::ZeroRotator;

	// Gekochte woningen (indices in CityGenerator::ApartmentHomes) + de actieve woon-/spawn-plek.
	UPROPERTY() TArray<int32> OwnedHomes;
	UPROPERTY() int32 ActiveHome = -1;
};

UCLASS()
class WEEDSHOPCORE_API UWeedShopSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 SaveVersion = 3;

	// Wanneer deze save geschreven is (UTC). Voor "Continue" -> nieuwste van handmatig vs autosave.
	UPROPERTY(VisibleAnywhere, Category = "Save")
	FDateTime SavedAt = FDateTime(0);

	// True als dit een autosave-bestand is (apart van de handmatige save).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bIsAutosave = false;

	// --- Gedeelde (host) wereld-staat ---
	UPROPERTY(VisibleAnywhere, Category = "Save")
	float TimeOfDaySeconds = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 DayNumber = 0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 TotalEarnedCents = 0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	uint8 MilestonePhase = 0;

	// Goals/milestone-doelen: tellers + geclaimde doelen.
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 GoalJointsRolled = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 GoalPlantsHarvested = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 GoalDealsDone = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 GoalGramsSold = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 GoalGramsCrafted = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<int32> GoalClaimed;

	// Gedeeld crew-level (voor de save-info).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 CrewLevel = 1;

	// Shop-licentie verdiend (level 50-mijlpaal).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bShopLicensed = false;

	// Vrij bouwen (testing/sandbox) -> overal plaatsen toegestaan.
	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bFreeBuild = false;

	// Huidige politie-heat (gedeeld) + de event-cooldown-staat (bust/overval timer + laatste-event-dag).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	float Heat = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	float HeatEventTimer = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 HeatLastEventDay = -1000;

	// Totale real-life speeltijd in seconden (cumulatief over sessies).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	double PlaytimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FName> PurchasedUpgrades;

	// Crew-XP binnen het huidige level (samen met CrewLevel voor exact herstel).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 CrewXP = 0;

	// NPC-relaties: respect/loyaliteit/verslaving + ontgrendeld nummer, per persoon.
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FNpcState> Npcs;

	// Telefoon: contacten + chat-berichten.
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FPhoneContact> Contacts;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FPhoneMessage> Messages;

	// --- Per-speler (op username) ---
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FPlayerSaveData> Players;

	// --- Geplaatste wereld-objecten (potten/planten, shelves/chests, rekken, tafels, meubels, ATM) ---
	UPROPERTY(VisibleAnywhere, Category = "Save")
	TArray<FPlacedObjectSave> Placed;

	// Compat: oude saves hadden alleen deze velden (host-cash/bank).
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 BalanceCents = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 BankCents = 0;
	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bBankAppUnlocked = false;
};

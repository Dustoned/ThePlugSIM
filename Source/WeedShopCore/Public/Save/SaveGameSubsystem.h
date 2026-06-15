// USaveGameSubsystem — slaat de gedeelde voortgang op/laadt 'm. Overleeft level-load
// (GameInstance-subsystem). Alleen de host/server schrijft en herstelt de gedeelde state;
// clients krijgen het via replicatie.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class AWeedShopGameState;
class UWeedShopSaveGame;
class APawn;

// Startmodus bij een New Game.
UENUM(BlueprintType)
enum class EGameStartMode : uint8
{
	Normal   UMETA(DisplayName = "Normal"),
	Sandbox  UMETA(DisplayName = "Sandbox"),    // bergen geld + ruime spullen
	Testing  UMETA(DisplayName = "Testing")     // starter-budget + starter-items
};

// Samenvatting van één save-slot voor de menu-picker.
USTRUCT(BlueprintType)
struct FSaveSlotInfo
{
	GENERATED_BODY()
	UPROPERTY() bool bExists = false;
	UPROPERTY() int32 DayNumber = 0;
	UPROPERTY() int64 TotalCents = 0;     // contant + bank, opgeteld over alle spelers
	UPROPERTY() int32 CrewLevel = 1;
	UPROPERTY() double PlaytimeSeconds = 0.0;
	UPROPERTY() int32 NumPlayers = 0;
	UPROPERTY() FDateTime SavedAt = FDateTime(0);
	UPROPERTY() bool bIsAutosave = false; // het getoonde bestand is een autosave
};

UCLASS()
class WEEDSHOPCORE_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Save")
	FString SlotName = TEXT("WeedShopSave");

	// Aantal save-slots.
	static constexpr int32 NumSlots = 3;

	// Slot-keuze (welke slot Save/Load gebruiken).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void SetSlot(int32 Slot);
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	int32 GetSlot() const { return CurrentSlot; }

	// Bestaat er een save in dit slot? + korte samenvatting (false als leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool HasSaveInSlot(int32 Slot) const;
	bool GetSlotInfo(int32 Slot, FString& OutSummary) const;

	// Volledige info voor de menu-picker (day, saldo, level, speeltijd, spelers, tijdstip).
	// GetSlotDetails toont de handmatige save (of de autosave als er geen handmatige is).
	bool GetSlotDetails(int32 Slot, FSaveSlotInfo& Out) const;
	// Idem maar voor een specifiek bestand (handmatig of autosave).
	bool GetSlotDetailsEx(int32 Slot, bool bAutosave, FSaveSlotInfo& Out) const;

	// Bestaat er specifiek een handmatige / autosave in dit slot?
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save") bool HasManualSaveInSlot(int32 Slot) const;
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save") bool HasAutoSaveInSlot(int32 Slot) const;

	// Laad een specifiek bestand van een slot (handmatig of autosave).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadSlotSpecific(int32 Slot, bool bAutosave);

	// --- Echte start/load: HERLAAD het level voor een gegarandeerd schone lei ---
	// New Game: wis het slot, herlaad het level -> verse wereld (geen save toegepast). Mode bepaalt
	// de startstaat (Normal = kaal, Sandbox = veel geld + spullen, Testing = starter-budget + items).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void RequestNewGame(int32 Slot, EGameStartMode Mode = EGameStartMode::Normal);
	// Load: herlaad het level en pas daarna de gekozen save toe (handmatig of autosave).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool RequestLoad(int32 Slot, bool bAutosave);
	// Continue: herlaad + laad de nieuwste save (huidig slot, anders eerste slot met een save).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool RequestContinue();

	// --- LAN co-op ---
	// Host: start een verse co-op-game als listen-server (het level herlaadt mét ?listen).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void HostNewGameLan(int32 Slot, EGameStartMode Mode = EGameStartMode::Normal);
	// Co-op speelmodus voor de volgende verse host-game: false = Co-op (samen), true = Competitive (versus).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void SetPendingCoopCompetitive(bool bCompetitive) { bPendingCompetitive = bCompetitive; }

	// Stabiele speler-id (platform net-id; valt terug op naam) — voor per-speler keys (bv. competitive relaties).
	static FString StablePlayerId(const APawn* Pawn);
	// Join: verbind direct met een host op IP[:poort] (LAN). Bijv. "192.168.1.50" of "192.168.1.50:7777".
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void JoinLan(const FString& IpPort);

	// Door de (zojuist geladen) host-pawn aangeroepen zodra de wereld klaar is. Voert de geplande
	// actie uit. Geeft true als de start afgehandeld is (caller hoeft het titelscherm NIET te tonen).
	bool RunPendingOnWorldReady();

	// Het meest recente save-tijdstip over alle slots (handmatig + autosave). False = geen save.
	bool GetMostRecentSaveTime(FDateTime& Out) const;

	// Autosave aan/uit (in GConfig bewaard; default aan). De host slaat alleen automatisch op als aan.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool IsAutosaveEnabled() const { return bAutosaveEnabled; }
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void SetAutosaveEnabled(bool bEnabled);

	// Nieuw spel in dit slot starten (verse staat; oude save blijft tot je opnieuw opslaat).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	void NewGameInSlot(int32 Slot);

	// Map-selectie voor nieuwe games (menu): leeg = de huidige/standaard map. Loads/continues
	// reizen automatisch naar de map waarop de save gemaakt is (MapPath in de save).
	void SetPendingMap(const FString& MapPath) { PendingMapPath = MapPath; }
	FString PendingMapPath;

	// Laad een specifiek slot.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadSlot(int32 Slot);

	// Continue: laad het laatst gebruikte slot, anders het eerste bestaande. False als er geen is.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool QuickContinue();

	// Schrijft de huidige gedeelde state + ALLE verbonden spelers (op username) naar de slot.
	// Alleen de host/server. False bij client of geen GameState.
	// bAutosave = naar het aparte autosave-bestand schrijven (overschrijft je echte save NIET).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool SaveGame(bool bAutosave = false);

	// Laadt de slot, herstelt de gedeelde state + alle nu verbonden spelers (alleen server).
	// bPreferNewest = neem het nieuwste van handmatig/autosave (voor Continue). Anders: altijd de
	// echte (handmatige) save, en alleen de autosave als er nog geen handmatige save bestaat.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Save")
	bool LoadGame(bool bPreferNewest = false);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Save")
	bool HasSave() const;

	// True als er (nog) geen save geladen is = verse game. Bij een geladen game herstelt de save zelf de
	// geplaatste objecten, dus dan moeten wereld-fixtures (meubels/ATM) NIET opnieuw gespawnd worden.
	bool IsFreshGame() const { return Loaded == nullptr; }

	// Server: herstel één speler (op username) uit de geladen save, indien aanwezig en nog niet
	// hersteld deze sessie. Aangeroepen wanneer een (co-op) speler de wereld in komt.
	void RestorePlayerByPawn(APawn* Pawn);

protected:
	AWeedShopGameState* GetWeedGameState() const;
	bool HasAuthorityWorld() const;
	FString SlotNameFor(int32 Slot) const;
	FString AutoSlotNameFor(int32 Slot) const; // apart autosave-bestand naast de echte save
	// Kies welk bestand geladen moet worden voor het huidige slot ("" = geen save aanwezig).
	FString ResolveLoadName(bool bPreferNewest) const;
	// Laadt + herstelt een specifiek save-bestand (interne kern van LoadGame/LoadSlotSpecific).
	bool LoadGameFromName(const FString& Name);
	// Vult een FSaveSlotInfo uit een specifiek save-bestand.
	bool FillSlotInfo(const FString& Name, FSaveSlotInfo& Out) const;

	int32 CurrentSlot = 0;

	// Helpers om per-speler data te verzamelen/toe te passen + identiteit.
	static void PlayerKeys(const APawn* Pawn, FString& OutId, FString& OutName); // stabiele id + weergavenaam
	static bool Matches(const struct FPlayerSaveData& Rec, const FString& Id, const FString& Name);
	void GatherPlayer(APawn* Pawn, struct FPlayerSaveData& Out) const;
	void ApplyPlayer(APawn* Pawn, const struct FPlayerSaveData& Data);

	// Geplaatste wereld-objecten verzamelen / opnieuw spawnen.
	void GatherPlaced(class UWorld* World, TArray<struct FPlacedObjectSave>& Out) const;
	void RespawnPlaced(class UWorld* World, const TArray<struct FPlacedObjectSave>& In);

	// In het geheugen gehouden geladen save (voor late-joiners die nog hersteld moeten worden).
	UPROPERTY()
	TObjectPtr<UWeedShopSaveGame> Loaded;

	// Welke usernames deze sessie al hersteld zijn (voorkomt dubbel herstel bij respawn).
	TSet<FString> RestoredPlayers;

	// Speeltijd-meting: base = waarde bij sessie-start (uit save), mark = wanneer die sessie begon.
	double PlaytimeBaseSeconds = 0.0;
	FDateTime PlaytimeMark = FDateTime(0);
	double CurrentPlaytimeSeconds() const;

	bool bAutosaveEnabled = true;

	// Geplande actie die ná een level-herlaad wordt uitgevoerd.
	enum class EPending : uint8 { None, Fresh, Load };
	EPending Pending = EPending::None;
	FString PendingLoadName;
	EGameStartMode PendingStartMode = EGameStartMode::Normal;
	// De mode waarin deze sessie draait (overleeft de hele sessie, anders dan PendingStartMode die na
	// het toepassen reset). Bepaalt of dev-tools/free-build aan staan en wordt mee opgeslagen.
	EGameStartMode SessionStartMode = EGameStartMode::Normal;
	bool bPendingCompetitive = false; // co-op modus voor de volgende verse host-game
	void ReloadCurrentLevel(const FString& Options = TEXT(""));
	// Geef de host-speler de startstaat (geld + items) van de gekozen modus.
	void ApplyStartMode(EGameStartMode Mode);
};

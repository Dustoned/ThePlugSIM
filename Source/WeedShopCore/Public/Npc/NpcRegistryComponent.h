// UNpcRegistryComponent — het centrale register van alle NPC's op de GameState. Houdt per
// persoon persistente stats bij (respect/loyaliteit/verslaving), gevoed uit DT_NPCs. Klanten
// (ACustomerBase) krijgen een NpcId toegewezen en lezen/schrijven hun stats hier, zodat ze
// over spawns heen bewaard blijven. Bij genoeg loyaliteit krijg je het 'nummer' (contact).
//
// Server-authoritative; states repliceren naar de clients.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NpcRegistryComponent.generated.h"

class UDataTable;
class APawn;

USTRUCT(BlueprintType)
struct FNpcState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	FName NpcId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float Respect = 15.f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float Loyalty = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float Addiction = 10.f;

	// Nummer ontgrendeld (genoeg loyaliteit) -> staat in de contactenlijst.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	bool bUnlocked = false;

	// Tijdstip (monotone "abs" = dag*1e6 + tijd-op-dag) van de LAATSTE deal — in persoon óf telefoon.
	// -1 = nog nooit. Voor de per-NPC cooldown zodat dezelfde persoon niet meteen terugkomt.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float LastDealAbs = -1.f;

	// Telefoon-afspraken vandaag (cap ~1-2/dag). Reset bij een nieuwe dag.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	int32 ApptDay = -1;
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	int32 ApptCountToday = 0;
	// Wanneer deze NPC voor het laatst een afspraak VROEG (voor de cooldown tegen blijven-vragen).
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float LastApptAbs = -1.f;

	// Tijdstip (NowAbs) van de LAATSTE geweigerde deal. -1 = nooit. Aparte timer van LastDealAbs -> korte
	// her-aanbied-cooldown na een weigering ZONDER 'tevreden klant'-effecten. Repliceert mee via States.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float LastRefusalAbs = -1.f;

	// Tijdstip (NowAbs) van de LAATSTE gegeven sample (gratis joint). -1 = nooit. Eigen cooldown zodat je
	// een NPC niet instant kunt maxen met een stapel joints. Repliceert mee via States.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float LastSampleAbs = -1.f;

	// Cooldown-vermenigvuldiger op de afspraak-cooldown (snel antwoord = korter, traag/opgegeven = langer).
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float ApptCooldownMult = 1.f;

	// Vaste uiterlijk-skin (index in de globale NPC-skin-pool). 1x toegewezen (tier-gewogen) en daarna
	// NOOIT meer gewijzigd -> dezelfde persoon ziet er altijd hetzelfde uit (ook na tier-stijging/save).
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	int32 SkinIndex = -1;
	// Schema-versie van de skin-toewijzing. Oude saves (0) krijgen 1x een re-roll naar de bredere geklede
	// banden (anders blijven ze hangen op de oude smalle Karl-verdeling = veel dezelfde skins).
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	int32 SkinVer = 0;

	// --- Klant-tier/level (los van respect/loyaliteit) ---
	// Klantwaarde-XP: loopt op door deals (verkochte grammen x loyaliteit x persoonlijke honger). Bepaalt de tier.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	int32 CustomerXP = 0;
	// Persoonlijke variatie zodat niet iedereen gelijk is: hoe snel ze klimmen + hoe gulzig ze bestellen (~0.6..1.6).
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float ValueMult = 1.f;

	// COMPETITIVE: welke speler deze klant momenteel het meest mag (hoogste loyaliteit) + die loyaliteit.
	// Op de BASIS-NPC-entry bijgehouden om "klant afgepakt" te detecteren. Leeg = nog niemand.
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	FString TopPlayerId;
	UPROPERTY(BlueprintReadOnly, Category = "NPC")
	float TopLoyalty = 0.f;
};

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UNpcRegistryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNpcRegistryComponent();

	// Respect-drempel waarop een NPC z'n nummer deelt (contact). Bewust respect-gedreven.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float UnlockRespect = 45.f;

	// (Legacy) loyaliteit-drempel — niet meer de primaire unlock, behouden voor compat.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float UnlockLoyalty = 40.f;

	// Max telefoon-afspraken per NPC per dag (1-2 voelt natuurlijk).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	int32 MaxApptsPerDay = 2;

	// Cooldown (in dag-cyclus-seconden) waarin dezelfde NPC niet opnieuw deal/gevraagd wordt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float DealCooldownSeconds = 240.f;

	// Kortere cooldown (zelfde dag-cyclus-seconden als DealCooldownSeconds) na een GEWEIGERDE deal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float RefusalCooldownSeconds = 75.f;

	// Cooldown (dag-cyclus-seconden) tussen twee gratis joints aan dezelfde NPC -> geen instant-maxen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float SampleCooldownSeconds = 120.f;

	// Server: geef een NpcId uit aan een nieuwe klant (round-robin, slaat NPC's op cooldown over).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	FName AssignNpc();

	// Server: zorg dat een specifieke NPC bestaat, bv. een vaste bewoner met huisnummer-naam.
	FName EnsureNpc(FName NpcId, const FText& DisplayName, float BaseRespect = 15.f, float BaseLoyalty = 0.f, float BaseAddiction = 10.f);

	// Server: overschrijf ALLEEN de DisplayName van een bestaande NPC (geen nieuwe entry, geen stats-reset).
	// Voor de gender-correcte bewoner-naam die de pawn na BuildAppearance vaststelt. Repliceert via States.
	void SetDisplayName(FName NpcId, const FText& DisplayName);

	// Competitive: per-speler relatie-entry (sleutel "NpcId#spelerId"). Beide spelers starten met DEZELFDE
	// basis-personality (van de basis-NPC) zodat de competitie eerlijk begint; daarna bouwen ze los op.
	FName EnsurePlayerNpc(FName Key, FName BaseNpc, const FText& DisplayName);

	// Competitive: registreer de loyaliteit van een speler bij een klant (basis-NPC). Geeft true terug als
	// deze speler de klant zojuist AFPAKTE van een andere speler (die al een stevige band had). OutPrev = die rivaal.
	bool NotePlayerLoyalty(FName BaseNpc, const FString& PlayerId, float Loyalty, FString& OutPrevOwnerId);

	// Competitive: welke speler deze klant het meest mag (TopPlayerId). Leeg = nog niemand.
	FString GetTopOwner(FName BaseNpc) const;

	// Competitive: aantal vaste klanten van een speler (per-speler-sleutels "#spelerId" met een echte band).
	int32 CountPlayerCustomers(const FString& PlayerId) const;

	// Deterministische (1x) gerandomiseerde persoonlijkheid voor een NpcId. Zelfde id -> zelfde stats,
	// zonder dat de NPC al geregistreerd hoeft te zijn (zodat de spawner vooraf kan zien wie koper is).
	// Addiction is naar boven verdeeld: ~20% stevig verslaafd, ~25% boven de koop-drempel (30), rest lager.
	static void PredictPersonality(FName NpcId, float& OutRespect, float& OutLoyalty, float& OutAddiction);

	// Server: leg vast dat deze NPC zojuist een deal deed (in persoon of telefoon) -> start cooldown.
	// PlayerId = de dealende speler (competitive: dual-write basis + per-speler entry; leeg/co-op = alleen basis).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void MarkDealt(FName NpcId, const FString& PlayerId = FString());

	// Heeft deze NPC recent (binnen DealCooldownSeconds) een deal gedaan?
	// PlayerId = de vragende speler (competitive: leest de per-speler entry; leeg/co-op = basis, ongewijzigd).
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsOnCooldown(FName NpcId, const FString& PlayerId = FString()) const;

	// Refusal-cooldown: Mark* = dual-write (basis + per-speler entry in competitive), IsOn* = per-speler read.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void MarkRefused(FName NpcId, const FString& PlayerId = FString());
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsOnRefusalCooldown(FName NpcId, const FString& PlayerId = FString()) const;

	// Sample-cooldown: leg vast dat deze NPC zojuist een gratis joint kreeg / check of 'ie op cooldown is.
	// Zelfde per-speler patroon als de refusal-cooldown (PlayerId leeg/co-op = basis, ongewijzigd).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void MarkSampled(FName NpcId, const FString& PlayerId = FString());
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsOnSampleCooldown(FName NpcId, const FString& PlayerId = FString()) const;

	// Heeft deze NPC z'n nummer al gedeeld (contact)?
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsUnlocked(FName NpcId) const;

	// Mag deze NPC vandaag (nog) een telefoon-afspraak sturen (onder de dag-cap)? Bewust ALTIJD op de
	// basis-entry (competitive deelt de dag-cap, anders vraagt dezelfde NPC 2x per dag per speler).
	bool CanAppointToday(FName NpcId) const;
	// Leg vast dat er net een afspraak naar deze NPC is gestuurd (telt mee voor de dag-cap).
	// PlayerId (competitive): schrijft LastApptAbs ook op de per-speler entry; de dag-cap blijft basis.
	void NoteAppointment(FName NpcId, const FString& PlayerId = FString());

	// Zet de cooldown-multiplier voor de volgende afspraak-cooldown (snel antwoord < 1, traag/opgegeven > 1).
	// PlayerId (competitive): dual-write op basis + per-speler entry.
	void SetApptCooldownMult(FName NpcId, float Mult, const FString& PlayerId = FString());

	// --- Klant-tier (1=Casual .. 5=Whale), afgeleid van CustomerXP. Iedereen kan klimmen. ---
	// PlayerId-param (competitive): de tier/XP van de PER-SPELER relatie-entry ("BaseNpc#PlayerId");
	// leeg of co-op/solo = de basis-entry (bit-voor-bit ongewijzigd gedrag).
	// Server: tel klantwaarde op na een deal (grammen). Loyaliteit + persoonlijke honger schalen mee.
	// Dual-write: basis blijft ALTIJD meegroeien (skin/productsmaak) + de per-speler entry (order-grootte).
	void AddCustomerValue(FName NpcId, int32 GramsSold, const FString& PlayerId = FString());
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	int32 GetCustomerTier(FName NpcId, const FString& PlayerId = FString()) const;          // 1..5
	// Vaste uiterlijk-skin-index: 1x toegewezen (tier-gewogen via Seed) en daarna persistent bewaard.
	// Geeft de bewaarde index terug, of wijst er bij eerste keer één toe. Server-side.
	int32 GetOrAssignSkin(FName NpcId, int32 Tier, int32 Seed);
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	int32 GetCustomerXP(FName NpcId, const FString& PlayerId = FString()) const;
	// Naam van een tier (1..5) + de bestel-range (grammen) van een tier (met persoonlijke variatie via NpcId).
	static FString TierName(int32 Tier);
	// Tier van de RESOLVED key (per-speler band in competitive); ValueMult van de BASIS-entry
	// (immuun voor oude saves waar de per-key ValueMult nog niet geseed was).
	void GetTierOrderGrams(FName NpcId, int32& OutMin, int32& OutMax, const FString& PlayerId = FString()) const;
	static int32 TierFromXP(int32 XP);
	// Voortgang 0..1 binnen de huidige tier (1.0 = al Whale). Voor de XP-balk in de telefoon.
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	float GetTierProgress01(FName NpcId, const FString& PlayerId = FString()) const;

	// Lees de stats van een NPC (false als onbekend).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	bool GetStats(FName NpcId, float& OutRespect, float& OutLoyalty, float& OutAddiction, FText& OutName) const;

	// Server: schrijf de stats terug en check de contact-unlock. DealingPawn = de speler die deze relatie opbouwt
	// (competitive: die krijgt het contact + de unlock-toast; leeg/nullptr = co-op gedeeld, ongewijzigd).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void ApplyStats(FName NpcId, float Respect, float Loyalty, float Addiction, APawn* DealingPawn = nullptr);

	// Tellingen over ECHTE basis-NPC's; per-speler '#'-relatie-entries tellen niet mee (geen dubbele mensen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	int32 GetUnlockedCount() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	int32 GetTotalCount() const;

	// Save/load van alle NPC-relaties (respect/loyaliteit/verslaving + ontgrendeld).
	const TArray<FNpcState>& GetStatesForSave() const { return States; }
	void RestoreStates(const TArray<FNpcState>& In);

	// Testing/dev: zet IEDEREEN op goede stats (respect/loyaliteit/verslaving) + ontgrendeld, en zet een
	// paar contacten in de telefoon, zodat je meteen overal kunt dealen/appen voor grondig testen.
	void WarmAllForTesting(class UContactsComponent* Con);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY()
	TObjectPtr<UDataTable> NpcTable;

	UPROPERTY(Replicated)
	TArray<FNpcState> States;

	int32 AssignCursor = 0;

	// Vult States uit DT_NPCs als dat nog niet gebeurd is (server).
	void EnsureSeeded();

	FNpcState* Find(FName NpcId);
	const FNpcState* Find(FName NpcId) const;

	// Sleutel-resolutie voor per-speler relaties: PlayerId leeg OF geen competitive -> BaseNpc;
	// anders "BaseNpc#PlayerId" (zelfde formaat als EnsurePlayerNpc). Reads op een niet-bestaande
	// per-key vallen via Find gewoon op 0 XP / geen cooldown (= verse relatie, correct).
	FName ResolveNpcKey(FName BaseNpc, const FString& PlayerId) const;

	// Per-speler entry voor een resolved key ophalen; maakt 'm aan (EnsurePlayerNpc) als 'ie nog niet
	// bestaat, zodat een dual-write (cooldown/XP) nooit verloren gaat. Alleen aanroepen met Key != BaseNpc.
	FNpcState* FindOrAddPlayerEntry(FName BaseNpc, FName Key);

	// Monotone "nu"-tijd uit de dag-cyclus (dag*1e6 + tijd-op-dag); 0 als geen cyclus.
	float NowAbs() const;
	// Huidig dagnummer (0 als geen cyclus).
	int32 CurrentDay() const;

	// Check loyaliteit-drempel; ontgrendel het contact bij de ContactsComponent. DealingPawn = de speler die
	// deze relatie opbouwt (competitive: eigenaar van het contact + de unlock-toast; nullptr = co-op gedeeld).
	void CheckUnlock(FNpcState& State, APawn* DealingPawn = nullptr);
};

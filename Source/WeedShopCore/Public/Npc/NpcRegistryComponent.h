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

	// Server: geef een NpcId uit aan een nieuwe klant (round-robin, slaat NPC's op cooldown over).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	FName AssignNpc();

	// Server: zorg dat een specifieke NPC bestaat, bv. een vaste bewoner met huisnummer-naam.
	FName EnsureNpc(FName NpcId, const FText& DisplayName, float BaseRespect = 15.f, float BaseLoyalty = 0.f, float BaseAddiction = 10.f);

	// Deterministische (1x) gerandomiseerde persoonlijkheid voor een NpcId. Zelfde id -> zelfde stats,
	// zonder dat de NPC al geregistreerd hoeft te zijn (zodat de spawner vooraf kan zien wie koper is).
	// Addiction is naar boven verdeeld: ~20% stevig verslaafd, ~25% boven de koop-drempel (30), rest lager.
	static void PredictPersonality(FName NpcId, float& OutRespect, float& OutLoyalty, float& OutAddiction);

	// Server: leg vast dat deze NPC zojuist een deal deed (in persoon of telefoon) -> start cooldown.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void MarkDealt(FName NpcId);

	// Heeft deze NPC recent (binnen DealCooldownSeconds) een deal gedaan?
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsOnCooldown(FName NpcId) const;

	// Heeft deze NPC z'n nummer al gedeeld (contact)?
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsUnlocked(FName NpcId) const;

	// Mag deze NPC vandaag (nog) een telefoon-afspraak sturen (onder de dag-cap)?
	bool CanAppointToday(FName NpcId) const;
	// Leg vast dat er net een afspraak naar deze NPC is gestuurd (telt mee voor de dag-cap).
	void NoteAppointment(FName NpcId);

	// Lees de stats van een NPC (false als onbekend).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	bool GetStats(FName NpcId, float& OutRespect, float& OutLoyalty, float& OutAddiction, FText& OutName) const;

	// Server: schrijf de stats terug en check de contact-unlock.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void ApplyStats(FName NpcId, float Respect, float Loyalty, float Addiction);

	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	int32 GetUnlockedCount() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	int32 GetTotalCount() const { return States.Num(); }

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

	// Monotone "nu"-tijd uit de dag-cyclus (dag*1e6 + tijd-op-dag); 0 als geen cyclus.
	float NowAbs() const;
	// Huidig dagnummer (0 als geen cyclus).
	int32 CurrentDay() const;

	// Check loyaliteit-drempel; ontgrendel het contact bij de ContactsComponent.
	void CheckUnlock(FNpcState& State);
};

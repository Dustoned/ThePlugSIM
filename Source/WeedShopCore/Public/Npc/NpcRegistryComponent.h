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
};

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UNpcRegistryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNpcRegistryComponent();

	// Loyaliteit-drempel waarop je het nummer (contact) krijgt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float UnlockLoyalty = 40.f;

	// Cooldown (in dag-cyclus-seconden) waarin dezelfde NPC niet opnieuw deal/gevraagd wordt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|NPC")
	float DealCooldownSeconds = 240.f;

	// Server: geef een NpcId uit aan een nieuwe klant (round-robin, slaat NPC's op cooldown over).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	FName AssignNpc();

	// Server: leg vast dat deze NPC zojuist een deal deed (in persoon of telefoon) -> start cooldown.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	void MarkDealt(FName NpcId);

	// Heeft deze NPC recent (binnen DealCooldownSeconds) een deal gedaan?
	UFUNCTION(BlueprintPure, Category = "WeedShop|NPC")
	bool IsOnCooldown(FName NpcId) const;

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

	// Check loyaliteit-drempel; ontgrendel het contact bij de ContactsComponent.
	void CheckUnlock(FNpcState& State);
};

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

	// Server: geef een NpcId uit aan een nieuwe klant (round-robin over de roster).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|NPC")
	FName AssignNpc();

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

	// Check loyaliteit-drempel; ontgrendel het contact bij de ContactsComponent.
	void CheckUnlock(FNpcState& State);
};

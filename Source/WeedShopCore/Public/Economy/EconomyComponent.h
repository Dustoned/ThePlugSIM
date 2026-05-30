// UEconomyComponent — de (gedeelde) kas. Houdt het saldo in **eurocents** bij (geen float-money).
//
// CO-OP: server-authoritative. Het saldo repliceert naar alle clients; alleen de server mag
// muteren (AddMoney/RemoveMoney). Gameplay die geld oplevert/kost (deal afgerond, upgrade gekocht)
// draait sowieso op de server en roept deze functies daar aan. UI luistert op OnBalanceChanged.
//
// Bedoeld om op de **GameState** te zitten als gedeelde co-op-kas (zie AWeedShopGameState).
// Je kunt 'm later ook per speler op de PlayerState/pawn hangen voor persoonlijk geld.
//
// Let op level-load: de GameState (en dus dit saldo) wordt per map opnieuw gemaakt. Cross-map
// behoud loopt via het save/load-systeem, niet via dit component.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EconomyComponent.generated.h"

// Vuurt bij elke saldo-wijziging (server én clients). NewBalanceCents = nieuw saldo in cents.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBalanceChanged, int64, NewBalanceCents);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UEconomyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEconomyComponent();

	// Startsaldo in cents. Concept: €50,00 = 5000. Door de server gezet bij BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	int64 StartingBalanceCents = 5000;

	// UI/HUD bindt hierop om het saldo te tonen.
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Economy")
	FOnBalanceChanged OnBalanceChanged;

	// Server-authoritative. Voegt geld toe (cents, > 0). No-op op clients.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	void AddMoney(int64 AmountCents);

	// Server-authoritative. Schrijft af; geeft false bij onvoldoende saldo of op een client.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	bool RemoveMoney(int64 AmountCents);

	// Is er genoeg saldo voor dit bedrag?
	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	bool CanAfford(int64 AmountCents) const { return BalanceCents >= AmountCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetBalanceCents() const { return BalanceCents; }

	// Saldo in hele euro's (voor UI-weergave).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	float GetBalanceEuros() const { return static_cast<float>(BalanceCents) / 100.0f; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Balance)
	int64 BalanceCents = 0;

	UFUNCTION()
	void OnRep_Balance();

	// Zet het saldo (alleen server) en vuurt de delegate ook lokaal op de server (host).
	void SetBalance(int64 NewCents);
};

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

// Vuurt op de server bij inkomsten (AddMoney) — voedt o.a. milestone-voortgang.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMoneyEarned, int64, AmountCents);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UEconomyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEconomyComponent();

	// Start-cash in cents. Lean begin (€100); je verdient de rest door te kweken/dealen. Sandbox/Testing
	// overschrijven dit via ApplyStartMode. Door de server gezet bij BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	int64 StartingBalanceCents = 10000;

	// Start-bankgeld (wit). Lean begin: €0 op de bank.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	int64 StartingBankCents = 0;

	// UI/HUD bindt hierop om het saldo te tonen.
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Economy")
	FOnBalanceChanged OnBalanceChanged;

	// Alleen inkomsten (server). Milestones tellen hierop mee.
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Economy")
	FOnMoneyEarned OnMoneyEarned;

	// Server-authoritative. Voegt geld toe (cents, > 0) en telt als inkomsten (milestones). No-op op clients.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	void AddMoney(int64 AmountCents);

	// Als AddMoney, maar telt NIET als 'verdiend' (voor test/debug, niet voor milestone-voortgang).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	void AddMoneyUntracked(int64 AmountCents);

	// Server-authoritative. Schrijft af; geeft false bij onvoldoende saldo of op een client.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	bool RemoveMoney(int64 AmountCents);

	// Is er genoeg saldo voor dit bedrag?
	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	bool CanAfford(int64 AmountCents) const { return BalanceCents >= AmountCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetBalanceCents() const { return BalanceCents; }

	// Server-only: zet het saldo direct (voor save/load-herstel).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	void SetBalanceCents(int64 NewCents);

	// Server-only: zet het bankgeld direct (voor save/load-herstel).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	void SetBankCents(int64 NewCents);

	// Saldo in hele euro's (voor UI-weergave). LET OP: dit is het CASH-saldo (zwart geld).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	float GetBalanceEuros() const { return static_cast<float>(BalanceCents) / 100.0f; }

	// === Cash (zwart) vs Bank (wit) ===
	// Cash = AddMoney/RemoveMoney/GetBalanceCents hierboven (klanten betalen cash, in-persoon = cash).
	// Bank = wit geld voor online/legale dingen (telefoon-winkel, upgrades, workers). Belast bij storten.

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetCashCents() const { return BalanceCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetBankCents() const { return BankCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	float GetBankEuros() const { return static_cast<float>(BankCents) / 100.0f; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	bool CanAffordBank(int64 AmountCents) const { return BankCents >= AmountCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	bool IsBankInDebt() const { return BankCents < 0; }

	// Server: haal geld van de bank dat WEL in de min mag (huur/schuld). Negatief saldo = schuld.
	void ChargeBank(int64 AmountCents);

	// Server: bankgeld erbij (wit inkomen). bTaxed = trek de inkomstenbelasting eraf bij binnenkomst.
	void AddBank(int64 AmountCents, bool bTaxed);

	// Server: bankgeld eraf; false bij onvoldoende.
	bool RemoveBank(int64 AmountCents);

	// Server: storten = cash -> bank. Trekt belasting af (% bij binnenkomst), respecteert de dag-limiet
	// en verhoogt heat (grote stortingen = verdacht). Geeft het bedrag dat op de bank kwam (0 = mislukt).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	int64 Deposit(int64 CashAmount);

	// Belastingpercentage op bankgeld (bij binnenkomst). 0..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	float DepositTaxPct = 0.25f;

	// Hoeveel cash je per dag mag witwassen (storten).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	int64 DailyDepositLimitCents = 5000000; // EUR 50.000 / dag

	// Heat per EUR 1.000 gestort dat je MET verkoop kunt verklaren (laag - dit is "schoon" geld).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	float CleanDepositHeatPer1000 = 0.3f;

	// Heat per EUR 1.000 gestort dat je NIET met verkoop kunt verklaren (gehamsterd/rewards/transfers
	// zonder shop-omzet). DIT is veruit de heftigste heat-bron: zo loont witwassen zonder te verkopen niet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	float DirtyDepositHeatPer1000 = 22.0f;

	// Extra: hoe meer je op DEZELFDE dag al stortte, hoe verdachter (heat-vermenigvuldiger per EUR 1.000 vandaag).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	float DailyDumpHeatRamp = 0.025f;

	// Server: registreer LEGITIEME omzet (een echte product-verkoop). Bepaalt hoeveel je "schoon" kunt storten.
	void NoteLegitIncome(int64 Cents);

	// Hoeveel je nog "schoon" (verklaard door verkoop) kunt storten voordat het verdacht wordt.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetLaunderHeadroomCents() const { return FMath::Max<int64>(0, LegitIncomeCents - LaunderedCents); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetDepositedTodayCents() const { return DepositedTodayCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int64 GetDailyDepositRemainingCents() const { return FMath::Max<int64>(0, DailyDepositLimitCents - DepositedTodayCents); }

	// === Overboeken naar een co-op vriend (vanaf de ATM) ===
	// Fee als % van het bedrag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	float TransferFeePct = 0.05f;

	// Max aantal overboekingen per dag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Economy")
	int32 MaxTransfersPerDay = 3;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int32 GetTransfersToday() const { return TransfersToday; }

	// Save/load van de dag-limiet-tellers (zodat reload de limieten niet reset).
	int32 GetDepositDay() const { return DepositDay; }
	void RestoreDailyLimits(int64 Deposited, int32 InDepositDay, int32 Transfers)
	{
		DepositedTodayCents = FMath::Max<int64>(0, Deposited);
		DepositDay = InDepositDay;
		TransfersToday = FMath::Max(0, Transfers);
	}

	// Save/load van de witwas-boekhouding (legitieme omzet + totaal gestort).
	int64 GetLegitIncomeCents() const { return LegitIncomeCents; }
	int64 GetLaunderedCents() const { return LaunderedCents; }
	void RestoreLaunderState(int64 LegitIncome, int64 Laundered)
	{
		LegitIncomeCents = FMath::Max<int64>(0, LegitIncome);
		LaunderedCents = FMath::Max<int64>(0, Laundered);
	}

	UFUNCTION(BlueprintPure, Category = "WeedShop|Economy")
	int32 GetTransfersRemainingToday() const { return FMath::Max(0, MaxTransfersPerDay - TransfersToday); }

	// Server: boek bankgeld over naar een co-op vriend tegen een fee, binnen de dag-limiet.
	// Per-speler geld: bedrag + fee verlaten DEZE bank; de ontvanger wordt door de aanroeper
	// (PhoneClientComponent::ServerTransfer) bijgeschreven. Geeft true bij succes.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Economy")
	bool TransferBank(int64 AmountCents);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Balance)
	int64 BalanceCents = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Balance)
	int64 BankCents = 0;

	// Vandaag al gestort (voor de dag-limiet) + de dag waarop dat geldt.
	UPROPERTY(Replicated)
	int64 DepositedTodayCents = 0;
	int32 DepositDay = 0;

	// Vandaag al overgeboekt (aantal) — voor de transactielimiet.
	UPROPERTY(Replicated)
	int32 TransfersToday = 0;

	// Witwas-boekhouding: totale legitieme omzet (verkopen) vs totaal gestort. Het verschil bepaalt of
	// een storting "schoon" (verklaard door verkoop) of "vuil" (verdacht) is -> heat.
	UPROPERTY(Replicated)
	int64 LegitIncomeCents = 0;
	UPROPERTY(Replicated)
	int64 LaunderedCents = 0;

	UFUNCTION()
	void OnRep_Balance();

	// Zet het cash-saldo (alleen server) en vuurt de delegate ook lokaal op de server (host).
	void SetBalance(int64 NewCents);
	void SetBank(int64 NewCents);

	// Reset de dag-teller voor storten als er een nieuwe dag is.
	void RefreshDepositDay();
};

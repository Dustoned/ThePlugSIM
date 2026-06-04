// UMilestoneComponent — gedeelde co-op-progressie op de GameState. Telt totaal-verdiend mee
// (via EconomyComponent::OnMoneyEarned), bereikt milestones uit DT_Milestones, ontgrendelt
// producten en stuurt de fase-overgang (Straatdealer -> Winkel -> Franchise).
//
// Server-authoritative; voortgang + fase repliceren naar de clients.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/MilestoneRow.h"
#include "MilestoneComponent.generated.h"

class UDataTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMilestoneReached, FName, MilestoneId, const FText&, Description);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShopPhaseChanged, EShopPhase, NewPhase);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UMilestoneComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMilestoneComponent();

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Progression")
	FOnMilestoneReached OnMilestoneReached;

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Progression")
	FOnShopPhaseChanged OnShopPhaseChanged;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Progression")
	int64 GetTotalEarnedCents() const { return TotalEarnedCents; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Progression")
	EShopPhase GetCurrentPhase() const { return CurrentPhase; }

	// Server-only: herstel totaal-verdiend + fase (voor save/load).
	void RestoreState(int64 InTotalEarnedCents, uint8 InPhase);

	// Is een product al ontgrendeld (via een bereikte milestone, of milestone-loos)?
	UFUNCTION(BlueprintPure, Category = "WeedShop|Progression")
	bool IsProductUnlocked(FName ProductId) const;

	// Server: tel inkomsten mee (gebonden aan elke speler-portemonnee OnMoneyEarned).
	UFUNCTION()
	void HandleMoneyEarned(int64 AmountCents);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// DataTable met milestones (geladen uit /Game/_Project/Data/DT_Milestones).
	UPROPERTY()
	TObjectPtr<UDataTable> MilestoneTable;

	UPROPERTY(Replicated)
	int64 TotalEarnedCents = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Phase)
	EShopPhase CurrentPhase = EShopPhase::StreetDealer;

	UPROPERTY(Replicated)
	TArray<FName> ReachedMilestones;

	UFUNCTION()
	void OnRep_Phase();

	// bSilent = markeer bereikte milestones zonder toast/broadcast (voor save/load-herstel, anders
	// poppen alle al-gehaalde milestones opnieuw op bij elke Continue/Load).
	void CheckMilestones(bool bSilent = false);
};

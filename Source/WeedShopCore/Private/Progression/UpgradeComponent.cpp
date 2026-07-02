#include "Progression/UpgradeComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Data/UpgradeRow.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "Progression/MilestoneComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

UUpgradeComponent::UUpgradeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	static ConstructorHelpers::FObjectFinder<UDataTable> TableFinder(
		TEXT("/Game/_Project/Data/DT_Upgrades.DT_Upgrades"));
	if (TableFinder.Succeeded())
	{
		UpgradeTable = TableFinder.Object;
	}
}

void UUpgradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UUpgradeComponent, Purchased);
}

bool UUpgradeComponent::BuyUpgrade(FName UpgradeId, UEconomyComponent* PayFrom)
{
	if (GetOwnerRole() != ROLE_Authority || !UpgradeTable || UpgradeId.IsNone())
	{
		return false;
	}
	if (Purchased.Contains(UpgradeId))
	{
		return false;
	}

	const FUpgradeRow* Row = UpgradeTable->FindRow<FUpgradeRow>(UpgradeId, TEXT("BuyUpgrade"), false);
	if (!Row)
	{
		return false;
	}

	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	if (!GS)
	{
		return false;
	}

	// Co-op: leid de KOPER-pawn af uit de meegegeven portemonnee (PayFrom = de eigen EconomyComponent van
	// de koper, zowel via de telefoon als via de UpgradeStation). Zo landen alle meldingen op DIENS client
	// i.p.v. host-lokaal. Zonder payer (legacy/host-fallback) blijft de oude Notify(-1,...) staan.
	APawn* BuyerPawn = PayFrom ? Cast<APawn>(PayFrom->GetOwner()) : nullptr;

	// Fase-eis.
	if (GS->GetMilestones() && GS->GetMilestones()->GetCurrentPhase() < Row->RequiredPhase)
	{
		if (BuyerPawn)
		{
			UWeedToast::NotifyPawn(BuyerPawn, -1, 3.f, FColor::Orange,
				FString::Printf(TEXT("Not available yet: %s"), *Row->DisplayName.ToString()));
		}
		else if (GEngine)
		{
			UWeedToast::Notify(-1, 3.f, FColor::Orange,
				FString::Printf(TEXT("Not available yet: %s"), *Row->DisplayName.ToString()));
		}
		return false;
	}

	// Betalen — upgrades koop je via de telefoon (online/legaal) -> met BANKGELD (wit) van de koper.
	UEconomyComponent* Econ = PayFrom ? PayFrom : GS->GetEconomy();
	const int64 UpgCost = Row->CostCents > 0 ? FMath::Max<int64>(100, WeedRoundEuros((int64)Row->CostCents)) : 0;
	if (!Econ || !Econ->RemoveBank(UpgCost))
	{
		if (BuyerPawn)
		{
			UWeedToast::NotifyPawn(BuyerPawn, -1, 3.f, FColor::Red,
				FString::Printf(TEXT("Not enough BANK money for %s (launder cash first)"), *Row->DisplayName.ToString()));
		}
		else if (GEngine)
		{
			UWeedToast::Notify(-1, 3.f, FColor::Red,
				FString::Printf(TEXT("Not enough BANK money for %s (launder cash first)"), *Row->DisplayName.ToString()));
		}
		return false;
	}

	Purchased.Add(UpgradeId);
	OnUpgradePurchased.Broadcast(UpgradeId);
	UE_LOG(LogWeedShop, Log, TEXT("Upgrade bought: %s (%s)"), *UpgradeId.ToString(), *Row->DisplayName.ToString());
	if (BuyerPawn)
	{
		UWeedToast::NotifyPawn(BuyerPawn, -1, 4.f, FColor::Green,
			FString::Printf(TEXT("Upgrade purchased: %s"), *Row->DisplayName.ToString()));
	}
	else if (GEngine)
	{
		UWeedToast::Notify(-1, 4.f, FColor::Green,
			FString::Printf(TEXT("Upgrade purchased: %s"), *Row->DisplayName.ToString()));
	}
	return true;
}

TArray<FName> UUpgradeComponent::GetAllUpgradeIds() const
{
	return UpgradeTable ? UpgradeTable->GetRowNames() : TArray<FName>();
}

void UUpgradeComponent::RestorePurchased(const TArray<FName>& InIds)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	Purchased = InIds;
	for (const FName& Id : Purchased) { OnUpgradePurchased.Broadcast(Id); } // effecten/UI bijwerken
}

bool UUpgradeComponent::GetUpgradeDisplay(FName UpgradeId, FText& OutName, int32& OutCostCents,
	bool& bOutPurchased, bool& bOutAvailable) const
{
	if (!UpgradeTable)
	{
		return false;
	}
	const FUpgradeRow* Row = UpgradeTable->FindRow<FUpgradeRow>(UpgradeId, TEXT("GetUpgradeDisplay"), false);
	if (!Row)
	{
		return false;
	}
	OutName = Row->DisplayName;
	OutCostCents = Row->CostCents > 0 ? (int32)FMath::Max<int64>(100, WeedRoundEuros((int64)Row->CostCents)) : 0;
	bOutPurchased = Purchased.Contains(UpgradeId);

	bOutAvailable = true;
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UMilestoneComponent* M = GS->GetMilestones())
		{
			bOutAvailable = M->GetCurrentPhase() >= Row->RequiredPhase;
		}
	}
	return true;
}

float UUpgradeComponent::GetEffectTotal(FName EffectTag) const
{
	if (!UpgradeTable || EffectTag.IsNone())
	{
		return 0.f;
	}

	float Total = 0.f;
	for (const FName& Id : Purchased)
	{
		const FUpgradeRow* Row = UpgradeTable->FindRow<FUpgradeRow>(Id, TEXT("GetEffectTotal"), false);
		if (Row && Row->EffectTag == EffectTag)
		{
			Total += Row->EffectMagnitude;
		}
	}
	return Total;
}

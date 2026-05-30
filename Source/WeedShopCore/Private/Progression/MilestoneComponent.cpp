#include "Progression/MilestoneComponent.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

UMilestoneComponent::UMilestoneComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Laad de milestone-tabel (mag ontbreken bij eerste run vóór import).
	static ConstructorHelpers::FObjectFinder<UDataTable> TableFinder(
		TEXT("/Game/_Project/Data/DT_Milestones.DT_Milestones"));
	if (TableFinder.Succeeded())
	{
		MilestoneTable = TableFinder.Object;
	}
}

void UMilestoneComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwnerRole() == ROLE_Authority)
	{
		if (AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
		{
			if (UEconomyComponent* Econ = GS->GetEconomy())
			{
				Econ->OnMoneyEarned.AddDynamic(this, &UMilestoneComponent::HandleMoneyEarned);
			}
		}
		CheckMilestones();
	}
}

void UMilestoneComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMilestoneComponent, TotalEarnedCents);
	DOREPLIFETIME(UMilestoneComponent, CurrentPhase);
	DOREPLIFETIME(UMilestoneComponent, ReachedMilestones);
}

void UMilestoneComponent::HandleMoneyEarned(int64 AmountCents)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	TotalEarnedCents += AmountCents;
	CheckMilestones();
}

void UMilestoneComponent::CheckMilestones()
{
	if (!MilestoneTable)
	{
		return;
	}

	TArray<FName> RowNames = MilestoneTable->GetRowNames();
	for (const FName& RowName : RowNames)
	{
		if (ReachedMilestones.Contains(RowName))
		{
			continue;
		}
		const FMilestoneRow* Row = MilestoneTable->FindRow<FMilestoneRow>(RowName, TEXT("CheckMilestones"), false);
		if (!Row || TotalEarnedCents < Row->ThresholdEarnedCents)
		{
			continue;
		}

		ReachedMilestones.Add(RowName);
		OnMilestoneReached.Broadcast(RowName, Row->Description);
		UE_LOG(LogWeedShop, Log, TEXT("Milestone bereikt: %s (%s)"), *RowName.ToString(), *Row->Description.ToString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Magenta,
				FString::Printf(TEXT("Milestone: %s"), *Row->Description.ToString()));
		}

		if (Row->UnlockPhase > CurrentPhase)
		{
			CurrentPhase = Row->UnlockPhase;
			OnShopPhaseChanged.Broadcast(CurrentPhase);
		}
	}
}

void UMilestoneComponent::OnRep_Phase()
{
	OnShopPhaseChanged.Broadcast(CurrentPhase);
}

bool UMilestoneComponent::IsProductUnlocked(FName ProductId) const
{
	if (ProductId.IsNone() || !MilestoneTable)
	{
		return true; // zonder data niets blokkeren
	}

	// Zoek of dit product door een milestone wordt ge-unlockt; zo niet -> vrij beschikbaar.
	bool bGatedByMilestone = false;
	for (const FName& RowName : MilestoneTable->GetRowNames())
	{
		const FMilestoneRow* Row = MilestoneTable->FindRow<FMilestoneRow>(RowName, TEXT("IsProductUnlocked"), false);
		if (Row && Row->UnlockProductId == ProductId)
		{
			bGatedByMilestone = true;
			if (ReachedMilestones.Contains(RowName))
			{
				return true;
			}
		}
	}
	return !bGatedByMilestone;
}

#include "Progression/GoalsComponent.h"
#include "Net/UnrealNetwork.h"
#include "Game/WeedShopGameState.h"
#include "Progression/MilestoneComponent.h"

UGoalsComponent::UGoalsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGoalsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGoalsComponent, JointsRolled);
	DOREPLIFETIME(UGoalsComponent, PlantsHarvested);
	DOREPLIFETIME(UGoalsComponent, DealsDone);
	DOREPLIFETIME(UGoalsComponent, GramsSold);
	DOREPLIFETIME(UGoalsComponent, GramsCrafted);
	DOREPLIFETIME(UGoalsComponent, ClaimedIdx);
}

const TArray<FGoalDef>& UGoalsComponent::Goals()
{
	// Vaste doelen met oplopende targets + scalende rewards (item + geld).
	// Metric: 0 geld(cents), 1 joints, 2 oogst, 3 deals, 4 gram verkocht, 5 gram gecraft (machines).
	static const TArray<FGoalDef> G = {
		// --- Deals ---
		{ TEXT("Make your first 5 deals"),    3, 5,     TEXT("Cont_Bag2"), 10,            1000 },
		{ TEXT("Close 25 deals"),             3, 25,    TEXT("Seed_NorthernLights"), 3,   5000 },
		{ TEXT("Close 100 deals"),            3, 100,   TEXT("Cont_Jar10"), 5,           20000 },
		{ TEXT("Close 500 deals"),            3, 500,   TEXT("Gear_Lamp2"), 1,          100000 },
		{ TEXT("Close 1.000 deals - kingpin"),3, 1000,  TEXT("Gear_Lamp3"), 1,          300000 },
		// --- Joints ---
		{ TEXT("Roll 10 joints"),             1, 10,    TEXT("Papers_Small"), 10,         1500 },
		{ TEXT("Roll 50 joints"),             1, 50,    TEXT("Papers_Big"), 10,           4000 },
		{ TEXT("Roll 100 joints"),            1, 100,   TEXT("Seed_AmnesiaHaze"), 3,      8000 },
		{ TEXT("Roll 500 joints"),            1, 500,   TEXT("Gear_Bloom"), 2,           40000 },
		{ TEXT("Roll 2.500 joints"),          1, 2500,  TEXT("Papers_Backwoods"), 10,   120000 },
		// --- Harvest ---
		{ TEXT("Harvest 5 plants"),           2, 5,     TEXT("Soil_Basic"), 5,            2000 },
		{ TEXT("Harvest 25 plants"),          2, 25,    TEXT("Pot_Clay"), 3,              8000 },
		{ TEXT("Harvest 100 plants"),         2, 100,   TEXT("Soil_Rich"), 5,            30000 },
		{ TEXT("Harvest 250 plants"),         2, 250,   TEXT("Gear_Tent2"), 1,           80000 },
		{ TEXT("Harvest 1.000 plants"),       2, 1000,  TEXT("Gear_Lamp3"), 1,          250000 },
		// --- Money earned ---
		{ TEXT("Earn EUR 1.000 total"),       0, 100000,    TEXT("Cont_Bag5"), 10,        5000 },
		{ TEXT("Earn EUR 10.000 total"),      0, 1000000,   TEXT("Gear_Water1"), 1,      30000 },
		{ TEXT("Earn EUR 100.000 total"),     0, 10000000,  TEXT("Gear_Lamp3"), 1,      200000 },
		{ TEXT("Earn EUR 1.000.000 - boss"),  0, 100000000, TEXT("Gear_Tent2"), 2,     1000000 },
		// --- Grams sold (dealen aan klanten) ---
		{ TEXT("Sell 100g of product"),       4, 100,   TEXT("Cont_Bag5"), 10,            6000 },
		{ TEXT("Sell 1.000g of product"),     4, 1000,  TEXT("Cont_Jar15"), 5,           35000 },
		{ TEXT("Sell 10.000g - heavy weight"),4, 10000, TEXT("Gear_Bloom"), 3,          200000 },
		// --- Crafted in the lab/kitchen (crystals/hash/edibles) ---
		{ TEXT("Craft 50g in the lab"),       5, 50,    TEXT("Butter"), 5,               10000 },
		{ TEXT("Craft 500g in the lab"),      5, 500,   TEXT("Oven_Std"), 1,             60000 },
		{ TEXT("Craft 5.000g - master chef"), 5, 5000,  TEXT("Gear_Tent2"), 1,          300000 },
	};
	return G;
}

int64 UGoalsComponent::GetMetricValue(int32 Metric) const
{
	switch (Metric)
	{
	case 1: return JointsRolled;
	case 2: return PlantsHarvested;
	case 3: return DealsDone;
	case 4: return GramsSold;
	case 5: return GramsCrafted;
	case 0:
	default:
	{
		if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
		{
			if (const UMilestoneComponent* M = GS->GetMilestones()) { return M->GetTotalEarnedCents(); }
		}
		return 0;
	}
	}
}

int64 UGoalsComponent::GetGoalProgress(int32 Idx) const
{
	if (!Goals().IsValidIndex(Idx)) { return 0; }
	return FMath::Min(GetMetricValue(Goals()[Idx].Metric), Goals()[Idx].Target);
}

bool UGoalsComponent::IsComplete(int32 Idx) const
{
	return Goals().IsValidIndex(Idx) && GetMetricValue(Goals()[Idx].Metric) >= Goals()[Idx].Target;
}

bool UGoalsComponent::IsClaimed(int32 Idx) const
{
	return ClaimedIdx.Contains(Idx);
}

int32 UGoalsComponent::GetClaimableCount() const
{
	int32 N = 0;
	for (int32 i = 0; i < Goals().Num(); ++i) { if (IsComplete(i) && !IsClaimed(i)) { ++N; } }
	return N;
}

void UGoalsComponent::NoteJointsRolled(int32 N)
{
	if (GetOwnerRole() == ROLE_Authority && N > 0) { JointsRolled += N; }
}

void UGoalsComponent::NoteHarvest(int32 N)
{
	if (GetOwnerRole() == ROLE_Authority && N > 0) { PlantsHarvested += N; }
}

void UGoalsComponent::NoteDeal()
{
	if (GetOwnerRole() == ROLE_Authority) { ++DealsDone; }
}

void UGoalsComponent::NoteGramsSold(int32 Grams)
{
	if (GetOwnerRole() == ROLE_Authority && Grams > 0) { GramsSold += Grams; }
}

void UGoalsComponent::NoteCrafted(int32 Grams)
{
	if (GetOwnerRole() == ROLE_Authority && Grams > 0) { GramsCrafted += Grams; }
}

bool UGoalsComponent::MarkClaimed(int32 Idx)
{
	if (GetOwnerRole() != ROLE_Authority) { return false; }
	if (!IsComplete(Idx) || IsClaimed(Idx)) { return false; }
	ClaimedIdx.Add(Idx);
	return true;
}

void UGoalsComponent::RestoreState(int32 Joints, int32 Harvests, int32 Deals, const TArray<int32>& Claimed)
{
	JointsRolled = Joints; PlantsHarvested = Harvests; DealsDone = Deals; ClaimedIdx = Claimed;
}

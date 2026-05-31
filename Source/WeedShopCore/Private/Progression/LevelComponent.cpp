#include "Progression/LevelComponent.h"

#include "WeedShopCore.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

ULevelComponent::ULevelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void ULevelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ULevelComponent, Level);
	DOREPLIFETIME(ULevelComponent, CurrentXP);
}

int32 ULevelComponent::XPForLevel(int32 Lvl)
{
	// Oplopende curve: 100 + (Lvl-1)*40. Lvl1->2 = 100, ... Lvl99->100 = 4020.
	// Totaal tot level 100 ~ 204.000 XP. 0 op/over max.
	if (Lvl < 1 || Lvl >= MaxLevel) { return 0; }
	return 100 + (Lvl - 1) * 40;
}

float ULevelComponent::GetLevelFraction() const
{
	const int32 Need = XPForLevel(Level);
	if (Need <= 0) { return 1.f; }
	return FMath::Clamp(float(CurrentXP) / float(Need), 0.f, 1.f);
}

void ULevelComponent::AddXP(int32 Amount)
{
	if (GetOwnerRole() != ROLE_Authority || Amount <= 0 || Level >= MaxLevel)
	{
		return;
	}
	CurrentXP += Amount;

	bool bLeveled = false;
	while (Level < MaxLevel)
	{
		const int32 Need = XPForLevel(Level);
		if (Need <= 0 || CurrentXP < Need) { break; }
		CurrentXP -= Need;
		++Level;
		bLeveled = true;
	}
	if (Level >= MaxLevel) { CurrentXP = 0; }

	if (bLeveled)
	{
		OnLevelUp.Broadcast(Level);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor(120, 220, 255),
				FString::Printf(TEXT("LEVEL UP!  You reached level %d"), Level));
		}
	}
}

void ULevelComponent::OnRep_Level()
{
	// Hook voor clients (UI werkt al via de pure getters elke frame).
}

#include "Cultivation/WaterCanComponent.h"

#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"

UWaterCanComponent::UWaterCanComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWaterCanComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWaterCanComponent, WaterCharges);
}

UInventoryComponent* UWaterCanComponent::GetInv() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

int32 UWaterCanComponent::GetMaxCharges() const
{
	const UInventoryComponent* Inv = GetInv();
	if (!Inv)
	{
		return 0;
	}
	// Betere flessen later = meer capaciteit. Nu: plastic fles = 3 slokken.
	if (Inv->HasItem(FName(TEXT("WaterBottle_Plastic")), 1))
	{
		return 3;
	}
	return 0;
}

void UWaterCanComponent::Fill()
{
	WaterCharges = GetMaxCharges();
}

bool UWaterCanComponent::TryUseCharge()
{
	if (GetMaxCharges() <= 0 || WaterCharges <= 0)
	{
		return false;
	}
	--WaterCharges;
	return true;
}

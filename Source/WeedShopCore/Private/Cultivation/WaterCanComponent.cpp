#include "Cultivation/WaterCanComponent.h"

#include "Cultivation/BottleTypes.h"
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
	// Capaciteit = som over alle flessen die je draagt (betere fles = meer slokken).
	int32 Total = 0;
	for (const FBottleDef& B : GetAllBottles())
	{
		Total += B.Charges * Inv->GetQuantity(B.ItemId);
	}
	return Total;
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

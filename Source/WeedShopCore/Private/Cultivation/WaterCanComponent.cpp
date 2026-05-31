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
	// Elke fles die je draagt voegt capaciteit toe (3 slokken per plastic fles). Meer flessen =
	// meer water dat je kunt meenemen. (Betere flessen kun je later met meer capaciteit toevoegen.)
	return Inv->GetQuantity(FName(TEXT("WaterBottle_Plastic"))) * 3;
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

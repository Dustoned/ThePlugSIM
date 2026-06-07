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
	if (!Inv) { return 0; }
	// Alleen de fles die je NU in je hand hebt telt (niet alle flessen samen).
	const FName Active = Inv->GetActiveItemId();
	for (const FBottleDef& B : GetAllBottles())
	{
		if (B.ItemId == Active) { return B.Charges; }
	}
	return 0;
}

void UWaterCanComponent::Fill()
{
	const int32 Max = GetMaxCharges();
	if (Max <= 0) { return; }                                  // geen fles in de hand
	if (WaterCharges > Max) { WaterCharges = Max; }            // kleinere fles vastgehouden -> klem
	WaterCharges = FMath::Min(Max, WaterCharges + FillPerClick); // per klik een beetje (grotere fles = vaker klikken)
}

bool UWaterCanComponent::TryUseCharge()
{
	const int32 Max = GetMaxCharges();
	if (Max <= 0) { return false; }                 // alleen waterbottle-in-de-hand kan water geven
	if (WaterCharges > Max) { WaterCharges = Max; }
	if (WaterCharges <= 0) { return false; }
	--WaterCharges;
	return true;
}

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
	DOREPLIFETIME(UWaterCanComponent, Waters);
}

UInventoryComponent* UWaterCanComponent::GetInv() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

FName UWaterCanComponent::ActiveBottleId() const
{
	const UInventoryComponent* Inv = GetInv();
	if (!Inv) { return NAME_None; }
	const FName Act = Inv->GetActiveItemId();
	return Act.ToString().StartsWith(TEXT("WaterBottle")) ? Act : NAME_None;
}

int32& UWaterCanComponent::WaterRef(FName BottleId)
{
	for (FBottleWater& W : Waters) { if (W.BottleId == BottleId) { return W.Charges; } }
	FBottleWater New; New.BottleId = BottleId; New.Charges = 0;
	const int32 Idx = Waters.Add(New);
	return Waters[Idx].Charges;
}

int32 UWaterCanComponent::GetMaxCharges() const
{
	const FName Id = ActiveBottleId();
	if (Id.IsNone()) { return 0; }
	for (const FBottleDef& B : GetAllBottles())
	{
		if (B.ItemId == Id) { return B.Charges; }
	}
	return 0;
}

int32 UWaterCanComponent::GetCharges() const
{
	const FName Id = ActiveBottleId();
	if (Id.IsNone()) { return 0; }
	for (const FBottleWater& W : Waters) { if (W.BottleId == Id) { return W.Charges; } }
	return 0;
}

void UWaterCanComponent::Fill()
{
	const FName Id = ActiveBottleId();
	const int32 Max = GetMaxCharges();
	if (Id.IsNone() || Max <= 0) { return; }   // geen fles in de hand
	int32& C = WaterRef(Id);
	if (C > Max) { C = Max; }
	C = FMath::Min(Max, C + FillPerClick);      // alleen DEZE fles vult (per klik een beetje)
}

bool UWaterCanComponent::TryUseCharge()
{
	const FName Id = ActiveBottleId();
	const int32 Max = GetMaxCharges();
	if (Id.IsNone() || Max <= 0) { return false; }
	int32& C = WaterRef(Id);
	if (C > Max) { C = Max; }
	if (C <= 0) { return false; }
	--C;
	return true;
}

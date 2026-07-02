#include "Cultivation/WaterCanComponent.h"

#include "Cultivation/BottleTypes.h"
#include "Inventory/InventoryComponent.h"

UWaterCanComponent::UWaterCanComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

int32 UWaterCanComponent::ActiveBottleStackId() const
{
	const UInventoryComponent* Inv = GetInv();
	if (!Inv || ActiveBottleId().IsNone()) { return 0; }
	return Inv->GetActiveStackId();
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

float UWaterCanComponent::GetWaterPerClick() const
{
	FBottleDef Def;
	if (GetBottleDef(ActiveBottleId(), Def)) { return Def.WaterPerClick; }
	return 0.25f; // fallback (geen fles / onbekende id)
}

int32 UWaterCanComponent::GetCharges() const
{
	const UInventoryComponent* Inv = GetInv();
	const int32 StackId = ActiveBottleStackId();
	if (!Inv || StackId == 0) { return 0; }
	// Water zit in het Quality-veld van DEZE fles-stack (per fles).
	return FMath::Max(0, FMath::RoundToInt(Inv->GetStackQualityById(StackId)));
}

void UWaterCanComponent::Fill()
{
	UInventoryComponent* Inv = GetInv();
	const int32 StackId = ActiveBottleStackId();
	const int32 Max = GetMaxCharges();
	if (!Inv || StackId == 0 || Max <= 0) { return; } // geen fles in de hand
	const int32 Cur = FMath::Min(GetCharges(), Max);   // klem (kleinere fles)
	Inv->SetStackQualityById(StackId, static_cast<float>(FMath::Min(Max, Cur + FillPerClick)));
}

bool UWaterCanComponent::TryUseCharge()
{
	UInventoryComponent* Inv = GetInv();
	const int32 StackId = ActiveBottleStackId();
	const int32 Max = GetMaxCharges();
	if (!Inv || StackId == 0 || Max <= 0) { return false; }
	const int32 Cur = FMath::Min(GetCharges(), Max);
	if (Cur <= 0) { return false; }
	Inv->SetStackQualityById(StackId, static_cast<float>(Cur - 1));
	return true;
}

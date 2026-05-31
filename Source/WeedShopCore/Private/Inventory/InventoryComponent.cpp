#include "Inventory/InventoryComponent.h"

#include "WeedShopCore.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInventoryComponent, Stacks);
}

bool UInventoryComponent::IsStackable(FName ItemId)
{
	// Waterflessen zijn niet-stapelbaar: elke fles een eigen slot.
	return !ItemId.ToString().StartsWith(TEXT("WaterBottle"));
}

int32 UInventoryComponent::FindStackIndex(FName ItemId) const
{
	return Stacks.IndexOfByPredicate([ItemId](const FInventoryStack& S) { return S.ItemId == ItemId; });
}

int32 UInventoryComponent::FindStackById(int32 StackId) const
{
	if (StackId == 0) { return INDEX_NONE; }
	return Stacks.IndexOfByPredicate([StackId](const FInventoryStack& S) { return S.StackId == StackId; });
}

bool UInventoryComponent::AddItem(FName ItemId, int32 Count, float Quality)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("AddItem genegeerd: alleen de server mag de voorraad muteren."));
		return false;
	}
	if (ItemId.IsNone() || Count <= 0)
	{
		return false;
	}

	// Gewicht-limiet.
	if (MaxWeight > 0.f && GetTotalWeight() + GetUnitWeight(ItemId) * Count > MaxWeight + 0.001f)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Inventory too heavy — sell or drop something.")); }
		return false;
	}

	const bool bStackable = IsStackable(ItemId);

	// Slot-limiet: nieuwe stapels die erbij komen.
	if (MaxStacks > 0)
	{
		const int32 ExistingIdx = FindStackIndex(ItemId);
		const int32 NewStacks = bStackable ? (ExistingIdx != INDEX_NONE ? 0 : 1) : Count;
		if (GetUsedSlots() + NewStacks > MaxStacks)
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No free inventory slots.")); }
			return false;
		}
	}

	if (bStackable)
	{
		const int32 Index = FindStackIndex(ItemId);
		if (Index != INDEX_NONE)
		{
			if (Quality >= 0.f)
			{
				const int32 OldQty = Stacks[Index].Quantity;
				Stacks[Index].Quality = (Stacks[Index].Quality * OldQty + Quality * Count) / FMath::Max(1, OldQty + Count);
			}
			Stacks[Index].Quantity += Count;
		}
		else
		{
			FInventoryStack NewStack;
			NewStack.ItemId = ItemId;
			NewStack.Quantity = Count;
			NewStack.Quality = FMath::Max(0.f, Quality);
			NewStack.StackId = NextStackId++;
			Stacks.Add(NewStack);
		}
	}
	else
	{
		// Niet-stapelbaar: Count losse stapels van 1 (elk eigen slot/StackId).
		for (int32 i = 0; i < Count; ++i)
		{
			FInventoryStack NewStack;
			NewStack.ItemId = ItemId;
			NewStack.Quantity = 1;
			NewStack.Quality = FMath::Max(0.f, Quality);
			NewStack.StackId = NextStackId++;
			Stacks.Add(NewStack);
		}
	}

	OnRep_Stacks();
	return true;
}

float UInventoryComponent::GetItemQuality(FName ItemId) const
{
	const int32 Index = FindStackIndex(ItemId);
	return Index != INDEX_NONE ? Stacks[Index].Quality : 0.f;
}

float UInventoryComponent::GetUnitWeight(FName ItemId) const
{
	const FString S = ItemId.ToString();
	if (S.StartsWith(TEXT("Bud_")))    { return 0.005f; }
	if (S.StartsWith(TEXT("Seed_")))   { return 0.002f; }
	if (S.StartsWith(TEXT("Joint_")))  { return 0.01f; }
	if (S.StartsWith(TEXT("Papers_"))) { return 0.05f; }
	if (S.StartsWith(TEXT("Soil_")))   { return 1.5f; }
	if (S.StartsWith(TEXT("Pot")))     { return 4.0f; }
	if (S.StartsWith(TEXT("WaterBottle"))) { return 0.6f; }
	return 0.2f;
}

float UInventoryComponent::GetTotalWeight() const
{
	float W = 0.f;
	for (const FInventoryStack& S : Stacks)
	{
		W += GetUnitWeight(S.ItemId) * S.Quantity;
	}
	return W;
}

bool UInventoryComponent::IsStackOnHotbar(int32 StackId) const
{
	return StackId != 0 && HotbarStacks.Contains(StackId);
}

void UInventoryComponent::UnassignHotbarStack(int32 StackId)
{
	for (int32& S : HotbarStacks)
	{
		if (S == StackId) { S = 0; }
	}
}

bool UInventoryComponent::RemoveItem(FName ItemId, int32 Count)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("RemoveItem genegeerd: alleen de server mag de voorraad muteren."));
		return false;
	}
	if (ItemId.IsNone() || Count <= 0 || GetQuantity(ItemId) < Count)
	{
		return false;
	}

	// Haal Count weg, eventueel over meerdere stapels (laatste eerst zodat StackId's stabiel blijven).
	int32 Remaining = Count;
	for (int32 i = Stacks.Num() - 1; i >= 0 && Remaining > 0; --i)
	{
		if (Stacks[i].ItemId != ItemId) { continue; }
		const int32 Take = FMath::Min(Remaining, Stacks[i].Quantity);
		Stacks[i].Quantity -= Take;
		Remaining -= Take;
		if (Stacks[i].Quantity <= 0)
		{
			UnassignHotbarStack(Stacks[i].StackId);
			Stacks.RemoveAt(i);
		}
	}

	OnRep_Stacks();
	return true;
}

int32 UInventoryComponent::GetQuantity(FName ItemId) const
{
	int32 Total = 0;
	for (const FInventoryStack& S : Stacks)
	{
		if (S.ItemId == ItemId) { Total += S.Quantity; }
	}
	return Total;
}

void UInventoryComponent::SetActiveSlot(int32 Slot)
{
	ActiveSlot = FMath::Clamp(Slot, 0, HotbarSize - 1);
}

void UInventoryComponent::CycleActiveSlot(int32 Dir)
{
	if (Dir == 0) { return; }
	ActiveSlot = ((ActiveSlot + (Dir > 0 ? 1 : -1)) % HotbarSize + HotbarSize) % HotbarSize;
}

int32 UInventoryComponent::GetHotbarStackId(int32 Slot) const
{
	return HotbarStacks.IsValidIndex(Slot) ? HotbarStacks[Slot] : 0;
}

FName UInventoryComponent::GetActiveItemId() const
{
	const int32 Idx = FindStackById(GetHotbarStackId(ActiveSlot));
	return Stacks.IsValidIndex(Idx) ? Stacks[Idx].ItemId : NAME_None;
}

void UInventoryComponent::AssignHotbarStack(int32 Slot, int32 StackId)
{
	if (HotbarStacks.Num() != HotbarSize) { HotbarStacks.SetNum(HotbarSize); }
	if (!HotbarStacks.IsValidIndex(Slot)) { return; }
	// Stond de stapel al in een ander slot? Wissel die twee.
	const int32 Existing = HotbarStacks.IndexOfByKey(StackId);
	if (Existing != INDEX_NONE && Existing != Slot)
	{
		HotbarStacks[Existing] = HotbarStacks[Slot];
	}
	HotbarStacks[Slot] = StackId;
}

void UInventoryComponent::RefreshHotbarAuto()
{
	if (HotbarStacks.Num() != HotbarSize) { HotbarStacks.SetNum(HotbarSize); }

	// 1) Verwijder toewijzingen van stapels die niet meer bestaan.
	for (int32& S : HotbarStacks)
	{
		if (S != 0 && FindStackById(S) == INDEX_NONE) { S = 0; }
	}

	// 2) Vul lege slots met stapels die nog nergens op de hotbar staan.
	for (const FInventoryStack& Stack : Stacks)
	{
		if (HotbarStacks.Contains(Stack.StackId)) { continue; }
		const int32 Empty = HotbarStacks.IndexOfByKey(0);
		if (Empty != INDEX_NONE) { HotbarStacks[Empty] = Stack.StackId; }
	}
}

void UInventoryComponent::OnRep_Stacks()
{
	RefreshHotbarAuto();
	OnInventoryChanged.Broadcast();
}

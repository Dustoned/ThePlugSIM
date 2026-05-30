#include "Inventory/InventoryComponent.h"

#include "WeedShopCore.h"
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

int32 UInventoryComponent::FindStackIndex(FName ItemId) const
{
	return Stacks.IndexOfByPredicate([ItemId](const FInventoryStack& S) { return S.ItemId == ItemId; });
}

bool UInventoryComponent::AddItem(FName ItemId, int32 Count)
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

	const int32 Index = FindStackIndex(ItemId);
	if (Index != INDEX_NONE)
	{
		Stacks[Index].Quantity += Count;
	}
	else
	{
		if (MaxStacks > 0 && Stacks.Num() >= MaxStacks)
		{
			UE_LOG(LogWeedShop, Verbose, TEXT("AddItem: geen ruimte voor nieuwe stapel %s (MaxStacks=%d)."),
				*ItemId.ToString(), MaxStacks);
			return false;
		}
		Stacks.Add(FInventoryStack{ ItemId, Count });
	}

	OnRep_Stacks(); // server: lokaal broadcasten (OnRep draait hier niet vanzelf)
	return true;
}

bool UInventoryComponent::RemoveItem(FName ItemId, int32 Count)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("RemoveItem genegeerd: alleen de server mag de voorraad muteren."));
		return false;
	}
	if (ItemId.IsNone() || Count <= 0)
	{
		return false;
	}

	const int32 Index = FindStackIndex(ItemId);
	if (Index == INDEX_NONE || Stacks[Index].Quantity < Count)
	{
		return false;
	}

	Stacks[Index].Quantity -= Count;
	if (Stacks[Index].Quantity <= 0)
	{
		Stacks.RemoveAt(Index);
	}

	OnRep_Stacks();
	return true;
}

int32 UInventoryComponent::GetQuantity(FName ItemId) const
{
	const int32 Index = FindStackIndex(ItemId);
	return Index != INDEX_NONE ? Stacks[Index].Quantity : 0;
}

void UInventoryComponent::SetActiveSlot(int32 Slot)
{
	ActiveSlot = FMath::Clamp(Slot, 0, HotbarSize - 1);
}

void UInventoryComponent::CycleActiveSlot(int32 Dir)
{
	if (Dir == 0)
	{
		return;
	}
	// Wrap binnen de hotbar (0..HotbarSize-1).
	ActiveSlot = ((ActiveSlot + (Dir > 0 ? 1 : -1)) % HotbarSize + HotbarSize) % HotbarSize;
}

FName UInventoryComponent::GetHotbarItem(int32 Slot) const
{
	return HotbarSlots.IsValidIndex(Slot) ? HotbarSlots[Slot] : NAME_None;
}

FName UInventoryComponent::GetActiveItemId() const
{
	const FName Item = GetHotbarItem(ActiveSlot);
	// Alleen "in de hand" als je het ook echt op voorraad hebt.
	return (!Item.IsNone() && GetQuantity(Item) > 0) ? Item : NAME_None;
}

void UInventoryComponent::AssignHotbar(int32 Slot, FName ItemId)
{
	if (!HotbarSlots.IsValidIndex(Slot))
	{
		// Zorg dat de array bestaat.
		HotbarSlots.SetNum(HotbarSize);
		if (!HotbarSlots.IsValidIndex(Slot))
		{
			return;
		}
	}
	// Zat het item al in een ander slot? Wissel die twee (verplaatsen, niet dupliceren).
	const int32 Existing = HotbarSlots.IndexOfByKey(ItemId);
	if (Existing != INDEX_NONE && Existing != Slot)
	{
		HotbarSlots[Existing] = HotbarSlots[Slot];
	}
	HotbarSlots[Slot] = ItemId;
}

void UInventoryComponent::RefreshHotbarAuto()
{
	if (HotbarSlots.Num() != HotbarSize)
	{
		HotbarSlots.SetNum(HotbarSize); // vult met NAME_None
	}

	// 1) Verwijder toewijzingen van items die niet meer op voorraad zijn.
	for (FName& Slot : HotbarSlots)
	{
		if (!Slot.IsNone() && GetQuantity(Slot) <= 0)
		{
			Slot = NAME_None;
		}
	}

	// 2) Vul lege slots met voorraad die nog nergens in de hotbar staat.
	for (const FInventoryStack& S : Stacks)
	{
		if (HotbarSlots.Contains(S.ItemId))
		{
			continue;
		}
		const int32 Empty = HotbarSlots.IndexOfByKey(NAME_None);
		if (Empty != INDEX_NONE)
		{
			HotbarSlots[Empty] = S.ItemId;
		}
	}
}

void UInventoryComponent::OnRep_Stacks()
{
	RefreshHotbarAuto();
	OnInventoryChanged.Broadcast();
}

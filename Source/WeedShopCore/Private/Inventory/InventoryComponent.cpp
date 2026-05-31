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

int32 UInventoryComponent::FindStackIndex(FName ItemId) const
{
	return Stacks.IndexOfByPredicate([ItemId](const FInventoryStack& S) { return S.ItemId == ItemId; });
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

	// Gewicht-limiet (geldt voor zowel nieuwe als bestaande stapels).
	if (MaxWeight > 0.f && GetTotalWeight() + GetUnitWeight(ItemId) * Count > MaxWeight + 0.001f)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Inventory too heavy — sell or drop something."));
		}
		return false;
	}

	// Slot-limiet: elke waterfles kost een slot (ook bij samenvoegen), andere items 1 per nieuwe stapel.
	if (MaxStacks > 0)
	{
		const bool bBottle = ItemId.ToString().StartsWith(TEXT("WaterBottle"));
		const int32 ExistingIdx = FindStackIndex(ItemId);
		const int32 ExtraSlots = bBottle ? Count : (ExistingIdx != INDEX_NONE ? 0 : 1);
		if (GetUsedSlots() + ExtraSlots > MaxStacks)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No free inventory slots."));
			}
			return false;
		}
	}

	const int32 Index = FindStackIndex(ItemId);
	if (Index != INDEX_NONE)
	{
		// Kwaliteit gewogen middelen bij samenvoegen (alleen als er kwaliteit-info is).
		if (Quality >= 0.f)
		{
			const int32 OldQty = Stacks[Index].Quantity;
			const float OldQ = Stacks[Index].Quality;
			Stacks[Index].Quality = (OldQ * OldQty + Quality * Count) / FMath::Max(1, OldQty + Count);
		}
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
		FInventoryStack NewStack;
		NewStack.ItemId = ItemId;
		NewStack.Quantity = Count;
		NewStack.Quality = FMath::Max(0.f, Quality);
		Stacks.Add(NewStack);
	}

	OnRep_Stacks(); // server: lokaal broadcasten (OnRep draait hier niet vanzelf)
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
	if (S.StartsWith(TEXT("Bud_")))    { return 0.005f; } // per gram
	if (S.StartsWith(TEXT("Seed_")))   { return 0.002f; }
	if (S.StartsWith(TEXT("Joint_")))  { return 0.01f; }
	if (S.StartsWith(TEXT("Papers_"))) { return 0.05f; } // per pakje
	if (S.StartsWith(TEXT("Soil_")))   { return 1.5f; }  // zware zak
	if (S.StartsWith(TEXT("Pot")))     { return 4.0f; }  // zware pot
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

int32 UInventoryComponent::GetUsedSlots() const
{
	int32 Used = 0;
	for (const FInventoryStack& S : Stacks)
	{
		// Waterflessen nemen elk een eigen slot in; andere stapels tellen als 1.
		Used += S.ItemId.ToString().StartsWith(TEXT("WaterBottle")) ? S.Quantity : 1;
	}
	return Used;
}

bool UInventoryComponent::IsOnHotbar(FName ItemId) const
{
	return HotbarSlots.Contains(ItemId);
}

void UInventoryComponent::UnassignHotbar(FName ItemId)
{
	for (FName& Slot : HotbarSlots)
	{
		if (Slot == ItemId) { Slot = NAME_None; }
	}
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

	// Vul lege hotbar-slots automatisch met voorraad die nog nergens in de hotbar staat. Items op
	// de hotbar worden niet in de inventory-grid getoond, dus geen dubbele weergave.
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

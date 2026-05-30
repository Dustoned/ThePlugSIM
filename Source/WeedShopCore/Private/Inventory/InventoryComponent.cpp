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

void UInventoryComponent::OnRep_Stacks()
{
	OnInventoryChanged.Broadcast();
}

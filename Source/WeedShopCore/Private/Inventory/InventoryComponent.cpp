#include "Inventory/InventoryComponent.h"

#include "WeedShopCore.h"
#include "Placement/PlaceableTypes.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Meubels (placeables die geen pot zijn) horen NIET automatisch op de hotbar; die sleep je
	// er zelf op als je ze wil verplaatsen. Bruikbare items (seeds/wiet/pots/flessen/...) wel.
	bool IsFurnitureItem(FName ItemId)
	{
		FPlaceableDef Def;
		return GetPlaceableDef(ItemId, Def) && !Def.bIsPot;
	}
}

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

int32 UInventoryComponent::FindMergeStackIndex(FName ItemId, float ThcPercent, float QualityPct) const
{
	// Items zonder kwaliteit-info (seeds/papers/soil, of thc/quality < 0): merge gewoon op item-id.
	const bool bMatchQuality = (ThcPercent >= 0.f || QualityPct >= 0.f);
	for (int32 i = 0; i < Stacks.Num(); ++i)
	{
		if (Stacks[i].ItemId != ItemId) { continue; }
		if (!bMatchQuality)
		{
			return i;
		}
		// Wiet/joints: alleen samen als THC% én Kwaliteit% (vrijwel) gelijk zijn.
		if (FMath::Abs(Stacks[i].Quality - FMath::Max(0.f, ThcPercent)) < 0.5f &&
			FMath::Abs(Stacks[i].QualityPct - FMath::Max(0.f, QualityPct)) < 0.5f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UInventoryComponent::CountStacksOf(FName ItemId) const
{
	int32 N = 0;
	for (const FInventoryStack& S : Stacks) { if (S.ItemId == ItemId) { ++N; } }
	return N;
}

void UInventoryComponent::GetMergePreview(FName ItemId, int32& OutQty, float& OutThcPercent, float& OutQualityPct, int32& OutBatches) const
{
	int32 Qty = 0, Batches = 0; double ThcSum = 0.0, QSum = 0.0;
	for (const FInventoryStack& S : Stacks)
	{
		if (S.ItemId != ItemId) { continue; }
		Qty += S.Quantity;
		ThcSum += double(S.Quality) * S.Quantity;
		QSum += double(S.QualityPct) * S.Quantity;
		++Batches;
	}
	OutQty = Qty;
	OutBatches = Batches;
	OutThcPercent = Qty > 0 ? float(ThcSum / Qty) : 0.f;
	OutQualityPct = Qty > 0 ? float(QSum / Qty) : 0.f;
}

bool UInventoryComponent::MergeItem(FName ItemId)
{
	if (GetOwnerRole() != ROLE_Authority) { return false; }
	if (CountStacksOf(ItemId) < 2) { return false; }

	int32 Qty = 0; double ThcSum = 0.0, QSum = 0.0; int32 KeepIdx = INDEX_NONE;
	for (int32 i = 0; i < Stacks.Num(); ++i)
	{
		if (Stacks[i].ItemId != ItemId) { continue; }
		Qty += Stacks[i].Quantity;
		ThcSum += double(Stacks[i].Quality) * Stacks[i].Quantity;
		QSum += double(Stacks[i].QualityPct) * Stacks[i].Quantity;
		if (KeepIdx == INDEX_NONE) { KeepIdx = i; }
	}
	if (KeepIdx == INDEX_NONE || Qty <= 0) { return false; }

	// Eén stapel houden met het gewogen gemiddelde; de rest weghalen (en van de hotbar af).
	Stacks[KeepIdx].Quantity = Qty;
	Stacks[KeepIdx].Quality = float(ThcSum / Qty);
	Stacks[KeepIdx].QualityPct = float(QSum / Qty);
	for (int32 i = Stacks.Num() - 1; i >= 0; --i)
	{
		if (i == KeepIdx || Stacks[i].ItemId != ItemId) { continue; }
		UnassignHotbarStack(Stacks[i].StackId);
		Stacks.RemoveAt(i);
	}

	OnRep_Stacks();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Merged into %dx (THC %.0f%%, Quality %.0f%%)"), Qty, Stacks[FindStackIndex(ItemId)].Quality, Stacks[FindStackIndex(ItemId)].QualityPct));
	}
	return true;
}

int32 UInventoryComponent::FindStackById(int32 StackId) const
{
	if (StackId == 0) { return INDEX_NONE; }
	return Stacks.IndexOfByPredicate([StackId](const FInventoryStack& S) { return S.StackId == StackId; });
}

bool UInventoryComponent::AddItem(FName ItemId, int32 Count, float ThcPercent, float QualityPct)
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
		const int32 ExistingIdx = FindMergeStackIndex(ItemId, ThcPercent, QualityPct);
		const int32 NewStacks = bStackable ? (ExistingIdx != INDEX_NONE ? 0 : 1) : Count;
		if (GetUsedSlots() + NewStacks > MaxStacks)
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No free inventory slots.")); }
			return false;
		}
	}

	if (bStackable)
	{
		// Wiet met afwijkende kwaliteit komt in een eigen stapel (mergen doe je later bewust).
		const int32 Index = FindMergeStackIndex(ItemId, ThcPercent, QualityPct);
		if (Index != INDEX_NONE)
		{
			const int32 OldQty = Stacks[Index].Quantity;
			const int32 NewQty = FMath::Max(1, OldQty + Count);
			// THC% en Kwaliteit% middelen, gewogen op aantal (mengen verschillende oogsten).
			if (ThcPercent >= 0.f)
			{
				Stacks[Index].Quality = (Stacks[Index].Quality * OldQty + ThcPercent * Count) / NewQty;
			}
			if (QualityPct >= 0.f)
			{
				Stacks[Index].QualityPct = (Stacks[Index].QualityPct * OldQty + QualityPct * Count) / NewQty;
			}
			Stacks[Index].Quantity += Count;
		}
		else
		{
			FInventoryStack NewStack;
			NewStack.ItemId = ItemId;
			NewStack.Quantity = Count;
			NewStack.Quality = FMath::Max(0.f, ThcPercent);
			NewStack.QualityPct = FMath::Max(0.f, QualityPct);
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
			NewStack.Quality = FMath::Max(0.f, ThcPercent);
			NewStack.QualityPct = FMath::Max(0.f, QualityPct);
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

float UInventoryComponent::GetItemQualityPct(FName ItemId) const
{
	const int32 Index = FindStackIndex(ItemId);
	return Index != INDEX_NONE ? Stacks[Index].QualityPct : 0.f;
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

	// 2) Vul lege slots met stapels die nog nergens op de hotbar staan (meubels uitgezonderd).
	for (const FInventoryStack& Stack : Stacks)
	{
		if (HotbarStacks.Contains(Stack.StackId)) { continue; }
		if (IsFurnitureItem(Stack.ItemId)) { continue; }
		const int32 Empty = HotbarStacks.IndexOfByKey(0);
		if (Empty != INDEX_NONE) { HotbarStacks[Empty] = Stack.StackId; }
	}
}

void UInventoryComponent::OnRep_Stacks()
{
	RefreshHotbarAuto();
	OnInventoryChanged.Broadcast();
}

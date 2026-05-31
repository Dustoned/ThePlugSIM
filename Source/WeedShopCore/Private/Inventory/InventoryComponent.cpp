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

void UInventoryComponent::SetCashDisplayEuros(int64 Euros)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const FName Cash(TEXT("Cash"));
	const int32 Q = (int32)FMath::Clamp<int64>(Euros, 0, (int64)MAX_int32);
	const int32 Idx = FindStackIndex(Cash);
	bool bChanged = false;

	if (Q <= 0)
	{
		if (Idx != INDEX_NONE) { UnassignHotbarStack(Stacks[Idx].StackId); Stacks.RemoveAt(Idx); bChanged = true; }
	}
	else if (Idx == INDEX_NONE)
	{
		FInventoryStack NewStack;
		NewStack.ItemId = Cash;
		NewStack.Quantity = Q;
		NewStack.StackId = NextStackId++;
		Stacks.Add(NewStack);
		bChanged = true;
	}
	else if (Stacks[Idx].Quantity != Q)
	{
		Stacks[Idx].Quantity = Q;
		bChanged = true;
	}

	if (bChanged) { OnRep_Stacks(); }
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
	if (S == TEXT("Cash")) { return 0.f; } // briefgeld weegt (praktisch) niets
	if (S.StartsWith(TEXT("WetBud_"))) { return 0.007f; } // nat is iets zwaarder
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
	bool bChanged = false;
	for (int32& S : HotbarStacks)
	{
		if (S == StackId) { S = 0; bChanged = true; }
	}
	if (bChanged) { OnInventoryChanged.Broadcast(); }
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
	KnownStacks.Add(StackId); // handmatig geplaatst telt als "gezien"
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::RefreshHotbarAuto()
{
	if (HotbarStacks.Num() != HotbarSize) { HotbarStacks.SetNum(HotbarSize); }

	// 1) Verwijder toewijzingen van stapels die niet meer bestaan.
	for (int32& S : HotbarStacks)
	{
		if (S != 0 && FindStackById(S) == INDEX_NONE) { S = 0; }
	}
	// Vergeten stapels uit de "gezien"-lijst halen zodat StackId's later hergebruikt kunnen worden.
	for (auto It = KnownStacks.CreateIterator(); It; ++It)
	{
		if (FindStackById(*It) == INDEX_NONE) { It.RemoveCurrent(); }
	}

	// 2) Zet ALLEEN gloednieuwe stapels (nog nooit gezien) automatisch op een leeg slot. Een handmatige
	//    unassign blijft zo gerespecteerd — we vullen 'm niet meteen weer terug. (meubels uitgezonderd)
	for (const FInventoryStack& Stack : Stacks)
	{
		if (KnownStacks.Contains(Stack.StackId)) { continue; }
		KnownStacks.Add(Stack.StackId);
		if (IsFurnitureItem(Stack.ItemId)) { continue; }
		if (Stack.ItemId == TEXT("Cash")) { continue; } // briefgeld hoort niet op de hotbar
		if (HotbarStacks.Contains(Stack.StackId)) { continue; }
		const int32 Empty = HotbarStacks.IndexOfByKey(0);
		if (Empty != INDEX_NONE) { HotbarStacks[Empty] = Stack.StackId; }
	}
}

void UInventoryComponent::RefreshGridAuto()
{
	const int32 Cells = (MaxStacks > 0) ? MaxStacks : Stacks.Num();
	if (GridOrder.Num() != Cells) { GridOrder.SetNum(Cells); } // SetNum behoudt bestaande posities

	// Verwijder verdwenen stapels (laat het gat staan zodat de rest blijft staan).
	for (int32& S : GridOrder)
	{
		if (S != 0 && FindStackById(S) == INDEX_NONE) { S = 0; }
	}
	// Nieuwe stapels in de eerste vrije cel zetten (en daarna blijven ze daar).
	for (const FInventoryStack& St : Stacks)
	{
		if (GridOrder.Contains(St.StackId)) { continue; }
		const int32 Empty = GridOrder.IndexOfByKey(0);
		if (Empty != INDEX_NONE) { GridOrder[Empty] = St.StackId; }
		else { GridOrder.Add(St.StackId); }
	}
}

int32 UInventoryComponent::CategoryRank(FName ItemId)
{
	const FString S = ItemId.ToString();
	if (S.StartsWith(TEXT("Bud_")))         { return 0; }
	if (S.StartsWith(TEXT("Joint_")))       { return 1; }
	if (S.StartsWith(TEXT("Seed_")))        { return 2; }
	if (S.StartsWith(TEXT("Papers_")))      { return 3; }
	if (S.StartsWith(TEXT("Pot_")))         { return 4; }
	if (S.StartsWith(TEXT("Soil_")))        { return 5; }
	if (S.StartsWith(TEXT("WaterBottle_"))) { return 6; }
	return 9;
}

void UInventoryComponent::MoveStackToCell(int32 StackId, int32 Cell)
{
	if (StackId == 0 || !GridOrder.IsValidIndex(Cell)) { return; }
	const int32 Cur = GridOrder.IndexOfByKey(StackId);
	if (Cur == INDEX_NONE || Cur == Cell) { return; }
	// Wissel de inhoud van bron- en doelcel (doel kan leeg of bezet zijn).
	GridOrder[Cur] = GridOrder[Cell];
	GridOrder[Cell] = StackId;
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::SortGrid(int32 Mode)
{
	RefreshGridAuto();
	TArray<int32> Ids;
	for (int32 S : GridOrder) { if (S != 0) { Ids.Add(S); } }

	Ids.Sort([this, Mode](int32 A, int32 B)
	{
		const int32 ia = FindStackById(A), ib = FindStackById(B);
		if (!Stacks.IsValidIndex(ia) || !Stacks.IsValidIndex(ib)) { return false; }
		const FInventoryStack& SA = Stacks[ia];
		const FInventoryStack& SB = Stacks[ib];
		if (Mode == 1) // aantal (hoog -> laag), gelijk -> op naam
		{
			if (SA.Quantity != SB.Quantity) { return SA.Quantity > SB.Quantity; }
		}
		else if (Mode == 2) // categorie, daarna naam
		{
			const int32 ca = CategoryRank(SA.ItemId), cb = CategoryRank(SB.ItemId);
			if (ca != cb) { return ca < cb; }
		}
		return SA.ItemId.LexicalLess(SB.ItemId);
	});

	for (int32 i = 0; i < GridOrder.Num(); ++i)
	{
		GridOrder[i] = Ids.IsValidIndex(i) ? Ids[i] : 0;
	}
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::OnRep_Stacks()
{
	RefreshHotbarAuto();
	RefreshGridAuto();
	OnInventoryChanged.Broadcast();
}

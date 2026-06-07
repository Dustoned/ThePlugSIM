#include "Inventory/InventoryComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"

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

bool UInventoryComponent::IsBag(FName ItemId)
{
	return ItemId.ToString().StartsWith(TEXT("Bag_"));
}

int32 UInventoryComponent::BagGrams(FName ItemId)
{
	const FString S = ItemId.ToString();
	int32 U;
	if (!S.FindLastChar('_', U)) { return 0; }
	const FString Tail = S.RightChop(U + 1);
	return Tail.IsNumeric() ? FCString::Atoi(*Tail) : 0;
}

FName UInventoryComponent::BagStrain(FName ItemId)
{
	FString S = ItemId.ToString();
	if (S.StartsWith(TEXT("Bag_"))) { S = S.RightChop(4); }
	int32 U;
	if (S.FindLastChar('_', U))
	{
		const FString Tail = S.RightChop(U + 1);
		if (Tail.IsNumeric()) { S = S.Left(U); } // strip de maat-suffix
	}
	return FName(*S);
}

FName UInventoryComponent::MakeBagId(FName Strain, int32 Grams)
{
	return FName(*FString::Printf(TEXT("Bag_%s_%d"), *Strain.ToString(), FMath::Max(1, Grams)));
}

int32 UInventoryComponent::BagGramsAvailable(FName Strain) const
{
	int32 Total = 0;
	for (const FInventoryStack& S : Stacks)
	{
		if (IsBag(S.ItemId) && BagStrain(S.ItemId) == Strain) { Total += S.Quantity * BagGrams(S.ItemId); }
	}
	return Total;
}

int32 UInventoryComponent::BagStockGrams(FName Strain, float& OutThc, float& OutQualPct) const
{
	OutThc = 0.f; OutQualPct = 0.f;
	int32 Grams = 0; double ThcAcc = 0.0, QualAcc = 0.0;
	for (const FInventoryStack& S : Stacks)
	{
		if (S.Quantity <= 0 || !IsBag(S.ItemId) || BagStrain(S.ItemId) != Strain) { continue; }
		const int32 G = S.Quantity * FMath::Max(1, BagGrams(S.ItemId));
		Grams += G; ThcAcc += static_cast<double>(S.Quality) * G; QualAcc += static_cast<double>(S.QualityPct) * G;
	}
	if (Grams > 0) { OutThc = static_cast<float>(ThcAcc / Grams); OutQualPct = static_cast<float>(QualAcc / Grams); }
	return Grams;
}

int32 UInventoryComponent::RemoveBagsForGrams(FName Strain, int32 DesiredGrams, float& OutThc, float& OutQualPct)
{
	OutThc = 0.f; OutQualPct = 0.f;
	if (GetOwnerRole() != ROLE_Authority || DesiredGrams <= 0) { return 0; }
	// Vul met HELE zakjes tot >= DesiredGrams met minimale overschot: pak telkens het grootste zakje
	// dat <= rest is; past niets meer maar is er nog rest, pak het kleinste zakje (overschot) en stop.
	int32 SoldGrams = 0;
	double ThcAcc = 0.0, QualAcc = 0.0; // gewogen op grammen
	int32 Remaining = DesiredGrams;
	int32 Guard = 0;
	while (Remaining > 0 && Guard++ < 10000)
	{
		int32 BestFitIdx = INDEX_NONE, BestFitGrams = -1;
		int32 SmallestIdx = INDEX_NONE, SmallestGrams = MAX_int32;
		for (int32 i = 0; i < Stacks.Num(); ++i)
		{
			if (Stacks[i].Quantity <= 0 || !IsBag(Stacks[i].ItemId) || BagStrain(Stacks[i].ItemId) != Strain) { continue; }
			const int32 G = BagGrams(Stacks[i].ItemId);
			if (G <= 0) { continue; }
			if (G <= Remaining && G > BestFitGrams) { BestFitGrams = G; BestFitIdx = i; }
			if (G < SmallestGrams) { SmallestGrams = G; SmallestIdx = i; }
		}
		const int32 PickIdx = (BestFitIdx != INDEX_NONE) ? BestFitIdx : SmallestIdx;
		if (PickIdx == INDEX_NONE) { break; } // geen zakjes meer
		const int32 G = BagGrams(Stacks[PickIdx].ItemId);
		ThcAcc += static_cast<double>(Stacks[PickIdx].Quality) * G;
		QualAcc += static_cast<double>(Stacks[PickIdx].QualityPct) * G;
		SoldGrams += G;
		Remaining -= G;
		Stacks[PickIdx].Quantity -= 1;
		if (Stacks[PickIdx].Quantity <= 0) { UnassignHotbarStack(Stacks[PickIdx].StackId); Stacks.RemoveAt(PickIdx); }
		if (BestFitIdx == INDEX_NONE) { break; } // overschot-zakje gepakt -> klaar
	}
	if (SoldGrams > 0)
	{
		OutThc = static_cast<float>(ThcAcc / SoldGrams);
		OutQualPct = static_cast<float>(QualAcc / SoldGrams);
		OnRep_Stacks();
	}
	return SoldGrams;
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
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Merged into %dx (THC %.0f%%, Quality %.0f%%)"), Qty, Stacks[FindStackIndex(ItemId)].Quality, Stacks[FindStackIndex(ItemId)].QualityPct));
	}
	return true;
}

int32 UInventoryComponent::FindStackById(int32 StackId) const
{
	if (StackId == 0) { return INDEX_NONE; }
	return Stacks.IndexOfByPredicate([StackId](const FInventoryStack& S) { return S.StackId == StackId; });
}

float UInventoryComponent::GetStackQualityById(int32 StackId) const
{
	const int32 Idx = FindStackById(StackId);
	return Stacks.IsValidIndex(Idx) ? Stacks[Idx].Quality : 0.f;
}

void UInventoryComponent::SetStackQualityById(int32 StackId, float Q)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const int32 Idx = FindStackById(StackId);
	if (Stacks.IsValidIndex(Idx))
	{
		Stacks[Idx].Quality = Q;
		OnRep_Stacks(); // host-UI direct bijwerken (clients via replicatie van Stacks)
	}
}

void UInventoryComponent::RemoveFromStackById(int32 StackId, int32 Count)
{
	if (GetOwnerRole() != ROLE_Authority || Count <= 0) { return; }
	const int32 Idx = FindStackById(StackId);
	if (!Stacks.IsValidIndex(Idx)) { return; }
	Stacks[Idx].Quantity -= Count;
	if (Stacks[Idx].Quantity <= 0)
	{
		UnassignHotbarStack(Stacks[Idx].StackId);
		Stacks.RemoveAt(Idx);
	}
	OnRep_Stacks();
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
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("Inventory too heavy — sell or drop something.")); }
		return false;
	}

	// Zakjes: discrete eenheden, max BagStackMax per slot; overloop -> nieuw slot.
	if (IsBag(ItemId))
	{
		int32 Remaining = Count;
		for (FInventoryStack& S : Stacks) // vul bestaande zakje-stapels van hetzelfde id tot het maximum
		{
			if (Remaining <= 0) { break; }
			if (S.ItemId != ItemId || S.Quantity >= BagStackMax) { continue; }
			const int32 Add = FMath::Min(BagStackMax - S.Quantity, Remaining);
			const int32 OldQty = S.Quantity; const int32 NewQty = OldQty + Add;
			if (ThcPercent >= 0.f) { S.Quality = (S.Quality * OldQty + ThcPercent * Add) / NewQty; }
			if (QualityPct >= 0.f) { S.QualityPct = (S.QualityPct * OldQty + QualityPct * Add) / NewQty; }
			S.Quantity = NewQty;
			Remaining -= Add;
		}
		while (Remaining > 0) // nieuwe stapels (elk tot het maximum)
		{
			if (MaxStacks > 0 && GetUsedSlots() >= MaxStacks)
			{
				if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("No free inventory slots.")); }
				break;
			}
			FInventoryStack NewStack;
			NewStack.ItemId = ItemId;
			NewStack.Quantity = FMath::Min(BagStackMax, Remaining);
			NewStack.Quality = FMath::Max(0.f, ThcPercent);
			NewStack.QualityPct = FMath::Max(0.f, QualityPct);
			NewStack.StackId = NextStackId++;
			Stacks.Add(NewStack);
			Remaining -= NewStack.Quantity;
		}
		OnRep_Stacks();
		return Remaining < Count;
	}

	const bool bStackable = IsStackable(ItemId);

	// Slot-limiet: nieuwe stapels die erbij komen.
	if (MaxStacks > 0)
	{
		const int32 ExistingIdx = FindMergeStackIndex(ItemId, ThcPercent, QualityPct);
		const int32 NewStacks = bStackable ? (ExistingIdx != INDEX_NONE ? 0 : 1) : Count;
		if (GetUsedSlots() + NewStacks > MaxStacks)
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("No free inventory slots.")); }
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
	if (S.StartsWith(TEXT("Bag_")))    { return 0.005f; } // verpakte wiet
	if (S.StartsWith(TEXT("Cont_")))   { return 0.02f; }  // lege bakjes/jars
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

void UInventoryComponent::ClearAll()
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	Stacks.Reset();
	KnownStacks.Reset();
	GridOrder.Reset();
	OnRep_Stacks();
}

void UInventoryComponent::RestoreStacksAndGrid(const TArray<FInventoryStack>& InStacks, const TArray<int32>& InCells)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }

	// Bewaar de huidige cash-stack (afgeleid van economy, niet in de save) -> komt op cel 0.
	FInventoryStack CashStack; bool bHadCash = false;
	for (const FInventoryStack& S : Stacks) { if (S.ItemId == FName(TEXT("Cash"))) { CashStack = S; bHadCash = true; break; } }

	Stacks.Reset();
	KnownStacks.Reset();
	GridOrder.Reset();
	const int32 Cells = (MaxStacks > 0) ? MaxStacks : (InStacks.Num() + 1);
	GridOrder.SetNumZeroed(Cells);

	if (bHadCash)
	{
		CashStack.StackId = NextStackId++;
		Stacks.Add(CashStack);
		KnownStacks.Add(CashStack.StackId);
		if (GridOrder.Num() > 0) { GridOrder[0] = CashStack.StackId; }
	}

	for (int32 i = 0; i < InStacks.Num(); ++i)
	{
		const FInventoryStack& Src = InStacks[i];
		if (Src.ItemId.IsNone() || Src.Quantity <= 0 || Src.ItemId == FName(TEXT("Cash"))) { continue; }
		FInventoryStack NewS = Src;
		NewS.StackId = NextStackId++;
		Stacks.Add(NewS);
		KnownStacks.Add(NewS.StackId);

		const int32 Cell = InCells.IsValidIndex(i) ? InCells[i] : INDEX_NONE;
		if (GridOrder.IsValidIndex(Cell) && GridOrder[Cell] == 0)
		{
			GridOrder[Cell] = NewS.StackId; // exact terug op z'n opgeslagen plek
		}
		else
		{
			const int32 Empty = GridOrder.IndexOfByKey(0);
			if (Empty != INDEX_NONE) { GridOrder[Empty] = NewS.StackId; } else { GridOrder.Add(NewS.StackId); }
		}
	}

	OnRep_Stacks(); // RefreshGridAuto behoudt bovenstaande plaatsing + vult eventuele rest
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
	RefreshActiveStack();
}

void UInventoryComponent::CycleActiveSlot(int32 Dir)
{
	if (Dir == 0) { return; }
	ActiveSlot = ((ActiveSlot + (Dir > 0 ? 1 : -1)) % HotbarSize + HotbarSize) % HotbarSize;
	RefreshActiveStack();
}

void UInventoryComponent::RefreshActiveStack()
{
	// Alleen de lokaal-bestuurde eigenaar bepaalt het actieve hand-item uit z'n (lokale) hotbar; de server
	// voor een REMOTE client houdt de via RPC gepushte waarde (anders zou de lege server-hotbar 'm wissen).
	const APawn* P = Cast<APawn>(GetOwner());
	if (!P || !P->IsLocallyControlled()) { return; }
	const int32 Sid = GetHotbarStackId(ActiveSlot);
	ActiveStackId = Sid;
	if (GetOwnerRole() != ROLE_Authority) { ServerReportActiveStack(Sid); } // client -> server syncen
}

void UInventoryComponent::ServerReportActiveStack_Implementation(int32 StackId)
{
	ActiveStackId = StackId;
}

int32 UInventoryComponent::GetHotbarStackId(int32 Slot) const
{
	return HotbarStacks.IsValidIndex(Slot) ? HotbarStacks[Slot] : 0;
}

FName UInventoryComponent::GetActiveItemId() const
{
	// ActiveStackId is op de eigenaar lokaal gezet en op de server via RPC gesynct -> werkt in co-op.
	const int32 Idx = FindStackById(ActiveStackId);
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
	RefreshActiveStack(); // hand-item kan gewisseld zijn -> server syncen
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
	//    unassign blijft zo gerespecteerd — we vullen 'm niet meteen weer terug. Alles vult eerst de
	//    hotbar (ook meubels zoals lampen/gootsteen, zodat je ze meteen kunt plaatsen); pas als de
	//    hotbar vol is gaat de rest naar de inventory. Alleen briefgeld hoort niet op de hotbar.
	for (const FInventoryStack& Stack : Stacks)
	{
		if (KnownStacks.Contains(Stack.StackId)) { continue; }
		KnownStacks.Add(Stack.StackId);
		if (Stack.ItemId == TEXT("Cash")) { continue; } // briefgeld hoort niet op de hotbar
		if (bPendingSplit) { continue; }                 // een split-helft hoort in het rooster, niet op de hotbar
		if (HotbarStacks.Contains(Stack.StackId)) { continue; }
		const int32 Empty = HotbarStacks.IndexOfByKey(0);
		if (Empty != INDEX_NONE) { HotbarStacks[Empty] = Stack.StackId; }
	}

	RefreshActiveStack(); // hand-item kan veranderd zijn (auto-toewijzing) -> server opnieuw syncen
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
	// Nieuwe stapels in de eerste vrije cel zetten (en daarna blijven ze daar). Een split-helft gaat
	// naar de specifiek gevraagde cel (PendingSplitCell), als die nog leeg is.
	for (const FInventoryStack& St : Stacks)
	{
		if (GridOrder.Contains(St.StackId)) { continue; }
		int32 Target = INDEX_NONE;
		if (bPendingSplit && PendingSplitCell >= 0 && GridOrder.IsValidIndex(PendingSplitCell) && GridOrder[PendingSplitCell] == 0)
		{
			Target = PendingSplitCell; // split-helft naar de gevraagde cel
		}
		if (Target == INDEX_NONE) { Target = GridOrder.IndexOfByKey(0); }
		if (Target != INDEX_NONE) { GridOrder[Target] = St.StackId; }
		else { GridOrder.Add(St.StackId); }
		if (bPendingSplit) { bPendingSplit = false; PendingSplitCell = -1; } // split-helft is geplaatst
	}
}

void UInventoryComponent::RequestSplit(int32 StackId, int32 Amount, int32 ToCell)
{
	if (Amount <= 0) { return; }
	bPendingSplit = true;       // de nieuwe helft hoort in het rooster, niet auto op de hotbar
	PendingSplitCell = ToCell;  // gewenste cel (-1 = eerste vrije)
	ServerSplitStack(StackId, Amount);
}

void UInventoryComponent::ServerMergeTwo_Implementation(int32 IntoStackId, int32 FromStackId)
{
	if (GetOwnerRole() != ROLE_Authority || IntoStackId == FromStackId) { return; }
	const int32 Ai = FindStackById(IntoStackId);
	const int32 Bi = FindStackById(FromStackId);
	if (!Stacks.IsValidIndex(Ai) || !Stacks.IsValidIndex(Bi)) { return; }
	if (Stacks[Ai].ItemId != Stacks[Bi].ItemId) { return; }
	if (!IsStackable(Stacks[Ai].ItemId) || Stacks[Ai].ItemId == TEXT("Cash")) { return; }

	const int32 Total = Stacks[Ai].Quantity + Stacks[Bi].Quantity;
	if (Total <= 0) { return; }
	// Gewogen gemiddelde voor THC% + kwaliteit% (zoals bij het samenvoegen van wiet).
	Stacks[Ai].Quality    = (Stacks[Ai].Quality    * Stacks[Ai].Quantity + Stacks[Bi].Quality    * Stacks[Bi].Quantity) / Total;
	Stacks[Ai].QualityPct = (Stacks[Ai].QualityPct * Stacks[Ai].Quantity + Stacks[Bi].QualityPct * Stacks[Bi].Quantity) / Total;
	Stacks[Ai].Quantity   = Total;
	UnassignHotbarStack(Stacks[Bi].StackId);
	Stacks.RemoveAt(Bi);
	OnRep_Stacks();
}

void UInventoryComponent::ServerSplitStack_Implementation(int32 StackId, int32 Amount)
{
	if (GetOwnerRole() != ROLE_Authority || Amount <= 0) { return; }
	const int32 Idx = FindStackById(StackId);
	if (!Stacks.IsValidIndex(Idx)) { return; }
	if (Stacks[Idx].ItemId == TEXT("Cash")) { return; }     // briefgeld niet splitsen
	if (Stacks[Idx].Quantity <= Amount) { return; }          // niks te splitsen (zou alles zijn)
	if (MaxStacks > 0 && GetUsedSlots() >= MaxStacks) { return; } // geen ruimte voor een extra stapel

	Stacks[Idx].Quantity -= Amount;
	FInventoryStack New;
	New.ItemId = Stacks[Idx].ItemId;
	New.Quality = Stacks[Idx].Quality;
	New.QualityPct = Stacks[Idx].QualityPct;
	New.Quantity = Amount;
	New.StackId = NextStackId++;
	Stacks.Add(New);
	OnRep_Stacks();
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

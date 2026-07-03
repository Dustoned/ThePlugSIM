#include "Inventory/InventoryComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "World/WorldItemPickup.h"
#include "Economy/EconomyComponent.h" // cash droppen = ook echt van het saldo af (spiegel-stapel)
#include "Placement/PlaceableTypes.h" // GetPlaceableDef -> furniture-gewicht

// Boter/edibles bederven buiten de koeling (ButterMix = gekookte boter, Edible = eindproduct,
// Butter = ingredient). QualityPct zakt -> mindere edibles / lagere verkoopwaarde.
bool UInventoryComponent::IsPerishableItem(FName ItemId)
{
	const FString S = ItemId.ToString();
	return S.StartsWith(TEXT("ButterMix")) || S.StartsWith(TEXT("Edible")) || S == TEXT("Butter")
		|| S.StartsWith(TEXT("Cookie")) || S.StartsWith(TEXT("Gummy"));
}

// Fridge-filter: bederfelijk spul + kook-ingredienten (thematisch: de kook-flow leest Sugar/Flour/Gelatin
// uit de INVENTORY, opslaan in de fridge is puur opslag) + Cash (huidig gedrag behouden).
bool UInventoryComponent::IsFridgeItem(FName ItemId)
{
	return IsPerishableItem(ItemId)
		|| ItemId == FName(TEXT("Sugar")) || ItemId == FName(TEXT("Flour"))
		|| ItemId == FName(TEXT("Gelatin")) || ItemId == FName(TEXT("Cash"));
}

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	// Versheid-pass (alleen server): elke 10s bederven boter/edibles in je inventory een beetje.
	// In een Fridge bewaarde stapels zitten in de shelf en degraderen daar NIET (zie StorageShelf).
	if (GetOwnerRole() == ROLE_Authority)
	{
		GetWorld()->GetTimerManager().SetTimer(PerishTimer, this, &UInventoryComponent::DegradePerishables, 10.f, true, 10.f);
	}
}

void UInventoryComponent::DegradePerishables()
{
	const float Step = 1.6f; // ~10 min van 100% naar 0% bij normale tijd (sneller bij time-speed)
	bool bChanged = false;
	for (FInventoryStack& St : Stacks)
	{
		if (IsPerishableItem(St.ItemId) && St.QualityPct > 0.f)
		{
			St.QualityPct = FMath::Max(0.f, St.QualityPct - Step);
			bChanged = true;
		}
	}
	if (bChanged) { OnRep_Stacks(); }
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInventoryComponent, Stacks);
	DOREPLIFETIME(UInventoryComponent, BackpackTier);
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
	if (S.StartsWith(TEXT("Bag_"))) { S = S.RightChop(4); } // "SilverHaze" | "SilverHaze_2" | "SilverHaze_Bag2_2"
	int32 U;
	if (S.FindChar(TEXT('_'), U)) { S = S.Left(U); }        // strain = eerste token
	return FName(*S);
}

FName UInventoryComponent::BagContainer(FName ItemId)
{
	FString S = ItemId.ToString();
	if (!S.StartsWith(TEXT("Bag_"))) { return NAME_None; }
	S = S.RightChop(4);
	TArray<FString> Parts; S.ParseIntoArray(Parts, TEXT("_"), true);
	if (Parts.Num() < 3) { return NAME_None; }              // oude 2-token bag -> geen container opgeslagen
	return FName(*(TEXT("Cont_") + Parts[1]));               // middelste token = container-key
}

FName UInventoryComponent::MakeBagId(FName Strain, FName ContainerId, int32 Grams)
{
	const int32 G = FMath::Max(1, Grams);
	if (ContainerId.IsNone())
	{
		return FName(*FString::Printf(TEXT("Bag_%s_%d"), *Strain.ToString(), G));
	}
	FString Key = ContainerId.ToString();
	Key.RemoveFromStart(TEXT("Cont_"));               // "Cont_Bag2" -> "Bag2"
	return FName(*FString::Printf(TEXT("Bag_%s_%s_%d"), *Strain.ToString(), *Key, G));
}

FName UInventoryComponent::MakeJointId(FName Strain, int32 Grams)
{
	const int32 G = FMath::Max(1, Grams);
	if (Strain.IsNone()) { return FName(*FString::Printf(TEXT("Joint_%dg"), G)); }
	return FName(*FString::Printf(TEXT("Joint_%s_%dg"), *Strain.ToString(), G));
}

int32 UInventoryComponent::JointGrams(FName ItemId)
{
	const FString S = ItemId.ToString();
	int32 U;
	if (!S.FindLastChar(TEXT('_'), U)) { return 0; }
	return FCString::Atoi(*S.RightChop(U + 1)); // "3g" -> 3 (Atoi stopt bij 'g')
}

FName UInventoryComponent::JointStrain(FName ItemId)
{
	FString S = ItemId.ToString();
	if (!S.StartsWith(TEXT("Joint_"))) { return NAME_None; }
	S = S.RightChop(6); // "3g" (oud) of "SilverHaze_3g" (nieuw)
	int32 U;
	if (S.FindChar(TEXT('_'), U)) { return FName(*S.Left(U)); } // eerste token = strain
	return NAME_None; // oud formaat, geen strain
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
		UE_LOG(LogWeedShop, Warning, TEXT("AddItem ignored: only the server may mutate the inventory."));
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
			// Limiet = backpack (MaxStacks) + hotbar (HotbarSize): de hotbar is aparte opslag, telt los van de 10.
			if (MaxStacks > 0 && GetUsedSlots() >= MaxStacks + HotbarSize)
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
		// Limiet = backpack (MaxStacks) + hotbar (HotbarSize): de hotbar is aparte opslag, telt los van de 10.
		if (GetUsedSlots() + NewStacks > MaxStacks + HotbarSize)
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

	// Briefgeld kan GESPLITST zijn (ServerSplitStack werkt nu ook op cash) -> reconcilieer op het TOTAAL
	// over alle cash-stapels, zodat een split niet bij de eerstvolgende saldo-wijziging wordt teruggedraaid.
	int64 Have = 0;
	for (const FInventoryStack& S : Stacks) { if (S.ItemId == Cash) { Have += S.Quantity; } }
	if (Have == Q) { return; }

	if (Q > Have)
	{
		// Erbij: op de eerste cash-stapel; is er (nog) geen, maak er een.
		const int32 Idx = FindStackIndex(Cash);
		if (Idx != INDEX_NONE)
		{
			Stacks[Idx].Quantity += (int32)(Q - Have);
		}
		else
		{
			FInventoryStack NewStack;
			NewStack.ItemId = Cash;
			NewStack.Quantity = Q;
			NewStack.StackId = NextStackId++;
			Stacks.Add(NewStack);
		}
	}
	else
	{
		// Eraf: van ACHTEREN naar voren afboeken (afgesplitste rest-stapels eerst leeg, hoofd-stapel het laatst).
		int64 Remove = Have - Q;
		for (int32 i = Stacks.Num() - 1; i >= 0 && Remove > 0; --i)
		{
			if (Stacks[i].ItemId != Cash) { continue; }
			const int32 Take = (int32)FMath::Min<int64>(Remove, Stacks[i].Quantity);
			Stacks[i].Quantity -= Take;
			Remove -= Take;
			if (Stacks[i].Quantity <= 0) { UnassignHotbarStack(Stacks[i].StackId); Stacks.RemoveAt(i); }
		}
	}

	OnRep_Stacks();
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

	// FURNITURE / PLACEABLES: echt zwaar (per categorie via de placeable-flags). Een paar meubels vult zo de
	// hele draagkracht. Getallen bewust GAMEY (niet realistisch) zodat de cap een echte rem is.
	{
		FPlaceableDef PDef;
		if (GetPlaceableDef(ItemId, PDef))
		{
			if (S == TEXT("Fridge")) { return 35.f; }
			if (PDef.bIsAtm || PDef.bIsSafe) { return 30.f; }
			if (PDef.bIsBed || PDef.bIsWardrobe) { return 25.f; }
			if (PDef.bIsPackBench || PDef.bIsProcessor || PDef.bIsShelf) { return 22.f; }
			if (PDef.bIsSink) { return 15.f; }
			if (PDef.bIsStructure || PDef.bIsCeilingPiece) { return 12.f; } // muren/vloeren (dev)
			if (PDef.bIsLamp || PDef.bIsLightSwitch || PDef.bIsWallMount) { return 2.f; } // klein wand-spul
			if (S.StartsWith(TEXT("Furn_"))) { return 18.f; } // overige meubels (sofa/tv/...)
			return 8.f; // overige placeables
		}
	}

	// VERPAKTE ZAKKEN: tarra = het CONTAINER-gewicht (zelfde getallen als de lege Cont_-items hieronder)
	// + de grammen erin (0.03 kg/g, gelijk aan losse bud). BEWUST realistisch geworden i.p.v. de oude vaste
	// 1.5 kg verpakking: een 2g-baggie is nu ~0.16 kg i.p.v. ~1.53 kg. MaxWeight blijft 60 — de draagkracht-
	// progressie zit in de backpack-tiers (ApplyBackpackTier), niet in kunstmatig zware zakjes.
	if (S.StartsWith(TEXT("Bag_")))
	{
		const FString Cont = BagContainer(ItemId).ToString(); // "Cont_Bag2"/"Cont_Jar..."/... of "None" (oude maatloze bag)
		float Tare = 0.1f;                                     // baggie / oude 2-token bag -> baggie-fallback
		if (Cont.Contains(TEXT("Jar")))     { Tare = 0.3f; }   // glazen pot
		if (Cont.Contains(TEXT("Garbage"))) { Tare = 0.3f; }   // grote vuilniszak
		if (Cont.Contains(TEXT("Block")))   { Tare = 0.2f; }   // pers-blok
		return Tare + (float)BagGrams(ItemId) * 0.03f;
	}

	// LEGE CONTAINERS (per type: glas weegt meer dan een baggie).
	if (S.StartsWith(TEXT("Cont_")))
	{
		if (S.Contains(TEXT("Jar")))     { return 0.3f; }  // glazen pot
		if (S.Contains(TEXT("Garbage"))) { return 0.3f; }  // grote vuilniszak
		if (S.Contains(TEXT("Block")))   { return 0.2f; }
		return 0.1f;                                        // baggies
	}

	// LOSSE WIET (per gram) + kweek-verbruik. Ook gamey-zwaar zodat een volle voorraad telt.
	if (S.StartsWith(TEXT("WetBud_"))) { return 0.04f; } // nat is iets zwaarder
	if (S.StartsWith(TEXT("Bud_")))    { return 0.03f; }
	if (S.StartsWith(TEXT("Seed_")))   { return 0.01f; }
	if (S.StartsWith(TEXT("Joint_")))  { return 0.02f; }
	if (S.StartsWith(TEXT("Papers_"))) { return 0.05f; }
	if (S.StartsWith(TEXT("Soil_")))   { return 5.f; }   // zak potgrond
	if (S.StartsWith(TEXT("Pot")))     { return 2.f; }   // lege pot
	if (S.StartsWith(TEXT("WaterBottle"))) { return 1.f; }

	// CONCENTRATEN + EDIBLE-EINDPRODUCTEN (per gram, prefixes uit ProcessorMachine::OutputPrefixFor).
	// Compact/geconcentreerd = licht (~0.01 kg/g); edibles/boter zijn verdund met vet = zwaarder.
	if (S.StartsWith(TEXT("Crystal_")))   { return 0.01f; } // kief/crystals (los, licht)
	if (S.StartsWith(TEXT("Hash_")))      { return 0.01f; } // geperste hasj (compact)
	if (S.StartsWith(TEXT("Rosin_")))     { return 0.01f; } // solventless rosin
	if (S.StartsWith(TEXT("Bubble_")))    { return 0.01f; } // bubble/ice hash
	if (S.StartsWith(TEXT("Oil_")))       { return 0.01f; } // cannabis-olie
	if (S.StartsWith(TEXT("Moonrock_")))  { return 0.03f; } // gecoate bud (iets zwaarder dan los concentraat)
	if (S.StartsWith(TEXT("Baked_")))     { return 0.04f; } // gedecarbde bud voor edibles (verdund)
	if (S.StartsWith(TEXT("ButterMix_"))) { return 0.04f; } // cannabis-boter (vet = zwaarder)
	if (S.StartsWith(TEXT("Edible_")))    { return 0.04f; } // edible-eindproduct (verdund)

	return 0.5f;
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

// --- Backpack-tiers (per speler, puur geld): tier bepaalt MaxStacks + MaxWeight ---

int64 UInventoryComponent::BackpackUpgradeCostCents(int32 CurrentTier)
{
	switch (CurrentTier)
	{
	case 0:  return 50000;    // T0 -> T1: EUR 500
	case 1:  return 250000;   // T1 -> T2: EUR 2.500
	case 2:  return 1000000;  // T2 -> T3: EUR 10.000
	default: return 0;        // al max (of ongeldige tier)
	}
}

void UInventoryComponent::GetBackpackTierCaps(int32 Tier, int32& OutMaxStacks, float& OutMaxWeight)
{
	switch (FMath::Clamp(Tier, 0, BackpackMaxTier))
	{
	default:
	case 0: OutMaxStacks = 10; OutMaxWeight = 60.f;  break;
	case 1: OutMaxStacks = 14; OutMaxWeight = 85.f;  break;
	case 2: OutMaxStacks = 18; OutMaxWeight = 115.f; break;
	case 3: OutMaxStacks = 24; OutMaxWeight = 150.f; break;
	}
}

void UInventoryComponent::ApplyBackpackTier()
{
	GetBackpackTierCaps(BackpackTier, MaxStacks, MaxWeight);
	// Rooster meteen laten meegroeien (RefreshGridAuto doet SetNum op MaxStacks, behoudt posities) en de
	// UI (grid/gewichtsbalk) in-place laten verversen. Draait op server EN client (via de OnRep).
	RefreshGridAuto();
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::OnRep_BackpackTier()
{
	ApplyBackpackTier();
}

void UInventoryComponent::SetBackpackTier(uint8 InTier)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	BackpackTier = FMath::Min<uint8>(InTier, (uint8)BackpackMaxTier);
	ApplyBackpackTier();
}

void UInventoryComponent::ServerBuyBackpackUpgrade_Implementation()
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	if (BackpackTier >= BackpackMaxTier)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Backpack is already maxed out."));
		return;
	}
	// Betalen met CASH via de EIGEN economy van de pawn-owner (per speler). Cash is een SPIEGEL van het
	// economy-saldo -> altijd via RemoveMoney, nooit via RemoveItem (reconcile zou de stapel herstellen).
	APawn* Pawn = Cast<APawn>(GetOwner());
	UEconomyComponent* Econ = Pawn ? Pawn->FindComponentByClass<UEconomyComponent>() : nullptr;
	const int64 Cost = BackpackUpgradeCostCents(BackpackTier);
	if (!Econ || !Econ->RemoveMoney(Cost))
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Not enough cash."));
		return;
	}
	++BackpackTier;
	ApplyBackpackTier();
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green,
		FString::Printf(TEXT("Backpack upgraded: %d slots / %.0f kg"), MaxStacks, MaxWeight));
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
	if (bChanged) { RefreshGridAuto(); OnInventoryChanged.Broadcast(); } // van hotbar af -> terug in 't backpack-rooster
}

bool UInventoryComponent::RemoveItem(FName ItemId, int32 Count)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("RemoveItem ignored: only the server may mutate the inventory."));
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

	// Bewaar de cash-SPIEGEL (afgeleid van het economy-saldo, geen echt item): ClearAll hoort de inventory te
	// legen, niet het geld "kwijt te maken". Zonder dit spawnde Sandbox zonder briefgeld: ApplyStartMode zet
	// eerst het saldo (spiegel-stapel ontstaat) en doet DAARNA ClearAll -> stapel weg en geen saldo-wijziging
	// meer die 'm terugbrengt. Zelfde idioom als RestoreStacksAndGrid.
	FInventoryStack CashStack; bool bHadCash = false;
	for (const FInventoryStack& S : Stacks) { if (S.ItemId == FName(TEXT("Cash"))) { CashStack = S; bHadCash = true; break; } }

	Stacks.Reset();
	KnownStacks.Reset();
	GridOrder.Reset();

	if (bHadCash)
	{
		CashStack.StackId = NextStackId++;
		Stacks.Add(CashStack);
		KnownStacks.Add(CashStack.StackId);
	}

	OnRep_Stacks();
}

void UInventoryComponent::RestoreStacksAndGrid(const TArray<FInventoryStack>& InStacks, const TArray<int32>& InCells, const TArray<int32>& InHotbarSlots)
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
	HotbarStacks.Reset(); HotbarStacks.SetNumZeroed(HotbarSize); // hotbar-toewijzing komt exact uit InHotbarSlots

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

		// Zat deze stapel op de hotbar? -> direct aan dat slot toewijzen en NIET in het backpack-rooster zetten.
		const int32 HSlot = InHotbarSlots.IsValidIndex(i) ? InHotbarSlots[i] : -1;
		if (HSlot >= 0 && HSlot < HotbarSize) { HotbarStacks[HSlot] = NewS.StackId; continue; }

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
	RefreshGridAuto();   // item zit nu op de hotbar -> z'n backpack-cel vrijgeven
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

	// Cel vrijgeven als de stapel weg is OF nu op de hotbar staat. De hotbar is APARTE opslag (8 eigen slots),
	// niet onderdeel van dit backpack-rooster -> zo bezetten hotbar-items geen backpack-cel meer (10 echt vrij).
	for (int32& S : GridOrder)
	{
		if (S != 0 && (FindStackById(S) == INDEX_NONE || IsStackOnHotbar(S))) { S = 0; }
	}
	// Nieuwe (niet-hotbar) stapels in de eerste vrije cel zetten (en daarna blijven ze daar). Een split-helft gaat
	// naar de specifiek gevraagde cel (PendingSplitCell), als die nog leeg is.
	for (const FInventoryStack& St : Stacks)
	{
		if (IsStackOnHotbar(St.StackId)) { continue; } // hotbar-items horen niet in 't backpack-rooster
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
	if (!IsStackable(Stacks[Ai].ItemId)) { return; } // (cash mag ook weer samengevoegd worden na een split)

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
	if (Stacks[Idx].Quantity <= Amount) { return; }          // niks te splitsen (zou alles zijn)
	if (MaxStacks > 0 && GetUsedSlots() >= MaxStacks + HotbarSize) { return; } // geen ruimte (backpack + hotbar vol)

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

void UInventoryComponent::ServerDropStack_Implementation(int32 StackId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const int32 Idx = FindStackById(StackId);
	if (!Stacks.IsValidIndex(Idx)) { return; }
	const FInventoryStack St = Stacks[Idx];
	if (St.ItemId.IsNone() || St.Quantity <= 0) { return; }

	int32 DropQty = St.Quantity;

	if (St.ItemId == FName(TEXT("Cash")))
	{
		// Briefgeld: de stapel is een SPIEGEL van het cash-saldo -> het bedrag moet ook echt van de economy af.
		// Eerst de stapel eruit, dan het saldo verlagen: de spiegel-reconcile (OnBalanceChanged ->
		// SetCashDisplayEuros) ziet daarna een kloppend totaal en laat de overige cash-stapels met rust.
		// De pickup zelf gaat bij oppakken weer naar de economy van de grijper (zie AWorldItemPickup::Interact).
		UEconomyComponent* Eco = GetOwner() ? GetOwner()->FindComponentByClass<UEconomyComponent>() : nullptr;
		if (!Eco) { return; }
		DropQty = (int32)FMath::Min<int64>((int64)St.Quantity, Eco->GetBalanceCents() / 100); // defensief: nooit meer dan het saldo
		RemoveFromStackById(StackId, St.Quantity);
		if (DropQty <= 0 || !Eco->RemoveMoney((int64)DropQty * 100)) { return; } // niks te droppen -> alleen de spiegel opgeruimd
	}
	else
	{
		RemoveFromStackById(StackId, St.Quantity); // de hele stapel de inventory uit
	}

	AActor* Own = GetOwner();
	UWorld* W = GetWorld();
	if (!Own || !W) { return; }
	FVector Fwd = Own->GetActorForwardVector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
	FVector Loc = Own->GetActorLocation() + Fwd * 90.f;
	Loc.Z -= (Own->GetSimpleCollisionHalfHeight() - 12.f); // bij de voeten neerleggen
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AWorldItemPickup* P = W->SpawnActor<AWorldItemPickup>(AWorldItemPickup::StaticClass(), FTransform(FRotator::ZeroRotator, Loc), SP))
	{
		P->Setup(St.ItemId, DropQty, St.Quality, St.QualityPct);
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
	if (Cur == Cell) { return; }
	if (Cur != INDEX_NONE)
	{
		// Al in het rooster -> wissel de inhoud van bron- en doelcel (doel kan leeg of bezet zijn).
		GridOrder[Cur] = GridOrder[Cell];
		GridOrder[Cell] = StackId;
	}
	else
	{
		// Stack zat nog NIET in het rooster (bv. net vanaf de hotbar gesleept): zet 'm op de losgelaten
		// cel; een eventuele bewoner van die cel schuift naar de eerste vrije plek. Zo landt 'ie precies
		// waar je 'm neerzet i.p.v. terug te springen naar z'n oude/automatische cel.
		const int32 Occ = GridOrder[Cell];
		GridOrder[Cell] = StackId;
		if (Occ != 0)
		{
			const int32 Free = GridOrder.IndexOfByKey(0);
			if (Free != INDEX_NONE) { GridOrder[Free] = Occ; } else { GridOrder.Add(Occ); }
		}
	}
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

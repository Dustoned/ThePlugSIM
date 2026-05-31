#include "Phone/PhoneClientComponent.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Economy/EconomyComponent.h"
#include "Customer/CustomerBase.h"
#include "Cultivation/PotTypes.h"
#include "Cultivation/GrowPlant.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h"
#include "UI/PhoneWidget.h"
#include "UI/DealWidget.h"
#include "UI/StatusHudWidget.h"
#include "UI/PlantInfoWidget.h"
#include "UI/HotbarWidget.h"
#include "UI/InventoryWidget.h"
#include "UI/CompassWidget.h"
#include "Blueprint/UserWidget.h"

UPhoneClientComponent::UPhoneClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AWeedShopGameState* UPhoneClientComponent::GetGS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
}

APlayerController* UPhoneClientComponent::GetPC() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(Pawn->GetController());
	}
	return nullptr;
}

UInventoryComponent* UPhoneClientComponent::GetOwnerInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

void UPhoneClientComponent::UpdateCursor()
{
	const bool bAnyUI = bOpen || bRollOpen || bDealOpen || bInventoryOpen || bPotUpgradeOpen || bMergeOpen;
	if (APlayerController* PC = GetPC())
	{
		PC->SetShowMouseCursor(bAnyUI);
		PC->bEnableClickEvents = bAnyUI;
		PC->bEnableMouseOverEvents = bAnyUI;
		if (bAnyUI)
		{
			// GameAndUI = standaard klik-/hover-routing van de HUD hit-boxes (dat werkte goed),
			// muis vastgezet in het venster, en cursor blijft zichtbaar als je de knop indrukt
			// (HideCursorDuringCapture(false)) zodat hij niet verdwijnt bij slepen.
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);

			// Capture-mode op standaard (CaptureDuringMouseDown) laten staan: NoCapture brak
			// het klikken (2-3x nodig) en de hover-events. Alleen cursor-verbergen uitzetten.
			if (UGameViewportClient* VP = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr)
			{
				VP->SetHideCursorDuringCapture(false);
			}
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void UPhoneClientComponent::EnsureWidget()
{
	if (PhoneWidget) { return; }
	APlayerController* PC = GetPC();
	if (!PC || !PC->IsLocalController()) { return; }

	PhoneWidget = CreateWidget<UPhoneWidget>(PC, UPhoneWidget::StaticClass());
	if (PhoneWidget)
	{
		PhoneWidget->SetPhone(this);
		PhoneWidget->AddToViewport(20);
	}
	// Status-HUD (altijd zichtbaar) + deal-scherm via dezelfde, bewezen route.
	StatusWidget = CreateWidget<UStatusHudWidget>(PC, UStatusHudWidget::StaticClass());
	if (StatusWidget) { StatusWidget->AddToViewport(0); }
	DealWidget = CreateWidget<UDealWidget>(PC, UDealWidget::StaticClass());
	if (DealWidget) { DealWidget->SetPhone(this); DealWidget->AddToViewport(30); }
	PlantWidget = CreateWidget<UPlantInfoWidget>(PC, UPlantInfoWidget::StaticClass());
	if (PlantWidget) { PlantWidget->AddToViewport(10); }
	HotbarWidget = CreateWidget<UHotbarWidget>(PC, UHotbarWidget::StaticClass());
	if (HotbarWidget) { HotbarWidget->AddToViewport(5); }
	InventoryWidget = CreateWidget<UInventoryWidget>(PC, UInventoryWidget::StaticClass());
	if (InventoryWidget) { InventoryWidget->SetPhone(this); InventoryWidget->AddToViewport(25); }
	CompassWidget = CreateWidget<UCompassWidget>(PC, UCompassWidget::StaticClass());
	if (CompassWidget) { CompassWidget->AddToViewport(3); }
}

void UPhoneClientComponent::Toggle()
{
	EnsureWidget();
	bOpen = !bOpen;
	if (bOpen)
	{
		bRollOpen = false; // niet allebei tegelijk
		bDealOpen = false;
		bInventoryOpen = false;
		bPotUpgradeOpen = false;
		bHomeScreen = true; // open altijd op het home-scherm met de apps
	}
	UpdateCursor();
}

void UPhoneClientComponent::OpenApp(int32 AppIndex)
{
	Tab = FMath::Clamp(AppIndex, 0, AppCount - 1);
	bHomeScreen = false;
}

void UPhoneClientComponent::GoHome()
{
	bHomeScreen = true;
}

void UPhoneClientComponent::ToggleRollUI()
{
	bRollOpen = !bRollOpen;
	if (bRollOpen)
	{
		bOpen = false;
		bDealOpen = false;
		bInventoryOpen = false;
		bPotUpgradeOpen = false;
		RollGrams = FMath::Clamp(RollGrams, MinGrams, GetMaxJointGrams());
	}
	UpdateCursor();
}

void UPhoneClientComponent::ToggleInventory()
{
	bInventoryOpen = !bInventoryOpen;
	if (bInventoryOpen)
	{
		bOpen = false;
		bRollOpen = false;
		bDealOpen = false;
		bPotUpgradeOpen = false;
	}
	UpdateCursor();
}

void UPhoneClientComponent::OpenMerge(FName ItemId)
{
	const UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv || Inv->CountStacksOf(ItemId) < 2)
	{
		return; // niets te mergen
	}
	MergeItemId = ItemId;
	bMergeOpen = true;
	UpdateCursor();
}

void UPhoneClientComponent::CloseMerge()
{
	bMergeOpen = false;
	MergeItemId = NAME_None;
	UpdateCursor();
}

void UPhoneClientComponent::ConfirmMerge()
{
	if (!MergeItemId.IsNone())
	{
		ServerMergeItem(MergeItemId);
	}
	bMergeOpen = false;
	MergeItemId = NAME_None;
	UpdateCursor();
}

void UPhoneClientComponent::ServerMergeItem_Implementation(FName ItemId)
{
	if (UInventoryComponent* Inv = GetOwnerInventory())
	{
		Inv->MergeItem(ItemId);
	}
}

void UPhoneClientComponent::SetRollGrams(int32 Grams)
{
	RollGrams = FMath::Clamp(Grams, MinGrams, GetMaxJointGrams());
}

void UPhoneClientComponent::ConfirmRoll()
{
	ServerRollJoint(RollGrams);
	bRollOpen = false;
	UpdateCursor();
}

namespace
{
	// Paper-tiers oplopend in capaciteit (gram per joint).
	struct FPaperDef { const TCHAR* Id; int32 Capacity; };
	static const FPaperDef GPapers[] = {
		{ TEXT("Papers_Small"),     2 },
		{ TEXT("Papers_Big"),       5 },
		{ TEXT("Papers_Blunt"),     7 },
		{ TEXT("Papers_Backwoods"), 10 },
	};
}

int32 UPhoneClientComponent::GetMaxJointGrams() const
{
	// Hoogste capaciteit van de papers die je hebt; geen papers = niet kunnen rollen.
	int32 Max = 0;
	if (UInventoryComponent* Inv = GetOwnerInventory())
	{
		for (const FPaperDef& P : GPapers)
		{
			if (Inv->HasItem(FName(P.Id), 1))
			{
				Max = FMath::Max(Max, P.Capacity);
			}
		}
	}
	return Max;
}

void UPhoneClientComponent::ServerRollJoint_Implementation(int32 Grams)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv)
	{
		return;
	}

	const int32 MaxG = GetMaxJointGrams();
	if (MaxG <= 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No papers — buy some from the supplier (phone)."));
		}
		return;
	}
	Grams = FMath::Clamp(Grams, MinGrams, MaxG);

	// Kies de kleinste vloei die past (spaart je dure papers).
	FName Paper = NAME_None;
	for (const FPaperDef& P : GPapers)
	{
		if (P.Capacity >= Grams && Inv->HasItem(FName(P.Id), 1))
		{
			Paper = FName(P.Id);
			break;
		}
	}
	if (Paper.IsNone())
	{
		return;
	}

	// Zoek een bud-stapel met genoeg gram.
	FName BudItem = NAME_None;
	for (const FInventoryStack& Stack : Inv->GetStacks())
	{
		if (Stack.ItemId.ToString().StartsWith(TEXT("Bud_")) && Stack.Quantity >= Grams)
		{
			BudItem = Stack.ItemId;
			break;
		}
	}
	if (BudItem.IsNone())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange,
				FString::Printf(TEXT("Not enough weed (%d g needed)."), Grams));
		}
		return;
	}

	// THC% + Kwaliteit% van de gebruikte wiet -> komt mee in de joint.
	const float BudThc = Inv->GetItemQuality(BudItem);
	const float BudQ = Inv->GetItemQualityPct(BudItem);

	Inv->RemoveItem(BudItem, Grams);
	Inv->RemoveItem(Paper, 1);

	// Joint-gram zit in de id (Joint_<G>g); THC% + Kwaliteit% bewaren we op de stapel.
	const FName JointId(*FString::Printf(TEXT("Joint_%dg"), Grams));
	Inv->AddItem(JointId, 1, BudThc, BudQ);
	if (GEngine)
	{
		const FString StrainName = BudItem.ToString().StartsWith(TEXT("Bud_"))
			? BudItem.ToString().RightChop(4) : BudItem.ToString();
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Joint rolled: %dg weed (%s) + 1 paper"), Grams, *StrainName));
	}
}

// --- Deal ---

void UPhoneClientComponent::OpenDeal(ACustomerBase* Customer)
{
	if (!Customer)
	{
		return;
	}
	// Alleen onderhandelen met iemand die ook echt wil kopen.
	if (Customer->State != ECustomerState::WantsToOrder && Customer->State != ECustomerState::Negotiating)
	{
		if (GEngine)
		{
			const bool bProspect = (Customer->State == ECustomerState::Prospect);
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Silver, bProspect
				? TEXT("They're not hooked yet - give them a free sample first (F).")
				: TEXT("This customer isn't looking to buy right now."));
		}
		return;
	}
	DealCustomer = Customer;
	bDealOpen = true;
	bOpen = false;
	bRollOpen = false;
	bInventoryOpen = false;
	bPotUpgradeOpen = false;
	DealAltProduct = NAME_None; // begin met het gevraagde product
	// Start op de marktprijs als vraagprijs (eerlijk bod).
	DealAskCents = Customer->GetMarketPriceCents();
	SetDealAskCents(DealAskCents);
	UpdateCursor();
}

FName UPhoneClientComponent::GetOfferedProduct() const
{
	if (!DealAltProduct.IsNone()) { return DealAltProduct; }
	const ACustomerBase* C = DealCustomer.Get();
	return C ? C->DesiredProductId : NAME_None;
}

bool UPhoneClientComponent::IsOfferingSubstitute() const
{
	const ACustomerBase* C = DealCustomer.Get();
	return C && !DealAltProduct.IsNone() && DealAltProduct != C->DesiredProductId;
}

int32 UPhoneClientComponent::GetOfferMarketCents() const
{
	const ACustomerBase* C = DealCustomer.Get();
	return C ? C->GetMarketPriceForProduct(GetOfferedProduct()) : 0;
}

void UPhoneClientComponent::SetOfferedProduct(FName ProductId)
{
	const ACustomerBase* C = DealCustomer.Get();
	// None of het gevraagde product -> terug naar "normaal".
	DealAltProduct = (C && ProductId == C->DesiredProductId) ? NAME_None : ProductId;
	// Reset de vraagprijs naar de markt van het nu aangeboden product (eerlijk bod).
	DealAskCents = GetOfferMarketCents();
	SetDealAskCents(DealAskCents);
}

void UPhoneClientComponent::SetDealAskCents(int32 Cents)
{
	const int32 Market = GetOfferMarketCents();
	if (Market <= 0)
	{
		DealAskCents = FMath::Max(0, Cents);
		return;
	}
	// Band: 40%..200% van de markt van het aangeboden product.
	const int32 Lo = FMath::RoundToInt(Market * 0.40f);
	const int32 Hi = FMath::RoundToInt(Market * 2.00f);
	DealAskCents = FMath::Clamp(Cents, Lo, Hi);
}

void UPhoneClientComponent::MarkUiClickConsumed()
{
	if (const UWorld* W = GetWorld()) { LastUiClickTime = W->GetTimeSeconds(); }
}

bool UPhoneClientComponent::DidUiConsumeClickRecently() const
{
	const UWorld* W = GetWorld();
	return W && (W->GetTimeSeconds() - LastUiClickTime) < 0.25;
}

void UPhoneClientComponent::CloseDeal()
{
	bDealOpen = false;
	DealCustomer = nullptr;
	UpdateCursor();
}

void UPhoneClientComponent::ConfirmDeal()
{
	if (ACustomerBase* C = DealCustomer.Get())
	{
		ServerSubmitOffer(C, GetOfferedProduct(), DealAskCents);
	}
	bDealOpen = false;
	DealCustomer = nullptr;
	DealAltProduct = NAME_None;
	UpdateCursor();
}

void UPhoneClientComponent::ServerSubmitOffer_Implementation(ACustomerBase* Customer, FName ProductId, int32 AskCents)
{
	if (!Customer)
	{
		return;
	}
	AWeedShopGameState* GS = GetGS();
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	UInventoryComponent* Stock = GetOwnerInventory();

	const EDealResult Result = Customer->SubmitOfferProduct(ProductId, AskCents, Econ, Stock);

	if (GEngine)
	{
		FColor Col = FColor::White;
		FString Msg;
		switch (Result)
		{
		case EDealResult::Accepted:
			Col = FColor::Green;
			Msg = FString::Printf(TEXT("Deal! Sold for EUR %.2f"), (AskCents * Customer->DesiredQuantity) / 100.f);
			break;
		case EDealResult::Haggle:
			Col = FColor::Yellow;  Msg = TEXT("Too expensive — they want to haggle."); break;
		case EDealResult::NoStock:
			Col = FColor::Orange; Msg = TEXT("You don't have the stock for this order."); break;
		default:
			Col = FColor::Red;    Msg = TEXT("Customer refused the offer."); break;
		}
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Col, Msg);
	}
}

void UPhoneClientComponent::ServerBuySupply_Implementation(FName SupplyId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetStore())
		{
			GS->GetStore()->BuySupply(SupplyId, GetOwnerInventory());
		}
	}
}

void UPhoneClientComponent::SetTab(int32 NewTab)
{
	Tab = FMath::Clamp(NewTab, 0, AppCount - 1);
	bHomeScreen = false;
}

void UPhoneClientComponent::CycleTab()
{
	// Q = terug naar het home-scherm (of, als je al thuis bent, niets).
	if (bOpen)
	{
		bHomeScreen = true;
	}
}

void UPhoneClientComponent::SetSupplierCat(int32 Cat)
{
	SupplierCat = FMath::Clamp(Cat, 0, UStoreComponent::SupplierCatCount - 1);
}

void UPhoneClientComponent::SellInventoryIndex(int32 StackIndex)
{
	if (const UInventoryComponent* Inv = GetOwnerInventory())
	{
		const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
		if (Stacks.IsValidIndex(StackIndex))
		{
			ServerSell(Stacks[StackIndex].ItemId);
		}
	}
}

void UPhoneClientComponent::SellInventoryIndexAll(int32 StackIndex)
{
	if (const UInventoryComponent* Inv = GetOwnerInventory())
	{
		const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
		if (Stacks.IsValidIndex(StackIndex))
		{
			ServerSellAll(Stacks[StackIndex].ItemId);
		}
	}
}

void UPhoneClientComponent::ServerSellAll_Implementation(FName ItemId)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Store || !Inv) { return; }
	// Verkoop tot er niets meer van dit item is (elke SellItem checkt zelf de voorraad).
	int32 Guard = Inv->GetQuantity(ItemId) + 4;
	while (Guard-- > 0 && Inv->HasItem(ItemId, 1))
	{
		if (!Store->SellItem(ItemId, Inv)) { break; }
	}
}

// --- Winkel: aantal-keuze + winkelwagen ---

int32 UPhoneClientComponent::GetPendingQty(FName ItemId) const
{
	const int32* P = PendingQty.Find(ItemId);
	return P ? *P : 1;
}

void UPhoneClientComponent::AdjustPendingQty(FName ItemId, int32 Delta)
{
	const int32 Cur = GetPendingQty(ItemId);
	PendingQty.Add(ItemId, FMath::Clamp(Cur + Delta, 1, 99));
}

void UPhoneClientComponent::AddToCart(FName ItemId)
{
	if (ItemId.IsNone()) { return; }
	const int32 Qty = GetPendingQty(ItemId);
	for (FCartLine& L : Cart)
	{
		if (L.ItemId == ItemId) { L.Qty = FMath::Clamp(L.Qty + Qty, 1, 999); return; }
	}
	FCartLine NewLine; NewLine.ItemId = ItemId; NewLine.Qty = Qty;
	Cart.Add(NewLine);
}

bool UPhoneClientComponent::GetCartLine(int32 Index, FName& OutItemId, int32& OutQty) const
{
	if (!Cart.IsValidIndex(Index)) { return false; }
	OutItemId = Cart[Index].ItemId;
	OutQty = Cart[Index].Qty;
	return true;
}

void UPhoneClientComponent::AdjustCartLine(int32 Index, int32 Delta)
{
	if (!Cart.IsValidIndex(Index)) { return; }
	Cart[Index].Qty += Delta;
	if (Cart[Index].Qty <= 0) { Cart.RemoveAt(Index); }
}

void UPhoneClientComponent::ClearCart()
{
	Cart.Reset();
}

int32 UPhoneClientComponent::GetCartTotalCents() const
{
	const AWeedShopGameState* GS = GetGS();
	const UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return 0; }
	int32 Total = 0;
	for (const FCartLine& L : Cart)
	{
		Total += Store->GetCatalogPriceCents(L.ItemId) * L.Qty;
	}
	return Total;
}

float UPhoneClientComponent::DeliveryFeePct(int32 Opt)
{
	switch (Opt) { case 2: return 0.25f; case 1: return 0.08f; default: return 0.01f; }
}
float UPhoneClientComponent::DeliveryDelaySeconds(int32 Opt)
{
	switch (Opt) { case 2: return 0.f; case 1: return 40.f; default: return 120.f; }
}
FString UPhoneClientComponent::DeliveryName(int32 Opt)
{
	switch (Opt) { case 2: return TEXT("Instant"); case 1: return TEXT("Express"); default: return TEXT("Standard"); }
}
FString UPhoneClientComponent::DeliveryTimeText(int32 Opt)
{
	switch (Opt) { case 2: return TEXT("now"); case 1: return TEXT("~40s"); default: return TEXT("~2 min"); }
}

void UPhoneClientComponent::Checkout(int32 DeliveryOption)
{
	if (Cart.Num() == 0) { return; }
	TArray<FName> Ids; TArray<int32> Qtys;
	for (const FCartLine& L : Cart) { Ids.Add(L.ItemId); Qtys.Add(L.Qty); }
	ServerBuyCart(Ids, Qtys, DeliveryOption);
	Cart.Reset();
}

void UPhoneClientComponent::DeliverCart(const TArray<FName>& ItemIds, const TArray<int32>& Quantities)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Store || !Inv) { return; }

	int32 Bought = 0, Failed = 0;
	for (int32 i = 0; i < ItemIds.Num(); ++i)
	{
		const int32 Qty = Quantities.IsValidIndex(i) ? Quantities[i] : 0;
		for (int32 q = 0; q < Qty; ++q)
		{
			if (Store->BuyAny(ItemIds[i], Inv)) { ++Bought; }
			else { ++Failed; break; }
		}
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Failed > 0 ? FColor::Orange : FColor::Green,
			FString::Printf(TEXT("Delivery arrived: %d item(s)%s"), Bought, Failed > 0 ? TEXT(" (some failed - low cash/phase)") : TEXT("")));
	}
}

void UPhoneClientComponent::ServerBuyCart_Implementation(const TArray<FName>& ItemIds, const TArray<int32>& Quantities, int32 DeliveryOption)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Store || !Inv) { return; }

	// Bezorgkosten = % van het besteed bedrag (subtotaal van de wagen), nu afgeschreven.
	int64 Subtotal = 0;
	for (int32 i = 0; i < ItemIds.Num(); ++i)
	{
		Subtotal += (int64)Store->GetCatalogPriceCents(ItemIds[i]) * (Quantities.IsValidIndex(i) ? Quantities[i] : 0);
	}
	const int32 Fee = FMath::RoundToInt(Subtotal * DeliveryFeePct(DeliveryOption));
	UEconomyComponent* Econ = GS->GetEconomy();
	if (Fee > 0 && Econ && !Econ->RemoveMoney(Fee))
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("Not enough money for the delivery fee.")); }
		return;
	}

	const float Delay = DeliveryDelaySeconds(DeliveryOption);
	if (Delay <= 0.f)
	{
		DeliverCart(ItemIds, Quantities); // instant
	}
	else
	{
		// Plan de levering; items + itemprijs komen na de levertijd.
		TArray<FName> CapIds = ItemIds; TArray<int32> CapQ = Quantities;
		FTimerHandle H;
		TWeakObjectPtr<UPhoneClientComponent> Self(this);
		GetWorld()->GetTimerManager().SetTimer(H, [Self, CapIds, CapQ]()
		{
			if (Self.IsValid()) { Self->DeliverCart(CapIds, CapQ); }
		}, Delay, false);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor(120, 200, 255),
				FString::Printf(TEXT("Order placed - %s delivery (%s). Fee EUR %.2f"), *DeliveryName(DeliveryOption), *DeliveryTimeText(DeliveryOption), Fee / 100.f));
		}
	}
}

void UPhoneClientComponent::OpenPotUpgrade(AGrowPlant* Pot)
{
	if (!Pot)
	{
		return;
	}
	UpgPot = Pot;
	bPotUpgradeOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::ClosePotUpgrade()
{
	bPotUpgradeOpen = false;
	UpgPot = nullptr;
	UpdateCursor();
}

void UPhoneClientComponent::BuyPotUpgrade(int32 UpgIndex)
{
	if (AGrowPlant* Pot = UpgPot.Get())
	{
		ServerBuyPotUpgrade(Pot, UpgIndex);
	}
}

void UPhoneClientComponent::ServerBuyPotUpgrade_Implementation(AGrowPlant* Pot, int32 UpgIndex)
{
	if (!Pot || Pot->HasPotUpgrade(UpgIndex))
	{
		return;
	}
	const int32 Cost = GetPotUpgradeCost(UpgIndex, Pot->GetPotTier());
	AWeedShopGameState* GS = GetGS();
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	if (Cost <= 0 || !Econ || !Econ->RemoveMoney(Cost))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Red, TEXT("Not enough money for that pot upgrade."));
		}
		return;
	}
	Pot->ApplyPotUpgrade(UpgIndex);
	if (GEngine)
	{
		const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
		const FString Name = Ups.IsValidIndex(UpgIndex) ? Ups[UpgIndex].DisplayName : TEXT("upgrade");
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Pot upgrade installed: %s"), *Name));
	}
}

void UPhoneClientComponent::ServerSell_Implementation(FName ItemId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetStore())
		{
			GS->GetStore()->SellItem(ItemId, GetOwnerInventory());
		}
	}
}

void UPhoneClientComponent::HandleNumberKey(FKey Key)
{
	int32 Index = -1;
	if (Key == EKeys::One)        Index = 0;
	else if (Key == EKeys::Two)   Index = 1;
	else if (Key == EKeys::Three) Index = 2;
	else if (Key == EKeys::Four)  Index = 3;
	else if (Key == EKeys::Five)  Index = 4;
	else if (Key == EKeys::Six)   Index = 5;
	if (Index >= 0)
	{
		DoAction(Index);
	}
}

void UPhoneClientComponent::DoAction(int32 Index)
{
	if (!bOpen)
	{
		return;
	}
	AWeedShopGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	if (Tab == 1) // Suppliers: items uit de huidige subcategorie
	{
		if (UStoreComponent* Store = GS->GetStore())
		{
			const TArray<FName> Items = Store->GetSupplierCategory(SupplierCat);
			if (Items.IsValidIndex(Index))
			{
				if (UStoreComponent::IsSeedCategory(SupplierCat))
				{
					ServerBuySeed(Items[Index]);
				}
				else
				{
					ServerBuySupply(Items[Index]);
				}
			}
		}
	}
	else if (Tab == 3) // Berichten: 0 = accepteren, 1 = weigeren
	{
		if (Index == 0)      { ServerRespond(true); }
		else if (Index == 1) { ServerRespond(false); }
	}
	else if (Tab == 0) // Upgrades
	{
		if (GS->GetUpgrades())
		{
			const TArray<FName> Ids = GS->GetUpgrades()->GetAllUpgradeIds();
			if (Ids.IsValidIndex(Index))
			{
				ServerBuyUpgrade(Ids[Index]);
			}
		}
	}
	// Tab == 2 (Contacten): geen actie
}

void UPhoneClientComponent::ServerBuyUpgrade_Implementation(FName UpgradeId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetUpgrades())
		{
			GS->GetUpgrades()->BuyUpgrade(UpgradeId);
		}
	}
}

void UPhoneClientComponent::ServerBuySeed_Implementation(FName StrainId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetStore())
		{
			GS->GetStore()->BuySeed(StrainId, GetOwnerInventory());
		}
	}
}

void UPhoneClientComponent::ServerRespond_Implementation(bool bAccept)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetContacts())
		{
			GS->GetContacts()->RespondTopPending(bAccept);
		}
	}
}

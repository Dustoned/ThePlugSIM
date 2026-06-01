#include "Phone/PhoneClientComponent.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
#include "Phone/ContactsComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Economy/EconomyComponent.h"
#include "Customer/CustomerBase.h"
#include "Customer/CustomerSpawner.h"
#include "World/DeliveryDrone.h"
#include "EngineUtils.h"
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
#include "UI/RollWidget.h"
#include "UI/CompassWidget.h"
#include "UI/HotkeyHintWidget.h"
#include "UI/AtmWidget.h"
#include "UI/PackWidget.h"
#include "UI/ShelfWidget.h"
#include "UI/PauseMenuWidget.h"
#include "World/StorageShelf.h"
#include "Blueprint/UserWidget.h"
#include "Net/UnrealNetwork.h"

UPhoneClientComponent::UPhoneClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPhoneClientComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPhoneClientComponent, bBankAppUnlocked);
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

UEconomyComponent* UPhoneClientComponent::GetOwnerEconomy() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UEconomyComponent>() : nullptr;
}

void UPhoneClientComponent::UpdateCursor()
{
	const bool bAnyUI = bOpen || bRollOpen || bDealOpen || bInventoryOpen || bPotUpgradeOpen || bMergeOpen || bAtmOpen || bPackOpen || bShelfOpen || bPauseOpen;
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
	RollWidget = CreateWidget<URollWidget>(PC, URollWidget::StaticClass());
	if (RollWidget) { RollWidget->SetPhone(this); RollWidget->AddToViewport(26); }
	CompassWidget = CreateWidget<UCompassWidget>(PC, UCompassWidget::StaticClass());
	if (CompassWidget) { CompassWidget->AddToViewport(3); }
	HotkeyWidget = CreateWidget<UHotkeyHintWidget>(PC, UHotkeyHintWidget::StaticClass());
	if (HotkeyWidget) { HotkeyWidget->AddToViewport(2); }
	AtmWidget = CreateWidget<UAtmWidget>(PC, UAtmWidget::StaticClass());
	if (AtmWidget) { AtmWidget->SetPhone(this); AtmWidget->AddToViewport(28); }
	PackWidget = CreateWidget<UPackWidget>(PC, UPackWidget::StaticClass());
	if (PackWidget) { PackWidget->SetPhone(this); PackWidget->AddToViewport(29); }
	ShelfWidget = CreateWidget<UShelfWidget>(PC, UShelfWidget::StaticClass());
	if (ShelfWidget) { ShelfWidget->SetPhone(this); ShelfWidget->AddToViewport(31); }
	PauseWidget = CreateWidget<UPauseMenuWidget>(PC, UPauseMenuWidget::StaticClass());
	if (PauseWidget) { PauseWidget->SetPhone(this); PauseWidget->AddToViewport(40); }
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
		bAtmOpen = false; bPackOpen = false; bShelfOpen = false;
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

void UPhoneClientComponent::OpenAtm()
{
	EnsureWidget();
	bAtmOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false; bShelfOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseAtm()
{
	bAtmOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::OpenPack(int32 Batch)
{
	EnsureWidget();
	PackBatchUI = FMath::Max(1, Batch);
	bPackOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false; bAtmOpen = false; bShelfOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::ClosePack()
{
	bPackOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::TogglePause()
{
	if (bPauseOpen) { ClosePause(); }
	else { OpenPause(); }
}

void UPhoneClientComponent::OpenPause()
{
	EnsureWidget();
	bPauseOpen = true;
	// Sluit alle andere schermen zodat het pauze-menu schoon bovenop ligt.
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bShelfOpen = false;
	// In standalone (single-player) pauzeren we de wereld echt; in co-op blijft de wereld lopen.
	if (APlayerController* PC = GetPC())
	{
		if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone)
		{
			PC->SetPause(true);
		}
	}
	UpdateCursor();
}

void UPhoneClientComponent::ClosePause()
{
	bPauseOpen = false;
	if (APlayerController* PC = GetPC())
	{
		PC->SetPause(false);
	}
	UpdateCursor();
}

void UPhoneClientComponent::OpenToApp(int32 AppIndex)
{
	EnsureWidget();
	ClosePause();
	bOpen = true;
	bHomeScreen = false;
	Tab = FMath::Clamp(AppIndex, 0, AppCount - 1);
	bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false;
	bAtmOpen = false; bPackOpen = false; bShelfOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::OpenShelf(AStorageShelf* Shelf)
{
	if (!Shelf) { return; }
	EnsureWidget();
	ShelfActor = Shelf;
	bShelfOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseShelf()
{
	bShelfOpen = false;
	ShelfActor = nullptr;
	UpdateCursor();
}

AStorageShelf* UPhoneClientComponent::GetShelf() const
{
	return ShelfActor.Get();
}

void UPhoneClientComponent::RequestShelfStore(FName ItemId, int32 Count)
{
	ServerShelfStore(ShelfActor.Get(), ItemId, Count);
}

void UPhoneClientComponent::RequestShelfTake(int32 SlotIndex, int32 Count)
{
	ServerShelfTake(ShelfActor.Get(), SlotIndex, Count);
}

void UPhoneClientComponent::ServerShelfStore_Implementation(AStorageShelf* Shelf, FName ItemId, int32 Count)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Shelf || !Inv || ItemId.IsNone() || Count <= 0) { return; }
	// Afstand-check (anti-cheat/lag).
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Shelf->GetActorLocation()) > 400.f) { return; }

	const int32 Have = Inv->GetQuantity(ItemId);
	const int32 Want = FMath::Min(Count, Have);
	if (Want <= 0) { return; }
	const float Thc = Inv->GetItemQuality(ItemId);
	const float Qual = Inv->GetItemQualityPct(ItemId);
	const int32 Stored = Shelf->ServerStore(ItemId, Want, Thc, Qual);
	if (Stored > 0) { Inv->RemoveItem(ItemId, Stored); }
	else if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("Shelf is full.")); }
}

void UPhoneClientComponent::ServerShelfTake_Implementation(AStorageShelf* Shelf, int32 SlotIndex, int32 Count)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Shelf || !Inv || Count <= 0) { return; }
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Shelf->GetActorLocation()) > 400.f) { return; }

	FName OutId; float OutThc = 0.f; float OutQual = 0.f;
	const int32 Taken = Shelf->ServerTake(SlotIndex, Count, OutId, OutThc, OutQual);
	if (Taken <= 0) { return; }
	if (!Inv->AddItem(OutId, Taken, OutThc, OutQual))
	{
		// Geen ruimte in de inventory -> terug op het schap.
		Shelf->ServerStore(OutId, Taken, OutThc, OutQual);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No room in your inventory.")); }
	}
}

int32 UPhoneClientComponent::ContainerCapacity(FName ContainerId)
{
	const FString S = ContainerId.ToString();
	if (S == TEXT("Cont_Bag2"))     { return 2; }
	if (S == TEXT("Cont_Bag5"))     { return 5; }
	if (S == TEXT("Cont_Jar10"))    { return 10; }
	if (S == TEXT("Cont_Jar15"))    { return 15; }
	if (S == TEXT("Cont_Block100")) { return 100; }
	if (S == TEXT("Cont_Garbage500")) { return 500; }
	return 0;
}

void UPhoneClientComponent::RequestPack(FName BudId, FName ContainerId)
{
	ServerPack(BudId, ContainerId, PackBatchUI);
}

void UPhoneClientComponent::ServerPack_Implementation(FName BudId, FName ContainerId, int32 Batch)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv) { return; }
	const FString BudStr = BudId.ToString();
	if (!BudStr.StartsWith(TEXT("Bud_"))) { return; }            // alleen GEDROOGDE buds verpakken
	const int32 Cap = ContainerCapacity(ContainerId);
	if (Cap <= 0) { return; }

	const FName BagId(*FString::Printf(TEXT("Bag_%s"), *BudStr.RightChop(4))); // Bud_X -> Bag_X
	const float Thc = Inv->GetItemQuality(BudId);
	const float Q = Inv->GetItemQualityPct(BudId);

	// Verwerk tot Batch zakjes (tier van de tafel) zolang je containers + wiet hebt.
	int32 BagsMade = 0, TotalGrams = 0;
	const int32 N = FMath::Max(1, Batch);
	for (int32 b = 0; b < N; ++b)
	{
		if (!Inv->HasItem(ContainerId, 1)) { break; }
		const int32 Have = Inv->GetQuantity(BudId);
		if (Have <= 0) { break; }
		const int32 Grams = FMath::Min(Cap, Have);
		if (!Inv->RemoveItem(BudId, Grams)) { break; }
		Inv->RemoveItem(ContainerId, 1);
		Inv->AddItem(BagId, Grams, Thc, Q);
		++BagsMade; TotalGrams += Grams;
	}
	if (GEngine && BagsMade > 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor(120, 220, 160),
			FString::Printf(TEXT("Packed %d bag(s) (%dg total)."), BagsMade, TotalGrams));
	}
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

void UPhoneClientComponent::LoadRoll()
{
	SetRollLoadedUI(true, RollGrams);
	// Onthoud welke wiet geladen is (voor de hint rechtsonder).
	FName WeedId; float Thc = 0.f, Q = 0.f;
	if (GetRollWeed(RollGrams, WeedId, Thc, Q))
	{
		RollLoadDesc = FString::Printf(TEXT("%dg %s - %.0f%% THC, %.0f%% quality"),
			RollGrams, *WeedUI::PrettyItemName(WeedId), Thc, Q);
	}
	else
	{
		RollLoadDesc = FString::Printf(TEXT("%dg loaded"), RollGrams);
	}
	bRollOpen = false; // menu sluit; rollen door rechtermuis in te houden
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

float UPhoneClientComponent::JointIntensity(int32 Grams, float ThcPercent, float QualityPct)
{
	// Joint-sterkte = KWALITEIT (niet THC; THC% verandert toch niet per joint). Het aantal gram dat je
	// erin doet schaalt de EFFECTIEVE kwaliteit: 1g van 70%-wiet voelt zwakker dan een volle joint van
	// dezelfde wiet. Zo levert een dun/zwak jointje minder op en zijn niet-verslaafde of doorgewinterde
	// rokers er minder snel van onder de indruk. ~3g = volle kwaliteit; een dikke joint kan richting 100%.
	(void)ThcPercent;
	const float Q = FMath::Clamp(QualityPct / 100.f, 0.f, 1.f);
	const float GramsFactor = FMath::Max(0, Grams) / 3.f; // 1g=0.33, 2g=0.67, 3g=1.0, meer=boven
	return FMath::Clamp(Q * GramsFactor, 0.f, 1.f);
}

bool UPhoneClientComponent::GetRollWeed(int32 Grams, FName& OutItemId, float& OutThcPercent, float& OutQualityPct) const
{
	OutItemId = NAME_None; OutThcPercent = 0.f; OutQualityPct = 0.f;
	const UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv) { return false; }
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.ItemId.ToString().StartsWith(TEXT("Bud_")) && St.Quantity >= Grams)
		{
			OutItemId = St.ItemId;
			OutThcPercent = St.Quality;
			OutQualityPct = St.QualityPct;
			return true;
		}
	}
	return false;
}

bool UPhoneClientComponent::GetRollWeedInfo(int32 Grams, float& OutThcPercent, float& OutQualityPct) const
{
	FName Id;
	return GetRollWeed(Grams, Id, OutThcPercent, OutQualityPct);
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
	UEconomyComponent* Econ = GetOwnerEconomy();
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

int32 UPhoneClientComponent::GetPendingSellQty(FName ItemId) const
{
	const int32* P = PendingSellQty.Find(ItemId);
	return P ? *P : 1;
}

void UPhoneClientComponent::AdjustPendingSellQty(FName ItemId, int32 Delta)
{
	const int32 Cur = GetPendingSellQty(ItemId);
	PendingSellQty.Add(ItemId, FMath::Clamp(Cur + Delta, 1, 999));
}

void UPhoneClientComponent::AddToCart(FName ItemId)
{
	if (ItemId.IsNone()) { return; }
	const int32 Qty = GetPendingQty(ItemId);
	for (FCartLine& L : Cart)
	{
		if (L.ItemId == ItemId && !L.bSell) { L.Qty = FMath::Clamp(L.Qty + Qty, 1, 999); return; }
	}
	FCartLine NewLine; NewLine.ItemId = ItemId; NewLine.Qty = Qty; NewLine.bSell = false;
	Cart.Add(NewLine);
}

void UPhoneClientComponent::AddSellToCart(FName ItemId)
{
	if (ItemId.IsNone()) { return; }
	// Niet meer in de wagen zetten dan je hebt.
	const UInventoryComponent* Inv = GetOwnerInventory();
	const int32 Have = Inv ? Inv->GetQuantity(ItemId) : 0;
	if (Have <= 0) { return; }
	const int32 Want = GetPendingSellQty(ItemId);
	for (FCartLine& L : Cart)
	{
		if (L.ItemId == ItemId && L.bSell) { L.Qty = FMath::Clamp(L.Qty + Want, 1, Have); return; }
	}
	FCartLine NewLine; NewLine.ItemId = ItemId; NewLine.Qty = FMath::Min(Want, Have); NewLine.bSell = true;
	Cart.Add(NewLine);
}

bool UPhoneClientComponent::GetCartLine(int32 Index, FName& OutItemId, int32& OutQty, bool& bOutSell) const
{
	if (!Cart.IsValidIndex(Index)) { return false; }
	OutItemId = Cart[Index].ItemId;
	OutQty = Cart[Index].Qty;
	bOutSell = Cart[Index].bSell;
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
	return GetCartBuyCents();
}

int32 UPhoneClientComponent::GetCartBuyCents() const
{
	const AWeedShopGameState* GS = GetGS();
	const UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return 0; }
	int32 Total = 0;
	for (const FCartLine& L : Cart)
	{
		if (!L.bSell) { Total += Store->GetCatalogPriceCents(L.ItemId) * L.Qty; }
	}
	return Total;
}

int32 UPhoneClientComponent::GetCartSellCents() const
{
	const AWeedShopGameState* GS = GetGS();
	const UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return 0; }
	int32 Total = 0;
	for (const FCartLine& L : Cart)
	{
		if (L.bSell) { Total += Store->GetSellValueCents(L.ItemId) * L.Qty; }
	}
	return Total;
}

int32 UPhoneClientComponent::GetCartNetCents(int32 DeliveryOption) const
{
	const int32 Buy = GetCartBuyCents();
	const int32 Sell = GetCartSellCents();
	const int32 Fee = FMath::RoundToInt(Buy * DeliveryFeePct(DeliveryOption)); // bezorgkosten alleen op koopdeel
	return Buy + Fee - Sell; // negatief = je ontvangt geld
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
	TArray<FName> BuyIds, SellIds; TArray<int32> BuyQ, SellQ;
	for (const FCartLine& L : Cart)
	{
		if (L.bSell) { SellIds.Add(L.ItemId); SellQ.Add(L.Qty); }
		else { BuyIds.Add(L.ItemId); BuyQ.Add(L.Qty); }
	}
	ServerBuyCart(BuyIds, BuyQ, SellIds, SellQ, DeliveryOption);
	Cart.Reset();
}

void UPhoneClientComponent::DeliverCart(int32 OrderId, const TArray<FName>& ItemIds, const TArray<int32>& Quantities)
{
	// Ruim de pending-regel + timer op (ook bij directe levering geen-op als OrderId 0).
	if (OrderId > 0)
	{
		PendingDeliveries.RemoveAll([OrderId](const FPendingDelivery& D) { return D.OrderId == OrderId; });
		DeliveryTimers.Remove(OrderId);
	}

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

void UPhoneClientComponent::ServerBuyCart_Implementation(const TArray<FName>& BuyIds, const TArray<int32>& BuyQtys,
	const TArray<FName>& SellIds, const TArray<int32>& SellQtys, int32 DeliveryOption)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Store || !Inv || !Econ) { return; }

	// 0) Level-gate: hogere tiers (rekken/tafels/containers) vereisen een minimum level.
	const int32 PlayerLvl = GS->GetLeveling() ? GS->GetLeveling()->GetLevel() : 1;
	for (int32 i = 0; i < BuyIds.Num(); ++i)
	{
		const int32 Req = UStoreComponent::RequiredLevelFor(BuyIds[i]);
		if (Req > PlayerLvl)
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.5f, FColor::Orange, FString::Printf(TEXT("%s unlocks at level %d."), *Store->GetCatalogName(BuyIds[i]).ToString(), Req)); }
			return;
		}
	}

	// 1) Koop-subtotaal + bezorgkosten (alleen op het koopdeel).
	int64 BuySub = 0;
	for (int32 i = 0; i < BuyIds.Num(); ++i)
	{
		BuySub += (int64)Store->GetCatalogPriceCents(BuyIds[i]) * (BuyQtys.IsValidIndex(i) ? BuyQtys[i] : 0);
	}
	const int64 Fee = FMath::RoundToInt(BuySub * DeliveryFeePct(DeliveryOption));

	// 2) Verkoop-opbrengst op basis van wat de speler echt heeft (clamp; nog niet verwijderen).
	int64 SellProceeds = 0;
	TArray<int32> SellActual; SellActual.SetNum(SellIds.Num());
	for (int32 i = 0; i < SellIds.Num(); ++i)
	{
		const int32 Want = SellQtys.IsValidIndex(i) ? SellQtys[i] : 0;
		const int32 N = FMath::Min(Want, Inv->GetQuantity(SellIds[i]));
		SellActual[i] = N;
		SellProceeds += (int64)Store->GetSellValueCents(SellIds[i]) * N;
	}

	// 3) De telefoon-winkel is online/legaal -> het KOOPdeel betaal je met BANKGELD (wit). De verkoop
	//    van je waar levert CASH (zwart) op. Genoeg bankgeld voor de aankoop?
	const int64 Cost = BuySub + Fee;
	if (Cost > 0 && !Econ->CanAffordBank(Cost))
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.5f, FColor::Red, TEXT("Not enough BANK money - launder some cash first (Bank app).")); }
		return;
	}

	// 4) Verkoop uitvoeren (items weg, opbrengst als CASH), daarna het koopdeel van de BANK afschrijven.
	for (int32 i = 0; i < SellIds.Num(); ++i)
	{
		if (SellActual[i] > 0) { Inv->RemoveItem(SellIds[i], SellActual[i]); }
	}
	if (SellProceeds > 0) { Econ->AddMoney(SellProceeds); } // cash (zwart)
	if (Cost > 0) { Econ->RemoveBank(Cost); }               // bank (wit)

	// 5) Geen koop-items? Dan zijn we klaar (alleen verkocht).
	int32 BuyCount = 0;
	for (int32 i = 0; i < BuyIds.Num(); ++i) { BuyCount += (BuyQtys.IsValidIndex(i) ? BuyQtys[i] : 0); }
	if (BuyCount <= 0)
	{
		if (GEngine && SellProceeds > 0)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Sold for EUR %.2f"), SellProceeds / 100.f));
		}
		return;
	}

	// 6) Koop-items: bezorgdrone naar de voordeur (items zijn al betaald -> gratis op pickup).
	const float Flight = FMath::Max(DeliveryDelaySeconds(DeliveryOption), 5.f);
	const int32 OrderId = NextOrderId++;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	FPendingDelivery PD;
	PD.OrderId = OrderId;
	PD.DeliveryOpt = DeliveryOption;
	PD.FeeCents = Fee;
	PD.PaidCents = Cost;       // koopprijs + fee, terug bij annuleren
	PD.PlacedTime = Now;
	PD.ArriveTime = Now + Flight;
	PD.Ids = BuyIds;
	PD.Qtys = BuyQtys;
	for (int32 i = 0; i < BuyIds.Num(); ++i)
	{
		const int32 Q = BuyQtys.IsValidIndex(i) ? BuyQtys[i] : 0;
		PD.ItemCount += Q;
		if (!PD.Summary.IsEmpty()) { PD.Summary += TEXT(", "); }
		PD.Summary += FString::Printf(TEXT("%dx %s"), Q, *WeedUI::PrettyItemName(BuyIds[i]));
	}

	if (UWorld* World = GetWorld())
	{
		const FVector Drop = FindDeliveryPoint();
		const FVector Start = Drop + FVector(-1700.f, 700.f, 1500.f);
		ADeliveryDrone* Drone = World->SpawnActor<ADeliveryDrone>(ADeliveryDrone::StaticClass(), FTransform(Start));
		if (Drone)
		{
			Drone->Setup(Start, Drop, Flight, OrderId, BuyIds, BuyQtys, this);
			PD.Drone = Drone;
		}
	}
	PendingDeliveries.Add(PD);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor(120, 200, 255),
			FString::Printf(TEXT("Order placed - %s delivery. A drone is bringing it to your door."), *DeliveryName(DeliveryOption)));
	}
}

FVector UPhoneClientComponent::FindDeliveryPoint() const
{
	const UWorld* World = GetWorld();
	FVector Point = FVector::ZeroVector;
	bool bFound = false;

	if (World)
	{
		// 1) Een door de level-designer getagde actor "DeliveryPoint".
		for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (It->ActorHasTag(FName(TEXT("DeliveryPoint")))) { Point = It->GetActorLocation(); bFound = true; break; }
		}
		// 2) Anders: de (eerste) plek waar klanten verschijnen = bij de voordeur.
		if (!bFound)
		{
			for (TActorIterator<ACustomerSpawner> It(const_cast<UWorld*>(World)); It; ++It)
			{
				Point = It->GetActorLocation(); bFound = true; break;
			}
		}
	}
	// 3) Fallback: voor de speler.
	if (!bFound)
	{
		if (const APawn* P = Cast<APawn>(GetOwner()))
		{
			Point = P->GetActorLocation() + P->GetActorForwardVector() * 300.f;
		}
	}

	// Op de grond plaatsen (recht naar beneden tracen).
	if (World)
	{
		FHitResult Hit;
		const FVector DStart = Point + FVector(0.f, 0.f, 300.f);
		const FVector DEnd = Point - FVector(0.f, 0.f, 600.f);
		FCollisionQueryParams Params;
		if (GetOwner()) { Params.AddIgnoredActor(GetOwner()); }
		if (World->LineTraceSingleByChannel(Hit, DStart, DEnd, ECC_Visibility, Params))
		{
			Point.Z = Hit.ImpactPoint.Z;
		}
	}
	return Point;
}

void UPhoneClientComponent::NotifyDroneArrived(int32 OrderId)
{
	for (FPendingDelivery& D : PendingDeliveries)
	{
		if (D.OrderId == OrderId) { D.bArrived = true; D.Drone = nullptr; break; }
	}
}

void UPhoneClientComponent::OnPackagePickedUp(int32 OrderId)
{
	PendingDeliveries.RemoveAll([OrderId](const FPendingDelivery& D) { return D.OrderId == OrderId; });
}

void UPhoneClientComponent::RequestDeposit(int64 CashAmount)
{
	ServerDeposit(CashAmount);
}

void UPhoneClientComponent::ServerDeposit_Implementation(int64 CashAmount)
{
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Econ) { return; }
	int64 Amt = CashAmount;
	if (Amt <= 0) { Amt = FMath::Min(Econ->GetCashCents(), Econ->GetDailyDepositRemainingCents()); } // max
	if (Amt > 0) { Econ->Deposit(Amt); }
}

void UPhoneClientComponent::RequestTransfer(int64 AmountCents)
{
	ServerTransfer(AmountCents);
}

void UPhoneClientComponent::ServerTransfer_Implementation(int64 AmountCents)
{
	UEconomyComponent* Mine = GetOwnerEconomy();
	if (!Mine || AmountCents <= 0) { return; }

	// Zoek een co-op vriend (de portemonnee van een andere speler) om naar te sturen.
	UEconomyComponent* Friend = nullptr;
	if (UWorld* W = GetWorld())
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			APawn* P = PC ? PC->GetPawn() : nullptr;
			if (!P || P == GetOwner()) { continue; }
			if (UEconomyComponent* E = P->FindComponentByClass<UEconomyComponent>()) { Friend = E; break; }
		}
	}
	if (!Friend)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange, TEXT("No co-op friend online to send money to.")); }
		return;
	}

	// Bedrag + fee verlaat MIJN bank; de vriend ontvangt het volle bedrag (belastingvrij).
	if (Mine->TransferBank(AmountCents))
	{
		Friend->AddBank(AmountCents, false);
	}
}

void UPhoneClientComponent::RequestBuyPhoneUpgrade()
{
	ServerBuyPhoneUpgrade();
}

void UPhoneClientComponent::ServerBuyPhoneUpgrade_Implementation()
{
	if (bBankAppUnlocked) { return; }
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Econ || !Econ->RemoveBank(PhoneUpgradeCostCents))
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("Not enough BANK money for the phone upgrade (launder cash first).")); }
		return;
	}
	bBankAppUnlocked = true;
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, TEXT("Phone upgraded - Bank app unlocked!")); }
}

float UPhoneClientComponent::GetDeliveryProgress(const FPendingDelivery& D) const
{
	const float Span = D.ArriveTime - D.PlacedTime;
	if (Span <= 0.f) { return 1.f; }
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : D.ArriveTime;
	return FMath::Clamp((Now - D.PlacedTime) / Span, 0.f, 1.f);
}

float UPhoneClientComponent::GetDeliverySecondsLeft(const FPendingDelivery& D) const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : D.ArriveTime;
	return FMath::Max(0.f, D.ArriveTime - Now);
}

void UPhoneClientComponent::CancelDelivery(int32 OrderId)
{
	ServerCancelDelivery(OrderId);
}

void UPhoneClientComponent::ServerCancelDelivery_Implementation(int32 OrderId)
{
	const int32 Idx = PendingDeliveries.IndexOfByPredicate([OrderId](const FPendingDelivery& D) { return D.OrderId == OrderId; });
	if (Idx == INDEX_NONE) { return; }

	// Ligt het pakket al bij de deur? Dan niet annuleren - gewoon oppakken.
	if (PendingDeliveries[Idx].bArrived)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange, TEXT("Already delivered - pick it up at the door.")); }
		return;
	}
	// Drone nog onderweg -> laat 'm verdwijnen.
	if (ADeliveryDrone* Drone = PendingDeliveries[Idx].Drone.Get())
	{
		Drone->Destroy();
	}
	// Het koopdeel (itemprijs + fee) was al bij checkout betaald -> volledig terugstorten.
	const int64 Refund = PendingDeliveries[Idx].PaidCents;
	if (Refund > 0)
	{
		if (UEconomyComponent* Econ = GetOwnerEconomy()) { Econ->AddBank(Refund, false); } // terug op de bank
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow,
			FString::Printf(TEXT("Order cancelled - EUR %.2f refunded"), Refund / 100.f));
	}
	PendingDeliveries.RemoveAt(Idx);
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
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (Cost <= 0 || !Econ || !Econ->RemoveBank(Cost)) // via telefoon -> bankgeld (wit)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Red, TEXT("Not enough BANK money for that pot upgrade (launder cash first)."));
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
			GS->GetUpgrades()->BuyUpgrade(UpgradeId, GetOwnerEconomy());
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

void UPhoneClientComponent::ServerRespondContact_Implementation(FName ContactId, bool bAccept)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetContacts())
		{
			GS->GetContacts()->RespondToContact(ContactId, bAccept);
		}
	}
}

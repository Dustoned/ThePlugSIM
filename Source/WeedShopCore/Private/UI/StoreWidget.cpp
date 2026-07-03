#include "UI/StoreWidget.h"
#include "WeedShopCore.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "World/StoreCounter.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
#include "Economy/EconomyComponent.h"
#include "Game/WeedShopGameState.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"

void UStoreWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* StoreBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, int32 FontSize, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 8.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		S.NormalPadding = FMargin(9.f, 4.f); S.PressedPadding = FMargin(9.f, 4.f);
		B->SetStyle(S);
		B->SetContent(WeedUI::Text(Tree, Label, FontSize, FLinearColor::White, true, true));
		return B;
	}

	FString ShopTitleFor(EShopKind Kind)
	{
		switch (Kind)
		{
		case EShopKind::Grow:       return TEXT("GROW SHOP");
		case EShopKind::Furniture:  return TEXT("FURNITURE STORE");
		case EShopKind::Supplies:   return TEXT("SUPPLIES STORE");
		case EShopKind::GasStation: return TEXT("GAS STATION");
		default:                    return TEXT("SHOP");
		}
	}

	void CatsForKind(EShopKind Kind, TArray<int32>& Out)
	{
		switch (Kind)
		{
		case EShopKind::Grow:       Out = { 0, 1, 5, 6, 9, 8 }; break;
		case EShopKind::Supplies:   Out = { 4, 3, 2, 10 }; break;
		case EShopKind::Furniture:  Out = { 7 }; break;
		case EShopKind::GasStation: Out = { 4 }; break;
		default:                    Out = { 4 }; break;
		}
	}

	FString StoreCatName(int32 Cat)
	{
		switch (Cat)
		{
		case 0: return TEXT("Seeds");
		case 1: return TEXT("Pots");
		case 2: return TEXT("Drying");
		case 3: return TEXT("Packing");
		case 4: return TEXT("Papers");
		case 5: return TEXT("Soil");
		case 6: return TEXT("Water");
		case 7: return TEXT("Furniture");
		case 8: return TEXT("Grow Upg.");
		case 9: return TEXT("Care");
		case 10: return TEXT("Hash");
		default: return TEXT("Items");
		}
	}

	// Prijs van een catalog-id (seed of supply). 0 als onbekend.
	int32 PriceOf(UStoreComponent* Store, FName Id, bool& bOutSeed)
	{
		FText N; int32 Price = 0; int32 Pack = 1;
		if (Store->GetSeedDisplay(Id, N, Price)) { bOutSeed = true; return Price; }
		if (Store->GetSupplyDisplay(Id, N, Price, Pack)) { bOutSeed = false; return Price; }
		bOutSeed = false; return 0;
	}
}

int32 UStoreWidget::CartTotalCents() const
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return 0; }
	int32 Total = 0;
	for (const TPair<FName, int32>& KV : Cart)
	{
		bool bSeed = false;
		Total += PriceOf(Store, KV.Key, bSeed) * KV.Value;
	}
	return Total;
}

void UStoreWidget::CartAdd(FName Id, int32 Delta)
{
	int32& Q = Cart.FindOrAdd(Id);
	Q = FMath::Clamp(Q + Delta, 0, 999);
	if (Q == 0) { Cart.Remove(Id); }
	bConfirmPending = false; // mand gewijzigd -> bevestiging vervalt
	LastSig.Reset(); // herbouw
}

TSharedRef<SWidget> UStoreWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UStoreWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StoreCard"));
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 18.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(18.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(760.f, 600.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop: titel + Exit.
	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("SHOP"), 20, WeedUI::ColText(), false, true);
	UHorizontalBoxSlot* TS = Head->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	Head->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Exit"), WeedUI::ColWarn(), 12,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseStore(); } }));
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Betaal-toggle + instant-melding.
	UHorizontalBox* PayRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PayRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("Pay with:"), 13, WeedUI::ColTextDim(), false))->SetVerticalAlignment(VAlign_Center);
	PayCashBtn = StoreBtn(WidgetTree, TEXT("Cash"), WeedUI::ColGood(), 12, [this]() { if (PhoneComp.IsValid()) { PhoneComp->SetStorePayBank(false); LastSig.Reset(); } });
	PayBankBtn = StoreBtn(WidgetTree, TEXT("Bank"), WeedUI::ColAccent(), 12, [this]() { if (PhoneComp.IsValid()) { PhoneComp->SetStorePayBank(true); LastSig.Reset(); } });
	PayRow->AddChildToHorizontalBox(PayCashBtn)->SetPadding(FMargin(10.f, 0.f, 6.f, 0.f));
	PayRow->AddChildToHorizontalBox(PayBankBtn);
	PayRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("   instant, no delivery fee"), 11, WeedUI::ColTextDim()))->SetVerticalAlignment(VAlign_Center);
	BalanceText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColText(), false, true);
	BalanceText->SetJustification(ETextJustify::Right);
	UHorizontalBoxSlot* BalS = PayRow->AddChildToHorizontalBox(BalanceText);
	BalS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BalS->SetVerticalAlignment(VAlign_Center);
	Outer->AddChildToVerticalBox(PayRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Categorie-tabs (gevuld in FillBody).
	TabRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Outer->AddChildToVerticalBox(TabRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Artikellijst.
	UBorder* ListBg = WidgetTree->ConstructWidget<UBorder>();
	ListBg->SetBrush(WeedUI::Rounded(WeedUI::ColWell(), 10.f));
	ListBg->SetPadding(FMargin(8.f));
	ItemList = WidgetTree->ConstructWidget<UScrollBox>();
	ListBg->SetContent(ItemList);
	Outer->AddChildToVerticalBox(ListBg)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Mand-balk: totaal + Checkout + leegmaken.
	UHorizontalBox* CartBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	CartText = WeedUI::Text(WidgetTree, TEXT("Cart empty"), 14, WeedUI::ColText(), false, true);
	UHorizontalBoxSlot* CTS = CartBar->AddChildToHorizontalBox(CartText);
	CTS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CTS->SetVerticalAlignment(VAlign_Center);
	CartBar->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Empty"), WeedUI::ColWarn(), 12,
		[this]() { Cart.Empty(); bConfirmPending = false; LastSig.Reset(); }))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	CheckoutBtn = StoreBtn(WidgetTree, TEXT("Checkout"), WeedUI::ColGood(), 14,
		[this]()
		{
			if (!PhoneComp.IsValid() || Cart.Num() == 0) { return; }
			if (!bConfirmPending) { bConfirmPending = true; LastSig.Reset(); return; } // 1e klik: vraag bevestiging
			TArray<FName> Ids; TArray<int32> Qtys;
			for (const TPair<FName, int32>& KV : Cart) { Ids.Add(KV.Key); Qtys.Add(KV.Value); }
			PhoneComp->StoreCheckout(Ids, Qtys);
			Cart.Empty(); bConfirmPending = false; LastSig.Reset();
		});
	CartBar->AddChildToHorizontalBox(CheckoutBtn);
	Outer->AddChildToVerticalBox(CartBar)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
}

void UStoreWidget::FillBody()
{
	if (!ItemList || !TabRow || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	AStoreCounter* Counter = Ph->GetStoreCounter();
	if (!Counter)
	{
		ItemList->ClearChildren(); TabRow->ClearChildren();
		StoreRowBoxes.Reset(); StoreRowSigs.Reset(); LastTabSig.Reset();
		return;
	}

	const EShopKind Kind = Counter->Kind;
	if (TitleText) { TitleText->SetText(FText::FromString(ShopTitleFor(Kind))); }

	const bool bBank = Ph->IsStorePayBank();
	if (PayCashBtn) { PayCashBtn->SetRenderOpacity(bBank ? 0.55f : 1.f); }
	if (PayBankBtn) { PayBankBtn->SetRenderOpacity(bBank ? 1.f : 0.55f); }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return; }
	// Co-op: catalogus-level-gate op de LOKALE speler (eigenaar van deze widget), niet op de host.
	const int32 PlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevelFor(GetOwningPlayerPawn()) : 999;

	TArray<int32> Cats; CatsForKind(Kind, Cats);
	if (ActiveCat < 0 || !Cats.Contains(ActiveCat)) { ActiveCat = Cats.Num() > 0 ? Cats[0] : 0; }

	// Tabs: ALLEEN herbouwen als de tab-set of de actieve tab wijzigt (niet bij elke cart +/- ).
	const FString TabSig = FString::Printf(TEXT("K%d|A%d|N%d"), (int32)Kind, ActiveCat, Cats.Num());
	if (TabSig != LastTabSig)
	{
		LastTabSig = TabSig;
		TabRow->ClearChildren();
		for (int32 Cat : Cats)
		{
			const bool bActive = (Cat == ActiveCat);
			const FLinearColor Col = bActive ? WeedUI::ColAccent() : WeedUI::ColInner();
			const int32 C = Cat;
			TabRow->AddChildToHorizontalBox(StoreBtn(WidgetTree, StoreCatName(Cat), Col, 12,
				[this, C]() { ActiveCat = C; LastSig.Reset(); }))->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
		}
	}

	// Artikelen van de actieve categorie -> eerst de zichtbare rij-data verzamelen.
	const bool bSeedsCat = UStoreComponent::IsSeedCategory(ActiveCat);
	const TArray<FName> Ids = Store->GetSupplierCategory(ActiveCat);
	struct FRowData { FName Id; FString NameLbl; FString Desc; int32 Price; int32 ReqLvl; bool bLocked; int32 InCart; };
	TArray<FRowData> Rows;
	for (const FName& Id : Ids)
	{
		FText Name; int32 Price = 0; int32 Pack = 1;
		bool bOk = bSeedsCat ? Store->GetSeedDisplay(Id, Name, Price) : Store->GetSupplyDisplay(Id, Name, Price, Pack);
		if (!bOk || Price <= 0) { continue; }
		const int32 ReqLvl = Store->RequiredLevelFor(Id);
		const bool bLocked = (ReqLvl > PlayerLvl);
		FString Desc = Store->GetCatalogDesc(Id).ToString();
		// Seeds: toon de echte teelt-stats (THC, opbrengst/plant, groeitijd) i.p.v. alleen tekst.
		if (bSeedsCat)
		{
			float Thc = 0.f, Yield = 0.f, GrowMin = 0.f;
			if (Store->GetStrainStats(Id, Thc, Yield, GrowMin))
			{
				Desc = FString::Printf(TEXT("THC ~%.0f%%   -   ~%.0fg yield   -   grow ~%.0f min"), Thc, Yield, GrowMin);
			}
		}
		const FString NameLbl = (bSeedsCat || Pack <= 1) ? Name.ToString() : FString::Printf(TEXT("%s  (x%d)"), *Name.ToString(), Pack);
		Rows.Add({ Id, NameLbl, Desc, Price, ReqLvl, bLocked, Cart.Contains(Id) ? Cart[Id] : 0 });
	}

	// Rij-pool op maat brengen (persistent) -> geen ClearChildren, dus geen flash/scroll-sprong.
	while (StoreRowBoxes.Num() < Rows.Num())
	{
		UBorder* RowBg = WidgetTree->ConstructWidget<UBorder>();
		RowBg->SetPadding(FMargin(9.f, 6.f, 9.f, 6.f));
		ItemList->AddChild(RowBg);
		if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(RowBg->Slot)) { Sl->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f)); } // rij-spacing
		StoreRowBoxes.Add(RowBg); StoreRowSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}
	while (StoreRowBoxes.Num() > Rows.Num())
	{
		const int32 Last = StoreRowBoxes.Num() - 1;
		if (StoreRowBoxes[Last]) { StoreRowBoxes[Last]->RemoveFromParent(); }
		StoreRowBoxes.RemoveAt(Last); StoreRowSigs.RemoveAt(Last);
	}

	// Per-rij diff: alleen een rij die ECHT wijzigde (bv. de aangeklikte cart +/- ) krijgt nieuwe inhoud.
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FRowData& R = Rows[i];
		const FString Sig = FString::Printf(TEXT("%s|%d|%d|%d|%s"), *R.Id.ToString(), R.Price, R.bLocked ? 1 : 0, R.InCart, *R.Desc);
		if (!StoreRowSigs.IsValidIndex(i) || !StoreRowBoxes.IsValidIndex(i) || !StoreRowBoxes[i]) { continue; }
		if (Sig == StoreRowSigs[i]) { continue; }
		StoreRowSigs[i] = Sig;

		UBorder* RowBg = StoreRowBoxes[i];
		RowBg->SetBrush(WeedUI::Rounded(R.bLocked ? WeedUI::ColSlotEmpty(0.9f) : WeedUI::ColSlot(0.95f), 7.f));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		RowBg->SetContent(Row);

		// Links: naam + beschrijving.
		UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
		Info->AddChildToVerticalBox(WeedUI::Text(WidgetTree, R.NameLbl, 13, R.bLocked ? WeedUI::ColTextDim() : WeedUI::ColText(), false, true));
		if (!R.Desc.IsEmpty()) { Info->AddChildToVerticalBox(WeedUI::Text(WidgetTree, R.Desc, 10, WeedUI::ColTextDim())); }
		UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
		IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);

		// Prijs.
		Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %d"), (int32)(WeedRoundEuros((int64)R.Price) / 100)), 13, WeedUI::ColText(), false, true))
			->SetVerticalAlignment(VAlign_Center);

		if (R.bLocked)
		{
			Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("  Lvl %d"), R.ReqLvl), 12, WeedUI::ColWarn(), false, true))
				->SetVerticalAlignment(VAlign_Center);
		}
		else
		{
			const FName AddId = R.Id;
			if (R.InCart > 0)
			{
				Row->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("-"), WeedUI::ColWarn(), 12, [this, AddId]() { CartAdd(AddId, -1); }))
					->SetPadding(FMargin(10.f, 0.f, 4.f, 0.f));
				Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), R.InCart), 13, WeedUI::ColText(), true, true))->SetVerticalAlignment(VAlign_Center);
			}
			Row->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("+"), WeedUI::ColAccent(), 12, [this, AddId]() { CartAdd(AddId, 1); }))
				->SetPadding(FMargin(R.InCart > 0 ? 4.f : 10.f, 0.f, 0.f, 0.f));
		}
	}

	// Saldo bovenin (cash + bank).
	if (BalanceText)
	{
		APawn* Pw = GetOwningPlayerPawn();
		const UEconomyComponent* Econ = Pw ? Pw->FindComponentByClass<UEconomyComponent>() : nullptr;
		BalanceText->SetText(Econ
			? FText::FromString(FString::Printf(TEXT("Cash EUR %lld     Bank EUR %lld"), (long long)(WeedRoundEuros(Econ->GetCashCents()) / 100), (long long)(WeedRoundEuros(Econ->GetBankCents()) / 100)))
			: FText::GetEmpty());
	}

	// Mand-balk + checkout-label (2e klik = bevestigen).
	int32 Lines = 0; for (const TPair<FName, int32>& KV : Cart) { Lines += KV.Value; }
	if (CartText)
	{
		CartText->SetText(FText::FromString(Lines == 0
			? FString(TEXT("Cart empty"))
			: FString::Printf(TEXT("Cart: %d item(s)   EUR %d"), Lines, (int32)(WeedRoundEuros((int64)CartTotalCents()) / 100))));
	}
	if (CheckoutBtn)
	{
		CheckoutBtn->SetContent(WeedUI::Text(WidgetTree,
			bConfirmPending ? FString::Printf(TEXT("Confirm purchase - EUR %d"), (int32)(WeedRoundEuros((int64)CartTotalCents()) / 100)) : FString(TEXT("Checkout")),
			14, FLinearColor::White, true, true));
	}
}

void UStoreWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsStoreOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); if (Cart.Num() > 0) { Cart.Empty(); } return; }

	FString Sig;
	if (AStoreCounter* C = PhoneComp->GetStoreCounter()) { Sig += FString::Printf(TEXT("K%d|"), (int32)C->Kind); }
	Sig += FString::Printf(TEXT("T%d|%s|C%d|"), ActiveCat, PhoneComp->IsStorePayBank() ? TEXT("bank") : TEXT("cash"), bConfirmPending ? 1 : 0);
	for (const TPair<FName, int32>& KV : Cart) { Sig += FString::Printf(TEXT("%s%d,"), *KV.Key.ToString(), KV.Value); }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

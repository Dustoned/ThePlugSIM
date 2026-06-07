#include "UI/StoreWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "World/StoreCounter.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
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
		case EShopKind::Supplies:   Out = { 4, 3, 2 }; break;
		case EShopKind::Furniture:  Out = { 7 }; break;
		case EShopKind::GasStation: Out = { 4 }; break;
		default:                    Out = { 4 }; break;
		}
	}

	FString CatName(int32 Cat)
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
		case 8: return TEXT("Gear");
		case 9: return TEXT("Care");
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
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.06f, 0.08f, 0.99f), 18.f));
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
	TitleText = WeedUI::Text(WidgetTree, TEXT("SHOP"), 20, FLinearColor(0.7f, 0.95f, 0.7f), false, true);
	UHorizontalBoxSlot* TS = Head->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	Head->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Exit"), FLinearColor(0.4f, 0.2f, 0.2f), 12,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseStore(); } }));
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Betaal-toggle + instant-melding.
	UHorizontalBox* PayRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PayRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("Betaal met:"), 13, FLinearColor(0.8f, 0.85f, 0.95f), false))->SetVerticalAlignment(VAlign_Center);
	PayCashBtn = StoreBtn(WidgetTree, TEXT("Cash"), FLinearColor(0.2f, 0.45f, 0.28f), 12, [this]() { if (PhoneComp.IsValid()) { PhoneComp->SetStorePayBank(false); LastSig.Reset(); } });
	PayBankBtn = StoreBtn(WidgetTree, TEXT("Bank"), FLinearColor(0.2f, 0.32f, 0.5f), 12, [this]() { if (PhoneComp.IsValid()) { PhoneComp->SetStorePayBank(true); LastSig.Reset(); } });
	PayRow->AddChildToHorizontalBox(PayCashBtn)->SetPadding(FMargin(10.f, 0.f, 6.f, 0.f));
	PayRow->AddChildToHorizontalBox(PayBankBtn);
	PayRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("   instant, geen bezorgkosten"), 11, FLinearColor(0.6f, 0.65f, 0.78f)))->SetVerticalAlignment(VAlign_Center);
	Outer->AddChildToVerticalBox(PayRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Categorie-tabs (gevuld in FillBody).
	TabRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Outer->AddChildToVerticalBox(TabRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Artikellijst.
	UBorder* ListBg = WidgetTree->ConstructWidget<UBorder>();
	ListBg->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 1.f), 10.f));
	ListBg->SetPadding(FMargin(8.f));
	ItemList = WidgetTree->ConstructWidget<UScrollBox>();
	ListBg->SetContent(ItemList);
	Outer->AddChildToVerticalBox(ListBg)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Mand-balk: totaal + Checkout + leegmaken.
	UHorizontalBox* CartBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	CartText = WeedUI::Text(WidgetTree, TEXT("Mand leeg"), 14, FLinearColor(1.f, 0.95f, 0.6f), false, true);
	UHorizontalBoxSlot* CTS = CartBar->AddChildToHorizontalBox(CartText);
	CTS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CTS->SetVerticalAlignment(VAlign_Center);
	CartBar->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Leeg"), FLinearColor(0.35f, 0.2f, 0.2f), 12,
		[this]() { Cart.Empty(); LastSig.Reset(); }))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	CartBar->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Checkout"), FLinearColor(0.2f, 0.55f, 0.3f), 14,
		[this]()
		{
			if (!PhoneComp.IsValid() || Cart.Num() == 0) { return; }
			TArray<FName> Ids; TArray<int32> Qtys;
			for (const TPair<FName, int32>& KV : Cart) { Ids.Add(KV.Key); Qtys.Add(KV.Value); }
			PhoneComp->StoreCheckout(Ids, Qtys);
			Cart.Empty(); LastSig.Reset();
		}));
	Outer->AddChildToVerticalBox(CartBar)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
}

void UStoreWidget::FillBody()
{
	if (!ItemList || !TabRow || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	AStoreCounter* Counter = Ph->GetStoreCounter();
	ItemList->ClearChildren();
	TabRow->ClearChildren();
	if (!Counter) { return; }

	const EShopKind Kind = Counter->Kind;
	if (TitleText) { TitleText->SetText(FText::FromString(ShopTitleFor(Kind))); }

	const bool bBank = Ph->IsStorePayBank();
	if (PayCashBtn) { PayCashBtn->SetRenderOpacity(bBank ? 0.55f : 1.f); }
	if (PayBankBtn) { PayBankBtn->SetRenderOpacity(bBank ? 1.f : 0.55f); }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return; }
	const int32 PlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevel() : 999;

	TArray<int32> Cats; CatsForKind(Kind, Cats);
	if (ActiveCat < 0 || !Cats.Contains(ActiveCat)) { ActiveCat = Cats.Num() > 0 ? Cats[0] : 0; }

	// Tabs.
	for (int32 Cat : Cats)
	{
		const bool bActive = (Cat == ActiveCat);
		const FLinearColor Col = bActive ? FLinearColor(0.22f, 0.5f, 0.3f) : FLinearColor(0.14f, 0.16f, 0.22f);
		const int32 C = Cat;
		TabRow->AddChildToHorizontalBox(StoreBtn(WidgetTree, CatName(Cat), Col, 12,
			[this, C]() { ActiveCat = C; LastSig.Reset(); }))->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
	}

	// Artikelen van de actieve categorie.
	const bool bSeedsCat = UStoreComponent::IsSeedCategory(ActiveCat);
	const TArray<FName> Ids = Store->GetSupplierCategory(ActiveCat);
	for (const FName& Id : Ids)
	{
		FText Name; int32 Price = 0; int32 Pack = 1;
		bool bOk = bSeedsCat ? Store->GetSeedDisplay(Id, Name, Price) : Store->GetSupplyDisplay(Id, Name, Price, Pack);
		if (!bOk || Price <= 0) { continue; }
		const int32 ReqLvl = Store->RequiredLevelFor(Id);
		const bool bLocked = (ReqLvl > PlayerLvl);
		const FString Desc = Store->GetCatalogDesc(Id).ToString();
		const int32 InCart = Cart.Contains(Id) ? Cart[Id] : 0;

		UBorder* RowBg = WidgetTree->ConstructWidget<UBorder>();
		RowBg->SetBrush(WeedUI::Rounded(bLocked ? FLinearColor(0.10f, 0.10f, 0.12f, 0.9f) : FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 7.f));
		RowBg->SetPadding(FMargin(9.f, 6.f, 9.f, 6.f));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		RowBg->SetContent(Row);

		// Links: naam + beschrijving.
		UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
		const FString NameLbl = (bSeedsCat || Pack <= 1) ? Name.ToString() : FString::Printf(TEXT("%s  (x%d)"), *Name.ToString(), Pack);
		Info->AddChildToVerticalBox(WeedUI::Text(WidgetTree, NameLbl, 13, bLocked ? FLinearColor(0.55f, 0.55f, 0.6f) : FLinearColor(0.95f, 0.97f, 1.f), false, true));
		if (!Desc.IsEmpty()) { Info->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Desc, 10, FLinearColor(0.6f, 0.64f, 0.74f))); }
		UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
		IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);

		// Prijs.
		Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %.2f"), Price / 100.f), 13, FLinearColor(0.95f, 0.9f, 0.6f), false, true))
			->SetVerticalAlignment(VAlign_Center);

		if (bLocked)
		{
			Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("  Lvl %d"), ReqLvl), 12, FLinearColor(1.f, 0.5f, 0.4f), false, true))
				->SetVerticalAlignment(VAlign_Center);
		}
		else
		{
			const FName AddId = Id;
			if (InCart > 0)
			{
				Row->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("-"), FLinearColor(0.35f, 0.2f, 0.2f), 12, [this, AddId]() { CartAdd(AddId, -1); }))
					->SetPadding(FMargin(10.f, 0.f, 4.f, 0.f));
				Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), InCart), 13, FLinearColor::White, true, true))->SetVerticalAlignment(VAlign_Center);
			}
			Row->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("+"), FLinearColor(0.2f, 0.5f, 0.3f), 12, [this, AddId]() { CartAdd(AddId, 1); }))
				->SetPadding(FMargin(InCart > 0 ? 4.f : 10.f, 0.f, 0.f, 0.f));
		}

		ItemList->AddChild(RowBg);
		ItemList->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
	}

	// Mand-balk.
	if (CartText)
	{
		int32 Lines = 0; for (const TPair<FName, int32>& KV : Cart) { Lines += KV.Value; }
		CartText->SetText(FText::FromString(Lines == 0
			? FString(TEXT("Mand leeg"))
			: FString::Printf(TEXT("Mand: %d artikel(en)   EUR %.2f"), Lines, CartTotalCents() / 100.f)));
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
	Sig += FString::Printf(TEXT("T%d|%s|"), ActiveCat, PhoneComp->IsStorePayBank() ? TEXT("bank") : TEXT("cash"));
	for (const TPair<FName, int32>& KV : Cart) { Sig += FString::Printf(TEXT("%s%d,"), *KV.Key.ToString(), KV.Value); }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

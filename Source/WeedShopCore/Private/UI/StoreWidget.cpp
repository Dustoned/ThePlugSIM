#include "UI/StoreWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "World/StoreCounter.h"
#include "Progression/StoreComponent.h"
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
		S.NormalPadding = FMargin(10.f, 5.f); S.PressedPadding = FMargin(10.f, 5.f);
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

	// Categorie-id's (SupplierCat) per winkel-type: 0=Seeds,1=Pots,2=Drying,3=Packing,4=Papers,
	// 5=Soil,6=Water,7=Furniture,8=Pot-gear,9=Plant-care.
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
	CS->SetSize(FVector2D(740.f, 580.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop-balk: titel + Exit.
	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("SHOP"), 20, FLinearColor(0.7f, 0.95f, 0.7f), false, true);
	UHorizontalBoxSlot* TS = Head->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	Head->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Exit"), FLinearColor(0.4f, 0.2f, 0.2f), 12,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseStore(); } }));
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Betaal-toggle: Cash / Bank.
	UHorizontalBox* PayRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PayRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("Betaal met:"), 13, FLinearColor(0.8f, 0.85f, 0.95f), false))
		->SetVerticalAlignment(VAlign_Center);
	PayCashBtn = StoreBtn(WidgetTree, TEXT("Cash"), FLinearColor(0.2f, 0.45f, 0.28f), 12, [this]() { if (PhoneComp.IsValid()) { PhoneComp->SetStorePayBank(false); LastSig.Reset(); } });
	PayBankBtn = StoreBtn(WidgetTree, TEXT("Bank"), FLinearColor(0.2f, 0.32f, 0.5f), 12, [this]() { if (PhoneComp.IsValid()) { PhoneComp->SetStorePayBank(true); LastSig.Reset(); } });
	PayRow->AddChildToHorizontalBox(PayCashBtn)->SetPadding(FMargin(10.f, 0.f, 6.f, 0.f));
	PayRow->AddChildToHorizontalBox(PayBankBtn);
	Outer->AddChildToVerticalBox(PayRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	Outer->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Geen bezorgkosten - je krijgt het direct."), 11, FLinearColor(0.6f, 0.65f, 0.78f)))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UBorder* ListBg = WidgetTree->ConstructWidget<UBorder>();
	ListBg->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 1.f), 10.f));
	ListBg->SetPadding(FMargin(8.f));
	ItemList = WidgetTree->ConstructWidget<UScrollBox>();
	ListBg->SetContent(ItemList);
	Outer->AddChildToVerticalBox(ListBg)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UStoreWidget::FillBody()
{
	if (!ItemList || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	AStoreCounter* Counter = Ph->GetStoreCounter();
	ItemList->ClearChildren();
	if (!Counter) { return; }

	const EShopKind Kind = Counter->Kind;
	if (TitleText) { TitleText->SetText(FText::FromString(ShopTitleFor(Kind))); }

	// Betaal-knoppen markeren welke actief is.
	const bool bBank = Ph->IsStorePayBank();
	if (PayCashBtn) { PayCashBtn->SetRenderOpacity(bBank ? 0.55f : 1.f); }
	if (PayBankBtn) { PayBankBtn->SetRenderOpacity(bBank ? 1.f : 0.55f); }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return; }

	TArray<int32> Cats; CatsForKind(Kind, Cats);
	for (int32 Cat : Cats)
	{
		const TArray<FName> Ids = Store->GetSupplierCategory(Cat);
		const bool bSeeds = UStoreComponent::IsSeedCategory(Cat);
		for (const FName& Id : Ids)
		{
			FText Name; int32 Price = 0; int32 Pack = 1;
			bool bOk = false;
			if (bSeeds) { bOk = Store->GetSeedDisplay(Id, Name, Price); }
			else        { bOk = Store->GetSupplyDisplay(Id, Name, Price, Pack); }
			if (!bOk || Price <= 0) { continue; }

			UBorder* RowBg = WidgetTree->ConstructWidget<UBorder>();
			RowBg->SetBrush(WeedUI::Rounded(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 7.f));
			RowBg->SetPadding(FMargin(9.f, 6.f, 9.f, 6.f));
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			RowBg->SetContent(Row);

			const FString Label = (bSeeds || Pack <= 1)
				? Name.ToString()
				: FString::Printf(TEXT("%s  (x%d)"), *Name.ToString(), Pack);
			UHorizontalBoxSlot* NS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 13, FLinearColor(0.95f, 0.97f, 1.f)));
			NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);

			Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %.2f"), Price / 100.f), 13, FLinearColor(0.95f, 0.9f, 0.6f), false, true))
				->SetVerticalAlignment(VAlign_Center);

			const FName BuyId = Id;
			Row->AddChildToHorizontalBox(StoreBtn(WidgetTree, TEXT("Koop"), FLinearColor(0.2f, 0.5f, 0.3f), 12,
				[this, BuyId]() { if (PhoneComp.IsValid()) { PhoneComp->StoreBuy(BuyId); } }))
				->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));

			ItemList->AddChild(RowBg);
			ItemList->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
		}
	}
}

void UStoreWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsStoreOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Herbouw alleen bij wijziging (winkel, betaalmodus).
	FString Sig;
	if (AStoreCounter* C = PhoneComp->GetStoreCounter()) { Sig += FString::Printf(TEXT("K%d|"), (int32)C->Kind); }
	Sig += PhoneComp->IsStorePayBank() ? TEXT("bank") : TEXT("cash");
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

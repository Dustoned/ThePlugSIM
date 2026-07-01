#include "UI/AtmWidget.h"
#include "WeedShopCore.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

void UAtmWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* AtmBtn(UWidgetTree* Tree, const FLinearColor& Col, float Radius, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, Radius);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, Radius);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, Radius);
		S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(S);
		return B;
	}

	UEconomyComponent* GetEcon(UWorld* W)
	{
		AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
		return GS ? GS->GetEconomy() : nullptr;
	}
}

TSharedRef<SWidget> UAtmWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UAtmWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Donkere ATM-kast.
	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AtmCard"));
	{ FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 20.f); Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f)); CardB->SetBrush(Br); }
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(540.f, 460.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop-balk: "ATM".
	UBorder* Head = WidgetTree->ConstructWidget<UBorder>();
	Head->SetBrush(WeedUI::Rounded(WeedUI::ColInner(1.f), 10.f));
	Head->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Head->SetContent(HeadRow);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("ATM  -  CITY BANK"), 18, WeedUI::ColAccent(), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* CloseB = AtmBtn(WidgetTree, WeedUI::ColWarn(), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseAtm(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Exit"), 12, FLinearColor::White, true));
	HeadRow->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	{ USizeBox* DivSz = WidgetTree->ConstructWidget<USizeBox>(); DivSz->SetHeightOverride(2.f); UBorder* Div = WidgetTree->ConstructWidget<UBorder>(); Div->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(0.75f), 1.f)); DivSz->SetContent(Div); Outer->AddChildToVerticalBox(DivSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f)); }

	// "Scherm" met de inhoud (donkere paars-getinte terminal-look).
	UBorder* ScreenB = WidgetTree->ConstructWidget<UBorder>();
	ScreenB->SetBrush(WeedUI::Rounded(WeedUI::ColWell(1.f), 12.f));
	ScreenB->SetPadding(FMargin(14.f));
	UVerticalBoxSlot* ScS = Outer->AddChildToVerticalBox(ScreenB);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	ScreenB->SetContent(Body);

	// ---- Persistente inhoud: één keer gebouwd, daarna alleen in-place bijgewerkt (geen teardown-on-click). ----
	UEconomyComponent* Econ = GetEcon(GetWorld());
	UPhoneClientComponent* Ph = PhoneComp.Get();

	auto Row = [this](UWidget* W, const FMargin& Pad) { Body->AddChildToVerticalBox(W)->SetPadding(Pad); };

	// "Out of service"-tekst (getoond als Economy ontbreekt) — persistent, zichtbaarheid via RefreshValues.
	OutOfServiceText = WeedUI::Text(WidgetTree, TEXT("Out of service."), 14, WeedUI::ColTextDim());
	Row(OutOfServiceText, FMargin(0, 0, 0, 0));
	OutOfServiceText->SetVisibility(ESlateVisibility::Collapsed);

	// Saldo-regels (altijd zichtbaar) — waardes worden in RefreshValues met SetText bijgewerkt.
	CashText = WeedUI::Text(WidgetTree, TEXT("Cash (black):  EUR 0"), 15, FLinearColor(0.95f, 0.9f, 0.5f));
	Row(CashText, FMargin(0, 0, 0, 2));
	BankText = WeedUI::Text(WidgetTree, TEXT("Bank (white):  EUR 0"), 15, FLinearColor(0.55f, 0.95f, 1.f));
	Row(BankText, FMargin(0, 0, 0, 2));

	// Tabs — één keer gebouwd; klik recolourt alleen de knoppen + wisselt de zichtbare pane (geen ClearChildren).
	static const TCHAR* TabNames[2] = { TEXT("Deposit"), TEXT("Send to friend") };
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	TabButtons.Reset();
	for (int32 i = 0; i < 2; ++i)
	{
		const FLinearColor Col = (i == AtmTab) ? WeedUI::ColAccentDim() : WeedUI::ColInner();
		UWeedActionButton* B = AtmBtn(WidgetTree, Col, 8.f, [this, i]() { if (AtmTab != i) { AtmTab = i; ApplyTab(); } });
		B->SetContent(WeedUI::Text(WidgetTree, TabNames[i], 12, FLinearColor::White, true));
		UHorizontalBoxSlot* BS = Tabs->AddChildToHorizontalBox(B);
		BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
		TabButtons.Add(B);
	}
	Row(Tabs, FMargin(0, 0, 0, 10));

	// ---- Deposit-pane (tab 0) ----
	DepositPane = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		auto DRow = [this](UWidget* W, const FMargin& Pad) { DepositPane->AddChildToVerticalBox(W)->SetPadding(Pad); };
		DRow(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Deposit cash -> bank.  Tax %.0f%% on entry.  Big deposits raise heat."),
			(Econ ? Econ->DepositTaxPct : 0.f) * 100.f), 11, WeedUI::ColTextDim()), FMargin(0, 0, 0, 2));
		DailyRoomText = WeedUI::Text(WidgetTree, TEXT("Daily room left: EUR 0"), 12, WeedUI::ColTextDim());
		DRow(DailyRoomText, FMargin(0, 0, 0, 8));

		const int64 Amts[3] = { 100000, 500000, 2000000 };
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 3; ++i)
		{
			const int64 A = Amts[i];
			UWeedActionButton* B = AtmBtn(WidgetTree, WeedUI::ColAccentDim(), 8.f, [Ph, A]() { if (Ph) { Ph->RequestDeposit(A); } });
			B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %lld"), (long long)(A / 100)), 13, FLinearColor::White, true));
			UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		DRow(Btns, FMargin(0, 0, 0, 6));
		UWeedActionButton* MaxB = AtmBtn(WidgetTree, WeedUI::ColAccent(), 8.f, [Ph]() { if (Ph) { Ph->RequestDeposit(-1); } });
		MaxB->SetContent(WeedUI::Text(WidgetTree, TEXT("Deposit max (up to daily limit)"), 13, FLinearColor::White, true));
		DRow(MaxB, FMargin(0, 0, 0, 0));
	}
	Row(DepositPane, FMargin(0, 0, 0, 0));

	// ---- Transfer-pane (tab 1) ----
	TransferPane = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		auto TRow = [this](UWidget* W, const FMargin& Pad) { TransferPane->AddChildToVerticalBox(W)->SetPadding(Pad); };
		TRow(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Send bank money to a co-op friend.  Fee %.0f%%."), (Econ ? Econ->TransferFeePct : 0.f) * 100.f),
			11, WeedUI::ColTextDim()), FMargin(0, 0, 0, 2));
		TransfersLeftText = WeedUI::Text(WidgetTree, TEXT("Transfers left today: 0 / 0"), 12, WeedUI::ColTextDim());
		TRow(TransfersLeftText, FMargin(0, 0, 0, 8));

		const int64 Amts[3] = { 50000, 100000, 500000 };
		const float FeePct = Econ ? Econ->TransferFeePct : 0.f;
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 3; ++i)
		{
			const int64 A = Amts[i];
			const int64 Fee = WeedRoundEuros((int64)(A * FeePct));
			UWeedActionButton* B = AtmBtn(WidgetTree, WeedUI::ColAccentDim(), 8.f, [Ph, A]() { if (Ph) { Ph->RequestTransfer(A); } });
			B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %lld\n(fee %lld)"), (long long)(A / 100), (long long)(Fee / 100)), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		TRow(Btns, FMargin(0, 0, 0, 6));
		TRow(WeedUI::Text(WidgetTree, TEXT("(Full amount to their bank; fee is on you.)"),
			10, WeedUI::ColTextDim()), FMargin(0, 4, 0, 0));
	}
	Row(TransferPane, FMargin(0, 0, 0, 0));

	ApplyTab();       // zet begin-tab-highlight + welke pane zichtbaar is
	RefreshValues();  // vul de begin-waardes
}

// Recolourt de 2 tab-knoppen in-place en toont alleen de actieve pane (geen teardown/rebuild).
void UAtmWidget::ApplyTab()
{
	for (int32 i = 0; i < TabButtons.Num(); ++i)
	{
		if (UWeedActionButton* B = TabButtons[i])
		{
			const FLinearColor Col = (i == AtmTab) ? WeedUI::ColAccentDim() : WeedUI::ColInner();
			FButtonStyle S = B->GetStyle();
			S.Normal = WeedUI::Rounded(Col, 8.f);
			S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
			S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
			B->SetStyle(S);
		}
	}
	if (DepositPane) { DepositPane->SetVisibility(AtmTab == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (TransferPane) { TransferPane->SetVisibility(AtmTab == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
}

// Werkt alleen de tekst-waardes bij op de persistente TextBlocks — geen ClearChildren, geen rebuild.
void UAtmWidget::RefreshValues()
{
	UEconomyComponent* Econ = GetEcon(GetWorld());

	// "Out of service" wanneer er geen Economy is: verberg de rest.
	const bool bLive = (Econ != nullptr);
	if (OutOfServiceText) { OutOfServiceText->SetVisibility(bLive ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }
	if (CashText) { CashText->SetVisibility(bLive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (BankText) { BankText->SetVisibility(bLive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bLive)
	{
		if (DepositPane) { DepositPane->SetVisibility(ESlateVisibility::Collapsed); }
		if (TransferPane) { TransferPane->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	// Panes weer aan volgens de actieve tab (voor het geval Economy net terug is).
	if (DepositPane) { DepositPane->SetVisibility(AtmTab == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (TransferPane) { TransferPane->SetVisibility(AtmTab == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	if (CashText) { CashText->SetText(FText::FromString(FString::Printf(TEXT("Cash (black):  EUR %lld"), (long long)(WeedRoundEuros(Econ->GetCashCents()) / 100)))); }
	if (BankText) { BankText->SetText(FText::FromString(FString::Printf(TEXT("Bank (white):  EUR %lld"), (long long)(WeedRoundEuros(Econ->GetBankCents()) / 100)))); }
	if (DailyRoomText) { DailyRoomText->SetText(FText::FromString(FString::Printf(TEXT("Daily room left: EUR %lld"), (long long)(WeedRoundEuros(Econ->GetDailyDepositRemainingCents()) / 100)))); }
	if (TransfersLeftText) { TransfersLeftText->SetText(FText::FromString(FString::Printf(TEXT("Transfers left today: %d / %d"), Econ->GetTransfersRemainingToday(), Econ->MaxTransfersPerDay))); }
}

void UAtmWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsAtmOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Alleen de tekst-waardes verversen als er iets wijzigt (saldo's) -> geen flicker, geen SetText-spam.
	// Tab-wissel gaat via de knop-callback (ApplyTab), niet via deze sig.
	UEconomyComponent* Econ = GetEcon(GetWorld());
	const FString Sig = Econ
		? FString::Printf(TEXT("%lld|%lld|%lld|%lld|%d"), (long long)Econ->GetCashCents(), (long long)Econ->GetBankCents(),
			(long long)Econ->GetSafeCents(), (long long)Econ->GetDepositedTodayCents(), Econ->GetTransfersToday())
		: FString(TEXT("na"));
	if (Sig != LastSig) { LastSig = Sig; RefreshValues(); }
}

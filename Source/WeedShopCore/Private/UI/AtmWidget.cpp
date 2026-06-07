#include "UI/AtmWidget.h"

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
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.03f, 0.05f, 0.06f, 0.99f), 20.f));
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
	Head->SetBrush(WeedUI::Rounded(FLinearColor(0.10f, 0.13f, 0.16f, 1.f), 10.f));
	Head->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Head->SetContent(HeadRow);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("ATM  -  CITY BANK"), 18, FLinearColor(0.4f, 1.f, 0.6f), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* CloseB = AtmBtn(WidgetTree, FLinearColor(0.4f, 0.2f, 0.2f), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseAtm(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Exit"), 12, FLinearColor::White, true));
	HeadRow->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	// "Scherm" met de inhoud (groenige rand voor de terminal-look).
	UBorder* ScreenB = WidgetTree->ConstructWidget<UBorder>();
	ScreenB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.10f, 0.08f, 1.f), 12.f));
	ScreenB->SetPadding(FMargin(14.f));
	UVerticalBoxSlot* ScS = Outer->AddChildToVerticalBox(ScreenB);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	ScreenB->SetContent(Body);
}

void UAtmWidget::FillBody()
{
	if (!Body) { return; }
	Body->ClearChildren();
	UEconomyComponent* Econ = GetEcon(GetWorld());
	if (!Econ) { Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Out of service."), 14, FLinearColor::Gray)); return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();

	auto Row = [this](UWidget* W, const FMargin& Pad) { Body->AddChildToVerticalBox(W)->SetPadding(Pad); };

	// Saldo-regels (altijd zichtbaar).
	Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Cash (black):  EUR %.2f"), Econ->GetBalanceEuros()), 15, FLinearColor(0.95f, 0.9f, 0.5f)), FMargin(0, 0, 0, 2));
	Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Bank (white):  EUR %.2f"), Econ->GetBankEuros()), 15, FLinearColor(0.55f, 0.95f, 1.f)), FMargin(0, 0, 0, 8));

	// Tabs.
	static const TCHAR* TabNames[2] = { TEXT("Deposit"), TEXT("Send to friend") };
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < 2; ++i)
	{
		const FLinearColor Col = (i == AtmTab) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.14f, 0.18f, 0.20f);
		UWeedActionButton* B = AtmBtn(WidgetTree, Col, 8.f, [this, i]() { AtmTab = i; LastSig.Reset(); FillBody(); });
		B->SetContent(WeedUI::Text(WidgetTree, TabNames[i], 12, FLinearColor::White, true));
		UHorizontalBoxSlot* BS = Tabs->AddChildToHorizontalBox(B);
		BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
	}
	Row(Tabs, FMargin(0, 0, 0, 10));

	if (AtmTab == 0) // Deposit / launder
	{
		Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Deposit cash -> bank.  Tax %.0f%% on entry.  Big deposits raise heat."),
			Econ->DepositTaxPct * 100.f), 11, FLinearColor(0.7f, 0.78f, 0.74f)), FMargin(0, 0, 0, 2));
		Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Daily room left: EUR %.2f"), Econ->GetDailyDepositRemainingCents() / 100.f),
			12, FLinearColor(0.85f, 0.9f, 0.85f)), FMargin(0, 0, 0, 8));

		const int64 Amts[3] = { 100000, 500000, 2000000 };
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 3; ++i)
		{
			const int64 A = Amts[i];
			UWeedActionButton* B = AtmBtn(WidgetTree, FLinearColor(0.18f, 0.42f, 0.30f), 8.f, [Ph, A]() { if (Ph) { Ph->RequestDeposit(A); } });
			B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %lld"), (long long)(A / 100)), 13, FLinearColor::White, true));
			UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		Row(Btns, FMargin(0, 0, 0, 6));
		UWeedActionButton* MaxB = AtmBtn(WidgetTree, FLinearColor(0.2f, 0.5f, 0.34f), 8.f, [Ph]() { if (Ph) { Ph->RequestDeposit(-1); } });
		MaxB->SetContent(WeedUI::Text(WidgetTree, TEXT("Deposit max (up to daily limit)"), 13, FLinearColor::White, true));
		Row(MaxB, FMargin(0, 0, 0, 0));
	}
	else // Send to friend
	{
		Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Send bank money to a co-op friend.  Fee %.0f%%."), Econ->TransferFeePct * 100.f),
			11, FLinearColor(0.7f, 0.78f, 0.74f)), FMargin(0, 0, 0, 2));
		Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Transfers left today: %d / %d"), Econ->GetTransfersRemainingToday(), Econ->MaxTransfersPerDay),
			12, FLinearColor(0.85f, 0.9f, 0.85f)), FMargin(0, 0, 0, 8));

		const int64 Amts[3] = { 50000, 100000, 500000 };
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 3; ++i)
		{
			const int64 A = Amts[i];
			const int64 Fee = (int64)(A * Econ->TransferFeePct);
			UWeedActionButton* B = AtmBtn(WidgetTree, FLinearColor(0.2f, 0.34f, 0.5f), 8.f, [Ph, A]() { if (Ph) { Ph->RequestTransfer(A); } });
			B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("EUR %lld\n(fee %lld)"), (long long)(A / 100), (long long)(Fee / 100)), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		Row(Btns, FMargin(0, 0, 0, 6));
		Row(WeedUI::Text(WidgetTree, TEXT("(Full amount to their bank; fee is on you.)"),
			10, FLinearColor(0.55f, 0.6f, 0.62f)), FMargin(0, 4, 0, 0));
	}
}

void UAtmWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsAtmOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Alleen herbouwen als er iets wijzigt (tab of saldo's) -> geen flicker.
	UEconomyComponent* Econ = GetEcon(GetWorld());
	const FString Sig = Econ
		? FString::Printf(TEXT("%d|%lld|%lld|%lld|%d"), AtmTab, (long long)Econ->GetCashCents(), (long long)Econ->GetBankCents(),
			(long long)Econ->GetDepositedTodayCents(), Econ->GetTransfersToday())
		: FString::Printf(TEXT("%d|na"), AtmTab);
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

#include "UI/DryingRackWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Cultivation/DryingRack.h"
#include "Inventory/InventoryComponent.h"

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
#include "Components/ProgressBar.h"
#include "GameFramework/Pawn.h"

void UDryingRackWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* DryBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 7.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 7.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 7.f);
		S.NormalPadding = FMargin(7.f, 4.f); S.PressedPadding = FMargin(7.f, 4.f);
		B->SetStyle(S);
		B->SetContent(WeedUI::Text(Tree, Label, 11, FLinearColor::White, true));
		return B;
	}

	FString FmtClock(float Seconds)
	{
		const int32 T = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), T / 60, T % 60);
	}
}

TSharedRef<SWidget> UDryingRackWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UDryingRackWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DryCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.06f, 0.08f, 0.99f), 18.f));
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(680.f, 480.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop-balk.
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("DRYING RACK"), 18, FLinearColor(0.75f, 0.9f, 0.6f), false, true);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	HeadRow->AddChildToHorizontalBox(DryBtn(WidgetTree, TEXT("Exit"), FLinearColor(0.4f, 0.2f, 0.2f),
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseDryRack(); } }));
	Outer->AddChildToVerticalBox(HeadRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	// Twee kolommen.
	UHorizontalBox* Cols = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* ColsSlot = Outer->AddChildToVerticalBox(Cols);
	ColsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto MakeColumn = [this](const FString& Title, const FLinearColor& Col, TObjectPtr<UScrollBox>& OutScroll) -> UWidget*
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		B->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 1.f), 10.f));
		B->SetPadding(FMargin(8.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		B->SetContent(VB);
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Title, 13, Col, false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		OutScroll = WidgetTree->ConstructWidget<UScrollBox>();
		VB->AddChildToVerticalBox(OutScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return B;
	};

	UWidget* DryCol = MakeColumn(TEXT("Drying  /  ready"), FLinearColor(0.75f, 0.9f, 0.6f), DryList);
	UWidget* WetCol = MakeColumn(TEXT("Your wet weed  (hang)"), FLinearColor(0.6f, 0.85f, 1.f), WetList);
	UHorizontalBoxSlot* L = Cols->AddChildToHorizontalBox(DryCol); L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); L->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
	UHorizontalBoxSlot* R = Cols->AddChildToHorizontalBox(WetCol); R->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); R->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
}

void UDryingRackWidget::FillBody()
{
	if (!DryList || !WetList || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	ADryingRack* Rack = Ph->GetDryRack();
	DryList->ClearChildren();
	WetList->ClearChildren();
	RowBars.Reset(); RowStatus.Reset(); RowEntryIndex.Reset();
	if (!Rack) { return; }

	const int32 Used = Rack->GetEntries().Num();
	const int32 Cap = Rack->GetCapacityPublic();
	const int32 Ready = Rack->NumReady();
	if (TitleText)
	{
		TitleText->SetText(FText::FromString(FString::Printf(TEXT("DRYING RACK   (%d/%d)"), Used, Cap)));
	}

	// --- Linkerkolom: drogende + klare batches ---
	if (Used == 0)
	{
		DryList->AddChild(WeedUI::Text(WidgetTree, TEXT("Nothing drying. Hang wet weed on the right."), 12, FLinearColor::Gray));
	}
	else
	{
		if (Ready > 0)
		{
			DryList->AddChild(DryBtn(WidgetTree, FString::Printf(TEXT("Collect all ready (%d)"), Ready), FLinearColor(0.22f, 0.5f, 0.3f),
				[Ph]() { Ph->RequestDryCollectAll(); }));
			DryList->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 4, FLinearColor::Transparent));
		}

		const float Total = FMath::Max(1.f, Rack->GetDryTotalSeconds());
		for (int32 i = 0; i < Rack->GetEntries().Num(); ++i)
		{
			const FDryEntry& E = Rack->GetEntries()[i];
			const FString Name = WeedUI::PrettyItemName(E.DryItemId);
			const FString Sub = FString::Printf(TEXT("%dg   %.0f%% THC"), E.Quantity, E.Thc);

			UBorder* RowCard = WidgetTree->ConstructWidget<UBorder>();
			RowCard->SetBrush(WeedUI::Rounded(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 7.f));
			RowCard->SetPadding(FMargin(7.f, 5.f, 7.f, 6.f));
			UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
			RowCard->SetContent(VB);

			// Titelregel: naam links, gram/THC rechts.
			UHorizontalBox* TitleLine = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* NS = TitleLine->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Name, 12, FLinearColor(0.95f, 0.97f, 1.f)));
			NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);
			TitleLine->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Sub, 10, FLinearColor(0.62f, 0.66f, 0.76f)))->SetVerticalAlignment(VAlign_Center);
			VB->AddChildToVerticalBox(TitleLine);

			// Progress-bar.
			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
			Bar->SetPercent(E.bDone ? 1.f : FMath::Clamp(E.Elapsed / Total, 0.f, 1.f));
			Bar->SetFillColorAndOpacity(E.bDone ? FLinearColor(0.4f, 0.95f, 0.5f) : FLinearColor(0.85f, 0.7f, 0.25f));
			VB->AddChildToVerticalBox(Bar)->SetPadding(FMargin(0.f, 5.f, 0.f, 3.f));

			// Statusregel + (indien klaar) Collect-knop.
			UHorizontalBox* StatusLine = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* Status = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.7f, 0.8f, 0.7f));
			UHorizontalBoxSlot* StS = StatusLine->AddChildToHorizontalBox(Status);
			StS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); StS->SetVerticalAlignment(VAlign_Center);
			if (E.bDone)
			{
				const int32 Idx = i;
				StatusLine->AddChildToHorizontalBox(DryBtn(WidgetTree, TEXT("Collect"), FLinearColor(0.2f, 0.45f, 0.28f),
					[Ph, Idx]() { Ph->RequestDryCollect(Idx); }));
			}
			VB->AddChildToVerticalBox(StatusLine)->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));

			DryList->AddChild(RowCard);
			DryList->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));

			RowBars.Add(Bar);
			RowStatus.Add(Status);
			RowEntryIndex.Add(i);
		}
	}

	// --- Rechterkolom: natte wiet uit je inventory ---
	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	const bool bFull = (Used >= Cap);
	if (Inv)
	{
		TArray<FName> Order; TMap<FName, int32> Totals;
		for (const FInventoryStack& St : Inv->GetStacks())
		{
			if (!St.ItemId.ToString().StartsWith(TEXT("WetBud_")) || St.Quantity <= 0) { continue; }
			if (!Totals.Contains(St.ItemId)) { Order.Add(St.ItemId); }
			Totals.FindOrAdd(St.ItemId) += St.Quantity;
		}
		if (Order.Num() == 0)
		{
			WetList->AddChild(WeedUI::Text(WidgetTree, TEXT("No wet weed. Harvest a plant first."), 12, FLinearColor::Gray));
		}
		else if (bFull)
		{
			WetList->AddChild(WeedUI::Text(WidgetTree, TEXT("Rack is full - collect dried batches first."), 12, FLinearColor(1.f, 0.7f, 0.5f)));
		}
		for (const FName& Id : Order)
		{
			const int32 Have = Totals[Id];
			const FString Name = WeedUI::PrettyItemName(Id);
			const FString Sub = FString::Printf(TEXT("%dg   %.0f%% THC"), Have, Inv->GetItemQuality(Id));

			UBorder* RowCard = WidgetTree->ConstructWidget<UBorder>();
			RowCard->SetBrush(WeedUI::Rounded(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 7.f));
			RowCard->SetPadding(FMargin(7.f, 5.f, 7.f, 5.f));
			UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
			RowCard->SetContent(VB);
			VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Name, 12, FLinearColor(0.95f, 0.97f, 1.f)));
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* SubS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Sub, 10, FLinearColor(0.62f, 0.66f, 0.76f)));
			SubS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SubS->SetVerticalAlignment(VAlign_Center);
			const FName HangId = Id;
			UWeedActionButton* HangBtn = DryBtn(WidgetTree, TEXT("Hang"), FLinearColor(0.2f, 0.45f, 0.5f),
				[Ph, HangId]() { Ph->RequestDryHang(HangId); });
			HangBtn->SetIsEnabled(!bFull);
			Row->AddChildToHorizontalBox(HangBtn)->SetPadding(FMargin(3.f, 0.f, 0.f, 0.f));
			VB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

			WetList->AddChild(RowCard);
			WetList->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
		}
	}
}

void UDryingRackWidget::UpdateProgress()
{
	if (!PhoneComp.IsValid()) { return; }
	ADryingRack* Rack = PhoneComp->GetDryRack();
	if (!Rack) { return; }
	const float Total = FMath::Max(1.f, Rack->GetDryTotalSeconds());
	const TArray<FDryEntry>& Entries = Rack->GetEntries();

	for (int32 r = 0; r < RowEntryIndex.Num(); ++r)
	{
		const int32 Idx = RowEntryIndex[r];
		if (!Entries.IsValidIndex(Idx)) { continue; }
		const FDryEntry& E = Entries[Idx];
		if (RowBars.IsValidIndex(r) && RowBars[r])
		{
			RowBars[r]->SetPercent(E.bDone ? 1.f : FMath::Clamp(E.Elapsed / Total, 0.f, 1.f));
		}
		if (RowStatus.IsValidIndex(r) && RowStatus[r])
		{
			FString Txt; FLinearColor Col;
			if (E.bDone)
			{
				if (E.OverTime > 60.f) { Txt = TEXT("Ready - quality dropping, collect now!"); Col = FLinearColor(1.f, 0.6f, 0.4f); }
				else { Txt = TEXT("Ready to collect"); Col = FLinearColor(0.5f, 1.f, 0.6f); }
			}
			else
			{
				Txt = FString::Printf(TEXT("%s left"), *FmtClock(Total - E.Elapsed));
				Col = FLinearColor(0.7f, 0.8f, 0.7f);
			}
			RowStatus[r]->SetText(FText::FromString(Txt));
			RowStatus[r]->SetColorAndOpacity(FSlateColor(Col));
		}
	}
}

void UDryingRackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsDryRackOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Signature: alleen herbouwen als de SET batches/voorraad wijzigt (aantal, ids, klaar-vlaggen).
	// De progress-bars + tijd-labels updaten elke tick los (animatie zonder flicker).
	FString Sig;
	if (ADryingRack* Rack = PhoneComp->GetDryRack())
	{
		Sig += FString::Printf(TEXT("D%d/%d:"), Rack->GetEntries().Num(), Rack->GetCapacityPublic());
		for (const FDryEntry& E : Rack->GetEntries()) { Sig += FString::Printf(TEXT("%s%d%d|"), *E.DryItemId.ToString(), E.Quantity, E.bDone ? 1 : 0); }
	}
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			Sig += TEXT("W:");
			for (const FInventoryStack& St : Inv->GetStacks())
			{
				if (St.ItemId.ToString().StartsWith(TEXT("WetBud_"))) { Sig += FString::Printf(TEXT("%s%d|"), *St.ItemId.ToString(), St.Quantity); }
			}
		}
	}
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }

	UpdateProgress();
}

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
#include "Components/WrapBox.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"

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

// ---------------------------------------------------------------------------
//  UDryCell
// ---------------------------------------------------------------------------
TSharedRef<SWidget> UDryCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DryCellRoot"));
		FSlateBrush Empty; Empty.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Root->SetBrush(Empty);
		Root->SetPadding(FMargin(0.f));
		if (Inner) { Root->SetContent(Inner); }
		WidgetTree->RootWidget = Root;
	}
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

FReply UDryCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bCanDrag = (bWet || bReady) && !ItemId.IsNone();
	if (bCanDrag && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

void UDryCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (ItemId.IsNone() || !(bWet || bReady)) { return; }
	UDryDragOp* Op = NewObject<UDryDragOp>(this);
	Op->bWet = bWet;
	Op->EntryIndex = EntryIndex;
	Op->ItemId = ItemId;
	Op->Qty = Qty;
	Op->Pivot = EDragPivot::CenterCenter;

	USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
	Vis->SetWidthOverride(52.f); Vis->SetHeightOverride(52.f);
	Vis->SetContent(WeedUI::ItemIcon(WidgetTree, ItemId, 52.f));
	Op->DefaultDragVisual = Vis;
	OutOperation = Op;
}

bool UDryCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UDryDragOp* Op = Cast<UDryDragOp>(InOperation);
	if (!Op || !Owner.IsValid()) { return false; }
	// Een natte-cel staat in de inventory-kolom (drying side = false); een drogende-cel in het rek (true).
	Owner->HandleDryDrop(!bWet, Op);
	return true;
}

// ---------------------------------------------------------------------------
//  UDryDropZone
// ---------------------------------------------------------------------------
TSharedRef<SWidget> UDryDropZone::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DryDropRoot"));
		FSlateBrush Empty; Empty.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Root->SetBrush(Empty);
		Root->SetPadding(FMargin(0.f));
		if (Inner) { Root->SetContent(Inner); }
		WidgetTree->RootWidget = Root;
	}
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

bool UDryDropZone::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UDryDragOp* Op = Cast<UDryDragOp>(InOperation);
	if (Op && Owner.IsValid()) { Owner->HandleDryDrop(bDryingSide, Op); return true; }
	return false;
}

// ---------------------------------------------------------------------------
//  UDryingRackWidget
// ---------------------------------------------------------------------------
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

	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("DRYING RACK"), 18, FLinearColor(0.75f, 0.9f, 0.6f), false, true);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	HeadRow->AddChildToHorizontalBox(DryBtn(WidgetTree, TEXT("Exit"), FLinearColor(0.4f, 0.2f, 0.2f),
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseDryRack(); } }));
	Outer->AddChildToVerticalBox(HeadRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	Outer->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Sleep natte wiet naar het rek om op te hangen; sleep een klare batch naar je inventory om te oogsten."), 11, FLinearColor(0.6f, 0.65f, 0.78f), false))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UHorizontalBox* Cols = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* ColsSlot = Outer->AddChildToVerticalBox(Cols);
	ColsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto MakeColumn = [this](const FString& Title, const FLinearColor& Col, TObjectPtr<UScrollBox>& OutScroll, bool bDryingSide) -> UWidget*
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		B->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 1.f), 10.f));
		B->SetPadding(FMargin(8.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		B->SetContent(VB);
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Title, 13, Col, false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		OutScroll = WidgetTree->ConstructWidget<UScrollBox>();
		VB->AddChildToVerticalBox(OutScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UDryDropZone* DZ = WidgetTree->ConstructWidget<UDryDropZone>();
		DZ->bDryingSide = bDryingSide; DZ->Owner = this; DZ->Inner = B;
		return DZ;
	};

	UWidget* DryCol = MakeColumn(TEXT("In het rek  (drogen)"), FLinearColor(0.75f, 0.9f, 0.6f), DryList, true);
	UWidget* WetCol = MakeColumn(TEXT("Jouw natte wiet"), FLinearColor(0.6f, 0.85f, 1.f), WetList, false);
	UHorizontalBoxSlot* L = Cols->AddChildToHorizontalBox(DryCol); L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); L->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
	UHorizontalBoxSlot* R = Cols->AddChildToHorizontalBox(WetCol); R->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); R->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
}

void UDryingRackWidget::HandleDryDrop(bool bDroppedOnDryingSide, UDryDragOp* Op)
{
	if (!Op || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	if (bDroppedOnDryingSide && Op->bWet)
	{
		// Natte wiet -> rek: ophangen.
		if (!Op->ItemId.IsNone()) { Ph->RequestDryHang(Op->ItemId); }
	}
	else if (!bDroppedOnDryingSide && !Op->bWet)
	{
		// Klare batch -> inventory: oogsten.
		if (Op->EntryIndex >= 0) { Ph->RequestDryCollect(Op->EntryIndex); }
	}
	LastSig.Reset();
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
	if (TitleText) { TitleText->SetText(FText::FromString(FString::Printf(TEXT("DRYING RACK   (%d/%d)"), Used, Cap))); }

	auto AddGrid = [this](UScrollBox* Into) -> UWrapBox*
	{
		UWrapBox* W = WidgetTree->ConstructWidget<UWrapBox>();
		W->SetInnerSlotPadding(FVector2D(5.f, 5.f));
		Into->AddChild(W);
		return W;
	};
	auto BadgePill = [this](const FString& Txt) -> UBorder*
	{
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
		Pill->SetBrush(WeedUI::Rounded(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), 7.f));
		Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
		Pill->SetContent(WeedUI::Text(WidgetTree, Txt, 10, FLinearColor(0.92f, 0.95f, 1.f), false, true));
		return Pill;
	};

	// --- Linkerkolom: drogende/klare batches ---
	{
		UWrapBox* Grid = AddGrid(DryList);
		const float Total = FMath::Max(1.f, Rack->GetDryTotalSeconds());
		for (int32 i = 0; i < Rack->GetEntries().Num(); ++i)
		{
			const FDryEntry& E = Rack->GetEntries()[i];
			const float Frac = E.bDone ? 1.f : FMath::Clamp(E.Elapsed / Total, 0.f, 1.f);

			UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
			Vis->SetBrush(WeedUI::Rounded(E.bDone ? FLinearColor(0.12f, 0.20f, 0.13f, 0.96f) : FLinearColor(0.16f, 0.14f, 0.09f, 0.96f), 8.f));
			Vis->SetPadding(FMargin(4.f));
			UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
			Vis->SetContent(Ov);

			UOverlaySlot* IconOS = Ov->AddChildToOverlay(WeedUI::ItemIcon(WidgetTree, E.DryItemId, 38.f));
			IconOS->SetHorizontalAlignment(HAlign_Center); IconOS->SetVerticalAlignment(VAlign_Top);
			IconOS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

			UOverlaySlot* BS = Ov->AddChildToOverlay(BadgePill(FString::Printf(TEXT("%dg"), E.Quantity)));
			BS->SetHorizontalAlignment(HAlign_Right); BS->SetVerticalAlignment(VAlign_Top);

			UTextBlock* Status = WeedUI::Text(WidgetTree, TEXT(""), 9, FLinearColor(0.7f, 0.8f, 0.7f), true);
			UOverlaySlot* StOS = Ov->AddChildToOverlay(Status);
			StOS->SetHorizontalAlignment(HAlign_Center); StOS->SetVerticalAlignment(VAlign_Bottom);
			StOS->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
			Bar->SetPercent(Frac);
			Bar->SetFillColorAndOpacity(E.bDone ? FLinearColor(0.4f, 0.95f, 0.5f) : FLinearColor(0.85f, 0.7f, 0.25f));
			UOverlaySlot* BarOS = Ov->AddChildToOverlay(Bar);
			BarOS->SetHorizontalAlignment(HAlign_Fill); BarOS->SetVerticalAlignment(VAlign_Bottom);

			UDryCell* C = WidgetTree->ConstructWidget<UDryCell>();
			C->bWet = false; C->EntryIndex = i; C->ItemId = E.DryItemId; C->Qty = E.Quantity; C->bReady = E.bDone; C->Owner = this; C->Inner = Vis;
			C->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%dg  -  %.0f%% THC%s"), *WeedUI::PrettyItemName(E.DryItemId), E.Quantity, E.Thc, E.bDone ? TEXT("\nKlaar - sleep naar je inventory") : TEXT("\nNog aan het drogen"))));

			USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
			Sz->SetWidthOverride(80.f); Sz->SetHeightOverride(80.f); Sz->SetContent(C);
			Grid->AddChildToWrapBox(Sz);

			RowBars.Add(Bar); RowStatus.Add(Status); RowEntryIndex.Add(i);
		}
		// "+"-vangnetcel: drop natte wiet hier om op te hangen (ook als het rek leeg is).
		{
			UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
			Vis->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 0.45f), 8.f));
			Vis->SetPadding(FMargin(4.f));
			UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
			Vis->SetContent(Ov);
			UOverlaySlot* HS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, Used == 0 ? TEXT("hang hier") : TEXT("+"), Used == 0 ? 9 : 18, FLinearColor(0.45f, 0.5f, 0.45f), true));
			HS->SetHorizontalAlignment(HAlign_Center); HS->SetVerticalAlignment(VAlign_Center);
			UDryCell* C = WidgetTree->ConstructWidget<UDryCell>();
			C->bWet = false; C->EntryIndex = -1; C->ItemId = NAME_None; C->bReady = false; C->Owner = this; C->Inner = Vis;
			USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
			Sz->SetWidthOverride(80.f); Sz->SetHeightOverride(80.f); Sz->SetContent(C);
			Grid->AddChildToWrapBox(Sz);
		}
	}

	// --- Rechterkolom: natte wiet uit inventory ---
	{
		UWrapBox* Grid = AddGrid(WetList);
		APawn* P = GetOwningPlayerPawn();
		const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
		int32 Shown = 0;
		if (Inv)
		{
			TArray<FName> Order; TMap<FName, int32> Totals;
			for (const FInventoryStack& St : Inv->GetStacks())
			{
				if (!St.ItemId.ToString().StartsWith(TEXT("WetBud_")) || St.Quantity <= 0) { continue; }
				if (!Totals.Contains(St.ItemId)) { Order.Add(St.ItemId); }
				Totals.FindOrAdd(St.ItemId) += St.Quantity;
			}
			for (const FName& Id : Order)
			{
				const int32 Have = Totals[Id];
				UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
				Vis->SetBrush(WeedUI::Rounded(FLinearColor(0.11f, 0.13f, 0.17f, 0.96f), 8.f));
				Vis->SetPadding(FMargin(4.f));
				UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
				Vis->SetContent(Ov);
				UOverlaySlot* IconOS = Ov->AddChildToOverlay(WeedUI::ItemIcon(WidgetTree, Id, 42.f));
				IconOS->SetHorizontalAlignment(HAlign_Center); IconOS->SetVerticalAlignment(VAlign_Center);
				UOverlaySlot* BS = Ov->AddChildToOverlay(BadgePill(FString::Printf(TEXT("%dg"), Have)));
				BS->SetHorizontalAlignment(HAlign_Right); BS->SetVerticalAlignment(VAlign_Bottom);

				UDryCell* C = WidgetTree->ConstructWidget<UDryCell>();
				C->bWet = true; C->EntryIndex = -1; C->ItemId = Id; C->Qty = Have; C->bReady = false; C->Owner = this; C->Inner = Vis;
				C->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%dg  -  %.0f%% THC\nSleep naar het rek om op te hangen"), *WeedUI::PrettyItemName(Id), Have, Inv->GetItemQuality(Id))));

				USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
				Sz->SetWidthOverride(80.f); Sz->SetHeightOverride(80.f); Sz->SetContent(C);
				Grid->AddChildToWrapBox(Sz);
				++Shown;
			}
		}
		// "+"-vangnetcel: drop een klare batch hier om te oogsten (ook als je geen natte wiet hebt).
		{
			UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
			Vis->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 0.45f), 8.f));
			Vis->SetPadding(FMargin(4.f));
			UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
			Vis->SetContent(Ov);
			UOverlaySlot* HS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, Shown == 0 ? TEXT("oogst hier") : TEXT("+"), Shown == 0 ? 9 : 18, FLinearColor(0.45f, 0.5f, 0.55f), true));
			HS->SetHorizontalAlignment(HAlign_Center); HS->SetVerticalAlignment(VAlign_Center);
			UDryCell* C = WidgetTree->ConstructWidget<UDryCell>();
			C->bWet = true; C->EntryIndex = -1; C->ItemId = NAME_None; C->bReady = false; C->Owner = this; C->Inner = Vis;
			USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
			Sz->SetWidthOverride(80.f); Sz->SetHeightOverride(80.f); Sz->SetContent(C);
			Grid->AddChildToWrapBox(Sz);
		}
	}

	UpdateProgress();
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
				if (E.OverTime > 60.f) { Txt = TEXT("Oogst nu!"); Col = FLinearColor(1.f, 0.6f, 0.4f); }
				else { Txt = TEXT("Klaar"); Col = FLinearColor(0.5f, 1.f, 0.6f); }
			}
			else
			{
				Txt = FmtClock(Total - E.Elapsed);
				Col = FLinearColor(0.85f, 0.88f, 0.8f);
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

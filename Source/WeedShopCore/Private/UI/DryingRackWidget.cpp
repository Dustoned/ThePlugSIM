#include "UI/DryingRackWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Cultivation/DryingRack.h"
#include "Inventory/InventoryComponent.h"
#include "UI/InventoryWidget.h" // UInvDragOp (sleep vanuit inventory/hotbar)
#include "UI/WeedToast.h"

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
	// Item in deze cel (klaar of nat) -> start drag, net als de inventory: sleep 'm naar je inventory/hotbar
	// (eruit halen) of naar het rek (drogen). Geen klik-to-collect meer; het werkt nu volledig met slepen.
	if (!ItemId.IsNone() && (bReady || bWet || bDraggableAlways) && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

void UDryCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (ItemId.IsNone() || !(bWet || bReady || bDraggableAlways)) { return; }
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
	if (!Owner.IsValid()) { return false; }
	// Een natte-cel staat in de inventory-kolom (drying side = false); een drogende-cel in het rek (true).
	if (UDryDragOp* Op = Cast<UDryDragOp>(InOperation)) { Owner->HandleDryDrop(!bWet, Op); return true; }
	if (UInvDragOp* Inv = Cast<UInvDragOp>(InOperation)) { Owner->HandleInvDrop(!bWet, Inv); return true; } // sleep vanuit hotbar/inventory
	return false;
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
	if (!Owner.IsValid()) { return false; }
	if (UDryDragOp* Op = Cast<UDryDragOp>(InOperation)) { Owner->HandleDryDrop(bDryingSide, Op); return true; }
	if (UInvDragOp* Inv = Cast<UInvDragOp>(InOperation)) { Owner->HandleInvDrop(bDryingSide, Inv); return true; }
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
	FSlateBrush CardBr = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 24.f); // zelfde card als de inventory (palet)
	CardBr.OutlineSettings.Width = 1.f; CardBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
	CardB->SetBrush(CardBr);
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	// Compact paneel BOVENIN het scherm; je echte inventory opent eronder.
	// Rechts-van-midden, vlak naast de inventory (die links-van-midden staat) -> dicht naast elkaar.
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(1.f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(560.f, 452.f));
	CS->SetPosition(FVector2D(-12.f, -30.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("DRYING RACK"), 18, WeedUI::ColText(), false, true);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	HeadRow->AddChildToHorizontalBox(DryBtn(WidgetTree, TEXT("Exit"), WeedUI::ColWarn(0.55f),
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseDryRack(); } }));
	Outer->AddChildToVerticalBox(HeadRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	Outer->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Drag weed here to dry it. Done? Drag it back to your inventory."), 11, WeedUI::ColTextDim(), false))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Drop-zone: transparante achtergrond zodat de (flauwe) lege slots zichtbaar zijn tegen de card,
	// net als bij de inventory (geen donkere vlak meer dat de slots opslokt).
	UBorder* RackBg = WidgetTree->ConstructWidget<UBorder>();
	RackBg->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.f), 10.f));
	RackBg->SetPadding(FMargin(2.f));
	UScrollBox* RackScroll = WidgetTree->ConstructWidget<UScrollBox>();
	RackScroll->SetOrientation(Orient_Vertical); // verticaal scrollen -> de slots wrappen in rijen (zoals de inventory)
	RackBg->SetContent(RackScroll);
	DryList = RackScroll;
	UDryDropZone* DZ = WidgetTree->ConstructWidget<UDryDropZone>();
	DZ->bDryingSide = true; DZ->Owner = this; DZ->Inner = RackBg;
	Outer->AddChildToVerticalBox(DZ)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); // slots compact bovenaan

	// Progress + plant-info onder de slots (vult de rest van het paneel netjes op).
	UScrollBox* DetailScroll = WidgetTree->ConstructWidget<UScrollBox>();
	DetailBox = WidgetTree->ConstructWidget<UVerticalBox>();
	DetailScroll->AddChild(DetailBox);
	Outer->AddChildToVerticalBox(DetailScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UDryingRackWidget::HandleDryDrop(bool bDroppedOnDryingSide, UDryDragOp* Op)
{
	if (!Op || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	if (bDroppedOnDryingSide && Op->bWet)
	{
		// Natte wiet -> rek: ophangen (zoek de stapel van dit item op en hang precies die op).
		if (!Op->ItemId.IsNone())
		{
			APawn* P = GetOwningPlayerPawn();
			const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
			if (Inv)
			{
				for (const FInventoryStack& S : Inv->GetStacks())
				{
					if (S.ItemId == Op->ItemId) { Ph->RequestDryHang(S.StackId); break; }
				}
			}
		}
	}
	else if (!bDroppedOnDryingSide && !Op->bWet)
	{
		// Klare batch -> inventory: oogsten.
		if (Op->EntryIndex >= 0) { Ph->RequestDryCollect(Op->EntryIndex); }
	}
	else if (bDroppedOnDryingSide && !Op->bWet)
	{
		// Een niet-natte item op het rek gesleept.
		UWeedToast::NotifyPawn(GetOwningPlayerPawn(), -1, 2.f, FColor::Orange, TEXT("Only wet weed can be dried."));
	}
	LastSig.Reset();
}

void UDryingRackWidget::CollectReady(int32 EntryIndex)
{
	if (PhoneComp.IsValid() && EntryIndex >= 0)
	{
		PhoneComp->RequestDryCollect(EntryIndex);
		LastSig.Reset();
	}
}

void UDryingRackWidget::HandleInvDrop(bool bDroppedOnDryingSide, UInvDragOp* Op)
{
	// Sleep vanuit de inventory/hotbar het rek in = ophangen (alleen natte wiet).
	if (!Op || !PhoneComp.IsValid() || !bDroppedOnDryingSide) { return; }
	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }
	const int32 Idx = Inv->FindStackById(Op->StackId);
	const TArray<FInventoryStack>& St = Inv->GetStacks();
	if (!St.IsValidIndex(Idx)) { return; }
	const FName ItemId = St[Idx].ItemId;
	if (ItemId.ToString().StartsWith(TEXT("WetBud_")))
	{
		PhoneComp->RequestDryHang(Op->StackId); // precies deze stapel (eigen THC%/kwaliteit)
		LastSig.Reset();
	}
	else
	{
		UWeedToast::NotifyPawn(P, -1, 2.f, FColor::Orange, TEXT("Only wet weed can be dried."));
	}
}

UDryCell* UDryingRackWidget::MakeDryCell(int32 SlotIdx, const FDryEntry* E)
{
	UDryCell* C = WidgetTree->ConstructWidget<UDryCell>();
	C->bWet = false; C->Owner = this;
	if (E)
	{
		UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
		FSlateBrush VisBr = WeedUI::Rounded(WeedUI::ColSlot(0.96f), 8.f); // neutrale slot zoals de inventory
		if (E->bDone) { VisBr.OutlineSettings.Width = 1.5f; VisBr.OutlineSettings.Color = FSlateColor(WeedUI::ColGood(0.75f)); } // ready -> groene ring
		Vis->SetBrush(VisBr);
		Vis->SetPadding(FMargin(4.f));
		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Vis->SetContent(Ov);

		// Groot, gecentreerd icoon (zoals de inventory) - de progressbar zit niet meer in de slot.
		UOverlaySlot* IconOS = Ov->AddChildToOverlay(WeedUI::ItemIcon(WidgetTree, E->DryItemId, 50.f));
		IconOS->SetHorizontalAlignment(HAlign_Center); IconOS->SetVerticalAlignment(VAlign_Center);

		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
		Pill->SetBrush(WeedUI::Rounded(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), 7.f));
		Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
		Pill->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%dg"), E->Quantity), 10, FLinearColor(0.92f, 0.95f, 1.f), false, true));
		UOverlaySlot* BS = Ov->AddChildToOverlay(Pill);
		BS->SetHorizontalAlignment(HAlign_Right); BS->SetVerticalAlignment(VAlign_Top);

		// Klein "ready"-vinkje onderaan de slot (de volledige progress staat onder de slots).
		if (E->bDone)
		{
			UOverlaySlot* DnOS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, TEXT("READY"), 8, WeedUI::ColGood(), true, true));
			DnOS->SetHorizontalAlignment(HAlign_Center); DnOS->SetVerticalAlignment(VAlign_Bottom);
			DnOS->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}

		C->EntryIndex = SlotIdx; C->ItemId = E->DryItemId; C->Qty = E->Quantity; C->bReady = E->bDone; C->Inner = Vis;
		C->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%dg  -  %.0f%% THC%s"), *WeedUI::PrettyItemName(E->DryItemId), E->Quantity, E->Thc, E->bDone ? TEXT("\nReady - drag to your inventory") : TEXT("\nStill drying"))));
	}
	else
	{
		// Lege slot: zelfde flauwe vierkante stijl als een leeg inventory-vak, drop-zone voor natte wiet.
		UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
		Vis->SetBrush(WeedUI::Rounded(WeedUI::ColSlotEmpty(0.55f), 8.f));
		C->EntryIndex = -1; C->ItemId = NAME_None; C->bReady = false; C->Inner = Vis;
	}
	return C;
}

void UDryingRackWidget::FillBody()
{
	if (!DryList || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	ADryingRack* Rack = Ph->GetDryRack();
	if (!Rack) { return; }

	const int32 Used = Rack->GetEntries().Num();
	const int32 Cap = Rack->GetCapacityPublic();
	if (TitleText) { TitleText->SetText(FText::FromString(FString::Printf(TEXT("DRYING RACK   (%d/%d)"), Used, Cap))); }

	// Persistente grid (éénmalig) -> NOOIT ClearChildren op DryList -> geen volledige-grid-flash bij ophangen/oogsten.
	if (!DryGrid)
	{
		DryGrid = WidgetTree->ConstructWidget<UWrapBox>();
		DryGrid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
		DryList->AddChild(DryGrid);
	}

	// Vaste grid: Cap cellen (entries vullen de eerste, de rest zijn lege drop-slots).
	while (DryCellBoxes.Num() < Cap)
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(86.f); Sz->SetHeightOverride(86.f);
		DryGrid->AddChildToWrapBox(Sz);
		DryCellBoxes.Add(Sz); DryCellSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}
	while (DryCellBoxes.Num() > Cap)
	{
		const int32 Last = DryCellBoxes.Num() - 1;
		if (DryCellBoxes[Last]) { DryCellBoxes[Last]->RemoveFromParent(); }
		DryCellBoxes.RemoveAt(Last); DryCellSigs.RemoveAt(Last);
	}

	// Per-cel diff: alleen een cel waarvan de inhoud ECHT wijzigde krijgt een nieuwe UDryCell (geen flash).
	const TArray<FDryEntry>& Entries = Rack->GetEntries();
	for (int32 i = 0; i < Cap; ++i)
	{
		const bool bEntry = (i < Used);
		FString Sig = TEXT("E");
		if (bEntry)
		{
			const FDryEntry& E = Entries[i];
			Sig = FString::Printf(TEXT("%s|%d|%d"), *E.DryItemId.ToString(), E.Quantity, E.bDone ? 1 : 0);
		}
		if (!DryCellSigs.IsValidIndex(i) || !DryCellBoxes.IsValidIndex(i)) { continue; }
		if (Sig == DryCellSigs[i]) { continue; }
		DryCellSigs[i] = Sig;
		if (DryCellBoxes[i]) { DryCellBoxes[i]->SetContent(MakeDryCell(i, bEntry ? &Entries[i] : nullptr)); }
	}

	// --- Progress + plant-info onder de slots: persistente RIJ-POOL (NOOIT ClearChildren -> geen flash,
	//     lopende progressbars blijven gewoon staan). Alleen staart-groei/krimp; per-rij sig-diff werkt de
	//     naam-tekst in-place bij; bars + tijd-labels doet UpdateProgress elke tick al. ---
	if (DetailBox)
	{
		// Lege-staat-label eenmalig aanmaken, daarna alleen togglen.
		if (!DetailEmptyText)
		{
			DetailEmptyText = WeedUI::Text(WidgetTree, TEXT("Nothing drying. Drag wet weed into a slot."), 11, WeedUI::ColTextDim());
			DetailBox->AddChildToVerticalBox(DetailEmptyText);
		}
		DetailEmptyText->SetVisibility(Used == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

		// Staart-groei: nieuwe rijen met vaste structuur (Border > VBox > [naam+tijd, bar]); inhoud komt
		// via de sig-diff hieronder + UpdateProgress.
		while (RowBorders.Num() < Used)
		{
			UBorder* RowB = WidgetTree->ConstructWidget<UBorder>();
			RowB->SetBrush(WeedUI::Rounded(WeedUI::ColInner(0.85f), 8.f));
			RowB->SetPadding(FMargin(10.f, 7.f, 10.f, 8.f));
			UVerticalBox* RV = WidgetTree->ConstructWidget<UVerticalBox>();
			RowB->SetContent(RV);

			UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* NameT = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColText(), false, true);
			UHorizontalBoxSlot* NS = Top->AddChildToHorizontalBox(NameT);
			NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);
			UTextBlock* TimeT = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColText(), false, true);
			Top->AddChildToHorizontalBox(TimeT)->SetVerticalAlignment(VAlign_Center);
			RV->AddChildToVerticalBox(Top);

			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
			USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
			BarSz->SetHeightOverride(16.f); BarSz->SetContent(Bar);
			RV->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));

			DetailBox->AddChildToVerticalBox(RowB)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			RowBorders.Add(RowB); RowNames.Add(NameT); RowBars.Add(Bar); RowStatus.Add(TimeT);
			DetailRowSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
		}
		// Staart-krimp: overtollige rijen weghalen (alle pools synchroon houden).
		while (RowBorders.Num() > Used)
		{
			const int32 Last = RowBorders.Num() - 1;
			if (RowBorders[Last]) { RowBorders[Last]->RemoveFromParent(); }
			RowBorders.RemoveAt(Last); RowNames.RemoveAt(Last); RowBars.RemoveAt(Last); RowStatus.RemoveAt(Last);
			DetailRowSigs.RemoveAt(Last);
		}

		// Per-rij diff: alleen bij een echte wijziging de naam-tekst in-place bijwerken. bDone zit in de
		// sig zodat de rij ook ververst bij klaar-worden (bar-kleur/tijd pakt UpdateProgress elke tick op).
		RowEntryIndex.SetNum(Used);
		for (int32 i = 0; i < Used; ++i)
		{
			RowEntryIndex[i] = i;
			const FDryEntry& E = Entries[i];
			const FString RowSig = FString::Printf(TEXT("%s|%d|%d|%.0f|%.0f"), *E.DryItemId.ToString(), E.Quantity, E.bDone ? 1 : 0, E.Quality, E.Thc);
			if (!DetailRowSigs.IsValidIndex(i) || RowSig == DetailRowSigs[i]) { continue; }
			DetailRowSigs[i] = RowSig;
			if (RowNames.IsValidIndex(i) && RowNames[i])
			{
				RowNames[i]->SetText(FText::FromString(FString::Printf(TEXT("%s    %dg  -  THC %.0f%%   Q %.0f%%"),
					*WeedUI::PrettyItemName(E.DryItemId), E.Quantity, E.Thc, E.Quality)));
			}
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
		// Bepaal status + kleur. Voor klaar-batches: grace-aftelling, daarna het live kwaliteitsverlies.
		FString Txt; FLinearColor Col; float BarPct; FLinearColor BarCol;
		if (E.bDone)
		{
			BarPct = 1.f;
			if (Rack->IsSealed())
			{
				Txt = TEXT("Ready - sealed (no loss)"); Col = FLinearColor(0.5f, 1.f, 0.6f); BarCol = FLinearColor(0.4f, 0.95f, 0.5f);
			}
			else
			{
				const float Until = Rack->SecondsUntilDecay(E.OverTime);
				if (Until > 0.f)
				{
					Txt = FString::Printf(TEXT("Ready - quality safe for %s"), *FmtClock(Until));
					Col = FLinearColor(0.6f, 1.f, 0.7f); BarCol = FLinearColor(0.4f, 0.95f, 0.5f);
				}
				else
				{
					const float Loss = Rack->OverdryLossFrac(E.OverTime);
					const float EffQ = E.Quality * (1.f - Loss);
					Txt = FString::Printf(TEXT("Drying out!  -%.0f%% quality  (now %.0f%%)"), Loss * 100.f, EffQ);
					Col = FLinearColor(1.f, 0.5f, 0.38f); BarCol = FLinearColor(0.95f, 0.45f, 0.3f);
				}
			}
		}
		else
		{
			BarPct = FMath::Clamp(E.Elapsed / Total, 0.f, 1.f);
			Txt = FString::Printf(TEXT("%s left  (%.0f%%)"), *FmtClock(Total - E.Elapsed), BarPct * 100.f);
			Col = FLinearColor(0.85f, 0.88f, 0.8f); BarCol = FLinearColor(0.85f, 0.7f, 0.25f);
		}
		if (RowBars.IsValidIndex(r) && RowBars[r])
		{
			RowBars[r]->SetPercent(BarPct);
			RowBars[r]->SetFillColorAndOpacity(BarCol);
		}
		if (RowStatus.IsValidIndex(r) && RowStatus[r])
		{
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

	// Sig = ALLEEN de rack-staat (entries + capaciteit/tier). De volledige inventory + hotbar zaten hier
	// vroeger ook in (legacy van de oude eigen inventory-kolom) -> elke backpack-mutatie triggerde FillBody
	// = flash. De echte inventory staat er als losse InventoryWidget naast en ververst zichzelf.
	FString Sig;
	if (ADryingRack* Rack = PhoneComp->GetDryRack())
	{
		Sig += FString::Printf(TEXT("D%d/%d:"), Rack->GetEntries().Num(), Rack->GetCapacityPublic());
		for (const FDryEntry& E : Rack->GetEntries()) { Sig += FString::Printf(TEXT("%s|%d|%d|%.0f|"), *E.DryItemId.ToString(), E.Quantity, E.bDone ? 1 : 0, E.Quality); }
	}
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }

	UpdateProgress();
}

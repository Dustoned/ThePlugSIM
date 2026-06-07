#include "UI/InventoryWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "UI/DryingRackWidget.h" // UDryDragOp: een klare batch in de inventory droppen = oogsten
#include "Inventory/InventoryComponent.h"
#include "Economy/EconomyComponent.h" // cash met centen tonen
#include "World/StorageShelf.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"

// ---------------------------------------------------------------------------
//  UInvCell — sleepbare/droppbare cel
// ---------------------------------------------------------------------------
TSharedRef<SWidget> UInvCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		const bool bHotbar = (SlotIndex >= 0);
		const bool bHasIcon = !IconId.IsNone();

		// Buitenkant: afgeronde kaart met een dunne accent-rand wanneer er een item in zit.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CellRoot"));
		FSlateBrush RB = WeedUI::Rounded(Bg, Radius);
		if (bHasIcon)
		{
			RB.OutlineSettings.Width = 1.5f;
			RB.OutlineSettings.Color = FSlateColor(FLinearColor(Accent.R, Accent.G, Accent.B, 0.55f));
		}
		Root->SetBrush(RB);
		Root->SetPadding(FMargin(bHotbar ? 4.f : 7.f));
		WidgetTree->RootWidget = Root;

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Root->SetContent(Ov);

		// Icoon ALTIJD gecentreerd -> rooster-cellen zien er hetzelfde uit als de hotbar-icon-slots.
		// (Naam/details staan in de hover-tooltip; aantal staat als badge in de hoek.)
		{
			UOverlaySlot* IconOS = Ov->AddChildToOverlay(
				bHasIcon ? WeedUI::ItemIcon(WidgetTree, IconId, IconSize)
				         : Cast<UWidget>(WeedUI::Text(WidgetTree, TEXT(""), 8, FLinearColor::Transparent)));
			IconOS->SetHorizontalAlignment(HAlign_Center);
			IconOS->SetVerticalAlignment(VAlign_Center);
		}
		// Merge-knopje onderaan (alleen rooster) als er meerdere stapels van dit item zijn.
		if (bShowMerge && !bHotbar)
		{
			UWeedActionButton* M = WidgetTree->ConstructWidget<UWeedActionButton>();
			M->OnClicked.AddDynamic(M, &UWeedActionButton::Handle);
			TFunction<void()> Fn = MergeFn;
			M->OnAction.BindLambda([Fn](int32, int32) { if (Fn) { Fn(); } });
			FButtonStyle S;
			S.Normal = WeedUI::Rounded(FLinearColor(0.45f, 0.36f, 0.10f, 0.92f), 5.f);
			S.Hovered = WeedUI::Rounded(FLinearColor(0.62f, 0.50f, 0.13f), 5.f);
			S.Pressed = WeedUI::Rounded(FLinearColor(0.38f, 0.30f, 0.08f), 5.f);
			S.NormalPadding = FMargin(5.f, 1.f); S.PressedPadding = FMargin(5.f, 1.f);
			M->SetStyle(S);
			M->SetContent(WeedUI::Text(WidgetTree, TEXT("merge"), 8, FLinearColor::White, true));
			UOverlaySlot* MS = Ov->AddChildToOverlay(M);
			MS->SetHorizontalAlignment(HAlign_Center);
			MS->SetVerticalAlignment(VAlign_Bottom);
		}

		// Slotnummer (hotbar) linksboven.
		if (bSlotNumber)
		{
			UOverlaySlot* NS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), SlotNumber), 9, FLinearColor(0.65f, 0.68f, 0.78f), false, true));
			NS->SetHorizontalAlignment(HAlign_Left);
			NS->SetVerticalAlignment(VAlign_Top);
			NS->SetPadding(FMargin(1.f, 0.f, 0.f, 0.f));
		}

		// Aantal/gram-badge als een pill (rechtsboven in het rooster, rechtsonder in de hotbar).
		if (!Badge.IsEmpty())
		{
			UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
			Pill->SetBrush(WeedUI::Rounded(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), 7.f));
			Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
			Pill->SetContent(WeedUI::Text(WidgetTree, Badge, 10, FLinearColor(0.92f, 0.95f, 1.f), false, true));
			UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
			PS->SetHorizontalAlignment(HAlign_Right);
			PS->SetVerticalAlignment(bHotbar ? VAlign_Bottom : VAlign_Top);
		}
	}
	// Volledige naam + details bij hover (zodat lange namen die niet in de cel passen toch leesbaar zijn).
	if (!Tooltip.IsEmpty()) { SetToolTipText(FText::FromString(Tooltip)); }
	// Hit-test zichtbaar zodat de cel muis/drag-events ontvangt.
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

FReply UInvCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggable && StackId != 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Shift+klik = split-popup openen (geen drag starten).
		if (InMouseEvent.IsShiftDown() && Owner.IsValid())
		{
			Owner->OpenSplitPopup(StackId);
			return FReply::Handled();
		}
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

void UInvCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (!bDraggable || StackId == 0) { return; }
	UInvDragOp* Op = NewObject<UInvDragOp>(this);
	Op->StackId = StackId;
	Op->FromSlot = SlotIndex;
	Op->FromCell = GridCell;
	Op->Pivot = EDragPivot::CenterCenter; // visual zit precies ONDER de cursor

	// Sleep-visual = het ECHTE item-icoon, gecentreerd op de muis (geen losse tag ernaast meer).
	const float DragSize = 54.f;
	USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
	Vis->SetWidthOverride(DragSize); Vis->SetHeightOverride(DragSize);
	if (!IconId.IsNone())
	{
		Vis->SetContent(WeedUI::ItemIcon(WidgetTree, IconId, DragSize));
	}
	else
	{
		Vis->SetContent(WeedUI::Text(WidgetTree, Line1.IsEmpty() ? TEXT("item") : Line1, 11, FLinearColor::White, true));
	}
	Op->DefaultDragVisual = Vis;

	OutOperation = Op;
}

bool UInvCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Een KLARE droogrek-batch (UDryDragOp, niet-nat) op de inventory droppen = oogsten naar je voorraad.
	if (UDryDragOp* DryOp = Cast<UDryDragOp>(InOperation))
	{
		if (!DryOp->bWet && DryOp->EntryIndex >= 0 && Owner.IsValid())
		{
			return Owner->AcceptDryBatchDrop(DryOp->EntryIndex);
		}
		return false;
	}

	UInvDragOp* Op = Cast<UInvDragOp>(InOperation);
	if (!Op || !Inv.IsValid() || Op->StackId == 0) { return false; }

	// Sleep een stapel op een ANDERE stapel van HETZELFDE item -> samenvoegen (mergen).
	if (SlotIndex < 0 && StackId != 0 && StackId != Op->StackId)
	{
		const int32 ThisIdx = Inv->FindStackById(StackId);
		const int32 DragIdx = Inv->FindStackById(Op->StackId);
		const TArray<FInventoryStack>& St = Inv->GetStacks();
		if (St.IsValidIndex(ThisIdx) && St.IsValidIndex(DragIdx)
			&& St[ThisIdx].ItemId == St[DragIdx].ItemId && St[ThisIdx].ItemId != FName(TEXT("Cash"))
			&& UInventoryComponent::IsStackable(St[ThisIdx].ItemId)) // geen flessen e.d. samenvoegen
		{
			Inv->RequestMergeTwo(StackId, Op->StackId); // alleen DEZE twee samenvoegen
			if (Owner.IsValid()) { Owner->MarkDirty(); }
			return true;
		}
	}

	if (SlotIndex >= 0)
	{
		// Drop op een hotbar-slot met HETZELFDE stapelbare item -> samenvoegen (blijft op de hotbar).
		if (StackId != 0 && StackId != Op->StackId)
		{
			const int32 ThisIdx = Inv->FindStackById(StackId);
			const int32 DragIdx = Inv->FindStackById(Op->StackId);
			const TArray<FInventoryStack>& St = Inv->GetStacks();
			if (St.IsValidIndex(ThisIdx) && St.IsValidIndex(DragIdx)
				&& St[ThisIdx].ItemId == St[DragIdx].ItemId && St[ThisIdx].ItemId != FName(TEXT("Cash"))
				&& UInventoryComponent::IsStackable(St[ThisIdx].ItemId))
			{
				Inv->RequestMergeTwo(StackId, Op->StackId); // in de hotbar-stapel samenvoegen
				if (Owner.IsValid()) { Owner->MarkDirty(); }
				return true;
			}
		}
		// Anders: toewijzen (Assign wisselt netjes als 'ie al ergens stond).
		Inv->AssignHotbarStack(SlotIndex, Op->StackId);
	}
	else if (GridCell >= 0)
	{
		if (Op->FromCell >= 0)
		{
			// Versleept binnen het rooster -> verplaats naar deze cel (wisselt met wat hier stond).
			Inv->MoveStackToCell(Op->StackId, GridCell);
		}
		else if (Op->FromSlot >= 0)
		{
			// Vanaf de hotbar in het rooster gesleept -> snelkoppeling van de hotbar halen.
			Inv->UnassignHotbarStack(Op->StackId);
		}
	}
	if (Owner.IsValid()) { Owner->MarkDirty(); }
	return true;
}

// ---------------------------------------------------------------------------
//  UInventoryWidget
// ---------------------------------------------------------------------------
void UInventoryWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

UInventoryComponent* UInventoryWidget::GetInv() const
{
	APawn* P = GetOwningPlayerPawn();
	return P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
}

TSharedRef<SWidget> UInventoryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UInventoryWidget::OnInvChanged() { bDirty = true; }

namespace
{
	UWeedActionButton* TileButton(UWidgetTree* Tree, const FLinearColor& Col, float Radius, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, Radius);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, Radius);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, Radius);
		S.NormalPadding = FMargin(6.f, 4.f); S.PressedPadding = FMargin(6.f, 4.f);
		B->SetStyle(S);
		return B;
	}
}

void UInventoryWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InvCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), 24.f));
	CardB->SetPadding(FMargin(18.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(840.f, 452.f)); // strak om de 24 vierkante slots (4 rijen) heen, weinig leegte
	CS->SetPosition(FVector2D(0.f, -30.f)); // iets omhoog zodat de in-game hotbar eronder vrij blijft
	CardSlot = CS; // bewaard om de inventory naast een paneel (droogrek) te schuiven

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	// Header.
	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* HT = Head->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("INVENTORY"), 18, FLinearColor(0.6f, 1.f, 0.6f), false, true));
	HT->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HT->SetVerticalAlignment(VAlign_Center);
	WeightText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.8f, 0.85f, 1.f));
	UHorizontalBoxSlot* WS = Head->AddChildToHorizontalBox(WeightText);
	WS->SetVerticalAlignment(VAlign_Center); WS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	// Sorteer-knop: cyclet Name -> Amount -> Category en sorteert het rooster.
	static const TCHAR* SortNames[3] = { TEXT("Name"), TEXT("Amount"), TEXT("Category") };
	UWeedActionButton* SortBtn = TileButton(WidgetTree, FLinearColor(0.2f, 0.32f, 0.46f), 8.f,
		[this]()
		{
			SortMode = (SortMode + 1) % 3;
			static const TCHAR* SN[3] = { TEXT("Name"), TEXT("Amount"), TEXT("Category") };
			if (SortLabel) { SortLabel->SetText(FText::FromString(FString::Printf(TEXT("Sort: %s"), SN[SortMode]))); }
			if (UInventoryComponent* I = GetInv()) { I->SortGrid(SortMode); }
		});
	SortLabel = WeedUI::Text(WidgetTree, FString::Printf(TEXT("Sort: %s"), SortNames[SortMode]), 12, FLinearColor::White, true);
	SortBtn->SetContent(SortLabel);
	Head->AddChildToHorizontalBox(SortBtn)->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* CloseBtn = TileButton(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->ToggleInventory(); } });
	CloseBtn->SetContent(WeedUI::Text(WidgetTree, TEXT("Close (I)"), 12, FLinearColor::White, true));
	UHorizontalBoxSlot* CloseS = Head->AddChildToHorizontalBox(CloseBtn);
	CloseS->SetVerticalAlignment(VAlign_Center); CloseS->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	VB->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Body: links het thuis-voorraad-lijstje, rechts de slots + hotbar.
	UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* BodyS = VB->AddChildToVerticalBox(Body);
	BodyS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// --- Links: HOME STASH (alle shelves/chests samengeteld) ---
	USizeBox* StashSize = WidgetTree->ConstructWidget<USizeBox>();
	StashSize->SetWidthOverride(232.f);
	UBorder* StashPanel = WidgetTree->ConstructWidget<UBorder>();
	StashPanel->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.08f, 0.96f), 10.f));
	StashPanel->SetPadding(FMargin(10.f, 8.f, 10.f, 8.f));
	StashSize->SetContent(StashPanel);
	UVerticalBox* StashVB = WidgetTree->ConstructWidget<UVerticalBox>();
	StashPanel->SetContent(StashVB);
	StashVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("HOME STASH"), 13, FLinearColor(0.55f, 0.95f, 0.65f), false, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	UScrollBox* StashScroll = WidgetTree->ConstructWidget<UScrollBox>();
	StashList = StashScroll;
	StashVB->AddChildToVerticalBox(StashScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* LS = Body->AddChildToHorizontalBox(StashSize);
	LS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));

	// --- Rechts: slots (wrap, scrollbaar). De hotbar zit NIET meer hier: je gebruikt de
	//     in-game hotbar onderaan (die is een sleep-doel zolang de inventory open is). ---
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* RS = Body->AddChildToHorizontalBox(Right);
	RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* GS = Right->AddChildToVerticalBox(Scroll);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); // grid hugt z'n inhoud -> hint komt er direct onder
	Grid = WidgetTree->ConstructWidget<UWrapBox>();
	Grid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	Scroll->AddChild(Grid);

	// Hint onderaan: sleep naar de hotbar onderin het scherm.
	Right->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Sleep een item naar de hotbar.  Shift+klik = splitsen.  Sleep op een gelijke stapel = samenvoegen."), 11, FLinearColor(0.55f, 0.6f, 0.72f)))
		->SetPadding(FMargin(2.f, 8.f, 0.f, 0.f));

	BuildSplitPopup(Root);
}

void UInventoryWidget::BuildSplitPopup(UCanvasPanel* Root)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.99f), 14.f));
	Panel->SetPadding(FMargin(18.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Stapel splitsen"), 15, FLinearColor(0.7f, 1.f, 0.7f), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	SplitLabel = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor::White, true);
	VB->AddChildToVerticalBox(SplitLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	SplitSlider = WidgetTree->ConstructWidget<USlider>();
	SplitSlider->SetMinValue(0.f); SplitSlider->SetMaxValue(1.f); SplitSlider->SetValue(0.5f);
	SplitSlider->OnValueChanged.AddDynamic(this, &UInventoryWidget::OnSplitSliderChanged);
	VB->AddChildToVerticalBox(SplitSlider)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	UWeedActionButton* Conf = TileButton(WidgetTree, FLinearColor(0.2f, 0.55f, 0.27f), 8.f, [this]() { ConfirmSplit(); });
	Conf->SetContent(WeedUI::Text(WidgetTree, TEXT("Splitsen"), 12, FLinearColor::White, true));
	UWeedActionButton* Canc = TileButton(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), 8.f, [this]() { CancelSplit(); });
	Canc->SetContent(WeedUI::Text(WidgetTree, TEXT("Annuleer"), 12, FLinearColor::White, true));
	Btns->AddChildToHorizontalBox(Conf)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* CS2 = Btns->AddChildToHorizontalBox(Canc);
	CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	VB->AddChildToVerticalBox(Btns);

	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(300.f);
	Sz->SetContent(Panel);
	SplitRoot = Sz;

	UCanvasPanelSlot* PS = Root->AddChildToCanvas(Sz);
	PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PS->SetAlignment(FVector2D(0.5f, 0.5f));
	PS->SetAutoSize(true);
	PS->SetPosition(FVector2D(0.f, -30.f));
	Sz->SetVisibility(ESlateVisibility::Collapsed);
}

void UInventoryWidget::OpenSplitPopup(int32 StackId)
{
	UInventoryComponent* I = GetInv();
	if (!I || !SplitRoot || !SplitSlider) { return; }
	const int32 Idx = I->FindStackById(StackId);
	const TArray<FInventoryStack>& St = I->GetStacks();
	if (!St.IsValidIndex(Idx)) { return; }
	if (!UInventoryComponent::IsStackable(St[Idx].ItemId) || St[Idx].Quantity < 2) { return; } // niks te splitsen
	SplitStackId = StackId;
	SplitTotal = St[Idx].Quantity;
	SplitSlider->SetValue(0.5f);
	OnSplitSliderChanged(0.5f);
	SplitRoot->SetVisibility(ESlateVisibility::Visible);
}

void UInventoryWidget::OnSplitSliderChanged(float V)
{
	if (!SplitLabel) { return; }
	const int32 Amount = FMath::Clamp(FMath::RoundToInt(V * SplitTotal), 1, FMath::Max(1, SplitTotal - 1));
	SplitLabel->SetText(FText::FromString(FString::Printf(TEXT("Afsplitsen: %d   (van %d)"), Amount, SplitTotal)));
}

void UInventoryWidget::ConfirmSplit()
{
	UInventoryComponent* I = GetInv();
	if (I && SplitStackId != 0 && SplitSlider && SplitTotal > 1)
	{
		const int32 Amount = FMath::Clamp(FMath::RoundToInt(SplitSlider->GetValue() * SplitTotal), 1, SplitTotal - 1);
		I->RequestSplit(SplitStackId, Amount, -1);
		MarkDirty();
	}
	CancelSplit();
}

void UInventoryWidget::CancelSplit()
{
	SplitStackId = 0;
	if (SplitRoot) { SplitRoot->SetVisibility(ESlateVisibility::Collapsed); }
}

void UInventoryWidget::MergeItemNow(FName ItemId)
{
	if (PhoneComp.IsValid()) { PhoneComp->MergeNow(ItemId); }
}

void UInventoryWidget::RebuildStash()
{
	if (!StashList) { return; }
	StashList->ClearChildren();

	// Tel alles uit alle shelves/chests samen, per item-id (gram + gewogen THC%).
	TArray<FName> Order;
	TMap<FName, int32> Qty;
	TMap<FName, float> ThcW; // som van thc*qty (voor gewogen gemiddelde)
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AStorageShelf> It(W); It; ++It)
		{
			for (const FShelfStack& S : It->Contents)
			{
				if (S.ItemId.IsNone() || S.Quantity <= 0) { continue; }
				if (!Qty.Contains(S.ItemId)) { Order.Add(S.ItemId); }
				Qty.FindOrAdd(S.ItemId) += S.Quantity;
				ThcW.FindOrAdd(S.ItemId) += S.Thc * S.Quantity;
			}
		}
	}

	if (Order.Num() == 0)
	{
		StashList->AddChild(WeedUI::Text(WidgetTree, TEXT("Niets opgeslagen.\nStop wiet in een shelf/chest."), 11, FLinearColor(0.55f, 0.58f, 0.66f)));
		return;
	}

	// Wiet eerst (Bud/Bag/Wet/Joint), daarna de rest; binnen groepen op naam.
	auto IsWeed = [](FName Id) { const FString S = Id.ToString(); return S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("Bag_")) || S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Joint_")); };
	Order.Sort([&](const FName& A, const FName& B)
	{
		const bool wa = IsWeed(A), wb = IsWeed(B);
		if (wa != wb) { return wa; }
		return WeedUI::PrettyItemName(A) < WeedUI::PrettyItemName(B);
	});

	for (const FName& Id : Order)
	{
		const int32 N = Qty[Id];
		const FString IdStr = Id.ToString();
		const bool bWeed = IsWeed(Id);
		const bool bWet = IdStr.StartsWith(TEXT("WetBud_"));
		const float Thc = (N > 0) ? (ThcW[Id] / N) : 0.f;

		UBorder* Row = WidgetTree->ConstructWidget<UBorder>();
		Row->SetBrush(WeedUI::Rounded(FLinearColor(0.09f, 0.10f, 0.14f, 0.9f), 6.f));
		Row->SetPadding(FMargin(6.f, 4.f, 6.f, 4.f));
		UHorizontalBox* RHB = WidgetTree->ConstructWidget<UHorizontalBox>();
		Row->SetContent(RHB);

		USizeBox* IconSz = WidgetTree->ConstructWidget<USizeBox>();
		IconSz->SetWidthOverride(26.f); IconSz->SetHeightOverride(26.f);
		IconSz->SetContent(WeedUI::ItemIcon(WidgetTree, Id, 26.f));
		UHorizontalBoxSlot* ISlot = RHB->AddChildToHorizontalBox(IconSz);
		ISlot->SetVerticalAlignment(VAlign_Center); ISlot->SetPadding(FMargin(0.f, 0.f, 7.f, 0.f));

		UVerticalBox* RVB = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBoxSlot* TSlot = RHB->AddChildToHorizontalBox(RVB);
		TSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TSlot->SetVerticalAlignment(VAlign_Center);

		FString Nm = WeedUI::PrettyItemName(Id);
		if (Nm.Len() > 22) { Nm = Nm.Left(21) + TEXT("."); }
		const FLinearColor NameCol = bWet ? FLinearColor(0.55f, 0.8f, 1.f) : (bWeed ? FLinearColor(0.7f, 1.f, 0.75f) : FLinearColor(0.92f, 0.93f, 1.f));
		RVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Nm, 12, NameCol));

		FString Sub;
		if (bWeed)
		{
			// Zakjes tellen in GRAMMEN (aantal x gram-per-zakje), los gedroogd/nat = 1g per stuk.
			const int32 Grams = UInventoryComponent::IsBag(Id) ? N * FMath::Max(1, UInventoryComponent::BagGrams(Id)) : N;
			Sub = FString::Printf(TEXT("%dg   %.0f%% THC%s"), Grams, Thc, bWet ? TEXT("  (wet)") : TEXT(""));
		}
		else { Sub = FString::Printf(TEXT("x%d"), N); }
		RVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Sub, 10, FLinearColor(0.6f, 0.64f, 0.74f)));

		StashList->AddChild(Row);
		StashList->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
	}
}

void UInventoryWidget::RebuildContent()
{
	RebuildStash();

	UInventoryComponent* Inv = GetInv();
	if (!Inv || !Grid) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();

	WeightText->SetText(FText::FromString(FString::Printf(TEXT("Slots %d/%d    Weight %.1f / %.0f kg"),
		Inv->GetUsedSlots(), Inv->MaxStacks, Inv->GetTotalWeight(), Inv->MaxWeight)));

	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();

	// --- Rooster met VASTE posities: items blijven staan waar je ze neerzet ---
	Grid->ClearChildren();
	const TArray<int32>& Order = Inv->GetGridOrder();
	for (int32 cell = 0; cell < Order.Num(); ++cell)
	{
		const int32 StackId = Order[cell];
		const int32 Idx = Inv->FindStackById(StackId);
		// Items die op de hotbar staan tonen we NIET in het rooster (ze staan onderin de hotbar).
		if (StackId != 0 && Stacks.IsValidIndex(Idx) && Inv->IsStackOnHotbar(StackId)) { continue; }

		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(86.f); Sz->SetHeightOverride(86.f); // vierkante icon-slot zoals de hotbar

		UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
		Cell->SlotIndex = -1; Cell->GridCell = cell;
		Cell->Inv = Inv; Cell->Owner = this;
		Cell->IconSize = 50.f;

		const bool bShowItem = (StackId != 0 && Stacks.IsValidIndex(Idx));
		if (bShowItem)
		{
			const FInventoryStack& S = Stacks[Idx];
			const FName ItemId = S.ItemId;
			const FString IdStr = ItemId.ToString();
			const bool bWet = IdStr.StartsWith(TEXT("WetBud_"));
			const bool bBud = IdStr.StartsWith(TEXT("Bud_")) || bWet;
			const bool bWeed = bBud || IdStr.StartsWith(TEXT("Joint_"));
			const bool bCash = (ItemId == TEXT("Cash"));

			Cell->StackId = StackId;
			Cell->bDraggable = true; // ook briefgeld kun je verslepen (herschikken / naar de hotbar)
			Cell->IconId = ItemId;
			Cell->Accent = WeedUI::ItemAccent(ItemId);

			// Volledige naam in de tooltip; in de cel kappen we te lange namen af met een ellips
			// (de tooltip + het icoon maken alsnog volledig duidelijk wat het is).
			const FString FullName = WeedUI::PrettyItemName(ItemId);
			Cell->Line1 = (FullName.Len() > 20) ? (FullName.Left(19) + TEXT("...")) : FullName;
			Cell->Tooltip = FullName;

			if (bCash)
			{
				Cell->Bg = FLinearColor(0.09f, 0.14f, 0.09f, 0.97f);
				// Toon het ECHTE saldo inclusief centen (de cash-stapel zelf telt alleen hele euro's).
				const APawn* Pw = GetOwningPlayerPawn();
				const UEconomyComponent* Ec = Pw ? Pw->FindComponentByClass<UEconomyComponent>() : nullptr;
				const double Euros = Ec ? (Ec->GetCashCents() / 100.0) : static_cast<double>(S.Quantity);
				Cell->Line2 = FString::Printf(TEXT("EUR %.2f"), Euros);
				Cell->Badge = (Euros >= 1000.0) ? FString::Printf(TEXT("%.0fk"), Euros / 1000.0) : FString::Printf(TEXT("%.0f"), Euros);
				Cell->Tooltip += FString::Printf(TEXT("\nEUR %.2f contant"), Euros);
			}
			else if (bWet)
			{
				Cell->Bg = FLinearColor(0.09f, 0.14f, 0.21f, 0.97f);
				Cell->Line2 = TEXT("WET - dry it first");
				Cell->Badge = FString::Printf(TEXT("%dg"), S.Quantity);
				Cell->Tooltip += FString::Printf(TEXT("\n%dg  -  NAT, eerst drogen  (THC %.0f%%)"), S.Quantity, S.Quality);
			}
			else
			{
				Cell->Bg = FLinearColor(0.10f, 0.11f, 0.15f, 0.96f);
				Cell->Line2 = bWeed
					? FString::Printf(TEXT("THC %.0f%%  Q %.0f%%"), S.Quality, S.QualityPct)
					: TEXT("");
				Cell->Badge = WeedUI::ItemQtyBadge(ItemId, S.Quantity);
				const bool bBagCell = UInventoryComponent::IsBag(ItemId);
				Cell->Tooltip += bBagCell
					? FString::Printf(TEXT("\n%dx %dg zakje  -  THC %.0f%%   Kwaliteit %.0f%%"), S.Quantity, UInventoryComponent::BagGrams(ItemId), S.Quality, S.QualityPct)
					: (bWeed
						? FString::Printf(TEXT("\n%dg  -  THC %.0f%%   Kwaliteit %.0f%%"), S.Quantity, S.Quality, S.QualityPct)
						: FString::Printf(TEXT("\nAantal: %d"), S.Quantity));
				if (bWeed && Ph && Inv->CountStacksOf(ItemId) > 1)
				{
					Cell->bShowMerge = true;
					Cell->MergeFn = [Ph, ItemId]() { Ph->MergeNow(ItemId); };
				}
			}
		}
		else
		{
			// Lege cel (of plek van een item dat nu op de hotbar staat): drop-doel, niet sleepbaar.
			Cell->StackId = 0; Cell->bDraggable = false;
			Cell->Bg = FLinearColor(0.09f, 0.09f, 0.12f, 0.30f);
		}
		Sz->SetContent(Cell);
		Grid->AddChildToWrapBox(Sz);
	}
}

bool UInventoryWidget::AcceptDryBatchDrop(int32 EntryIndex)
{
	if (!PhoneComp.IsValid() || EntryIndex < 0) { return false; }
	PhoneComp->RequestDryCollect(EntryIndex); // oogst de batch naar je voorraad
	bDirty = true;
	return true;
}

void UInventoryWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UInventoryComponent* Inv = GetInv();

	// Bind aan voorraad-wijzigingen zodat we herbouwen na elke mutatie.
	if (Inv && Inv != BoundInv.Get())
	{
		if (UInventoryComponent* Old = BoundInv.Get()) { Old->OnInventoryChanged.RemoveDynamic(this, &UInventoryWidget::OnInvChanged); }
		Inv->OnInventoryChanged.AddDynamic(this, &UInventoryWidget::OnInvChanged);
		BoundInv = Inv;
		bDirty = true;
	}

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsInventoryOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { return; }

	// Naast een gekoppeld paneel (droogrek) staat de inventory RECHTS, het paneel links -> naast elkaar.
	// Anders gewoon gecentreerd.
	if (CardSlot)
	{
		const bool bSideBySide = PhoneComp.IsValid() && PhoneComp->IsDryRackOpen();
		if (bSideBySide)
		{
			CardSlot->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));
			CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
			CardSlot->SetPosition(FVector2D(-30.f, -30.f));
		}
		else
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D(0.f, -30.f));
		}
	}

	if (bDirty)
	{
		bDirty = false;
		RebuildContent();
	}
}

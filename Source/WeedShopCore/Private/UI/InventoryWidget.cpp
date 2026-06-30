#include "UI/InventoryWidget.h"
#include "WeedShopCore.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "UI/DryingRackWidget.h" // UDryDragOp: een klare batch in de inventory droppen = oogsten
#include "UI/ShelfWidget.h"      // UShelfDragOp: een item uit een schap in de inventory droppen = pakken
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
#include "Components/ProgressBar.h"
#include "Components/BackgroundBlur.h"
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
			// Strain-getagde items: per-strain gekleurde, iets dikkere frame (matcht de tag-pill).
			const bool bTagged = !Tag.IsEmpty();
			RB.OutlineSettings.Width = bTagged ? 2.0f : 1.5f;
			RB.OutlineSettings.Color = bTagged
				? FSlateColor(WeedUI::TagColor(Tag, 0.95f, 0.70f))
				: FSlateColor(FLinearColor(Accent.R, Accent.G, Accent.B, 0.55f));
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
				bHasIcon ? WeedUI::ItemIcon(WidgetTree, IconId, IconSize, WaterOverride)
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
			S.Normal = WeedUI::Rounded(FLinearColor(0.42f, 0.27f, 0.62f, 0.92f), 5.f);
			S.Hovered = WeedUI::Rounded(FLinearColor(0.55f, 0.36f, 0.80f), 5.f);
			S.Pressed = WeedUI::Rounded(FLinearColor(0.32f, 0.20f, 0.50f), 5.f);
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

		// Strain/variant-TAG-bubble onderaan de cel (alleen rooster) -> onderscheid items met hetzelfde icoon.
		if (!Tag.IsEmpty() && !bHotbar)
		{
			UTextBlock* TagT = WeedUI::Text(WidgetTree, Tag, 9, FLinearColor(0.98f, 1.f, 0.99f), false, true);
			UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
			TagPill->SetBrush(WeedUI::Rounded(WeedUI::TagColor(Tag, 0.42f, 0.62f), 6.f));
			TagPill->SetPadding(FMargin(5.f, 0.f, 5.f, 1.f));
			TagPill->SetContent(TagT);
			TagPill->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* TagOS = Ov->AddChildToOverlay(TagPill);
			TagOS->SetHorizontalAlignment(HAlign_Center);
			TagOS->SetVerticalAlignment(VAlign_Bottom);
			TagOS->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
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
	if (StackId != 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Shift+klik = split/drop-popup openen (werkt ook voor niet-sleepbare stapels zoals Cash).
		if (InMouseEvent.IsShiftDown() && Owner.IsValid())
		{
			Owner->OpenSplitPopup(StackId);
			return FReply::Handled();
		}
		if (bDraggable) { return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton); }
	}
	return FReply::Unhandled();
}

void UInvCell::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (StackId != 0)
	{
		// Hover = oplichten i.p.v. opschalen -> niets steekt buiten de cel uit / clipt onder andere panelen.
		SetColorAndOpacity(FLinearColor(1.22f, 1.22f, 1.22f, 1.f));
		if (Owner.IsValid()) { Owner->ShowItemDetails(this); }
	}
}

void UInvCell::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	SetColorAndOpacity(FLinearColor::White);
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
		Vis->SetContent(WeedUI::ItemIcon(WidgetTree, IconId, DragSize, WaterOverride));
	}
	else
	{
		Vis->SetContent(WeedUI::Text(WidgetTree, Line1.IsEmpty() ? TEXT("item") : Line1, 11, FLinearColor::White, true));
	}
	Op->DefaultDragVisual = Vis;

	// Sleep je 'm los BUITEN een drop-doel (op niks) -> hele stapel op de grond droppen.
	Op->DropInv = Inv;
	Op->OnDragCancelled.AddDynamic(Op, &UInvDragOp::HandleDroppedOutside);

	OutOperation = Op;
}

void UInvDragOp::HandleDroppedOutside(UDragDropOperation* Operation)
{
	if (DropInv.IsValid() && StackId != 0)
	{
		DropInv->RequestDropStack(StackId);
	}
}

bool UInvCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Een KLARE droogrek-batch (UDryDragOp, niet-nat) op de inventory OF de hotbar droppen = oogsten naar je
	// voorraad. Werkt overal: we halen de PhoneClientComponent uit de pawn (eigenaar van de inventory).
	if (UDryDragOp* DryOp = Cast<UDryDragOp>(InOperation))
	{
		if (!DryOp->bWet && DryOp->EntryIndex >= 0 && Inv.IsValid())
		{
			if (AActor* PawnOwner = Inv->GetOwner())
			{
				if (UPhoneClientComponent* Ph = PawnOwner->FindComponentByClass<UPhoneClientComponent>())
				{
					// Op een ROOSTER-cel gedropt -> daar plaatsen i.p.v. automatisch op de hotbar.
					if (GridCell >= 0) { Inv->SetPendingGridCell(GridCell); }
					Ph->RequestDryCollect(DryOp->EntryIndex);
					return true;
				}
			}
		}
		return false;
	}

	// Een item UIT een opslag-schap (UShelfDragOp) op de inventory OF de hotbar droppen = uit het schap halen.
	if (UShelfDragOp* ShelfOp = Cast<UShelfDragOp>(InOperation))
	{
		if (ShelfOp->bFromShelf && ShelfOp->ShelfIndex >= 0 && ShelfOp->Qty > 0 && Inv.IsValid())
		{
			if (AActor* PawnOwner = Inv->GetOwner())
			{
				if (UPhoneClientComponent* Ph = PawnOwner->FindComponentByClass<UPhoneClientComponent>())
				{
					// Op een ROOSTER-cel gedropt -> daar plaatsen i.p.v. automatisch op de hotbar.
					if (GridCell >= 0) { Inv->SetPendingGridCell(GridCell); }
					Ph->RequestShelfTake(ShelfOp->ShelfIndex, ShelfOp->Qty);
					return true;
				}
			}
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
			// Gelijke kwaliteit -> direct mergen; verschillend -> eerst bevestigen (popup).
			if (Owner.IsValid()) { return Owner->TryMergeOrConfirm(StackId, Op->StackId); }
			Inv->RequestMergeTwo(StackId, Op->StackId); // fallback zonder owner
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
				// Gelijke kwaliteit -> direct mergen; verschillend -> eerst bevestigen (popup).
				if (Owner.IsValid()) { return Owner->TryMergeOrConfirm(StackId, Op->StackId); }
				Inv->RequestMergeTwo(StackId, Op->StackId); // fallback zonder owner
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
			// Vanaf de hotbar in het rooster gesleept -> snelkoppeling van de hotbar halen EN het item op
			// de losgelaten cel zetten (anders verschijnt 'ie weer op z'n oude/eerste-vrije rooster-cel).
			Inv->UnassignHotbarStack(Op->StackId);
			Inv->MoveStackToCell(Op->StackId, GridCell);
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

	// Background-blur + dim achter het venster (premium focus); getoggled met de kaart in NativeTick.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(6.f);
	{ UCanvasPanelSlot* BS = Root->AddChildToCanvas(Blur); BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); BS->SetOffsets(FMargin(0.f)); }
	Blur->SetVisibility(ESlateVisibility::Collapsed);
	BlurBg = Blur;
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrush(WeedUI::Rounded(WeedUI::Hex(0x0B0E14, 0.45f), 0.f));
	{ UCanvasPanelSlot* DS = Root->AddChildToCanvas(Dim); DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); DS->SetOffsets(FMargin(0.f)); }
	Dim->SetVisibility(ESlateVisibility::Collapsed);
	DimBg = Dim;

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InvCard"));
	FSlateBrush CardBr = WeedUI::Rounded(WeedUI::Hex(0x252B3A), 16.f);
	CardBr.OutlineSettings.Width = 1.f;
	CardBr.OutlineSettings.Color = FSlateColor(WeedUI::Hex(0x3A4152, 0.55f)); // subtiele lichtere premium-stroke
	CardB->SetBrush(CardBr);
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
	UHorizontalBoxSlot* HT = Head->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("INVENTORY"), 22, WeedUI::Hex(0xF1EAFE), false, true));
	HT->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HT->SetVerticalAlignment(VAlign_Center);
	// Status: Slots/Weight-tekst + dunne weight-bar eronder.
	UVerticalBox* StatusVB = WidgetTree->ConstructWidget<UVerticalBox>();
	WeightText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::Hex(0xB8B4C8));
	StatusVB->AddChildToVerticalBox(WeightText)->SetHorizontalAlignment(HAlign_Right);
	USizeBox* WBSz = WidgetTree->ConstructWidget<USizeBox>();
	WBSz->SetWidthOverride(160.f); WBSz->SetHeightOverride(5.f);
	WeightBar = WidgetTree->ConstructWidget<UProgressBar>();
	{
		FProgressBarStyle PB;
		PB.BackgroundImage = WeedUI::Rounded(WeedUI::Hex(0x1B202B), 3.f);
		PB.FillImage = WeedUI::Rounded(FLinearColor::White, 3.f);
		WeightBar->SetWidgetStyle(PB);
		WeightBar->SetFillColorAndOpacity(WeedUI::Hex(0xB98CFF));
	}
	WBSz->SetContent(WeightBar);
	StatusVB->AddChildToVerticalBox(WBSz)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	UHorizontalBoxSlot* WS = Head->AddChildToHorizontalBox(StatusVB);
	WS->SetVerticalAlignment(VAlign_Center); WS->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
	// Sorteer-knop: cyclet Name -> Amount -> Category en sorteert het rooster.
	static const TCHAR* SortNames[3] = { TEXT("Name"), TEXT("Amount"), TEXT("Category") };
	UWeedActionButton* SortBtn = TileButton(WidgetTree, WeedUI::Hex(0x3A4152), 8.f,
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
	UWeedActionButton* CloseBtn = TileButton(WidgetTree, WeedUI::Hex(0x3A4152), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->ToggleInventory(); } });
	CloseBtn->SetContent(WeedUI::Text(WidgetTree, TEXT("X"), 13, WeedUI::Hex(0xF1EAFE), true));
	UHorizontalBoxSlot* CloseS = Head->AddChildToHorizontalBox(CloseBtn);
	CloseS->SetVerticalAlignment(VAlign_Center); CloseS->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	VB->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	// Accent-divider onder de kop -> scheidt header van inhoud (echte-game-look).
	{ USizeBox* HDiv = WidgetTree->ConstructWidget<USizeBox>(); HDiv->SetHeightOverride(1.f); UBorder* HDivB = WidgetTree->ConstructWidget<UBorder>(); HDivB->SetBrush(WeedUI::Rounded(WeedUI::Hex(0x3A4152, 0.7f), 1.f)); HDiv->SetContent(HDivB); VB->AddChildToVerticalBox(HDiv)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f)); }

	// Body: links het thuis-voorraad-lijstje, rechts de slots + hotbar.
	UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* BodyS = VB->AddChildToVerticalBox(Body);
	BodyS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// --- Links: HOME STASH (alle shelves/chests samengeteld) ---
	USizeBox* StashSize = WidgetTree->ConstructWidget<USizeBox>();
	StashSize->SetWidthOverride(232.f);
	StashBox = StashSize; // verbergen als je in een machine zit (puur preview, niet om te slepen)
	UBorder* StashPanel = WidgetTree->ConstructWidget<UBorder>();
	StashPanel->SetBrush(WeedUI::Rounded(WeedUI::Hex(0x303747), 12.f)); // inner panel (iets lichter / raised)
	StashPanel->SetPadding(FMargin(10.f, 8.f, 10.f, 8.f));
	StashSize->SetContent(StashPanel);
	UOverlay* LeftOv = WidgetTree->ConstructWidget<UOverlay>();
	StashPanel->SetContent(LeftOv);
	UVerticalBox* StashVB = WidgetTree->ConstructWidget<UVerticalBox>();
	{ UOverlaySlot* SOS = LeftOv->AddChildToOverlay(StashVB); SOS->SetHorizontalAlignment(HAlign_Fill); SOS->SetVerticalAlignment(VAlign_Fill); }
	StashContent = StashVB;
	StashVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("HOME STASH"), 12, WeedUI::Hex(0xB8B4C8), false, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	UScrollBox* StashScroll = WidgetTree->ConstructWidget<UScrollBox>();
	StashList = StashScroll;
	StashVB->AddChildToVerticalBox(StashScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Item-details-paneel (collapsed; verschijnt bij hover over een slot).
	UVerticalBox* DetailsVB = WidgetTree->ConstructWidget<UVerticalBox>();
	{ UOverlaySlot* DOS = LeftOv->AddChildToOverlay(DetailsVB); DOS->SetHorizontalAlignment(HAlign_Fill); DOS->SetVerticalAlignment(VAlign_Fill); }
	DetailsVB->SetVisibility(ESlateVisibility::Collapsed);
	DetailsContent = DetailsVB;
	DetailsIconBox = WidgetTree->ConstructWidget<USizeBox>();
	DetailsIconBox->SetWidthOverride(88.f); DetailsIconBox->SetHeightOverride(88.f);
	{ UVerticalBoxSlot* DIS = DetailsVB->AddChildToVerticalBox(DetailsIconBox); DIS->SetHorizontalAlignment(HAlign_Center); DIS->SetPadding(FMargin(0.f, 6.f, 0.f, 8.f)); }
	DetailsName = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::Hex(0xF1EAFE), false, true);
	DetailsName->SetAutoWrapText(true);
	DetailsVB->AddChildToVerticalBox(DetailsName)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	DetailsBody = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::Hex(0xB8B4C8));
	DetailsBody->SetAutoWrapText(true);
	DetailsVB->AddChildToVerticalBox(DetailsBody)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	{
		UWeedActionButton* SplitB = TileButton(WidgetTree, WeedUI::Hex(0x3A2B52), 8.f, [this]() { if (DetailsStackId != 0) { OpenSplitPopup(DetailsStackId); } });
		SplitB->SetContent(WeedUI::Text(WidgetTree, TEXT("Split stack"), 12, WeedUI::Hex(0xF1EAFE), true));
		DetailsSplitBtn = SplitB;
		DetailsVB->AddChildToVerticalBox(SplitB)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}

	UHorizontalBoxSlot* LS = Body->AddChildToHorizontalBox(StashSize);
	LS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));

	// --- Rechts: slots (wrap, scrollbaar). De hotbar zit NIET meer hier: je gebruikt de
	//     in-game hotbar onderaan (die is een sleep-doel zolang de inventory open is). ---
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* RS = Body->AddChildToHorizontalBox(Right);
	RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Verzonken "well" achter de slots -> diepte (slots zitten in een paneel, niet op het platte vlak).
	UBorder* GridWell = WidgetTree->ConstructWidget<UBorder>();
	GridWell->SetBrush(WeedUI::Rounded(WeedUI::Hex(0x1B202B), 12.f)); // verzonken (donkerder dan paneel)
	GridWell->SetPadding(FMargin(10.f));
	UVerticalBoxSlot* GS = Right->AddChildToVerticalBox(GridWell);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); // well hugt z'n inhoud -> hint komt er direct onder
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	GridWell->SetContent(Scroll);
	Grid = WidgetTree->ConstructWidget<UWrapBox>();
	Grid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	Scroll->AddChild(Grid);

	// Hint onderaan: sleep naar de hotbar onderin het scherm.
	Right->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Drag to the hotbar  ·  Shift+click = split"), 11, FLinearColor(0.55f, 0.6f, 0.72f)))
		->SetPadding(FMargin(2.f, 8.f, 0.f, 0.f));

	BuildSplitPopup(Root);
	BuildMergePopup(Root);
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
	Canc->SetContent(WeedUI::Text(WidgetTree, TEXT("Cancel"), 12, FLinearColor::White, true));
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
	PS->SetZOrder(50); // boven de slots/grid (was er soms achter)
	Sz->SetVisibility(ESlateVisibility::Collapsed);
}

void UInventoryWidget::ShowItemDetails(UInvCell* Cell)
{
	if (!Cell || Cell->StackId == 0 || !DetailsContent || !StashContent) { return; }
	DetailsStackId = Cell->StackId;
	if (DetailsIconBox) { DetailsIconBox->SetContent(WeedUI::ItemIcon(WidgetTree, Cell->IconId, 84.f, Cell->WaterOverride)); }
	if (DetailsName) { DetailsName->SetText(FText::FromString(Cell->Line1.IsEmpty() ? WeedUI::PrettyItemName(Cell->IconId) : Cell->Line1)); }
	if (DetailsBody) { DetailsBody->SetText(FText::FromString(Cell->Tooltip)); }
	if (DetailsSplitBtn) { DetailsSplitBtn->SetVisibility(Cell->bDraggable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	StashContent->SetVisibility(ESlateVisibility::Collapsed);
	DetailsContent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UInventoryWidget::OpenSplitPopup(int32 StackId)
{
	UInventoryComponent* I = GetInv();
	if (!I || !SplitRoot || !SplitSlider) { return; }
	const int32 Idx = I->FindStackById(StackId);
	const TArray<FInventoryStack>& St = I->GetStacks();
	if (!St.IsValidIndex(Idx)) { return; }
	// Cash mag NIET meer gedropt worden: geen cash-split/drop-popup openen.
	if (St[Idx].ItemId == FName(TEXT("Cash")))
	{
		if (AActor* Own = I->GetOwner())
		{
			if (UPhoneClientComponent* Ph = Own->FindComponentByClass<UPhoneClientComponent>())
			{
				Ph->Toast(TEXT("You can't drop cash."), FColor(255, 90, 90), 2.0f);
			}
		}
		return;
	}
	bSplitIsCash = false;
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
	if (bSplitIsCash)
	{
		const int32 Amount = FMath::Clamp(FMath::RoundToInt(V * SplitTotal), 1, FMath::Max(1, SplitTotal));
		SplitLabel->SetText(FText::FromString(FString::Printf(TEXT("Drop: EUR %d   (of EUR %d)"), Amount, SplitTotal)));
		return;
	}
	const int32 Amount = FMath::Clamp(FMath::RoundToInt(V * SplitTotal), 1, FMath::Max(1, SplitTotal - 1));
	SplitLabel->SetText(FText::FromString(FString::Printf(TEXT("Split off: %d   (of %d)"), Amount, SplitTotal)));
}

void UInventoryWidget::ConfirmSplit()
{
	UInventoryComponent* I = GetInv();
	if (I && SplitStackId != 0 && SplitSlider && SplitTotal >= 1)
	{
		if (bSplitIsCash)
		{
			// Cash droppen: trek het bedrag van je cash + spawn een oppakbaar geldstapeltje (server).
			const int32 Euros = FMath::Clamp(FMath::RoundToInt(SplitSlider->GetValue() * SplitTotal), 1, SplitTotal);
			if (AActor* Own = I->GetOwner())
			{
				if (UEconomyComponent* Eco = Own->FindComponentByClass<UEconomyComponent>()) { Eco->ServerDropCash(Euros); }
			}
			MarkDirty();
		}
		else if (SplitTotal > 1)
		{
			const int32 Amount = FMath::Clamp(FMath::RoundToInt(SplitSlider->GetValue() * SplitTotal), 1, SplitTotal - 1);
			I->RequestSplit(SplitStackId, Amount, -1);
			MarkDirty();
		}
	}
	CancelSplit();
}

void UInventoryWidget::CancelSplit()
{
	SplitStackId = 0;
	if (SplitRoot) { SplitRoot->SetVisibility(ESlateVisibility::Collapsed); }
}

void UInventoryWidget::BuildMergePopup(UCanvasPanel* Root)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.99f), 14.f));
	Panel->SetPadding(FMargin(18.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Stapels samenvoegen"), 15, FLinearColor(0.7f, 1.f, 0.7f), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	MergeLabel = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor::White, true);
	VB->AddChildToVerticalBox(MergeLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	UWeedActionButton* Conf = TileButton(WidgetTree, FLinearColor(0.2f, 0.55f, 0.27f), 8.f, [this]() { ConfirmMerge(); });
	Conf->SetContent(WeedUI::Text(WidgetTree, TEXT("Samenvoegen"), 12, FLinearColor::White, true));
	UWeedActionButton* Canc = TileButton(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), 8.f, [this]() { CancelMerge(); });
	Canc->SetContent(WeedUI::Text(WidgetTree, TEXT("Cancel"), 12, FLinearColor::White, true));
	Btns->AddChildToHorizontalBox(Conf)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* CS2 = Btns->AddChildToHorizontalBox(Canc);
	CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	VB->AddChildToVerticalBox(Btns);

	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(320.f);
	Sz->SetContent(Panel);
	MergeRoot = Sz;

	UCanvasPanelSlot* PS = Root->AddChildToCanvas(Sz);
	PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PS->SetAlignment(FVector2D(0.5f, 0.5f));
	PS->SetAutoSize(true);
	PS->SetPosition(FVector2D(0.f, -30.f));
	PS->SetZOrder(50);
	Sz->SetVisibility(ESlateVisibility::Collapsed);
}

bool UInventoryWidget::TryMergeOrConfirm(int32 IntoStackId, int32 FromStackId)
{
	UInventoryComponent* I = GetInv();
	if (!I || IntoStackId == 0 || FromStackId == 0 || IntoStackId == FromStackId) { return false; }
	const int32 IntoIdx = I->FindStackById(IntoStackId);
	const int32 FromIdx = I->FindStackById(FromStackId);
	const TArray<FInventoryStack>& St = I->GetStacks();
	if (!St.IsValidIndex(IntoIdx) || !St.IsValidIndex(FromIdx)) { return false; }

	const float ThcA = St[IntoIdx].Quality,   ThcB = St[FromIdx].Quality;
	const float QuaA = St[IntoIdx].QualityPct, QuaB = St[FromIdx].QualityPct;

	// Gelijke kwaliteit (binnen dezelfde 0.5-epsilon als de auto-merge) -> verliesvrij, direct mergen.
	// Ook als de popup om wat voor reden niet bestaat: niet de actie verliezen, gewoon mergen.
	if ((FMath::Abs(ThcA - ThcB) < 0.5f && FMath::Abs(QuaA - QuaB) < 0.5f) || !MergeRoot || !MergeLabel)
	{
		I->RequestMergeTwo(IntoStackId, FromStackId);
		MarkDirty();
		return true;
	}

	// Verschillende kwaliteit -> bevestigen: toon het gewogen-gemiddelde-resultaat vooraf.
	const int32 Total = St[IntoIdx].Quantity + St[FromIdx].Quantity;
	const float AvgThc = (Total > 0) ? (ThcA * St[IntoIdx].Quantity + ThcB * St[FromIdx].Quantity) / Total : 0.f;
	const float AvgQua = (Total > 0) ? (QuaA * St[IntoIdx].Quantity + QuaB * St[FromIdx].Quantity) / Total : 0.f;
	PendingMergeInto = IntoStackId;
	PendingMergeFrom = FromStackId;
	MergeLabel->SetText(FText::FromString(FString::Printf(
		TEXT("THC: %.0f%% + %.0f%%  ->  %.0f%%\nKwaliteit: %.0f%% + %.0f%%  ->  %.0f%%"),
		ThcA, ThcB, AvgThc, QuaA, QuaB, AvgQua)));
	MergeRoot->SetVisibility(ESlateVisibility::Visible);
	return true;
}

void UInventoryWidget::ConfirmMerge()
{
	UInventoryComponent* I = GetInv();
	if (I && PendingMergeInto != 0 && PendingMergeFrom != 0)
	{
		I->RequestMergeTwo(PendingMergeInto, PendingMergeFrom);
		MarkDirty();
	}
	CancelMerge();
}

void UInventoryWidget::CancelMerge()
{
	PendingMergeInto = 0;
	PendingMergeFrom = 0;
	if (MergeRoot) { MergeRoot->SetVisibility(ESlateVisibility::Collapsed); }
}

void UInventoryWidget::MergeItemNow(FName ItemId)
{
	if (PhoneComp.IsValid()) { PhoneComp->MergeNow(ItemId); }
}

void UInventoryWidget::RebuildStash()
{
	if (!StashList) { return; }
	StashList->ClearChildren();

	// HOME STASH toont ALLEEN kweek-/wiet-items (zaden + wiet + hash/edibles/concentraten); meubels,
	// supplies en apparaten die je ook in een kast/chest legt horen hier niet thuis.
	auto IsStashItem = [](FName Id)
	{
		const FString S = Id.ToString();
		return S.StartsWith(TEXT("Seed_")) || S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("Bag_"))
			|| S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Joint_"))
			|| S.StartsWith(TEXT("Hash")) || S.StartsWith(TEXT("Edible")) || S.Contains(TEXT("Brownie"))
			|| S.StartsWith(TEXT("Moonrock")) || S.StartsWith(TEXT("Rosin")) || S.StartsWith(TEXT("Oil_")) || S.StartsWith(TEXT("Isolator"));
	};
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
				if (!IsStashItem(S.ItemId)) { continue; } // alleen kweek/wiet in de HOME STASH
				if (!Qty.Contains(S.ItemId)) { Order.Add(S.ItemId); }
				Qty.FindOrAdd(S.ItemId) += S.Quantity;
				ThcW.FindOrAdd(S.ItemId) += S.Thc * S.Quantity;
			}
		}
	}

	if (Order.Num() == 0)
	{
		StashList->AddChild(WeedUI::Text(WidgetTree, TEXT("Nothing stored.\nPut weed in a shelf/chest."), 11, FLinearColor(0.55f, 0.58f, 0.66f)));
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
		IconSz->SetWidthOverride(46.f); IconSz->SetHeightOverride(46.f);
		IconSz->SetContent(WeedUI::ItemIcon(WidgetTree, Id, 46.f));
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
	// LET OP: de HOME STASH wordt NIET meer hier herbouwd. Die heeft een eigen signatuur (zie NativeTick) en
	// herbouwt alleen als de shelf-inhoud echt wijzigt - anders flikkerde de hele stash (+ volledige-wereld
	// shelf-scan) bij elke backpack-sleep mee.
	UInventoryComponent* Inv = GetInv();
	if (!Inv || !Grid) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();

	// Toon de gevulde BACKPACK-cellen / 10 (de hotbar is aparte opslag en telt hier niet mee -> geen "17/10" meer).
	int32 BackpackUsed = 0;
	for (int32 S : Inv->GetGridOrder()) { if (S != 0) { ++BackpackUsed; } }
	WeightText->SetText(FText::FromString(FString::Printf(TEXT("Slots %d/%d    Weight %.1f / %.0f"),
		FMath::Min(BackpackUsed, Inv->MaxStacks), Inv->MaxStacks, Inv->GetTotalWeight(), Inv->MaxWeight)));
	if (WeightBar)
	{
		const float WFrac = (Inv->MaxWeight > 0.f) ? FMath::Clamp(Inv->GetTotalWeight() / Inv->MaxWeight, 0.f, 1.f) : 0.f;
		WeightBar->SetPercent(WFrac);
		WeightBar->SetFillColorAndOpacity(WFrac >= 0.85f ? WeedUI::Hex(0xFF6B6B) : (WFrac >= 0.65f ? WeedUI::Hex(0xFF6BD6) : WeedUI::Hex(0xB98CFF))); // bijna vol -> warm/rood
	}

	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();

	// --- Rooster met VASTE posities: de cel-SLOTS blijven VAST in de WrapBox; we vervangen alleen de inhoud
	//     van cellen die ECHT wijzigden (op een sleep zijn dat er 2). Geen ClearChildren -> geen flikker. ---
	const TArray<int32>& Order = Inv->GetGridOrder();
	const int32 NCells = Order.Num();
	if (CellBoxes.Num() != NCells)
	{
		Grid->ClearChildren();
		CellBoxes.Reset(); CellSigs.Reset();
		for (int32 i = 0; i < NCells; ++i)
		{
			USizeBox* B = WidgetTree->ConstructWidget<USizeBox>();
			B->SetWidthOverride(86.f); B->SetHeightOverride(86.f);
			Grid->AddChildToWrapBox(B);
			CellBoxes.Add(B); CellSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
		}
	}
	for (int32 cell = 0; cell < NCells; ++cell)
	{
		const int32 StackId = Order[cell];
		const int32 Idx = Inv->FindStackById(StackId);
		// Hotbar-items tonen we niet in het rooster, maar hun cel blijft als lege cel staan (rooster verspringt niet).
		const bool bOnHotbar = (StackId != 0 && Stacks.IsValidIndex(Idx) && Inv->IsStackOnHotbar(StackId));
		const bool bShowItem = (StackId != 0 && Stacks.IsValidIndex(Idx) && !bOnHotbar);

		// Signatuur van de zichtbare cel-staat: onveranderd -> cel met rust laten (geen rebuild, geen flikker).
		FString Sig = TEXT("E");
		if (bShowItem)
		{
			const FInventoryStack& Sg = Stacks[Idx];
			int64 CashEuros = 0;
			if (Sg.ItemId == TEXT("Cash"))
			{
				const APawn* Pw = GetOwningPlayerPawn();
				const UEconomyComponent* Ec = Pw ? Pw->FindComponentByClass<UEconomyComponent>() : nullptr;
				CashEuros = Ec ? (WeedRoundEuros(Ec->GetCashCents()) / 100) : (int64)Sg.Quantity;
			}
			const int32 WaterLv = Sg.ItemId.ToString().StartsWith(TEXT("WaterBottle")) ? FMath::RoundToInt(Sg.Quality) : -1;
			Sig = FString::Printf(TEXT("I|%s|%d|%.1f|%.1f|%d|%lld|%d"), *Sg.ItemId.ToString(), Sg.Quantity, Sg.Quality, Sg.QualityPct, WaterLv, (long long)CashEuros, Inv->CountStacksOf(Sg.ItemId));
		}
		if (!CellSigs.IsValidIndex(cell) || !CellBoxes.IsValidIndex(cell)) { continue; }
		if (Sig == CellSigs[cell]) { continue; } // niets veranderd aan deze cel
		CellSigs[cell] = Sig;

		UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
		Cell->SlotIndex = -1; Cell->GridCell = cell;
		Cell->Inv = Inv; Cell->Owner = this;
		Cell->IconSize = 68.f;
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
			// Waterfles: toon het vol/leeg-niveau van DEZE fles (uit z'n eigen stack-Quality), niet van de actieve fles.
			Cell->WaterOverride = ItemId.ToString().StartsWith(TEXT("WaterBottle")) ? FMath::RoundToInt(S.Quality) : -1;
			Cell->Accent = WeedUI::ItemAccent(ItemId);

			// Volledige naam in de tooltip; in de cel kappen we te lange namen af met een ellips
			// (de tooltip + het icoon maken alsnog volledig duidelijk wat het is).
			const FString FullName = WeedUI::PrettyItemName(ItemId);
			Cell->Line1 = (FullName.Len() > 20) ? (FullName.Left(19) + TEXT("...")) : FullName;
			Cell->Tag = WeedUI::ItemTagShort(ItemId); // korte strain/rank-code in de bubble onderaan de cel
			Cell->Tooltip = FullName;

			if (bCash)
			{
				Cell->Bg = FLinearColor(0.09f, 0.14f, 0.09f, 0.97f);
				// Toon het saldo in hele euro's (de game rekent/toont alles in hele euro's).
				const APawn* Pw = GetOwningPlayerPawn();
				const UEconomyComponent* Ec = Pw ? Pw->FindComponentByClass<UEconomyComponent>() : nullptr;
				const int64 Euros = Ec ? (WeedRoundEuros(Ec->GetCashCents()) / 100) : static_cast<int64>(S.Quantity);
				Cell->Line2 = FString::Printf(TEXT("EUR %lld"), (long long)Euros);
				Cell->Badge = (Euros >= 1000) ? FString::Printf(TEXT("%.0fk"), Euros / 1000.0) : FString::Printf(TEXT("%lld"), (long long)Euros);
				Cell->Tooltip += FString::Printf(TEXT("\nEUR %lld contant"), (long long)Euros);
			}
			else if (bWet)
			{
				Cell->Bg = FLinearColor(0.09f, 0.14f, 0.21f, 0.97f);
				Cell->Line2 = TEXT("WET - dry it first");
				Cell->Badge = FString::Printf(TEXT("%dg"), S.Quantity);
				Cell->Tooltip = WeedUI::ItemTooltip(ItemId, S.Quantity, S.Quality, S.QualityPct);
				Cell->Tooltip += FString::Printf(TEXT("\nWeight %.1f"), Inv->GetUnitWeight(ItemId) * S.Quantity);
			}
			else
			{
				Cell->Bg = WeedUI::Hex(0x3A4152, 0.96f);
				Cell->Line2 = bWeed
					? FString::Printf(TEXT("THC %.0f%%  Q %.0f%%"), S.Quality, S.QualityPct)
					: TEXT("");
				Cell->Badge = WeedUI::ItemQtyBadge(ItemId, S.Quantity);
				Cell->Tooltip = WeedUI::ItemTooltip(ItemId, S.Quantity, S.Quality, S.QualityPct);
				Cell->Tooltip += FString::Printf(TEXT("\nWeight %.1f"), Inv->GetUnitWeight(ItemId) * S.Quantity);
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
			// Zelfde duidelijke contrast als het droogrek.
			Cell->StackId = 0; Cell->bDraggable = false;
			Cell->Bg = WeedUI::Hex(0x2A3140, 0.5f); // lege cel: subtieler/donkerder dan gevuld
		}
		CellBoxes[cell]->SetContent(Cell);
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
	const ESlateVisibility BackdropVis = bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (BlurBg) { BlurBg->SetVisibility(BackdropVis); }
	if (DimBg) { DimBg->SetVisibility(BackdropVis); }
	if (!bOpen)
	{
		// Reset het details-paneel zodat de HOME STASH weer toont bij heropenen.
		if (DetailsContent) { DetailsContent->SetVisibility(ESlateVisibility::Collapsed); }
		if (StashContent) { StashContent->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		DetailsStackId = 0;
		return;
	}

	// Naast een gekoppeld paneel (droogrek): HOME STASH verbergen (puur preview), card smaller, en het paar
	// dicht bij elkaar in het midden (rek rechts-van-midden, inventory links-van-midden). Anders: normaal gecentreerd.
	const bool bSideBySide = PhoneComp.IsValid() && (PhoneComp->IsDryRackOpen() || PhoneComp->IsShelfOpen());
	if (StashBox) { StashBox->SetVisibility(bSideBySide ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); }
	if (CardSlot)
	{
		if (bSideBySide)
		{
			CardSlot->SetSize(FVector2D(584.f, 452.f));
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.f, 0.5f));
			CardSlot->SetPosition(FVector2D(12.f, -30.f));
		}
		else
		{
			CardSlot->SetSize(FVector2D(840.f, 452.f));
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D(0.f, -30.f));
		}
	}

	// HOME STASH: alleen herbouwen als de shelf-inhoud ECHT veranderde (niet bij elke grid-sleep). Goedkope
	// signatuur i.p.v. de volledige widget-herbouw + wereld-scan elke keer (dat liet de halve inventory flikkeren).
	if (!bSideBySide)
	{
		FString SSig;
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AStorageShelf> It(W); It; ++It)
			{
				for (const FShelfStack& S : It->Contents) { SSig.Appendf(TEXT("%s%d|"), *S.ItemId.ToString(), S.Quantity); }
			}
		}
		if (SSig != LastStashSig) { LastStashSig = SSig; RebuildStash(); }
	}

	if (bDirty)
	{
		bDirty = false;
		RebuildContent();
	}
}

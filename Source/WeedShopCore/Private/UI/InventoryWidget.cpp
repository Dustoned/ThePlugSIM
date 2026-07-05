#include "UI/InventoryWidget.h"
#include "WeedShopCore.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "UI/DryingRackWidget.h" // UDryDragOp: een klare batch in de inventory droppen = oogsten
#include "UI/ShelfWidget.h"      // UShelfDragOp: een item uit een schap in de inventory droppen = pakken
#include "Inventory/InventoryComponent.h"
#include "World/StorageShelf.h"
#include "Cultivation/PotTypes.h"    // IsPotItem: quick-view aantal verbergen voor plaatsbare potten (zelfde regel als de hand-preview)
#include "Placement/BuildComponent.h" // IsInOwnedHome: competitive stash-filter (eigen kamer)
#include "Game/WeedShopGameState.h"   // IsCompetitive
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
#include "Framework/Application/SlateApplication.h" // IsDragDropping() voor de drag-ghost-herstel
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
		// Geen gekleurde rand meer om gevulde cellen (user: niet consistent met de hotbar). De per-strain
		// tag-pill onderaan blijft wél gekleurd; de cel zelf is gewoon de afgeronde bg.
		Root->SetBrush(RB);
		Root->SetPadding(bHotbar ? FMargin(4.f) : FMargin(7.f, 7.f, 7.f, 3.f)); // grid: minder onder -> tag netjes onderin
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
			S.Normal = WeedUI::Rounded(WeedUI::ColAccentDim(0.96f), 5.f);
			S.Hovered = WeedUI::Rounded(WeedUI::ColAccentDim() * 1.3f, 5.f);
			S.Pressed = WeedUI::Rounded(WeedUI::ColAccentDim() * 0.8f, 5.f);
			S.NormalPadding = FMargin(5.f, 1.f); S.PressedPadding = FMargin(5.f, 1.f);
			M->SetStyle(S);
			M->SetContent(WeedUI::Text(WidgetTree, TEXT("merge"), 8, WeedUI::ColText(), true));
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
			Pill->SetBrush(WeedUI::Rounded(WeedUI::ColBg(0.85f), 7.f));
			Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
			Pill->SetContent(WeedUI::Text(WidgetTree, Badge, 10, WeedUI::ColText(), false, true));
			UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
			PS->SetHorizontalAlignment(HAlign_Right);
			PS->SetVerticalAlignment(bHotbar ? VAlign_Bottom : VAlign_Top);
		}

		// Strain/variant-TAG-bubble onderaan de cel (alleen rooster) -> onderscheid items met hetzelfde icoon.
		if (!Tag.IsEmpty() && !bHotbar)
		{
			// Iets groter + dunne donkere outline: op size 9 oogde de (Exo-)tekst te dun voor snelle herkenning.
			UTextBlock* TagT = WeedUI::Text(WidgetTree, Tag, 10, FLinearColor(0.98f, 1.f, 0.99f), false, true);
			{
				FSlateFontInfo TagFont = WeedUI::Font(10, true);
				TagFont.OutlineSettings.OutlineSize = 1;
				TagFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);
				TagT->SetFont(TagFont);
			}
			UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
			TagPill->SetBrush(WeedUI::Rounded(WeedUI::TagColorForItem(IconId), 6.f)); // strain -> eigen kleur; standaard-item -> grijs
			TagPill->SetPadding(FMargin(5.f, 0.f, 5.f, 1.f));
			TagPill->SetContent(TagT);
			TagPill->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* TagOS = Ov->AddChildToOverlay(TagPill);
			TagOS->SetHorizontalAlignment(HAlign_Center);
			TagOS->SetVerticalAlignment(VAlign_Bottom);
			TagOS->SetPadding(FMargin(0.f, 0.f, 0.f, 0.f));
		}

		// Hover-glow bovenop alles (binnen de cel -> clipt niet); zichtbaar bij hover.
		UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
		Glow->SetBrush(WeedUI::Rounded(FLinearColor(0.85f, 0.82f, 1.f, 0.18f), Radius));
		Glow->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* GlowOS = Ov->AddChildToOverlay(Glow);
		GlowOS->SetHorizontalAlignment(HAlign_Fill); GlowOS->SetVerticalAlignment(VAlign_Fill);
		HoverGlow = Glow;
	}
	// Zwevende hover-tooltip = volledige naam + info-body. (Tooltip is body-only voor het details-paneel,
	// waar de naam al groot staat; hiér prefixen we de naam zodat lange namen toch leesbaar blijven.)
	if (!Tooltip.IsEmpty()) { SetToolTipText(FText::FromString(WeedUI::PrettyItemName(IconId) + TEXT("\n") + Tooltip)); }
	// Hit-test zichtbaar zodat de cel muis/drag-events ontvangt.
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

FReply UInvCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (StackId != 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Shift+klik = split-popup openen (werkt op alle stapels, ook briefgeld).
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
		// Hover = glow-overlay binnen de cel (clipt niet; SetColorAndOpacity>1 wordt door UMG geclampt -> onzichtbaar).
		if (HoverGlow) { HoverGlow->SetVisibility(ESlateVisibility::HitTestInvisible); }
		// Details-paneel vullen: grid-cellen via Owner, hotbar-DropCells via DetailsOwner (geen Owner).
		UInventoryWidget* DetailsW = Owner.IsValid() ? Owner.Get() : DetailsOwner.Get();
		if (DetailsW) { DetailsW->ShowItemDetails(this); }
	}
}

void UInvCell::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	if (HoverGlow) { HoverGlow->SetVisibility(ESlateVisibility::Collapsed); }
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
	if (Owner.IsValid()) { Owner->BeginDragGhost(this); } // bron-cel dimmen tijdens slepen (pro "opgepakt"-look)
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
	return HandleDropOp(InOperation);
}

UInvCell* UInvCell::FindNearestCell(const TArray<UInvCell*>& Cells, const FVector2D& ScreenPos, float MaxEdgeFrac)
{
	UInvCell* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	const FVector2f P(ScreenPos);
	for (UInvCell* C : Cells)
	{
		if (!C) { continue; }
		const FGeometry& Geo = C->GetCachedGeometry();
		const FVector2f A(Geo.GetAbsolutePosition());
		const FVector2f Sz(Geo.GetAbsoluteSize());
		if (Sz.X <= 1.f || Sz.Y <= 1.f) { continue; } // nog niet gelayout (bv. net gebouwd/collapsed)
		// Afstand van de drop-positie tot de CELRAND (0 = op de cel zelf; die had de drop dan al gepakt).
		const float DX = FMath::Max3(A.X - P.X, P.X - (A.X + Sz.X), 0.f);
		const float DY = FMath::Max3(A.Y - P.Y, P.Y - (A.Y + Sz.Y), 0.f);
		const float Dist = FMath::Sqrt(DX * DX + DY * DY);
		if (Dist <= MaxEdgeFrac * FMath::Min(Sz.X, Sz.Y) && Dist < BestDist)
		{
			BestDist = Dist;
			Best = C;
		}
	}
	return Best;
}

bool UInvCell::HandleDropOp(UDragDropOperation* InOperation)
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
			&& St[ThisIdx].ItemId == St[DragIdx].ItemId
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
				&& St[ThisIdx].ItemId == St[DragIdx].ItemId
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
			// Optimistisch: toon de move METEEN (cel-widgets omwisselen) zodat de server-round-trip geen "plop"
			// geeft. GEEN MarkDirty: dat zou RebuildContent vóór de server-bevestiging op de OUDE staat draaien
			// en de swap terugdraaien. OnInventoryChanged (na replicatie) reconcilieert vanzelf.
			if (Owner.IsValid()) { Owner->OptimisticGridSwap(Op->FromCell, GridCell); }
			return true;
		}
		else if (Op->FromSlot >= 0)
		{
			// Vanaf de hotbar in het rooster gesleept -> snelkoppeling van de hotbar halen EN het item op
			// de losgelaten cel zetten (anders verschijnt 'ie weer op z'n oude/eerste-vrije rooster-cel).
			Inv->UnassignHotbarStack(Op->StackId);
			Inv->MoveStackToCell(Op->StackId, GridCell);
			// Optimistisch: toon het item METEEN in de doel-cel (geen server-round-trip "plop"). GEEN MarkDirty:
			// OnInventoryChanged reconcilieert straks (sig matcht -> RebuildContent slaat de cel over).
			if (Owner.IsValid()) { Owner->OptimisticFillCell(GridCell, Op->StackId); }
			return true;
		}
	}
	if (Owner.IsValid()) { Owner->MarkDirty(); }
	return true;
}

// ---------------------------------------------------------------------------
//  UInvPopupHost — los viewport-widget dat de split/merge-popups host (hoge ZOrder)
// ---------------------------------------------------------------------------
TSharedRef<SWidget> UInvPopupHost::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		HostCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PopupHostRoot"));
		WidgetTree->RootWidget = HostCanvas;
	}
	// Klik-transparant waar geen popup is; de popups zelf zijn Visible en vangen hun eigen klikken.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return Super::RebuildWidget();
}

// ---------------------------------------------------------------------------
//  UInventoryWidget
// ---------------------------------------------------------------------------
void UInventoryWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

UCanvasPanel* UInventoryWidget::EnsurePopupHost()
{
	if (PopupHost && PopupHost->HostCanvas) { return PopupHost->HostCanvas; }
	APlayerController* PC = GetOwningPlayer();
	if (!PC) { return nullptr; }
	PopupHost = CreateWidget<UInvPopupHost>(PC, UInvPopupHost::StaticClass());
	if (!PopupHost) { return nullptr; }
	PopupHost->AddToViewport(60); // boven pauze(40)/store(33)/droogrek(32)/schap(31) -> popup ligt bovenop alles
	// De popup-panelen éénmalig in de host-canvas bouwen (idempotent: SplitRoot/MergeRoot pas daarna gezet).
	if (PopupHost->HostCanvas)
	{
		if (!SplitRoot) { BuildSplitPopup(PopupHost->HostCanvas); }
		if (!MergeRoot) { BuildMergePopup(PopupHost->HostCanvas); }
	}
	return PopupHost->HostCanvas;
}

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

bool UInventoryWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Vangnet voor drops in de GAPS van het rooster (tussen/net naast de cellen): snap naar de dichtstbijzijnde
	// cel binnen de drempel en handel af alsof je daar dropte. Dit draait ALLEEN als geen enkele cel de drop
	// zelf consumeerde (Slate stopt het bubbelen bij een handled drop) -> geen dubbele afhandeling mogelijk.
	TArray<UInvCell*> Cells;
	Cells.Reserve(CellBoxes.Num());
	for (USizeBox* B : CellBoxes)
	{
		if (UInvCell* C = B ? Cast<UInvCell>(B->GetContent()) : nullptr) { Cells.Add(C); }
	}
	if (UInvCell* Nearest = UInvCell::FindNearestCell(Cells, InDragDropEvent.GetScreenSpacePosition()))
	{
		return Nearest->HandleDropOp(InOperation);
	}
	// Verder weg dan de drempel: bestaand gedrag (onafgehandeld -> drag-cancel -> stapel op de grond droppen).
	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

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
	DetailsIconBox->SetWidthOverride(78.f); DetailsIconBox->SetHeightOverride(78.f); // iets kleiner: knop past binnen het paneel
	{ UVerticalBoxSlot* DIS = DetailsVB->AddChildToVerticalBox(DetailsIconBox); DIS->SetHorizontalAlignment(HAlign_Center); DIS->SetPadding(FMargin(0.f, 6.f, 0.f, 8.f)); }
	// Type-tag (klein, hoofdletters, bold + gekleurd + dunne outline) - exact zoals de hand-preview.
	DetailsType = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColGood(), false, true);
	{
		FSlateFontInfo TagFont = WeedUI::Font(11, true);
		TagFont.OutlineSettings.OutlineSize = 1;
		TagFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);
		DetailsType->SetFont(TagFont);
	}
	DetailsType->SetAutoWrapText(true);
	DetailsVB->AddChildToVerticalBox(DetailsType)->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
	DetailsName = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::Hex(0xF1EAFE), false, true);
	DetailsName->SetAutoWrapText(true);
	DetailsVB->AddChildToVerticalBox(DetailsName)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	DetailsQty = WeedUI::Text(WidgetTree, TEXT(""), 20, WeedUI::ColGood(), false, true);
	DetailsVB->AddChildToVerticalBox(DetailsQty)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	// Dun scheidingslijntje.
	DetailsDivider = WidgetTree->ConstructWidget<UBorder>();
	DetailsDivider->SetBrush(WeedUI::Rounded(WeedUI::ColStroke(0.6f), 1.f));
	DetailsDivider->SetPadding(FMargin(0.f, 0.7f, 0.f, 0.7f));
	DetailsVB->AddChildToVerticalBox(DetailsDivider)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	// Twee-koloms stat-rijen (label links dim, waarde rechts) - herbouwd bij een echte itemwissel. In een
	// SCROLLBOX met Fill-hoogte: bij veel stat-rijen (of een lange naam erboven) scrollt DIT deel i.p.v. de
	// "Split stack"-knop buiten het paneel te duwen (speler-klacht: knop viel er soms buiten). De knop staat
	// hieronder als vaste Auto-voet -> altijd zichtbaar binnen het paneel.
	UScrollBox* StatScroll = WidgetTree->ConstructWidget<UScrollBox>();
	DetailsStatBox = WidgetTree->ConstructWidget<UVerticalBox>();
	StatScroll->AddChild(DetailsStatBox);
	DetailsVB->AddChildToVerticalBox(StatScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	// GEEN hint-regel in de inv quick-view: die beschrijvende tekst staat al in de hand-preview (als je het
	// item vasthoudt). Weglaten = minder ruis + scheelt hoogte zodat de "Split stack"-knop binnen het paneel past.
	// (DetailsHint blijft null -> de SetText in ShowItemDetails is een no-op.)
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

	// Hint onderaan: alleen de nuttige split-hint (klein/dim); de sleep-uitleg is weg (spreekt voor zich).
	Right->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Shift+click = split"), 10, FLinearColor(0.5f, 0.54f, 0.66f)))
		->SetPadding(FMargin(2.f, 8.f, 0.f, 0.f));

	// De split/merge-popups worden NIET meer hier (in de inventory-card) gebouwd -> ze zouden anders achter
	// naast-openstaande panelen (schap/droogrek/store) vallen. Ze leven in een losse popup-host op hoge
	// viewport-ZOrder; die + de popups worden lazy gebouwd bij de eerste popup (EnsurePopupHost).
}

void UInventoryWidget::BuildSplitPopup(UCanvasPanel* Root)
{
	// De popup leeft in de popup-HOST-tree (los viewport-widget op hoge ZOrder), niet in de inventory-card.
	UWidgetTree* Tree = (PopupHost && PopupHost->WidgetTree) ? PopupHost->WidgetTree.Get() : WidgetTree.Get();
	UBorder* Panel = Tree->ConstructWidget<UBorder>();
	{ FSlateBrush CardBr = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 14.f); CardBr.OutlineSettings.Width = 1.f; CardBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f)); Panel->SetBrush(CardBr); }
	Panel->SetPadding(FMargin(18.f));

	UVerticalBox* VB = Tree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);
	VB->AddChildToVerticalBox(WeedUI::Text(Tree, TEXT("Stapel splitsen"), 15, WeedUI::ColAccent(), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	SplitLabel = WeedUI::Text(Tree, TEXT(""), 13, WeedUI::ColText(), true);
	VB->AddChildToVerticalBox(SplitLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	SplitSlider = Tree->ConstructWidget<USlider>();
	SplitSlider->SetMinValue(0.f); SplitSlider->SetMaxValue(1.f); SplitSlider->SetValue(0.5f);
	SplitSlider->SetSliderHandleColor(WeedUI::ColAccent());
	SplitSlider->SetSliderBarColor(WeedUI::ColStroke());
	SplitSlider->OnValueChanged.AddDynamic(this, &UInventoryWidget::OnSplitSliderChanged);
	VB->AddChildToVerticalBox(SplitSlider)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UHorizontalBox* Btns = Tree->ConstructWidget<UHorizontalBox>();
	UWeedActionButton* Conf = TileButton(Tree, WeedUI::ColAccent(), 8.f, [this]() { ConfirmSplit(); });
	Conf->SetContent(WeedUI::Text(Tree, TEXT("Splitsen"), 12, WeedUI::ColText(), true));
	UWeedActionButton* Canc = TileButton(Tree, WeedUI::ColInner(), 8.f, [this]() { CancelSplit(); });
	Canc->SetContent(WeedUI::Text(Tree, TEXT("Cancel"), 12, WeedUI::ColText(), true));
	Btns->AddChildToHorizontalBox(Conf)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* CS2 = Btns->AddChildToHorizontalBox(Canc);
	CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	VB->AddChildToVerticalBox(Btns);

	USizeBox* Sz = Tree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(300.f);
	Sz->SetContent(Panel);
	SplitRoot = Sz;

	UCanvasPanelSlot* PS = Root->AddChildToCanvas(Sz);
	PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PS->SetAlignment(FVector2D(0.5f, 0.5f));
	PS->SetAutoSize(true);
	PS->SetPosition(FVector2D(0.f, -30.f));
	PS->SetZOrder(50); // binnen de host bovenaan (de host zelf ligt al op viewport-ZOrder 60)
	Sz->SetVisibility(ESlateVisibility::Collapsed);
}

void UInventoryWidget::ShowItemDetails(UInvCell* Cell)
{
	if (!Cell || Cell->StackId == 0 || !DetailsContent || !StashContent) { return; }
	DetailsStackId = Cell->StackId;
	if (DetailsIconBox) { DetailsIconBox->SetContent(WeedUI::ItemIcon(WidgetTree, Cell->IconId, 84.f, Cell->WaterOverride)); }
	// Volledige naam (Line1 is in de cel afgekapt met "..."; hier is wél ruimte).
	if (DetailsName) { DetailsName->SetText(FText::FromString(Cell->IconId.IsNone() ? Cell->Line1 : WeedUI::PrettyItemName(Cell->IconId))); }

	// Detail-DATA uit de gedeelde bron (zelfde als de hand-preview) -> nette type-tag, twee-koloms stats en hint.
	// Thc/QualPct + aantal uit DEZE stack halen (per-slot, via de StackId van de cel).
	const FName Id = Cell->IconId;
	const FString IdStr = Id.ToString();
	int32 Qty = 0; float Thc = 0.f; float QualPct = 0.f;
	if (UInventoryComponent* Inv = GetInv())
	{
		const int32 Idx = Inv->FindStackById(Cell->StackId);
		const TArray<FInventoryStack>& St = Inv->GetStacks();
		if (St.IsValidIndex(Idx))
		{
			Qty = St[Idx].Quantity;
			Thc = St[Idx].Quality;
			QualPct = St[Idx].QualityPct;
		}
	}
	const WeedUI::FItemDetailInfo Detail = WeedUI::BuildItemDetail(this, Id, Qty, Thc, QualPct);

	// Type-tag: gekleurde categorie-tag (bold + outline blijven staan uit BuildShell).
	if (DetailsType)
	{
		DetailsType->SetText(FText::FromString(Detail.Type));
		DetailsType->SetColorAndOpacity(FSlateColor(Detail.TypeColor));
	}

	// Aantal groot bij de titel: gram voor wiet, "Nx Xg" voor zakjes, anders "xN". Voor gereedschap/plaatsbare
	// dingen (fles, pot, rek, bench, meubels) is een aantal zinloos -> verbergen (zelfde regels als de hand-preview).
	if (DetailsQty)
	{
		const bool bBottle = IdStr.StartsWith(TEXT("WaterBottle"));
		const bool bEquip = IsPotItem(Id)
			|| IdStr.StartsWith(TEXT("DryRack_")) || IdStr.StartsWith(TEXT("Bench_"))
			|| IdStr.StartsWith(TEXT("Lamp")) || IdStr.StartsWith(TEXT("Tent"))
			|| Id == TEXT("Shelf") || Id == TEXT("Chest") || Id == TEXT("Table")
			|| Id == TEXT("Mattress") || Id == TEXT("Fridge") || Id == TEXT("Atm");
		if (bBottle || bEquip)
		{
			DetailsQty->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			DetailsQty->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UInventoryComponent::IsBag(Id))
			{
				DetailsQty->SetText(FText::FromString(WeedUI::ItemQtyBadge(Id, Qty))); // "Nx Xg"
			}
			else
			{
				const bool bGrams = IdStr.StartsWith(TEXT("WetBud_")) || IdStr.StartsWith(TEXT("Bud_"));
				DetailsQty->SetText(FText::FromString(bGrams ? FString::Printf(TEXT("%d g"), Qty) : FString::Printf(TEXT("x%d"), Qty)));
			}
			DetailsQty->SetColorAndOpacity(FSlateColor(Detail.TypeColor));
		}
	}

	// Twee-koloms stat-rijen (label links dim, waarde rechts) - identiek aan de hand-preview's AddStat.
	// Herbouw bij een echte itemwissel (net als de hand-preview die z'n StatBox bij een sleutel-wissel herbouwt);
	// dit is GEEN per-tick/klik-rebuild van de hele lijst, dus geen schending van de persistente-UI-regel.
	if (DetailsStatBox)
	{
		DetailsStatBox->ClearChildren();
		auto AddStat = [this](const FString& Label, const FString& Value)
		{
			UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* L = WeedUI::Text(WidgetTree, Label, 12, WeedUI::ColTextDim());
			UHorizontalBoxSlot* LS2 = RowH->AddChildToHorizontalBox(L);
			LS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS2->SetVerticalAlignment(VAlign_Center);
			UTextBlock* V = WeedUI::Text(WidgetTree, Value, 13, WeedUI::ColText(), false, true);
			V->SetJustification(ETextJustify::Right);
			UHorizontalBoxSlot* VS = RowH->AddChildToHorizontalBox(V);
			VS->SetVerticalAlignment(VAlign_Center);
			DetailsStatBox->AddChildToVerticalBox(RowH)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		};
		for (const TPair<FString, FString>& Stat : Detail.Stats) { AddStat(Stat.Key, Stat.Value); }
	}

	// Korte hint (dim) onderaan.
	if (DetailsHint) { DetailsHint->SetText(FText::FromString(Detail.Hint)); }

	if (DetailsSplitBtn) { DetailsSplitBtn->SetVisibility(Cell->bDraggable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	StashContent->SetVisibility(ESlateVisibility::Collapsed);
	DetailsContent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UInventoryWidget::BeginDragGhost(UInvCell* Cell)
{
	if (!Cell) { return; }
	Cell->SetRenderOpacity(0.3f); // bron-cel half-transparant zolang je sleept
	DragGhostCell = Cell;
}

void UInventoryWidget::OptimisticGridSwap(int32 CellA, int32 CellB)
{
	// Bij een grid->grid-drop: wissel de twee cel-widgets FYSIEK om i.p.v. te wachten op de server-round-trip
	// + reconstructie. Het item staat zo DIRECT in de nieuwe cel (geen "plop"-flikker). We hergebruiken de
	// bestaande widgets (geen ConstructWidget) en wisselen de signaturen mee, zodat RebuildContent na de
	// server-bevestiging exact dezelfde staat ziet -> die cellen overslaat -> naadloos.
	if (CellA == CellB || !CellBoxes.IsValidIndex(CellA) || !CellBoxes.IsValidIndex(CellB)) { return; }
	USizeBox* BA = CellBoxes[CellA];
	USizeBox* BB = CellBoxes[CellB];
	if (!BA || !BB) { return; }

	UWidget* WA = BA->GetContent();
	UWidget* WB = BB->GetContent();
	if (WA) { WA->RemoveFromParent(); }
	if (WB) { WB->RemoveFromParent(); }
	BA->SetContent(WB);
	BB->SetContent(WA);

	// Verplaatste cellen: GridCell bijwerken (drag/drop blijft kloppen vóór de reconcile) + de sleep-dim opheffen.
	if (UInvCell* CWB = Cast<UInvCell>(WB)) { CWB->GridCell = CellA; CWB->SetRenderOpacity(1.f); }
	if (UInvCell* CWA = Cast<UInvCell>(WA)) { CWA->GridCell = CellB; CWA->SetRenderOpacity(1.f); }

	if (CellSigs.IsValidIndex(CellA) && CellSigs.IsValidIndex(CellB)) { CellSigs.Swap(CellA, CellB); }
	DragGhostCell.Reset(); // ghost is hersteld (opacity 1) -> niet meer in NativeTick aanraken
}

void UInventoryWidget::OpenSplitPopup(int32 StackId)
{
	UInventoryComponent* I = GetInv();
	if (!I) { return; }
	EnsurePopupHost(); // popup-host + panelen lazy bouwen (bovenop naast-openstaande panelen)
	if (!SplitRoot || !SplitSlider) { return; }
	const int32 Idx = I->FindStackById(StackId);
	const TArray<FInventoryStack>& St = I->GetStacks();
	if (!St.IsValidIndex(Idx)) { return; }
	// Cash splitst/dropt nu als elk ander item: splitsen via deze popup, droppen door de stapel de
	// inventory uit te slepen (ServerDropStack spawnt een oppakbaar geldstapeltje + boekt het saldo af).
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
	SplitLabel->SetText(FText::FromString(FString::Printf(TEXT("Split off: %d   (of %d)"), Amount, SplitTotal)));
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

void UInventoryWidget::BuildMergePopup(UCanvasPanel* Root)
{
	// De popup leeft in de popup-HOST-tree (los viewport-widget op hoge ZOrder), niet in de inventory-card.
	UWidgetTree* Tree = (PopupHost && PopupHost->WidgetTree) ? PopupHost->WidgetTree.Get() : WidgetTree.Get();
	UBorder* Panel = Tree->ConstructWidget<UBorder>();
	{ FSlateBrush CardBr = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 14.f); CardBr.OutlineSettings.Width = 1.f; CardBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f)); Panel->SetBrush(CardBr); }
	Panel->SetPadding(FMargin(18.f));

	UVerticalBox* VB = Tree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);
	VB->AddChildToVerticalBox(WeedUI::Text(Tree, TEXT("Stapels samenvoegen"), 15, WeedUI::ColAccent(), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	MergeLabel = WeedUI::Text(Tree, TEXT(""), 13, WeedUI::ColText(), true);
	VB->AddChildToVerticalBox(MergeLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UHorizontalBox* Btns = Tree->ConstructWidget<UHorizontalBox>();
	UWeedActionButton* Conf = TileButton(Tree, WeedUI::ColAccent(), 8.f, [this]() { ConfirmMerge(); });
	Conf->SetContent(WeedUI::Text(Tree, TEXT("Samenvoegen"), 12, WeedUI::ColText(), true));
	UWeedActionButton* Canc = TileButton(Tree, WeedUI::ColInner(), 8.f, [this]() { CancelMerge(); });
	Canc->SetContent(WeedUI::Text(Tree, TEXT("Cancel"), 12, WeedUI::ColText(), true));
	Btns->AddChildToHorizontalBox(Conf)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* CS2 = Btns->AddChildToHorizontalBox(Canc);
	CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	VB->AddChildToVerticalBox(Btns);

	USizeBox* Sz = Tree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(320.f);
	Sz->SetContent(Panel);
	MergeRoot = Sz;

	UCanvasPanelSlot* PS = Root->AddChildToCanvas(Sz);
	PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PS->SetAlignment(FVector2D(0.5f, 0.5f));
	PS->SetAutoSize(true);
	PS->SetPosition(FVector2D(0.f, -30.f));
	PS->SetZOrder(50); // binnen de host bovenaan (de host zelf ligt al op viewport-ZOrder 60)
	Sz->SetVisibility(ESlateVisibility::Collapsed);
}

bool UInventoryWidget::TryMergeOrConfirm(int32 IntoStackId, int32 FromStackId)
{
	UInventoryComponent* I = GetInv();
	if (!I || IntoStackId == 0 || FromStackId == 0 || IntoStackId == FromStackId) { return false; }
	EnsurePopupHost(); // zorg dat de bevestig-popup bestaat (+ op de bovenste laag) vóór we 'm nodig hebben
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
	// Pool + per-rij signatuur (zelfde patroon als RebuildContent): GEEN ClearChildren meer bij een
	// schap-mutatie — alleen rijen waarvan de signatuur ECHT wijzigde krijgen nieuwe inhoud, de rest
	// blijft staan. Voorkomt de volledige stash-flikker bij elke schap-mutatie (co-op: partner muteert
	// een schap; solo: eigen mutaties terwijl de inventory open staat).
	if (!StashList) { return; }

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
		// COMPETITIVE: de stash telt ALLE schappen op zonder eigenaar-filter -> elke speler zou de opslag van
		// de tegenstander zien. Filter daarom op de EIGEN kamer/woning-box van de lokale speler (IsInOwnedHome
		// dekt zowel de pack-map huis-box als de gespiegelde competitive-kamer). Buiten competitive ongewijzigd:
		// de stash is dan bewust gedeeld (gewone co-op = samen 1 huis).
		const AWeedShopGameState* GScomp = W->GetGameState<AWeedShopGameState>();
		const bool bComp = GScomp && GScomp->IsCompetitive();
		UBuildComponent* LocalBuild = nullptr;
		if (bComp)
		{
			if (APawn* LP = GetOwningPlayerPawn())
			{
				LocalBuild = LP->FindComponentByClass<UBuildComponent>();
			}
		}
		for (TActorIterator<AStorageShelf> It(W); It; ++It)
		{
			// In competitive: sla schappen over die niet in de eigen kamer/woning van de lokale speler staan.
			if (bComp && LocalBuild && !LocalBuild->IsInOwnedHome(It->GetActorLocation())) { continue; }
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

	// Wiet eerst (Bud/Bag/Wet/Joint), daarna de rest; binnen groepen op naam. (Lege lijst = no-op.)
	auto IsWeed = [](FName Id) { const FString S = Id.ToString(); return S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("Bag_")) || S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Joint_")); };
	Order.Sort([&](const FName& A, const FName& B)
	{
		const bool wa = IsWeed(A), wb = IsWeed(B);
		if (wa != wb) { return wa; }
		return WeedUI::PrettyItemName(A) < WeedUI::PrettyItemName(B);
	});

	// Persistente lege-staat-regel: 1x aanmaken (als eerste kind), daarna alleen tonen/verbergen.
	if (!StashEmptyText)
	{
		StashEmptyText = WeedUI::Text(WidgetTree, TEXT("Nothing stored.\nPut weed in a shelf/chest."), 11, WeedUI::ColTextDim());
		StashList->AddChild(StashEmptyText);
	}
	StashEmptyText->SetVisibility(Order.Num() == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// Staart-groei: alleen ontbrekende rij-slots toevoegen (structureel; bestaande slots blijven staan).
	while (StashRows.Num() < Order.Num())
	{
		USizeBox* B = WidgetTree->ConstructWidget<USizeBox>();
		StashList->AddChild(B);
		StashRows.Add(B);
		StashSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}
	// Staart-krimp: overtollige slots alleen verbergen (inhoud + sig blijven staan; groeit de lijst later
	// terug naar hetzelfde item, dan matcht de sig en staat de rij zonder herbouw meteen goed).
	for (int32 i = Order.Num(); i < StashRows.Num(); ++i)
	{
		if (StashRows[i] && StashRows[i]->GetVisibility() != ESlateVisibility::Collapsed) { StashRows[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}

	for (int32 i = 0; i < Order.Num(); ++i)
	{
		if (!StashRows.IsValidIndex(i) || !StashRows[i]) { continue; }
		if (StashRows[i]->GetVisibility() != ESlateVisibility::Visible) { StashRows[i]->SetVisibility(ESlateVisibility::Visible); }

		const FName Id = Order[i];
		const int32 N = Qty[Id];
		const FString IdStr = Id.ToString();
		const bool bWeed = IsWeed(Id);
		const bool bWet = IdStr.StartsWith(TEXT("WetBud_"));
		const float Thc = (N > 0) ? (ThcW[Id] / N) : 0.f;

		// Per-rij signatuur: alles wat de rij toont volgt uit Id/N/Thc (naam, kleur, badge, grammen).
		// Onveranderd -> rij met rust laten (geen herbouw, geen flikker) — zelfde idee als GridCellSig.
		const FString Sig = FString::Printf(TEXT("%s|%d|%.1f"), *IdStr, N, Thc);
		if (StashSigs.IsValidIndex(i) && Sig == StashSigs[i]) { continue; } // niets veranderd aan deze rij
		if (StashSigs.IsValidIndex(i)) { StashSigs[i] = Sig; }

		UBorder* Row = WidgetTree->ConstructWidget<UBorder>();
		Row->SetBrush(WeedUI::Rounded(WeedUI::ColInner(0.85f), 6.f));
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
		const FLinearColor NameCol = bWet ? FLinearColor(0.55f, 0.8f, 1.f) : (bWeed ? FLinearColor(0.7f, 1.f, 0.75f) : WeedUI::ColText()); // nat=blauw / wiet=groen (semantisch), rest=palet
		RVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Nm, 12, NameCol));

		FString Sub;
		if (bWeed)
		{
			// Zakjes tellen in GRAMMEN (aantal x gram-per-zakje), los gedroogd/nat = 1g per stuk.
			const int32 Grams = UInventoryComponent::IsBag(Id) ? N * FMath::Max(1, UInventoryComponent::BagGrams(Id)) : N;
			Sub = FString::Printf(TEXT("%dg   %.0f%% THC%s"), Grams, Thc, bWet ? TEXT("  (wet)") : TEXT(""));
		}
		else { Sub = FString::Printf(TEXT("x%d"), N); }
		RVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Sub, 10, WeedUI::ColTextDim()));

		// De rij + het smalle spacer-regeltje eronder samen in het vaste rij-slot (voorheen twee losse
		// ScrollBox-kinderen; visueel identiek).
		UVerticalBox* Wrap = WidgetTree->ConstructWidget<UVerticalBox>();
		Wrap->AddChildToVerticalBox(Row);
		Wrap->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
		StashRows[i]->SetContent(Wrap);
	}
}

UInvCell* UInventoryWidget::BuildGridCellWidget(int32 cell, int32 StackId, bool bShowItem, const FInventoryStack* SPtr, UInventoryComponent* Inv, UPhoneClientComponent* Ph)
{
	// Bouwt één grid-cel (item óf leeg). GEDEELD door RebuildContent en de optimistische drop-update, zodat een
	// optimistisch geplaatste cel IDENTIEK is aan wat RebuildContent zou bouwen -> naadloze reconcile (geen flikker).
	UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
	Cell->SlotIndex = -1; Cell->GridCell = cell;
	Cell->Inv = Inv; Cell->Owner = this;
	Cell->IconSize = 68.f;
	if (bShowItem && SPtr)
	{
		const FInventoryStack& S = *SPtr;
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

		// In de cel kappen we te lange namen af met een ellips; het details-paneel/de tooltip tonen
		// de volledige naam zelf (Tooltip = info-BODY zonder naam, anders stond de naam er 2x).
		const FString FullName = WeedUI::PrettyItemName(ItemId);
		Cell->Line1 = (FullName.Len() > 20) ? (FullName.Left(19) + TEXT("...")) : FullName;
		Cell->Tag = WeedUI::ItemTagShort(ItemId); // korte strain/rank-code in de bubble onderaan de cel

		if (bCash)
		{
			Cell->Bg = FLinearColor(0.09f, 0.14f, 0.09f, 0.97f);
			// Toon het bedrag van DEZE stapel (cash kan gesplitst zijn); het totaal = de som van de stapels.
			const int64 Euros = static_cast<int64>(S.Quantity);
			Cell->Line2 = FString::Printf(TEXT("EUR %lld"), (long long)Euros);
			Cell->Badge = (Euros >= 1000) ? FString::Printf(TEXT("%.0fk"), Euros / 1000.0) : FString::Printf(TEXT("%lld"), (long long)Euros);
			Cell->Tooltip = FString::Printf(TEXT("EUR %lld contant"), (long long)Euros);
		}
		else if (bWet)
		{
			Cell->Bg = FLinearColor(0.09f, 0.14f, 0.21f, 0.97f);
			Cell->Line2 = TEXT("WET - dry it first");
			Cell->Badge = FString::Printf(TEXT("%dg"), S.Quantity);
			Cell->Tooltip = WeedUI::ItemInfoBody(ItemId, S.Quantity, S.Quality, S.QualityPct);
			Cell->Tooltip += FString::Printf(TEXT("\nWeight %.1f"), Inv->GetUnitWeight(ItemId) * S.Quantity);
		}
		else
		{
			Cell->Bg = WeedUI::Hex(0x3A4152, 0.96f);
			Cell->Line2 = bWeed
				? FString::Printf(TEXT("THC %.0f%%  Q %.0f%%"), S.Quality, S.QualityPct)
				: TEXT("");
			Cell->Badge = WeedUI::ItemQtyBadge(ItemId, S.Quantity);
			Cell->Tooltip = WeedUI::ItemInfoBody(ItemId, S.Quantity, S.Quality, S.QualityPct);
			// (body kan leeg zijn voor items zonder catalogus-omschrijving -> geen kale newline ervoor)
			Cell->Tooltip += FString::Printf(TEXT("%sWeight %.1f"), Cell->Tooltip.IsEmpty() ? TEXT("") : TEXT("\n"), Inv->GetUnitWeight(ItemId) * S.Quantity);
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
	return Cell;
}

// Signatuur van een grid-cel (zelfde formule die RebuildContent gebruikt) - gedeeld met de optimistische update
// zodat de geoptimiseerde cel en de na-server-rebuild exact dezelfde sig hebben -> RebuildContent slaat 'm over.
FString UInventoryWidget::GridCellSig(int32 StackId, bool bShowItem, const FInventoryStack* SPtr, UInventoryComponent* Inv) const
{
	if (!bShowItem || !SPtr || !Inv) { return TEXT("E"); }
	const FInventoryStack& Sg = *SPtr;
	// (Cash toont per stapel z'n eigen Quantity -> die zit al in de sig; geen aparte economy-lookup meer.)
	const int32 WaterLv = Sg.ItemId.ToString().StartsWith(TEXT("WaterBottle")) ? FMath::RoundToInt(Sg.Quality) : -1;
	return FString::Printf(TEXT("I|%s|%d|%.1f|%.1f|%d|%d"), *Sg.ItemId.ToString(), Sg.Quantity, Sg.Quality, Sg.QualityPct, WaterLv, Inv->CountStacksOf(Sg.ItemId));
}

// Optimistische drop-update: zet METEEN een item-cel (Fill) of lege cel (Clear) neer zonder op de server-round-trip
// te wachten. Sig wordt mee gezet zodat RebuildContent (na server-bevestiging) de cel ongemoeid laat -> geen flikker.
void UInventoryWidget::OptimisticFillCell(int32 cell, int32 StackId)
{
	UInventoryComponent* Inv = GetInv();
	if (!Inv || !CellBoxes.IsValidIndex(cell)) { return; }
	const int32 Idx = Inv->FindStackById(StackId);
	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
	if (!Stacks.IsValidIndex(Idx)) { return; }
	const FInventoryStack& S = Stacks[Idx];
	if (CellBoxes[cell]) { CellBoxes[cell]->SetContent(BuildGridCellWidget(cell, StackId, true, &S, Inv, PhoneComp.Get())); }
	if (CellSigs.IsValidIndex(cell)) { CellSigs[cell] = GridCellSig(StackId, true, &S, Inv); }
	DragGhostCell.Reset();
}

void UInventoryWidget::OptimisticClearCell(int32 cell)
{
	if (!CellBoxes.IsValidIndex(cell) || !CellBoxes[cell]) { return; }
	CellBoxes[cell]->SetContent(BuildGridCellWidget(cell, 0, false, nullptr, GetInv(), PhoneComp.Get()));
	if (CellSigs.IsValidIndex(cell)) { CellSigs[cell] = TEXT("E"); }
	DragGhostCell.Reset();
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
		const FInventoryStack* SPtr = bShowItem ? &Stacks[Idx] : nullptr;

		// Signatuur van de zichtbare cel-staat: onveranderd -> cel met rust laten (geen rebuild, geen flikker).
		// ZELFDE formule als de optimistische updates (GridCellSig) -> reconcile blijft naadloos.
		const FString Sig = GridCellSig(StackId, bShowItem, SPtr, Inv);
		if (!CellSigs.IsValidIndex(cell) || !CellBoxes.IsValidIndex(cell)) { continue; }
		if (Sig == CellSigs[cell]) { continue; } // niets veranderd aan deze cel
		CellSigs[cell] = Sig;

		// Gedeelde cel-bouw (zelfde code als de optimistische drop-update) -> geen dubbele opbouw-logica meer.
		CellBoxes[cell]->SetContent(BuildGridCellWidget(cell, StackId, bShowItem, SPtr, Inv, Ph));
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

	// Drag-ghost herstellen zodra het slepen klaar is (drop OF cancel) -> bron-cel weer vol zichtbaar.
	if (DragGhostCell.IsValid() && !FSlateApplication::Get().IsDragDropping())
	{
		DragGhostCell->SetRenderOpacity(1.f);
		DragGhostCell.Reset();
	}

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
		// Popups horen bij de open inventory: sluit ze mee (ze leven op een los viewport-widget en zouden
		// anders blijven hangen als je de inventory dicht doet terwijl een popup open staat).
		if ((SplitRoot && SplitRoot->GetVisibility() != ESlateVisibility::Collapsed)) { CancelSplit(); }
		if ((MergeRoot && MergeRoot->GetVisibility() != ESlateVisibility::Collapsed)) { CancelMerge(); }
		return;
	}

	// Naast een gekoppeld paneel (droogrek): HOME STASH verbergen (puur preview), card smaller, en het paar
	// dicht bij elkaar in het midden (rek rechts-van-midden, inventory links-van-midden). Anders: normaal gecentreerd.
	const bool bSideBySide = PhoneComp.IsValid() && (PhoneComp->IsDryRackOpen() || PhoneComp->IsShelfOpen());
	// Layoutblok alleen bij een echte wissel (changed-check) — de canvas-slot-properties blijven staan.
	if (LastSideBySide != (bSideBySide ? 1 : 0))
	{
		LastSideBySide = bSideBySide ? 1 : 0;
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
	}

	// HOME STASH: alleen herbouwen als de shelf-inhoud ECHT veranderde (niet bij elke grid-sleep). Goedkope
	// signatuur i.p.v. de volledige widget-herbouw + wereld-scan elke keer (dat liet de halve inventory flikkeren).
	// Perf: de shelf-SET (wereld-scan) wordt 1x/s ververst; de INHOUD-sig leest per tick vers uit de cache.
	if (!bSideBySide)
	{
		ShelfSetAge += DeltaTime;
		if (ShelfSetAge >= 1.f)
		{
			ShelfSetAge = 0.f;
			CachedShelves.Reset();
			if (UWorld* W = GetWorld())
			{
				for (TActorIterator<AStorageShelf> It(W); It; ++It) { CachedShelves.Add(*It); }
			}
		}
		FString SSig;
		for (const TWeakObjectPtr<AStorageShelf>& WkS : CachedShelves)
		{
			const AStorageShelf* Shelf = WkS.Get();
			if (!Shelf) { continue; }
			for (const FShelfStack& S : Shelf->Contents) { SSig.Appendf(TEXT("%s%d|"), *S.ItemId.ToString(), S.Quantity); }
		}
		if (SSig != LastStashSig) { LastStashSig = SSig; RebuildStash(); }
	}

	if (bDirty)
	{
		bDirty = false;
		RebuildContent();
	}
}

void UInventoryWidget::NativeDestruct()
{
	// De popup-host is een LOS viewport-widget (niet in onze widget-tree) -> zelf opruimen bij destruct
	// (map-load/teardown), anders blijft 'ie op de viewport hangen.
	if (PopupHost) { PopupHost->RemoveFromParent(); PopupHost = nullptr; }
	Super::NativeDestruct();
}

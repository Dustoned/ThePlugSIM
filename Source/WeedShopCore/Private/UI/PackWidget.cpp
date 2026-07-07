#include "UI/PackWidget.h"

#include "UI/WeedUiStyle.h"
#include "UI/WeedItemPickGrid.h"
#include "Phone/PhoneClientComponent.h"
#include "Inventory/InventoryComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "UI/WeedToast.h"
#include "GameFramework/Pawn.h"

void UPackWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	FButtonStyle PackStyle(const FLinearColor& Col)
	{
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 8.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		S.NormalPadding = FMargin(8.f, 4.f); S.PressedPadding = FMargin(8.f, 4.f);
		return S;
	}

	UWeedActionButton* PackBtn(UWidgetTree* Tree, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		B->SetStyle(PackStyle(Col));
		return B;
	}

	UBorder* PackPanel(UWidgetTree* Tree)
	{
		UBorder* B = Tree->ConstructWidget<UBorder>();
		FSlateBrush Br = WeedUI::StorageSlotBrushWithFill(WeedUI::ColInner(0.72f), true, false, WeedUI::ColStroke(0.34f), 8.f);
		Br.OutlineSettings.Width = 1.f;
		Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.32f));
		B->SetBrush(Br);
		B->SetPadding(FMargin(10.f, 8.f, 10.f, 10.f));
		return B;
	}

	UInventoryComponent* GetInv(APawn* P) { return P ? P->FindComponentByClass<UInventoryComponent>() : nullptr; }

	// Vaste container-lijst voor het pack-scherm (zelfde volgorde/labels als voorheen).
	static const TCHAR* const kConts[6] = { TEXT("Cont_Bag2"), TEXT("Cont_Bag5"), TEXT("Cont_Jar10"), TEXT("Cont_Jar15"), TEXT("Cont_Block100"), TEXT("Cont_Garbage500") };

	// Max aantal jobs in de queue tegelijk (lopend + wachtend) -> houdt de bench-kaart hanteerbaar hoog.
	static constexpr int32 kMaxQueue = 8;

	// Basis-inpaktijd (seconden) per container-type: schaalt ongeveer met de capaciteit. Wordt in PackOne
	// nog gedeeld door de bench-snelheid (tier). Klein zakje snel, jar langzamer, block/sack het langst.
	static float PackDurationFor(FName Cont)
	{
		const FString S = Cont.ToString();
		if (S == TEXT("Cont_Bag2"))      { return 1.2f; }
		if (S == TEXT("Cont_Bag5"))      { return 2.0f; }
		if (S == TEXT("Cont_Jar10"))     { return 4.5f; }  // 50g
		if (S == TEXT("Cont_Jar15"))     { return 7.5f; }  // 100g
		if (S == TEXT("Cont_Block100"))  { return 13.0f; } // 250g
		if (S == TEXT("Cont_Garbage500")){ return 22.0f; } // 500g
		return 3.0f;
	}

	// Zelfstandig naamwoord per container-type (voor nette labels: "Pack 3 jars" i.p.v. "bags").
	static const TCHAR* ContainerNoun(FName Cont)
	{
		const FString S = Cont.ToString();
		if (S == TEXT("Cont_Jar10") || S == TEXT("Cont_Jar15")) { return TEXT("jar"); }
		if (S == TEXT("Cont_Block100"))                         { return TEXT("block"); }
		if (S == TEXT("Cont_Garbage500"))                       { return TEXT("sack"); }
		return TEXT("bag");
	}

	// Bouwt de zichtbare inhoud van een pack-cel: icoon (met badge rechtsboven) + labeltekst eronder. Gedeeld
	// door de sleep-wiet-cellen en de drop-container-cellen zodat beide identiek ogen (vaste celbreedte -> nette rijen).
	UWidget* MakeItemCellContent(UWidgetTree* Tree, FName ItemId, const FString& Badge, const FString& Label)
	{
		UVerticalBox* VB = Tree->ConstructWidget<UVerticalBox>();

		UOverlay* Ov = Tree->ConstructWidget<UOverlay>();
		USizeBox* IconBox = Tree->ConstructWidget<USizeBox>();
		IconBox->SetWidthOverride(62.f); IconBox->SetHeightOverride(62.f);
		IconBox->SetContent(WeedUI::ItemIcon(Tree, ItemId, 62.f));
		Ov->AddChildToOverlay(IconBox);
		if (!Badge.IsEmpty())
		{
			UBorder* Pill = Tree->ConstructWidget<UBorder>();
			Pill->SetBrush(WeedUI::Rounded(WeedUI::ColPanel(0.92f), 6.f));
			Pill->SetPadding(FMargin(4.f, 1.f));
			Pill->SetContent(WeedUI::Text(Tree, Badge, 10, WeedUI::ColAccent(), true, true));
			UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
			PS->SetHorizontalAlignment(HAlign_Right);
			PS->SetVerticalAlignment(VAlign_Top);
		}
		VB->AddChildToVerticalBox(Ov)->SetHorizontalAlignment(HAlign_Center);

		UTextBlock* L = WeedUI::Text(Tree, Label, 10, WeedUI::ColText(), false, true);
		L->SetJustification(ETextJustify::Center);
		L->SetAutoWrapText(true);
		// Vaste labelhoogte -> alle cellen even hoog, tekst blijft binnen de cel (geen uitstekende/oneven rijen).
		USizeBox* LB = Tree->ConstructWidget<USizeBox>();
		LB->SetWidthOverride(96.f); LB->SetMinDesiredHeight(26.f);
		LB->SetContent(L);
		UVerticalBoxSlot* LS = VB->AddChildToVerticalBox(LB);
		LS->SetHorizontalAlignment(HAlign_Center);
		LS->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));

		USizeBox* Wrap = Tree->ConstructWidget<USizeBox>();
		Wrap->SetWidthOverride(100.f);
		Wrap->SetContent(VB);
		return Wrap;
	}
}

// ===================== Sleep-cellen (bron = gedroogde wiet, doel = container) =====================
TSharedRef<SWidget> UPackWeedCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		Frame = B;
		// Solide cel-bg zodat de HELE cel grijpbaar is (niet alleen de icoon-pixel) - zoals de deal-bron-cel.
		B->SetBrush(WeedUI::StorageSlotBrush(true, false, WeedUI::ColAccent(0.6f), 9.f));
		B->SetPadding(FMargin(6.f));
		B->SetHorizontalAlignment(HAlign_Center); B->SetVerticalAlignment(VAlign_Center);
		if (Content) { B->SetContent(Content); }
		WidgetTree->RootWidget = B;
	}
	SetVisibility(ESlateVisibility::Visible); // hit-testbaar: anders start de sleep nooit
	return Super::RebuildWidget();
}

void UPackWeedCell::SetInner(UWidget* W)
{
	Content = W;
	if (Frame) { Frame->SetContent(W); }
}

FReply UPackWeedCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (GramsAvail > 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UPackWeedCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (GramsAvail <= 0 || BudId.IsNone()) { return; }
	UPackDragOp* Op = NewObject<UPackDragOp>(GetTransientPackage(), UPackDragOp::StaticClass());
	Op->BudId = BudId;
	Op->Pivot = EDragPivot::CenterCenter;
	// Sleep-visual = het echte wiet-icoon aan de muis (zelfde patroon als de deal/inventory-drag).
	if (WidgetTree)
	{
		USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
		Vis->SetWidthOverride(58.f); Vis->SetHeightOverride(58.f);
		Vis->SetContent(WeedUI::ItemIcon(WidgetTree, BudId, 58.f));
		Op->DefaultDragVisual = Vis;
	}
	OutOperation = Op;
}

TSharedRef<SWidget> UPackContCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		Frame = B;
		B->SetPadding(FMargin(6.f));
		B->SetHorizontalAlignment(HAlign_Center); B->SetVerticalAlignment(VAlign_Center);
		if (Content) { B->SetContent(Content); }
		WidgetTree->RootWidget = B;
		SetDragHover(false); // basis-brush (accent-kader = "sleep hier naartoe")
	}
	SetVisibility(ESlateVisibility::Visible); // hit-testbaar: anders vangt de cel geen drop
	return Super::RebuildWidget();
}

void UPackContCell::SetDragHover(bool bHover)
{
	if (!Frame) { return; }
	Frame->SetBrush(WeedUI::SelectableSlotBrush(true, bHover, WeedUI::ColAccent(0.95f), 9.f));
}

void UPackContCell::SetInner(UWidget* W)
{
	Content = W;
	if (Frame) { Frame->SetContent(W); }
}

FReply UPackContCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Een verpakte zak (BagId gezet) is sleepbaar -> uitpakken. Een lege container niet.
	if (!BagId.IsNone() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UPackContCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (BagId.IsNone()) { return; } // lege container = niet sleepbaar
	UPackDragOp* Op = NewObject<UPackDragOp>(GetTransientPackage(), UPackDragOp::StaticClass());
	Op->BagId = BagId; // een zak sleep je naar de wiet-kolom links = uitpakken
	Op->Pivot = EDragPivot::CenterCenter;
	if (WidgetTree)
	{
		USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
		Vis->SetWidthOverride(58.f); Vis->SetHeightOverride(58.f);
		Vis->SetContent(WeedUI::ItemIcon(WidgetTree, BagId, 58.f));
		Op->DefaultDragVisual = Vis;
	}
	OutOperation = Op;
}

void UPackContCell::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	// Highlight alleen als je hier iets kunt droppen: WIET op een lege container of een NIET-VOLLE zak.
	if (UPackDragOp* Op = Cast<UPackDragOp>(InOperation))
	{
		if (!Op->BudId.IsNone() && (BagId.IsNone() || !bBagFull)) { SetDragHover(true); }
	}
}

void UPackContCell::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	SetDragHover(false);
}

bool UPackContCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	SetDragHover(false);
	if (UPackDragOp* Op = Cast<UPackDragOp>(InOperation))
	{
		if (Owner.IsValid() && !Op->BudId.IsNone()) // WIET gedropt
		{
			if (!BagId.IsNone()) { if (!bBagFull) { Owner->TopUpDropped(Op->BudId, BagId); } } // niet-volle zak -> bijvullen
			else                 { Owner->PackOne(Op->BudId, ContId); }                        // lege container -> nieuw packen
			return true;
		}
	}
	return false; // een gesleepte ZAK (Op->BagId) hoort hier niet -> laat de wiet-kolom links 'm vangen
}

// --- UPackBagCell (unpack-bron: sleepbare verpakte zak; gepoold + in-place via SetInner) ---
TSharedRef<SWidget> UPackBagCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		Frame = B;
		B->SetBrush(WeedUI::StorageSlotBrush(true, false, WeedUI::ColAccent(0.6f), 9.f));
		B->SetPadding(FMargin(6.f));
		B->SetHorizontalAlignment(HAlign_Center); B->SetVerticalAlignment(VAlign_Center);
		if (Content) { B->SetContent(Content); }
		WidgetTree->RootWidget = B;
	}
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

void UPackBagCell::SetInner(UWidget* W)
{
	Content = W;                          // als de cel nog niet gebouwd is: RebuildWidget pakt dit op
	if (Frame) { Frame->SetContent(W); }  // al gebouwd: meteen in-place swappen (geen herbouw van de cel)
}

FReply UPackBagCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (CountAvail > 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UPackBagCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (CountAvail <= 0 || BagId.IsNone()) { return; }
	UPackDragOp* Op = NewObject<UPackDragOp>(GetTransientPackage(), UPackDragOp::StaticClass());
	Op->BagId = BagId;
	Op->Pivot = EDragPivot::CenterCenter;
	if (WidgetTree)
	{
		USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
		Vis->SetWidthOverride(58.f); Vis->SetHeightOverride(58.f);
		Vis->SetContent(WeedUI::ItemIcon(WidgetTree, BagId, 58.f));
		Op->DefaultDragVisual = Vis;
	}
	OutOperation = Op;
}

// --- UPackUnwrapCell (unpack-doel: sleep een zak erop -> grammen-popup) ---
TSharedRef<SWidget> UPackUnwrapCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		Frame = B;
		B->SetPadding(FMargin(10.f, 8.f, 10.f, 10.f));
		B->SetHorizontalAlignment(HAlign_Fill); B->SetVerticalAlignment(VAlign_Top); // vult de kolom (wiet-cellen)
		if (Content) { B->SetContent(Content); }
		WidgetTree->RootWidget = B;
		SetDragHover(false);
	}
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

void UPackUnwrapCell::SetDragHover(bool bHover)
{
	if (!Frame) { return; }
	Frame->SetBrush(WeedUI::SelectableSlotBrush(true, bHover, WeedUI::ColAccent(0.95f), 9.f));
}

void UPackUnwrapCell::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	if (UPackDragOp* Op = Cast<UPackDragOp>(InOperation)) { if (!Op->BagId.IsNone()) { SetDragHover(true); } }
}

void UPackUnwrapCell::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	SetDragHover(false);
}

bool UPackUnwrapCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	SetDragHover(false);
	if (UPackDragOp* Op = Cast<UPackDragOp>(InOperation))
	{
		if (Owner.IsValid() && !Op->BagId.IsNone())
		{
			Owner->UnpackDropped(Op->BagId);
			return true;
		}
	}
	return false;
}

TSharedRef<SWidget> UPackWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPackWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PackCard"));
	{
		FSlateBrush CardBr = WeedUI::Rounded(WeedUI::ColPanel(0.96f), 12.f);
		CardBr.OutlineSettings.Width = 1;
		CardBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.48f));
		CardB->SetBrush(CardBr);
	}
	CardB->SetPadding(FMargin(18.f));
	Card = CardB;

	// Vast bovenanker + autosize: de kaart groeit naar de inhoud (NOOIT scrollen) en blijft met z'n
	// bovenkant op een vaste plek staan - dus hij verschuift niet als er rijen bij/af komen (bv. na het
	// inpakken van een bag verschijnt de unpack-sectie onderaan; de pack-knop blijft op z'n plek).
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.16f, 0.5f, 0.16f)); // iets lager + gecentreerd
	CS->SetAlignment(FVector2D(0.5f, 0.0f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, 0.f));

	// Vaste breedte zodat de kaart niet in breedte meeademt met de tekst (iets ruimer).
	USizeBox* WidthBox = WidgetTree->ConstructWidget<USizeBox>();
	WidthBox->SetWidthOverride(720.f);
	CardB->SetContent(WidthBox);

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	WidthBox->SetContent(Outer);

	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* TS = Head->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("PACKING BENCH"), 18, WeedUI::ColAccent(), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	// Geen aparte Unpack-tab meer: uitpakken doe je door een volle zak naar de wiet-kolom links te slepen (alles op 1 scherm).
	bUnpackTab = false;
	UWeedActionButton* CloseB = PackBtn(WidgetTree, WeedUI::ColInner(), [this]() { if (PhoneComp.IsValid()) { PhoneComp->ClosePack(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 12, WeedUI::ColText(), true));
	Head->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Geen ScrollBox meer: de body staat direct in de kaart en de kaart groeit mee.
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Outer->AddChildToVerticalBox(Body);

	// Eén scherm (geen aparte unpack-pane meer): packen + bijvullen + uitpakken zit allemaal in de pack-pane.
	PackPane = WidgetTree->ConstructWidget<UVerticalBox>();
	Body->AddChildToVerticalBox(PackPane);
	BuildPackPane(PackPane);
	PackPane->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// "Hoeveel inpakken?"-popup bovenop (modal). Op de ROOT-canvas zodat Visible<->Collapsed de kaart niet herlayout.
	BuildAmountPopup(Root);
}

// === Bouwt het volledige pack-scherm ÉÉN keer op (alle secties + slider + steppers). Daarna updatet
//     RefreshPack alleen waardes/zichtbaarheid/rij-pool in place. ===
void UPackWidget::BuildPackPane(UVerticalBox* Parent)
{
	auto Row = [Parent](UWidget* W, const FMargin& P) { Parent->AddChildToVerticalBox(W)->SetPadding(P); };

	// === Sleep-inpakken: sleep een gedroogde strain (links) op een container (rechts) -> 1 volle container. ===
	// (Vervangt de oude keuze+slider+knop-flow. "Sleep = 1 volle container, sleep opnieuw voor meer.")
	PackHintLabel = WeedUI::Text(WidgetTree, TEXT("Weed onto a jar = pack.   Jar onto the weed = unpack."), 12, WeedUI::ColTextDim(), false, true);
	PackHintLabel->SetAutoWrapText(true);
	Row(PackHintLabel, FMargin(0, 0, 0, 10));

	UHorizontalBox* ChoiceRow = WidgetTree->ConstructWidget<UHorizontalBox>();

	// --- Links: gedroogde wiet (sleepbare bron-cellen) EN uitpak-drop-zone (sleep een zak hierop = uitpakken) ---
	UPackUnwrapCell* WeedZone = WidgetTree->ConstructWidget<UPackUnwrapCell>();
	WeedZone->Owner = this;
	UVerticalBox* WeedCol = WidgetTree->ConstructWidget<UVerticalBox>();
	WeedZone->Content = WeedCol;
	WeedCol->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("DRIED WEED"), 10, WeedUI::ColAccent(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
	WeedDragBox = WidgetTree->ConstructWidget<UWrapBox>();
	WeedDragBox->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	WeedCol->AddChildToVerticalBox(WeedDragBox);
	NoBudLabel = WeedUI::Text(WidgetTree, TEXT("No dried weed. Dry it on a rack, or drop a jar here to unpack."), 11, WeedUI::ColTextDim());
	NoBudLabel->SetAutoWrapText(true);
	WeedCol->AddChildToVerticalBox(NoBudLabel)->SetPadding(FMargin(0, 6, 0, 0));
	NoBudLabel->SetVisibility(ESlateVisibility::Collapsed);
	UHorizontalBoxSlot* WS = ChoiceRow->AddChildToHorizontalBox(WeedZone);
	WS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	WS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));

	// --- Rechts: lege containers (packen) + je verpakte zakken (sleep eruit = uitpakken; niet-vol = bijvullen) ---
	UBorder* ContainerCard = PackPanel(WidgetTree);
	ContCard = ContainerCard;
	ContSection = WidgetTree->ConstructWidget<UVerticalBox>();
	ContainerCard->SetContent(ContSection);
	{
		auto CRow = [this](UWidget* W, const FMargin& P) { ContSection->AddChildToVerticalBox(W)->SetPadding(P); };
		CRow(WeedUI::Text(WidgetTree, TEXT("CONTAINERS & BAGS"), 10, WeedUI::ColAccent(), false, true), FMargin(0.f, 0.f, 0.f, 7.f));
		ContDropBox = WidgetTree->ConstructWidget<UWrapBox>();
		ContDropBox->SetInnerSlotPadding(FVector2D(6.f, 6.f));
		CRow(ContDropBox, FMargin(0, 0, 0, 0));
		NoContLabel = WeedUI::Text(WidgetTree, TEXT("No bags/jars. Buy them in the Grow shop."), 11, WeedUI::ColWarn());
		CRow(NoContLabel, FMargin(0, 6, 0, 0));
		NoContLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	UHorizontalBoxSlot* CS = ChoiceRow->AddChildToHorizontalBox(ContainerCard);
	CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CS->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	Row(ChoiceRow, FMargin(0, 0, 0, 10));

	// === Channel-lanes: de laadbalken van wat er nu ingepakt wordt (rij-pool wordt in RefreshLanes gevuld). ===
	{
		UBorder* LaneCard = PackPanel(WidgetTree);
		LaneSection = WidgetTree->ConstructWidget<UVerticalBox>();
		LaneCard->SetContent(LaneSection);
		LaneTitle = WeedUI::Text(WidgetTree, TEXT("PACKING"), 10, WeedUI::ColAccent(), false, true);
		LaneSection->AddChildToVerticalBox(LaneTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		LaneIdleLabel = WeedUI::Text(WidgetTree, TEXT("Nothing packing yet - drag weed onto a container above."), 11, WeedUI::ColTextDim());
		LaneSection->AddChildToVerticalBox(LaneIdleLabel);
		Row(LaneCard, FMargin(0, 0, 0, 0));
	}
	// (Uitpakken zit in de aparte "Unpack bags"-tab in de header.)
}

// === Bouwt het unpack-scherm ÉÉN keer op: sleepbare bag-cellen (links) -> uitpak-dropzone (rechts). ===
void UPackWidget::BuildUnpackPane(UVerticalBox* Parent)
{
	auto Row = [Parent](UWidget* W, const FMargin& P) { Parent->AddChildToVerticalBox(W)->SetPadding(P); };

	Row(WeedUI::Text(WidgetTree, TEXT("Drag a packed bag onto the unwrap zone, then choose how many grams to take out."), 12, WeedUI::ColTextDim(), false, true), FMargin(0, 0, 0, 10));

	UnpackEmptyLabel = WeedUI::Text(WidgetTree, TEXT("No packed bags to unpack - pack some first."), 11, WeedUI::ColTextDim());
	Row(UnpackEmptyLabel, FMargin(0, 0, 0, 6));
	UnpackEmptyLabel->SetVisibility(ESlateVisibility::Collapsed);

	UHorizontalBox* ChoiceRow = WidgetTree->ConstructWidget<UHorizontalBox>();

	// --- Links: sleepbare verpakte-zak-cellen ---
	UBorder* BagCard = PackPanel(WidgetTree);
	UVerticalBox* BagCol = WidgetTree->ConstructWidget<UVerticalBox>();
	BagCard->SetContent(BagCol);
	BagCol->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("PACKED BAGS"), 10, WeedUI::ColAccent(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	BagCol->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Drag a bag"), 13, WeedUI::ColText(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
	UnpackBagBox = WidgetTree->ConstructWidget<UWrapBox>();
	UnpackBagBox->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	BagCol->AddChildToVerticalBox(UnpackBagBox);
	UHorizontalBoxSlot* BS = ChoiceRow->AddChildToHorizontalBox(BagCard);
	BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	BS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));

	// --- Rechts: de uitpak-dropzone ---
	UBorder* UnwrapCard = PackPanel(WidgetTree);
	UVerticalBox* UnwrapCol = WidgetTree->ConstructWidget<UVerticalBox>();
	UnwrapCard->SetContent(UnwrapCol);
	UnwrapCol->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("UNWRAP"), 10, WeedUI::ColAccent(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	UnwrapCol->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Drop a bag here"), 13, WeedUI::ColText(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
	{
		UPackUnwrapCell* Zone = WidgetTree->ConstructWidget<UPackUnwrapCell>();
		Zone->Owner = this;
		UVerticalBox* ZV = WidgetTree->ConstructWidget<UVerticalBox>();
		ZV->AddChildToVerticalBox(WeedUI::UiGlyph(WidgetTree, TEXT("ui_package"), 34.f, WeedUI::ColAccent(0.9f), WeedUI::EIcon::Box))->SetHorizontalAlignment(HAlign_Center);
		UTextBlock* ZT = WeedUI::Text(WidgetTree, TEXT("Drop to unwrap"), 11, WeedUI::ColTextDim(), false, true);
		ZT->SetJustification(ETextJustify::Center);
		ZV->AddChildToVerticalBox(ZT)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		Zone->Content = ZV;
		USizeBox* ZB = WidgetTree->ConstructWidget<USizeBox>(); ZB->SetMinDesiredHeight(96.f); ZB->SetContent(Zone);
		UnwrapCol->AddChildToVerticalBox(ZB);
	}
	UHorizontalBoxSlot* US = ChoiceRow->AddChildToHorizontalBox(UnwrapCard);
	US->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	US->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	Row(ChoiceRow, FMargin(0, 0, 0, 6));
}

// Eén scherm: altijd de pack-pane bijwerken (packen + bijvullen + uitpakken).
void UPackWidget::FillBody()
{
	if (!Body || !PhoneComp.IsValid()) { return; }
	RefreshPack();
}

// Zet het aantal bags EN werkt slider + labels meteen bij (geen herbouw -> slider springt niet). Pack.
void UPackWidget::SetB(int32 N)
{
	SelBags = FMath::Clamp(N, 1, FMath::Max(1, MaxBags));
	const int32 G = FMath::Min(PackBudHave, SelBags * SelGrams);
	if (GramSlider)   { GramSlider->SetValue(MaxBags > 1 ? float(SelBags - 1) / float(MaxBags - 1) : 1.f); }
	const FString Noun = ContainerNoun(SelContainer); // "bag"/"jar"/"block"/"sack"
	const FString NounPl = Noun + (SelBags == 1 ? TEXT("") : TEXT("s"));
	FString NounCap = Noun; if (NounCap.Len() > 0) { NounCap[0] = FChar::ToUpper(NounCap[0]); }
	if (PackSummaryLabel)
	{
		PackSummaryLabel->SetText(FText::FromString(FString::Printf(TEXT("Uses %dg %s in %d %s"),
			G, *WeedUI::PrettyItemName(SelStrain), SelBags, *NounPl)));
	}
	if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %d %s - %dg"), SelBags, *NounPl, G))); }
	if (GramLabel)    { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("%ss: %d / %d"), *NounCap, SelBags, MaxBags))); }

	// Half/Max-knop-highlight (de knoppen tonen de keuze al).
	const int32 HalfN = FMath::Max(1, MaxBags / 2);
	if (BagsMaxBtn)  { StyleChoiceBtn(BagsMaxBtn,  SelBags == MaxBags); }
	if (BagsHalfBtn) { StyleChoiceBtn(BagsHalfBtn, SelBags == HalfN && HalfN != MaxBags); }
}

// Zet het aantal bags EN werkt slider + labels meteen bij. Unpack.
void UPackWidget::SetUB(int32 N)
{
	SelBags = FMath::Clamp(N, 1, FMath::Max(1, MaxBags));
	const int32 G = SelBags * UnpackPerBag;
	if (UnpackSlider)   { UnpackSlider->SetValue(MaxBags > 1 ? float(SelBags - 1) / float(MaxBags - 1) : 1.f); }
	if (UnpackLabel)
	{
		UnpackLabel->SetText(FText::FromString(FString::Printf(TEXT("Returns %dg loose weed from %d bag%s"), G, SelBags, SelBags == 1 ? TEXT("") : TEXT("s"))));
		UnpackLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (UnpackBtnLabel) { UnpackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Unpack %d bag%s - %dg"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }

	// Half/Max-knop-highlight (de knoppen tonen de keuze al -> UnpackLabel is clutter en blijft collapsed).
	const int32 HalfN = FMath::Max(1, MaxBags / 2);
	if (UnpackMaxBtn)  { StyleChoiceBtn(UnpackMaxBtn,  SelBags == MaxBags); }
	if (UnpackHalfBtn) { StyleChoiceBtn(UnpackHalfBtn, SelBags == HalfN && HalfN != MaxBags); }
}

// Highlight een keuze-knop als actief: rustige basis + accent-outline (zelfde idioom als UWeedItemPickGrid).
void UPackWidget::StyleChoiceBtn(UWeedActionButton* B, bool bActive)
{
	if (!B) { return; }
	B->SetStyle(WeedUI::SelectableSlotButtonStyle(true, bActive, WeedUI::ColAccent(0.95f), 8.f, FMargin(8.f, 4.f)));
}

// Drag-drop van wiet op een container: kan er meer dan 1 container van gepakt worden -> vraag HOEVEEL (popup,
// net als de deal). Anders meteen 1 job. De queue (Jobs) draait de eerste N=lanes tegelijk; de rest wacht.
void UPackWidget::PackOne(FName BudId, FName ContId)
{
	if (!PhoneComp.IsValid() || BudId.IsNone() || ContId.IsNone()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	APawn* Pawn = GetOwningPlayerPawn();

	const int32 Cap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(ContId));
	// Beschikbaar NA wat al in de queue vastzit (niet dubbel inzetten).
	int32 RemWeed = Inv->GetQuantity(BudId), RemCont = Inv->GetQuantity(ContId);
	for (const FPackJob& J : Jobs) { if (J.BudId == BudId) { RemWeed -= J.Grams; } if (J.ContId == ContId) { RemCont -= 1; } }
	if (RemWeed <= 0)
	{
		UWeedToast::NotifyPawn(Pawn, -1, 2.0f, FColor(230, 180, 90),
			FString::Printf(TEXT("Not enough %s left to pack."), *WeedUI::PrettyItemName(BudId)), FString());
		return;
	}
	if (RemCont <= 0)
	{
		UWeedToast::NotifyPawn(Pawn, -1, 2.0f, FColor(230, 180, 90),
			FString::Printf(TEXT("No empty %s left."), ContainerNoun(ContId)), FString());
		return;
	}
	// Queue-cap: houd de kaart hanteerbaar (max kMaxQueue rijen). Vol -> even laten leeglopen.
	const int32 QueueRoom = kMaxQueue - Jobs.Num();
	if (QueueRoom <= 0)
	{
		UWeedToast::NotifyPawn(Pawn, -1, 2.0f, FColor(230, 180, 90),
			FString::Printf(TEXT("Packing queue is full (%d) - let some finish first."), kMaxQueue), FString());
		return;
	}
	// Max GRAMMEN die je nu kunt inpakken: begrensd door je wiet, je containers (elk tot cap) en de queue-ruimte.
	const int32 MaxGrams = FMath::Min3(RemWeed, RemCont * Cap, QueueRoom * Cap);
	// Popup ALLEEN als er meer is dan in EEN container past (dan valt er te verdelen/kiezen). Past het in
	// een enkele container -> gewoon meteen inpakken, geen popup.
	if (MaxGrams <= Cap) { EnqueueGrams(BudId, ContId, FMath::Max(1, MaxGrams)); }
	else                 { OpenPackPopup(BudId, ContId); } // 2 sliders: gram/zakje + aantal zakjes
}

// Grams gram inpakken: verdeel over containers (elk tot z'n capaciteit; laatste deels). Respecteert de queue-cap.
// Duur van een pack-job schaalt met hoe vol het zakje wordt (klein zakje = sneller). Gedeeld door bench-speed.
static float PackJobDuration(FName Cont, int32 Grams, int32 Cap, float Speed)
{
	const float Frac = FMath::Clamp(float(FMath::Max(1, Grams)) / float(FMath::Max(1, Cap)), 0.05f, 1.f);
	return FMath::Max(0.25f, PackDurationFor(Cont) / FMath::Max(0.1f, Speed) * Frac);
}

void UPackWidget::EnqueueGrams(FName Bud, FName Cont, int32 Grams)
{
	if (!PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	const int32 Cap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(Cont));
	int32 RemWeed = Inv->GetQuantity(Bud), RemCont = Inv->GetQuantity(Cont);
	for (const FPackJob& J : Jobs) { if (J.BudId == Bud) { RemWeed -= J.Grams; } if (J.ContId == Cont && J.TargetBag.IsNone()) { RemCont -= 1; } }
	int32 ToPack = FMath::Clamp(Grams, 1, FMath::Min(RemWeed, RemCont * Cap));
	int32 Added = 0;
	while (ToPack > 0 && RemCont > 0 && Jobs.Num() < kMaxQueue)
	{
		const int32 G = FMath::Min(Cap, ToPack);
		FPackJob J;
		J.BudId = Bud; J.ContId = Cont;
		J.Grams = G;
		J.Elapsed = 0.f; J.Duration = PackJobDuration(Cont, G, Cap, PhoneComp->GetPackSpeed());
		Jobs.Add(J);
		ToPack -= G; RemCont -= 1; ++Added;
	}
	if (Added > 0) { RefreshLanes(); }
}

// Count zakjes van GramsPerBag inpakken (uit de 2-slider popup). Respecteert wiet/containers/queue.
void UPackWidget::EnqueueBags(FName Bud, FName Cont, int32 GramsPerBag, int32 Count)
{
	if (!PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	const int32 Cap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(Cont));
	const int32 GPer = FMath::Clamp(GramsPerBag, 1, Cap);
	int32 RemWeed = Inv->GetQuantity(Bud), RemCont = Inv->GetQuantity(Cont);
	for (const FPackJob& J : Jobs) { if (J.BudId == Bud) { RemWeed -= J.Grams; } if (J.ContId == Cont && J.TargetBag.IsNone()) { RemCont -= 1; } }
	int32 Added = 0;
	for (int32 k = 0; k < Count && RemWeed >= GPer && RemCont > 0 && Jobs.Num() < kMaxQueue; ++k)
	{
		FPackJob J;
		J.BudId = Bud; J.ContId = Cont;
		J.Grams = GPer;
		J.Elapsed = 0.f; J.Duration = PackJobDuration(Cont, GPer, Cap, PhoneComp->GetPackSpeed());
		Jobs.Add(J);
		RemWeed -= GPer; RemCont -= 1; ++Added;
	}
	if (Added > 0) { RefreshLanes(); }
}

// 1 bijvul-job: voeg Grams gram wiet toe aan de niet-volle zak Bag. Kwaliteit mengt server-side (gewogen).
void UPackWidget::EnqueueTopUp(FName Bud, FName Bag, int32 Grams)
{
	if (!PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	const FName Cont = UInventoryComponent::BagContainer(Bag);
	if (Cont.IsNone()) { return; }
	const int32 Cap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(Cont));
	const int32 Gb = FMath::Max(1, UInventoryComponent::BagGrams(Bag));
	const int32 Space = FMath::Max(0, Cap - Gb);
	if (Space <= 0) { return; } // al vol
	int32 RemWeed = Inv->GetQuantity(Bud);
	for (const FPackJob& J : Jobs) { if (J.BudId == Bud) { RemWeed -= J.Grams; } }
	const int32 Add = FMath::Clamp(Grams, 1, FMath::Min(Space, RemWeed));
	if (Add <= 0 || Jobs.Num() >= kMaxQueue) { return; }
	FPackJob J;
	J.BudId = Bud; J.ContId = Cont; J.TargetBag = Bag;
	J.Grams = Add;
	J.Elapsed = 0.f; J.Duration = PackJobDuration(Cont, Add, Cap, PhoneComp->GetPackSpeed());
	Jobs.Add(J);
	RefreshLanes();
}

// Drag-drop van wiet op een niet-volle zak: strain-check + 1 bijvul-job per zak, dan de merge-popup (hoeveel erbij).
void UPackWidget::TopUpDropped(FName BudId, FName BagId)
{
	if (!PhoneComp.IsValid() || BudId.IsNone() || BagId.IsNone()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	APawn* Pawn = GetOwningPlayerPawn();
	const FName BagStrain = UInventoryComponent::BagStrain(BagId);
	const FName BudStrain(*BudId.ToString().RightChop(4));
	if (BagStrain != BudStrain)
	{
		UWeedToast::NotifyPawn(Pawn, -1, 2.0f, FColor(230, 180, 90),
			FString::Printf(TEXT("That container holds %s - can't mix strains."), *BagStrain.ToString()), FString());
		return;
	}
	for (const FPackJob& J : Jobs) { if (J.TargetBag == BagId) { return; } } // al een bijvul-job voor deze zak
	// Hoeveel kan er nog bij? = min(vrije ruimte, wiet die je hebt minus wat al in jobs zit).
	const FName Cont = UInventoryComponent::BagContainer(BagId);
	if (Cont.IsNone()) { return; }
	const int32 Cap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(Cont));
	const int32 Space = FMath::Max(0, Cap - FMath::Max(1, UInventoryComponent::BagGrams(BagId)));
	int32 RemWeed = Inv->GetQuantity(BudId);
	for (const FPackJob& J : Jobs) { if (J.BudId == BudId) { RemWeed -= J.Grams; } }
	const int32 MaxAdd = FMath::Min(Space, RemWeed);
	if (MaxAdd <= 0)
	{
		UWeedToast::NotifyPawn(Pawn, -1, 2.0f, FColor(230, 180, 90),
			Space <= 0 ? TEXT("That container is already full.") : FString::Printf(TEXT("No more %s to add."), *WeedUI::PrettyItemName(BudId)), FString());
		return;
	}
	if (MaxAdd <= 1) { EnqueueTopUp(BudId, BagId, MaxAdd); } // triviaal -> direct
	else             { OpenTopUpPopup(BudId, BagId, MaxAdd); } // merge-popup (slider default vol)
}

// ===================== "Hoeveel inpakken?"-popup (modal, gespiegeld van UDealWidget::BuildAmountPopup) =====================
void UPackWidget::BuildAmountPopup(UCanvasPanel* Root)
{
	if (!Root) { return; }
	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();

	// Full-screen hit-test-blocker (dim): maakt de rest van het scherm modaal geblokkeerd.
	UBorder* Blocker = WidgetTree->ConstructWidget<UBorder>();
	Blocker->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f));
	Blocker->SetVisibility(ESlateVisibility::Visible);
	UOverlaySlot* BS = Ov->AddChildToOverlay(Blocker);
	BS->SetHorizontalAlignment(HAlign_Fill); BS->SetVerticalAlignment(VAlign_Fill);

	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(360.f);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 16.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.7f));
		Panel->SetBrush(Br);
	}
	Panel->SetPadding(FMargin(16.f, 14.f));
	UVerticalBox* PV = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(PV);
	AmountTitle = WeedUI::Text(WidgetTree, TEXT("How many to pack?"), 15, WeedUI::ColAccent(), false, true);
	PV->AddChildToVerticalBox(AmountTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	// Slider 1: gram/zakje (pack) of grammen-eruit (unpack).
	AmountLabel = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColText(), false, true);
	PV->AddChildToVerticalBox(AmountLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	AmountSlider = WidgetTree->ConstructWidget<USlider>();
	AmountSlider->SetSliderHandleColor(WeedUI::ColAccent());
	AmountSlider->SetSliderBarColor(WeedUI::ColSlot());
	AmountSlider->SetMinValue(0.f); AmountSlider->SetMaxValue(1.f); AmountSlider->SetValue(1.f);
	{ FSliderStyle SS = AmountSlider->GetWidgetStyle(); SS.SetBarThickness(8.f); AmountSlider->SetWidgetStyle(SS); }
	AmountSlider->OnValueChanged.AddDynamic(this, &UPackWidget::OnAmountChanged);
	{
		USizeBox* SH = WidgetTree->ConstructWidget<USizeBox>(); SH->SetHeightOverride(20.f); SH->SetContent(AmountSlider);
		PV->AddChildToVerticalBox(SH)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	// Pack-only rij 2: aantal zakjes + samenvatting (verborgen bij de unpack-popup).
	UVerticalBox* Row2 = WidgetTree->ConstructWidget<UVerticalBox>();
	AmountPackRow2 = Row2;
	{
		AmountLabel2 = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColText(), false, true);
		Row2->AddChildToVerticalBox(AmountLabel2)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		AmountSlider2 = WidgetTree->ConstructWidget<USlider>();
		AmountSlider2->SetSliderHandleColor(WeedUI::ColAccent());
		AmountSlider2->SetSliderBarColor(WeedUI::ColSlot());
		AmountSlider2->SetMinValue(0.f); AmountSlider2->SetMaxValue(1.f); AmountSlider2->SetValue(1.f);
		{ FSliderStyle SS = AmountSlider2->GetWidgetStyle(); SS.SetBarThickness(8.f); AmountSlider2->SetWidgetStyle(SS); }
		AmountSlider2->OnValueChanged.AddDynamic(this, &UPackWidget::OnAmount2Changed);
		{
			USizeBox* SH2 = WidgetTree->ConstructWidget<USizeBox>(); SH2->SetHeightOverride(20.f); SH2->SetContent(AmountSlider2);
			Row2->AddChildToVerticalBox(SH2)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}
		AmountSummary = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColAccent(), false, true);
		Row2->AddChildToVerticalBox(AmountSummary)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}
	PV->AddChildToVerticalBox(Row2)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakePopBtn = [this](const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, UTextBlock** OutLbl) -> UWeedActionButton*
	{
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(12.f, 7.f); St.PressedPadding = FMargin(12.f, 7.f);
		B->SetStyle(St);
		UTextBlock* T = WeedUI::Text(WidgetTree, Label, 14, WeedUI::ColText(), true, true);
		if (OutLbl) { *OutLbl = T; }
		B->SetContent(T);
		return B;
	};
	// "All" = zet de slider op max en bevestig meteen (venster sluit) -> snel alles laden.
	UHorizontalBoxSlot* ABS = Btns->AddChildToHorizontalBox(MakePopBtn(TEXT("All"), WeedUI::ColAccentDim(0.95f),
		[this]() { if (AmountSlider) { AmountSlider->SetValue(1.f); } if (AmountSlider2) { AmountSlider2->SetValue(1.f); } ConfirmAmount(); }, nullptr));
	ABS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ABS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	UTextBlock* ConfLbl = nullptr;
	UHorizontalBoxSlot* GBS = Btns->AddChildToHorizontalBox(MakePopBtn(TEXT("Pack"), WeedUI::ColAccent(),
		[this]() { ConfirmAmount(); }, &ConfLbl));
	GBS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GBS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	AmountConfirmText = ConfLbl;
	UHorizontalBoxSlot* CBS = Btns->AddChildToHorizontalBox(MakePopBtn(TEXT("Cancel"), WeedUI::ColSlot(),
		[this]() { CancelAmount(); }, nullptr));
	CBS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PV->AddChildToVerticalBox(Btns);

	Sz->SetContent(Panel);
	UOverlaySlot* PS = Ov->AddChildToOverlay(Sz);
	PS->SetHorizontalAlignment(HAlign_Center); PS->SetVerticalAlignment(VAlign_Center);

	AmountRoot = Ov;
	UCanvasPanelSlot* CPS = Root->AddChildToCanvas(Ov);
	CPS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	CPS->SetOffsets(FMargin(0.f));
	Ov->SetVisibility(ESlateVisibility::Collapsed);
}

void UPackWidget::OpenPackPopup(FName Bud, FName Cont)
{
	if (!AmountRoot || !AmountSlider || !AmountSlider2) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	AmountMode = EPackAmountMode::Pack;
	PendingBud = Bud; PendingCont = Cont; PendingBag = NAME_None;
	PendingCap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(Cont));
	int32 RemWeed = Inv->GetQuantity(Bud), RemCont = Inv->GetQuantity(Cont);
	for (const FPackJob& J : Jobs) { if (J.BudId == Bud) { RemWeed -= J.Grams; } if (J.ContId == Cont && J.TargetBag.IsNone()) { RemCont -= 1; } }
	PendingWeed = FMath::Max(0, RemWeed);
	PendingContOwned = FMath::Max(0, RemCont);
	PendingRoom = FMath::Max(0, kMaxQueue - Jobs.Num());
	if (AmountTitle) { AmountTitle->SetText(FText::FromString(FString::Printf(TEXT("Pack %s in %ss"), *WeedUI::PrettyItemName(Bud), ContainerNoun(Cont)))); }
	if (AmountConfirmText) { AmountConfirmText->SetText(FText::FromString(TEXT("Pack"))); }
	if (AmountPackRow2) { AmountPackRow2->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	AmountSlider->SetValue(1.f);  // gram/zakje default = vol
	AmountSlider2->SetValue(1.f); // aantal default = max
	RefreshPackPopupLabels();
	AmountRoot->SetVisibility(ESlateVisibility::Visible);
}

void UPackWidget::OpenUnpackPopup(FName Bag, int32 MaxGrams)
{
	if (!AmountRoot || !AmountSlider) { return; }
	AmountMode = EPackAmountMode::Unpack;
	PendingBag = Bag; PendingBud = NAME_None; PendingCont = NAME_None; PendingMax = FMath::Max(1, MaxGrams);
	if (AmountTitle) { AmountTitle->SetText(FText::FromString(TEXT("How many grams to take out?"))); }
	if (AmountConfirmText) { AmountConfirmText->SetText(FText::FromString(TEXT("Take out"))); }
	if (AmountPackRow2) { AmountPackRow2->SetVisibility(ESlateVisibility::Collapsed); } // 1 slider
	AmountSlider->SetValue(0.5f);
	OnAmountChanged(0.5f);
	AmountRoot->SetVisibility(ESlateVisibility::Visible);
}

void UPackWidget::OpenTopUpPopup(FName Bud, FName Bag, int32 MaxAdd)
{
	if (!AmountRoot || !AmountSlider) { return; }
	AmountMode = EPackAmountMode::TopUp;
	PendingBud = Bud; PendingBag = Bag; PendingCont = NAME_None; PendingMax = FMath::Max(1, MaxAdd);
	if (AmountTitle) { AmountTitle->SetText(FText::FromString(FString::Printf(TEXT("Top up %s - how much to add?"), *WeedUI::PrettyItemName(Bud)))); }
	if (AmountConfirmText) { AmountConfirmText->SetText(FText::FromString(TEXT("Top up"))); }
	if (AmountPackRow2) { AmountPackRow2->SetVisibility(ESlateVisibility::Collapsed); } // 1 slider
	AmountSlider->SetValue(1.f); // default VOL (speler: "hier mag je de slider vol zetten")
	OnAmountChanged(1.f);
	AmountRoot->SetVisibility(ESlateVisibility::Visible);
}

// Beide pack-labels + samenvatting herberekenen uit de twee sliders. MaxBags hangt af van gram/zakje.
void UPackWidget::RefreshPackPopupLabels()
{
	if (!AmountSlider || !AmountSlider2) { return; }
	const int32 GPer = FMath::Clamp(FMath::RoundToInt(AmountSlider->GetValue() * PendingCap), 1, PendingCap);
	const int32 LclMax = FMath::Clamp(FMath::Min3(PendingContOwned, PendingWeed / GPer, PendingRoom), 1, kMaxQueue);
	const int32 Bags = FMath::Clamp(FMath::RoundToInt(AmountSlider2->GetValue() * LclMax), 1, LclMax);
	if (AmountLabel)  { AmountLabel->SetText(FText::FromString(FString::Printf(TEXT("Grams per %s:  %dg  (max %d)"), ContainerNoun(PendingCont), GPer, PendingCap))); }
	if (AmountLabel2) { FString NounPl = ContainerNoun(PendingCont); NounPl += (Bags == 1 ? TEXT("") : TEXT("s")); AmountLabel2->SetText(FText::FromString(FString::Printf(TEXT("How many %s:  %d  (max %d)"), *NounPl, Bags, LclMax))); }
	if (AmountSummary){ AmountSummary->SetText(FText::FromString(FString::Printf(TEXT("= %dg total"), GPer * Bags))); }
}

void UPackWidget::OnAmountChanged(float V)
{
	if (AmountMode == EPackAmountMode::Pack) { RefreshPackPopupLabels(); return; }
	if (!AmountLabel) { return; }
	const int32 N = FMath::Clamp(FMath::RoundToInt(V * PendingMax), 1, FMath::Max(1, PendingMax));
	if (AmountMode == EPackAmountMode::Unpack)
	{
		AmountLabel->SetText(FText::FromString(FString::Printf(TEXT("Take %dg out  (of %dg)"), N, PendingMax)));
	}
	else // TopUp
	{
		AmountLabel->SetText(FText::FromString(FString::Printf(TEXT("Add %dg  (of %dg - quality will mix)"), N, PendingMax)));
	}
}

void UPackWidget::OnAmount2Changed(float V)
{
	if (AmountMode == EPackAmountMode::Pack) { RefreshPackPopupLabels(); }
}

void UPackWidget::ConfirmAmount()
{
	if (AmountMode == EPackAmountMode::Pack)
	{
		if (AmountSlider && AmountSlider2 && !PendingBud.IsNone() && !PendingCont.IsNone())
		{
			const int32 GPer = FMath::Clamp(FMath::RoundToInt(AmountSlider->GetValue() * PendingCap), 1, PendingCap);
			const int32 LclMax = FMath::Clamp(FMath::Min3(PendingContOwned, PendingWeed / GPer, PendingRoom), 1, kMaxQueue);
			const int32 Bags = FMath::Clamp(FMath::RoundToInt(AmountSlider2->GetValue() * LclMax), 1, LclMax);
			EnqueueBags(PendingBud, PendingCont, GPer, Bags);
		}
	}
	else if (AmountSlider && PendingMax > 0)
	{
		const int32 N = FMath::Clamp(FMath::RoundToInt(AmountSlider->GetValue() * PendingMax), 1, PendingMax);
		if (AmountMode == EPackAmountMode::Unpack)
		{
			if (!PendingBag.IsNone()) { EnqueueUnpack(PendingBag, N); } // channel-job (halve tijd) i.p.v. instant
		}
		else // TopUp
		{
			if (!PendingBud.IsNone() && !PendingBag.IsNone()) { EnqueueTopUp(PendingBud, PendingBag, N); }
		}
	}
	CancelAmount();
}

void UPackWidget::CancelAmount()
{
	AmountMode = EPackAmountMode::Pack;
	PendingBud = NAME_None; PendingCont = NAME_None; PendingBag = NAME_None; PendingMax = 0;
	if (AmountRoot) { AmountRoot->SetVisibility(ESlateVisibility::Collapsed); }
}

// Lane-rij-pool groeien/krimpen naar het aantal jobs (patroon uit UDryingRackWidget: geen ClearChildren ->
// lopende balken blijven staan). Naam + icoon per lane; balk/tijd doet UpdateLaneProgress elke tick.
void UPackWidget::RefreshLanes()
{
	if (!LaneSection) { return; }
	const int32 Used = Jobs.Num();
	if (LaneTitle)
	{
		const int32 Lanes = PhoneComp.IsValid() ? FMath::Max(1, PhoneComp->GetPackBatch()) : 1;
		const int32 Running = FMath::Min(Lanes, Used);
		const int32 Queued = FMath::Max(0, Used - Lanes);
		LaneTitle->SetText(FText::FromString(Queued > 0
			? FString::Printf(TEXT("PACKING  (%d running, %d queued)"), Running, Queued)
			: FString::Printf(TEXT("PACKING  (%d / %d lanes)"), Running, Lanes)));
	}
	if (LaneIdleLabel) { LaneIdleLabel->SetVisibility(Used > 0 ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }

	// Staart-groei: nieuwe lane-rij (Border > HBox[ icoon | VBox[ HBox[naam, tijd], balk ] ]).
	while (LaneRows.Num() < Used)
	{
		UBorder* RowB = WidgetTree->ConstructWidget<UBorder>();
		RowB->SetBrush(WeedUI::Rounded(WeedUI::ColInner(0.85f), 8.f));
		RowB->SetPadding(FMargin(8.f, 6.f, 8.f, 7.f));
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		RowB->SetContent(H);

		USizeBox* Icon = WidgetTree->ConstructWidget<USizeBox>();
		Icon->SetWidthOverride(40.f); Icon->SetHeightOverride(40.f);
		UHorizontalBoxSlot* IS = H->AddChildToHorizontalBox(Icon);
		IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBoxSlot* VS = H->AddChildToHorizontalBox(V);
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); VS->SetVerticalAlignment(VAlign_Center);

		UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* NameT = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColText(), false, true);
		UHorizontalBoxSlot* NS = Top->AddChildToHorizontalBox(NameT);
		NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);
		UTextBlock* TimeT = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, true);
		Top->AddChildToHorizontalBox(TimeT)->SetVerticalAlignment(VAlign_Center);
		V->AddChildToVerticalBox(Top);

		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
		Bar->SetFillColorAndOpacity(WeedUI::ColAccent());
		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
		BarSz->SetHeightOverride(12.f); BarSz->SetContent(Bar);
		V->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

		LaneSection->AddChildToVerticalBox(RowB)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		LaneRows.Add(RowB); LaneIcons.Add(Icon); LaneNames.Add(NameT); LaneTimes.Add(TimeT); LaneBars.Add(Bar);
		LaneShownCont.Add(NAME_None);
	}
	// Staart-krimp: overtollige rijen weg (alle pools synchroon houden).
	while (LaneRows.Num() > Used)
	{
		const int32 Last = LaneRows.Num() - 1;
		if (LaneRows[Last]) { LaneRows[Last]->RemoveFromParent(); }
		LaneRows.RemoveAt(Last); LaneIcons.RemoveAt(Last); LaneNames.RemoveAt(Last); LaneTimes.RemoveAt(Last); LaneBars.RemoveAt(Last);
		LaneShownCont.RemoveAt(Last);
	}

	// Per-lane naam + icoon (icoon = het gepakte item, strain-gekleurd; alleen swappen als de lane-inhoud wijzigt).
	for (int32 i = 0; i < Used; ++i)
	{
		const FPackJob& J = Jobs[i];
		const bool bUnp = !J.UnpackBag.IsNone();
		const bool bTop = !J.TargetBag.IsNone();
		const FName Strain(*J.BudId.ToString().RightChop(4)); // Bud_X -> X (leeg bij unpack)
		const FName IconBag = bUnp ? J.UnpackBag : (bTop ? J.TargetBag : UInventoryComponent::MakeBagId(Strain, J.ContId, FMath::Max(1, J.Grams)));
		if (LaneNames.IsValidIndex(i) && LaneNames[i])
		{
			LaneNames[i]->SetText(FText::FromString(bUnp
				? FString::Printf(TEXT("Unpack %s  -%dg"), *WeedUI::PrettyItemName(J.UnpackBag), J.Grams)
				: bTop
					? FString::Printf(TEXT("Top up %s %s  +%dg"), *WeedUI::PrettyItemName(J.BudId), ContainerNoun(J.ContId), J.Grams)
					: FString::Printf(TEXT("%dg %s %s"), J.Grams, *WeedUI::PrettyItemName(J.BudId), ContainerNoun(J.ContId))));
		}
		if (LaneIcons.IsValidIndex(i) && LaneIcons[i] && (!LaneShownCont.IsValidIndex(i) || LaneShownCont[i] != IconBag))
		{
			LaneIcons[i]->SetContent(WeedUI::ItemIcon(WidgetTree, IconBag, 40.f));
			LaneShownCont[i] = IconBag;
		}
	}
	UpdateLaneProgress();
}

// Per-lane balk-percentage + resttijd in place (elke tick, geen rebuild). Actieve lanes (< Lanes) lopen; de
// wachtende jobs tonen "queued" met een lege, gedimde balk.
void UPackWidget::UpdateLaneProgress()
{
	const int32 Lanes = PhoneComp.IsValid() ? FMath::Max(1, PhoneComp->GetPackBatch()) : 1;
	for (int32 i = 0; i < Jobs.Num(); ++i)
	{
		const FPackJob& J = Jobs[i];
		const bool bActive = i < Lanes;
		const float Pct = bActive ? FMath::Clamp(J.Duration > 0.f ? J.Elapsed / J.Duration : 1.f, 0.f, 1.f) : 0.f;
		if (LaneBars.IsValidIndex(i) && LaneBars[i])
		{
			LaneBars[i]->SetPercent(Pct);
			LaneBars[i]->SetFillColorAndOpacity(bActive ? WeedUI::ColAccent() : WeedUI::ColSlot());
		}
		if (LaneTimes.IsValidIndex(i) && LaneTimes[i])
		{
			LaneTimes[i]->SetText(FText::FromString(bActive
				? FString::Printf(TEXT("%.1fs"), FMath::Max(0.f, J.Duration - J.Elapsed))
				: TEXT("queued")));
		}
	}
}

// Jobs voortduwen; op voltooiing echt inpakken via de bestaande server-auth route. Alleen aangeroepen terwijl
// de bench open is (NativeTick-guard) -> weglopen/sluiten pauzeert de balken (channel-model).
void UPackWidget::TickJobs(float Dt)
{
	if (Jobs.Num() == 0) { return; }
	// Alleen de eerste N=lanes draaien tegelijk; de rest wacht in de queue. Zodra er één klaar is, schuift de
	// volgende automatisch de actieve zone in (index < Lanes) en begint z'n balk.
	const int32 Lanes = PhoneComp.IsValid() ? FMath::Max(1, PhoneComp->GetPackBatch()) : 1;
	const int32 Active = FMath::Min(Lanes, Jobs.Num());
	bool bChanged = false;
	for (int32 i = Active - 1; i >= 0; --i)
	{
		Jobs[i].Elapsed += Dt;
		if (Jobs[i].Elapsed >= Jobs[i].Duration)
		{
			if (PhoneComp.IsValid())
			{
				if (!Jobs[i].UnpackBag.IsNone())      { PhoneComp->RequestUnpackGrams(Jobs[i].UnpackBag, Jobs[i].Grams); }        // uitpakken
				else if (!Jobs[i].TargetBag.IsNone()) { PhoneComp->RequestTopUp(Jobs[i].BudId, Jobs[i].TargetBag, Jobs[i].Grams); } // bijvullen
				else                                  { PhoneComp->RequestPackGrams(Jobs[i].BudId, Jobs[i].ContId, Jobs[i].Grams); } // nieuw zakje
			}
			Jobs.RemoveAt(i);
			bChanged = true;
		}
	}
	if (bChanged) { RefreshLanes(); }   // pool krimpt + naam/icoon herzetten (RefreshLanes eindigt met UpdateLaneProgress)
	else          { UpdateLaneProgress(); }
}

// Werkt de pack-flow bij: (her)vult de sleepbare wiet-cellen + de drop-container-cellen. Wordt alleen
// aangeroepen bij openen/tab-switch/na een pack (geen per-frame rebuild) -> geen flikker tijdens kijken.
void UPackWidget::RefreshPack()
{
	if (!PackPane || !PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	// --- 1) Sleepbare wiet-cellen: EEN cel PER strain (aggregeer stacks van dezelfde Bud_-Id: twee harvests
	//        met net-andere THC% zijn losse stacks). Grammen sommeren; Quality/THC van de grootste stapel. ---
	struct FBudRow { FName Id; int32 Qty; float Quality; float QualityPct; int32 TopQty; };
	TArray<FBudRow> Buds;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (!S.ItemId.ToString().StartsWith(TEXT("Bud_"))) { continue; }
		FBudRow* Existing = Buds.FindByPredicate([&S](const FBudRow& R) { return R.Id == S.ItemId; });
		if (Existing)
		{
			Existing->Qty += S.Quantity;
			if (S.Quantity > Existing->TopQty) { Existing->Quality = S.Quality; Existing->QualityPct = S.QualityPct; Existing->TopQty = S.Quantity; }
		}
		else
		{
			Buds.Add({ S.ItemId, S.Quantity, S.Quality, S.QualityPct, S.Quantity });
		}
	}
	// STABIELE volgorde (op strain): zonder dit wisselt de inventory-stack-volgorde en herbouwt de sig-diff
	// bij elke pack/unpack bijna alle cellen -> flash. Gesorteerd = alleen de echt-gewijzigde cel ververst.
	Buds.Sort([](const FBudRow& A, const FBudRow& B) { return A.Id.Compare(B.Id) < 0; });
	const bool bAnyBud = Buds.Num() > 0;
	if (NoBudLabel) { NoBudLabel->SetVisibility(bAnyBud ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }

	if (WeedDragBox)
	{
		const int32 Used = Buds.Num();
		while (WeedCellPool.Num() < Used)
		{
			UPackWeedCell* Cell = WidgetTree->ConstructWidget<UPackWeedCell>();
			WeedDragBox->AddChildToWrapBox(Cell);
			WeedCellPool.Add(Cell);
			WeedCellSig.Add(TEXT("\x01"));
		}
		while (WeedCellPool.Num() > Used)
		{
			const int32 Last = WeedCellPool.Num() - 1;
			if (WeedCellPool[Last]) { WeedCellPool[Last]->RemoveFromParent(); }
			WeedCellPool.RemoveAt(Last);
			if (WeedCellSig.IsValidIndex(Last)) { WeedCellSig.RemoveAt(Last); }
		}
		for (int32 i = 0; i < Used; ++i)
		{
			const FBudRow& R = Buds[i];
			const FString Sig = FString::Printf(TEXT("%s|%d|%.0f|%.0f"), *R.Id.ToString(), R.Qty, R.Quality, R.QualityPct);
			if (WeedCellSig.IsValidIndex(i) && Sig == WeedCellSig[i]) { continue; }
			if (WeedCellSig.IsValidIndex(i)) { WeedCellSig[i] = Sig; }
			UPackWeedCell* Cell = WeedCellPool[i];
			if (!Cell) { continue; }
			Cell->BudId = R.Id; Cell->GramsAvail = R.Qty;
			Cell->SetInner(MakeItemCellContent(WidgetTree, R.Id, FString::Printf(TEXT("%dg"), R.Qty), WeedUI::PrettyItemName(R.Id)));
			Cell->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%dg - THC %.0f%% Q %.0f%%\nDrag onto a container to pack it."),
				*WeedUI::PrettyItemName(R.Id), R.Qty, R.Quality, R.QualityPct)));
		}
	}

	// --- 2) Rechter kolom: lege containers (nieuw packen) + AL je verpakte zakken (sleep eruit = uitpakken;
	//        niet-volle zak = ook bijvul-doel). BagId gezet = zak-cel; anders lege container. ---
	struct FDropRow { FName ContId; FName BagId; int32 Cap; int32 Owned; int32 Cur; bool bFull; };
	TArray<FDropRow> Drops;
	for (int32 i = 0; i < 6; ++i)
	{
		const FName ContId(kConts[i]);
		const int32 Owned = Inv->GetQuantity(ContId);
		if (Owned <= 0) { continue; }
		Drops.Add({ ContId, NAME_None, UPhoneClientComponent::ContainerCapacity(ContId), Owned, 0, false });
	}
	// Alle verpakte zakken (vol en niet-vol): sleepbaar om uit te pakken; niet-vol is ook bijvul-doel.
	TArray<FDropRow> BagRows;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (S.Quantity <= 0 || !S.ItemId.ToString().StartsWith(TEXT("Bag_"))) { continue; }
		const FName Cont = UInventoryComponent::BagContainer(S.ItemId);
		const int32 Cap = Cont.IsNone() ? FMath::Max(1, UInventoryComponent::BagGrams(S.ItemId)) : UPhoneClientComponent::ContainerCapacity(Cont);
		const int32 Cur = FMath::Max(1, UInventoryComponent::BagGrams(S.ItemId));
		FDropRow* Ex = BagRows.FindByPredicate([&S](const FDropRow& R) { return R.BagId == S.ItemId; });
		if (Ex) { Ex->Owned += S.Quantity; }
		else    { BagRows.Add({ Cont, S.ItemId, Cap, S.Quantity, Cur, Cur >= Cap }); }
	}
	// STABIELE volgorde (cap, dan gram, dan id) -> geen flash bij een voorraad-wijziging (zie ook Buds hierboven).
	BagRows.Sort([](const FDropRow& A, const FDropRow& B)
	{
		if (A.Cap != B.Cap) { return A.Cap < B.Cap; }
		if (A.Cur != B.Cur) { return A.Cur < B.Cur; }
		return A.BagId.Compare(B.BagId) < 0;
	});
	Drops.Append(BagRows);
	const bool bAnyDrop = Drops.Num() > 0;
	if (ContCard) { ContCard->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (NoContLabel) { NoContLabel->SetVisibility(bAnyDrop ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }

	if (ContDropBox)
	{
		const int32 Used = Drops.Num();
		while (ContCellPool.Num() < Used)
		{
			UPackContCell* Cell = WidgetTree->ConstructWidget<UPackContCell>();
			Cell->Owner = this;
			ContDropBox->AddChildToWrapBox(Cell);
			ContCellPool.Add(Cell);
			ContCellSig.Add(TEXT("\x01"));
		}
		while (ContCellPool.Num() > Used)
		{
			const int32 Last = ContCellPool.Num() - 1;
			if (ContCellPool[Last]) { ContCellPool[Last]->RemoveFromParent(); }
			ContCellPool.RemoveAt(Last);
			if (ContCellSig.IsValidIndex(Last)) { ContCellSig.RemoveAt(Last); }
		}
		for (int32 i = 0; i < Used; ++i)
		{
			const FDropRow& R = Drops[i];
			const bool bBag = !R.BagId.IsNone();
			const FString Sig = FString::Printf(TEXT("%s|%s|%d|%d|%d|%d"), *R.ContId.ToString(), *R.BagId.ToString(), R.Cap, R.Owned, R.Cur, R.bFull ? 1 : 0);
			if (ContCellSig.IsValidIndex(i) && Sig == ContCellSig[i]) { continue; }
			if (ContCellSig.IsValidIndex(i)) { ContCellSig[i] = Sig; }
			UPackContCell* Cell = ContCellPool[i];
			if (!Cell) { continue; }
			Cell->Owner = this;
			Cell->ContId = R.ContId;
			Cell->BagId = R.BagId;     // gezet -> sleepbare zak-cel (uitpakken)
			Cell->bBagFull = R.bFull;
			if (bBag)
			{
				// Zak: volle badge "50g", niet-vol "30/50g". Label = alleen strain (icoon toont al jar/block/sack).
				const FString Badge = R.bFull ? FString::Printf(TEXT("%dg"), R.Cur) : FString::Printf(TEXT("%d/%dg"), R.Cur, R.Cap);
				const FName BagBudId(*FString::Printf(TEXT("Bud_%s"), *UInventoryComponent::BagStrain(R.BagId).ToString()));
				FString Label = WeedUI::PrettyItemName(BagBudId); // "Silver Haze" (geen " jar")
				if (R.Owned > 1) { Label += FString::Printf(TEXT("  x%d"), R.Owned); }
				Cell->SetInner(MakeItemCellContent(WidgetTree, R.BagId, Badge, Label));
				Cell->SetToolTipText(FText::FromString(R.bFull
					? FString::Printf(TEXT("%s\n%dg full - x%d\nDrag onto the weed on the left to unpack."), *WeedUI::PrettyItemName(R.BagId), R.Cur, R.Owned)
					: FString::Printf(TEXT("%s\n%d/%dg - x%d\nDrop same-strain weed to top up (quality mixes), or drag onto the weed to unpack."), *WeedUI::PrettyItemName(R.BagId), R.Cur, R.Cap, R.Owned)));
			}
			else
			{
				// Lege container: nieuw zakje packen.
				Cell->SetInner(MakeItemCellContent(WidgetTree, R.ContId, FString::Printf(TEXT("x%d"), R.Owned),
					FString::Printf(TEXT("%dg %s"), R.Cap, ContainerNoun(R.ContId))));
				Cell->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\nup to %dg - x%d owned\nDrop weed here to pack one."),
					*WeedUI::PrettyItemName(R.ContId), R.Cap, R.Owned)));
			}
		}
	}

	RefreshLanes(); // lopende inpak-balken mee in sync houden (bij openen + na een voorraad-wijziging)
}

// (Her)vult de sleepbare verpakte-zak-cellen (nette namen via MakeItemCellContent, net als bij packen).
// Wordt alleen bij openen/tab-switch/na een unpack aangeroepen -> geen per-frame rebuild.
void UPackWidget::RefreshUnpack()
{
	if (!UnpackPane || !PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	// Welke verpakte bags heb je? Aggregeer per Bag_-Id (aantal stapels van dezelfde bag samen tellen).
	struct FBagRow { FName Id; int32 Owned; };
	TArray<FBagRow> Bags;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (!S.ItemId.ToString().StartsWith(TEXT("Bag_")) || S.Quantity <= 0) { continue; }
		FBagRow* Existing = Bags.FindByPredicate([&S](const FBagRow& R) { return R.Id == S.ItemId; });
		if (Existing) { Existing->Owned += S.Quantity; }
		else          { Bags.Add({ S.ItemId, S.Quantity }); }
	}
	const bool bAnyBag = Bags.Num() > 0;
	if (UnpackEmptyLabel) { UnpackEmptyLabel->SetVisibility(bAnyBag ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }

	if (UnpackBagBox)
	{
		const int32 Used = Bags.Num();
		// Staart-groei van de pool (nieuwe lege cellen); staart-krimp (overtollige weg). GEEN ClearChildren.
		while (UnpackCellPool.Num() < Used)
		{
			UPackBagCell* Cell = WidgetTree->ConstructWidget<UPackBagCell>();
			UnpackBagBox->AddChildToWrapBox(Cell);
			UnpackCellPool.Add(Cell);
			UnpackCellSig.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
		}
		while (UnpackCellPool.Num() > Used)
		{
			const int32 Last = UnpackCellPool.Num() - 1;
			if (UnpackCellPool[Last]) { UnpackCellPool[Last]->RemoveFromParent(); }
			UnpackCellPool.RemoveAt(Last);
			if (UnpackCellSig.IsValidIndex(Last)) { UnpackCellSig.RemoveAt(Last); }
		}
		// Per-cel in-place update: alleen als BagId/count/gram voor deze cel wijzigt (change-sig) -> geen flash.
		for (int32 i = 0; i < Used; ++i)
		{
			const FBagRow& R = Bags[i];
			const int32 BagG = FMath::Max(1, UInventoryComponent::BagGrams(R.Id));
			const FString Sig = FString::Printf(TEXT("%s|%d|%d"), *R.Id.ToString(), R.Owned, BagG);
			if (UnpackCellSig.IsValidIndex(i) && Sig == UnpackCellSig[i]) { continue; }
			if (UnpackCellSig.IsValidIndex(i)) { UnpackCellSig[i] = Sig; }
			UPackBagCell* Cell = UnpackCellPool[i];
			if (!Cell) { continue; }
			Cell->BagId = R.Id; Cell->CountAvail = R.Owned;
			// Grammen-per-zak prominent in de badge (net als de wiet-cellen bij packen); count erbij als je er meer hebt.
			FString Label = WeedUI::PrettyItemName(R.Id);
			if (R.Owned > 1) { Label += FString::Printf(TEXT("  x%d"), R.Owned); }
			Cell->SetInner(MakeItemCellContent(WidgetTree, R.Id, FString::Printf(TEXT("%dg"), BagG), Label));
			const int32 TotalG = BagG * R.Owned;
			Cell->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%dg/bag - x%d owned (%dg total)\nDrag onto the unwrap zone to take grams out."),
				*WeedUI::PrettyItemName(R.Id), BagG, R.Owned, TotalG)));
		}
	}
}

// Drag-drop van een zak op de uitpak-zone: kan er meer dan 1 gram uit -> grammen-popup; anders meteen 1g eruit.
// Sleep een zak op de wiet-kolom (links) = UITPAKKEN: vraag hoeveel gram (popup), dan een unpack-job (halve tijd).
void UPackWidget::UnpackDropped(FName BagId)
{
	if (!PhoneComp.IsValid() || BagId.IsNone()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	const int32 Owned = Inv->GetQuantity(BagId);
	if (Owned <= 0) { return; }
	const int32 PerBag = FMath::Max(1, UInventoryComponent::BagGrams(BagId));
	int32 TotalG = Owned * PerBag;
	for (const FPackJob& J : Jobs) { if (J.UnpackBag == BagId) { TotalG -= J.Grams; } } // minus wat al in de queue zit
	if (TotalG <= 0) { return; }
	if (Jobs.Num() >= kMaxQueue)
	{
		UWeedToast::NotifyPawn(GetOwningPlayerPawn(), -1, 2.0f, FColor(230, 180, 90),
			FString::Printf(TEXT("Bench queue is full (%d) - let some finish first."), kMaxQueue), FString());
		return;
	}
	if (TotalG <= 1) { EnqueueUnpack(BagId, 1); }        // triviaal -> direct
	else             { OpenUnpackPopup(BagId, TotalG); } // vraag hoeveel gram
}

// 1 unpack-job: haal Grams gram uit de zak Bag. Duurt de HELFT van wat inpakken van die grammen zou kosten.
void UPackWidget::EnqueueUnpack(FName Bag, int32 Grams)
{
	if (!PhoneComp.IsValid() || Bag.IsNone()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }
	const int32 PerBag = FMath::Max(1, UInventoryComponent::BagGrams(Bag));
	int32 Total = Inv->GetQuantity(Bag) * PerBag;
	for (const FPackJob& J : Jobs) { if (J.UnpackBag == Bag) { Total -= J.Grams; } }
	const int32 G = FMath::Clamp(Grams, 1, Total);
	if (G <= 0 || Jobs.Num() >= kMaxQueue) { return; }
	const FName Cont = UInventoryComponent::BagContainer(Bag);
	const int32 Cap = Cont.IsNone() ? PerBag : FMath::Max(1, UPhoneClientComponent::ContainerCapacity(Cont));
	// Half van de pack-tijd voor dezelfde grammen (incl. bench-speed).
	const float Dur = FMath::Max(0.25f, PackDurationFor(Cont) / FMath::Max(0.1f, PhoneComp->GetPackSpeed()) * (float(G) / float(FMath::Max(1, Cap))) * 0.5f);
	FPackJob J;
	J.UnpackBag = Bag; J.ContId = Cont; J.Grams = G;
	J.Elapsed = 0.f; J.Duration = Dur;
	Jobs.Add(J);
	RefreshLanes();
}

void UPackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsPackOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); CancelAmount(); return; } // bench dicht -> ook de hoeveelheid-popup sluiten

	// Channel-inpakken: balken vollopen + op voltooiing echt inpakken. Staat NA de bOpen-guard, dus sluiten/
	// weglopen (bPackOpen=false) bevriest de jobs -> "weglopen = pauze". Realtime (DeltaTime) is prima: de bench
	// is alleen open terwijl je 'm bekijkt.
	TickJobs(DeltaTime);

	// Live de bag-slider uitlezen (zonder herbouw, anders springt de slider).
	USlider* ActiveSlider = bUnpackTab ? UnpackSlider.Get() : GramSlider.Get();
	if (ActiveSlider)
	{
		const int32 NewN = (MaxBags <= 1) ? 1 : FMath::Clamp(1 + FMath::RoundToInt(ActiveSlider->GetValue() * float(MaxBags - 1)), 1, MaxBags);
		if (NewN != SelBags)
		{
			SelBags = NewN;
			// Alleen knop-label + Half/Max-highlight bijwerken (NIET SetB/SetUB -> die zou de slider laten springen).
			const int32 HalfN = FMath::Max(1, MaxBags / 2);
			if (bUnpackTab)
			{
				const int32 G = SelBags * UnpackPerBag;
				if (UnpackLabel)
				{
					UnpackLabel->SetText(FText::FromString(FString::Printf(TEXT("Returns %dg loose weed from %d bag%s"), G, SelBags, SelBags == 1 ? TEXT("") : TEXT("s"))));
					UnpackLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				}
				if (UnpackBtnLabel) { UnpackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Unpack %d bag%s - %dg"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
				if (UnpackMaxBtn)  { StyleChoiceBtn(UnpackMaxBtn,  SelBags == MaxBags); }
				if (UnpackHalfBtn) { StyleChoiceBtn(UnpackHalfBtn, SelBags == HalfN && HalfN != MaxBags); }
			}
			else
			{
				const int32 G = FMath::Min(PackBudHave, SelBags * SelGrams);
				if (PackSummaryLabel)
				{
					PackSummaryLabel->SetText(FText::FromString(FString::Printf(TEXT("Uses %dg %s with %d %s"),
						G, *WeedUI::PrettyItemName(SelStrain), SelBags, *WeedUI::PrettyItemName(SelContainer))));
				}
				if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %d bag%s - %dg"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
				if (GramLabel)    { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("Bags: %d / %d"), SelBags, MaxBags))); }
				if (BagsMaxBtn)  { StyleChoiceBtn(BagsMaxBtn,  SelBags == MaxBags); }
				if (BagsHalfBtn) { StyleChoiceBtn(BagsHalfBtn, SelBags == HalfN && HalfN != MaxBags); }
			}
		}
	}

	// Diff-refresh als de relevante voorraad of de strain-keuze wijzigt (NIET bij slider-bewegen).
	// De Refresh-functies bouwen niets af: ze updaten/voegen-toe/verwijderen alleen gewijzigde rijen.
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	FString Sig = SelStrain.ToString() + TEXT("/") + SelContainer.ToString() + TEXT("/") + SelUnpackBag.ToString() + (bUnpackTab ? TEXT("/U") : TEXT("/P"));
	if (Inv) { for (const FInventoryStack& S : Inv->GetStacks()) { const FString Id = S.ItemId.ToString(); if (Id.StartsWith(TEXT("Bud_")) || Id.StartsWith(TEXT("Cont_")) || Id.StartsWith(TEXT("Bag_"))) { Sig += FString::Printf(TEXT("|%s:%d"), *Id, S.Quantity); } } }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

#include "UI/ShelfWidget.h"

#include "UI/WeedUiStyle.h"
#include "UI/InventoryWidget.h" // UInvDragOp: vanuit je echte inventory in het schap droppen = opslaan
#include "UI/WeedItemPickGrid.h" // fridge "Make edibles" = icoon-grid-picker (persistent, geen rebuild-per-klik)
#include "Phone/PhoneClientComponent.h"
#include "World/StorageShelf.h"
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
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"

// ---------------------------------------------------------------------------
//  UShelfCell — sleepbare/droppbare cel
// ---------------------------------------------------------------------------
TSharedRef<SWidget> UShelfCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		const bool bHasItem = !ItemId.IsNone();
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ShelfCellRoot"));
		FSlateBrush ShRB = WeedUI::Rounded(bHasItem ? WeedUI::ColSlot(0.96f) : WeedUI::ColSlotEmpty(0.55f), 10.f);
		if (bHasItem) { ShRB.OutlineSettings.Width = 1.5f; ShRB.OutlineSettings.Color = FSlateColor(WeedUI::ColAccent(0.55f)); }
		Root->SetBrush(ShRB);
		Root->SetPadding(FMargin(5.f));
		WidgetTree->RootWidget = Root;

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Root->SetContent(Ov);

		if (bHasItem)
		{
			UOverlaySlot* IconOS = Ov->AddChildToOverlay(WeedUI::ItemIcon(WidgetTree, ItemId, IconSize));
			IconOS->SetHorizontalAlignment(HAlign_Center); IconOS->SetVerticalAlignment(VAlign_Center);
			if (!Badge.IsEmpty())
			{
				UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
				Pill->SetBrush(WeedUI::Rounded(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), 7.f));
				Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
				Pill->SetContent(WeedUI::Text(WidgetTree, Badge, 10, FLinearColor(0.92f, 0.95f, 1.f), false, true));
				UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
				PS->SetHorizontalAlignment(HAlign_Right); PS->SetVerticalAlignment(VAlign_Bottom);
			}
		}
		else
		{
			UOverlaySlot* HS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, TEXT("+"), 20, WeedUI::ColTextDim(0.7f), true));
			HS->SetHorizontalAlignment(HAlign_Center); HS->SetVerticalAlignment(VAlign_Center);
		}
	}
	if (!Tooltip.IsEmpty()) { SetToolTipText(FText::FromString(Tooltip)); }
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

FReply UShelfCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!ItemId.IsNone() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

void UShelfCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (ItemId.IsNone()) { return; }
	UShelfDragOp* Op = NewObject<UShelfDragOp>(this);
	Op->bFromShelf = bShelfSide;
	Op->ShelfIndex = ShelfIndex;
	Op->ItemId = ItemId;
	Op->Qty = Qty;
	Op->Pivot = EDragPivot::CenterCenter;

	USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
	Vis->SetWidthOverride(52.f); Vis->SetHeightOverride(52.f);
	Vis->SetContent(WeedUI::ItemIcon(WidgetTree, ItemId, 52.f));
	Op->DefaultDragVisual = Vis;
	OutOperation = Op;
}

bool UShelfCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (!Owner.IsValid()) { return false; }
	// Vanuit je echte inventory/hotbar in het schap gesleept -> opslaan.
	if (UInvDragOp* InvOp = Cast<UInvDragOp>(InOperation)) { Owner->HandleInvStore(InvOp); return true; }
	// Een schap-item terug op het schap droppen doet niks (uit het schap halen = naar je inventory slepen).
	return Cast<UShelfDragOp>(InOperation) != nullptr;
}

// ---------------------------------------------------------------------------
//  UShelfDropZone — hele kolom als drop-doel
// ---------------------------------------------------------------------------
TSharedRef<SWidget> UShelfDropZone::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DropRoot"));
		FSlateBrush Empty; Empty.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f)); // transparant, maar wel hit-testbaar
		Root->SetBrush(Empty);
		Root->SetPadding(FMargin(0.f));
		if (Inner) { Root->SetContent(Inner); }
		WidgetTree->RootWidget = Root;
	}
	SetVisibility(ESlateVisibility::Visible); // moet hit-testbaar zijn om drops op de lege ruimte te vangen
	return Super::RebuildWidget();
}

bool UShelfDropZone::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (!Owner.IsValid()) { return false; }
	if (UInvDragOp* InvOp = Cast<UInvDragOp>(InOperation)) { Owner->HandleInvStore(InvOp); return true; }
	return Cast<UShelfDragOp>(InOperation) != nullptr; // schap-item terug op het schap = niks
}

// ---------------------------------------------------------------------------
//  UShelfWidget
// ---------------------------------------------------------------------------
void UShelfWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* ShelfBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 7.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 7.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 7.f);
		S.NormalPadding = FMargin(6.f, 3.f); S.PressedPadding = FMargin(6.f, 3.f);
		B->SetStyle(S);
		B->SetContent(WeedUI::Text(Tree, Label, 11, FLinearColor::White, true));
		return B;
	}
}

TSharedRef<SWidget> UShelfWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UShelfWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ShelfCard"));
	FSlateBrush CardBr = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 18.f);
	CardBr.OutlineSettings.Width = 1.f; CardBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f)); // dunne rand zoals de inventory-card
	CardB->SetBrush(CardBr);
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	// Links-van-het-midden, net als de droogrek: je ECHTE inventory staat rechts ernaast open. Zo gebruik
	// je overal hetzelfde inventory-systeem (slepen tussen je inventory/hotbar en het schap).
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(1.f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(560.f, 452.f));
	CS->SetPosition(FVector2D(-12.f, -30.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("STORAGE"), 18, WeedUI::ColText(), false, true);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	HeadRow->AddChildToHorizontalBox(ShelfBtn(WidgetTree, TEXT("Exit"), WeedUI::ColWarn(0.55f),
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseShelf(); } }));
	Outer->AddChildToVerticalBox(HeadRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	Outer->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Drag from your inventory to store. Drag a shelf item onto your inventory to take it out."), 11, WeedUI::ColTextDim()))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Eén kolom: de inhoud van het schap. (De rechter "jouw inventory"-kolom is weg - dat is nu je echte
	// inventory ernaast.) Hele kolom is drop-doel zodat je overal in het vak kunt loslaten om op te slaan.
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(WeedUI::Rounded(WeedUI::ColWell(), 10.f));
	B->SetPadding(FMargin(8.f));
	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	B->SetContent(VB);
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("On the shelf"), 13, WeedUI::ColTextDim(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	ShelfList = WidgetTree->ConstructWidget<UScrollBox>();
	VB->AddChildToVerticalBox(ShelfList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UShelfDropZone* DZ = WidgetTree->ConstructWidget<UShelfDropZone>();
	DZ->bShelfSide = true; DZ->Owner = this; DZ->Inner = B;
	Outer->AddChildToVerticalBox(DZ)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UShelfWidget::HandleShelfDrop(bool bDroppedOnShelfSide, UShelfDragOp* Op)
{
	if (!Op || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	if (bDroppedOnShelfSide && !Op->bFromShelf)
	{
		// Inventory -> schap: opslaan.
		if (!Op->ItemId.IsNone() && Op->Qty > 0) { Ph->RequestShelfStore(Op->ItemId, Op->Qty); }
	}
	else if (!bDroppedOnShelfSide && Op->bFromShelf)
	{
		// Schap -> inventory: eruit halen.
		if (Op->ShelfIndex >= 0 && Op->Qty > 0) { Ph->RequestShelfTake(Op->ShelfIndex, Op->Qty); }
	}
	LastSig.Reset(); // forceer een refresh
}

void UShelfWidget::HandleInvStore(UInvDragOp* Op)
{
	if (!Op || !PhoneComp.IsValid() || Op->StackId == 0) { return; }
	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }
	const int32 Idx = Inv->FindStackById(Op->StackId);
	const TArray<FInventoryStack>& St = Inv->GetStacks();
	if (!St.IsValidIndex(Idx)) { return; }
	const FInventoryStack& S = St[Idx];
	if (S.ItemId == FName(TEXT("Cash")) || S.Quantity <= 0) { return; } // geen briefgeld opslaan
	PhoneComp->RequestShelfStore(S.ItemId, S.Quantity); // de hele gesleepte stapel het schap in
	LastSig.Reset();
}

void UShelfWidget::FillBody()
{
	if (!ShelfList || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	AStorageShelf* Shelf = Ph->GetShelf();

	if (TitleText && Shelf)
	{
		TitleText->SetText(FText::FromString(FString::Printf(TEXT("%s   (%d/%d)"), *Shelf->GetTitle(), Shelf->Contents.Num(), Shelf->GetCapacity())));
	}

	// Persistente grid + fridge-sectie (éénmalig) -> NOOIT ClearChildren op ShelfList -> geen volledige-lijst-flash.
	if (!ShelfGrid)
	{
		ShelfGrid = WidgetTree->ConstructWidget<UWrapBox>();
		ShelfGrid->SetInnerSlotPadding(FVector2D(5.f, 5.f));
		ShelfList->AddChild(ShelfGrid);
		FridgeSection = WidgetTree->ConstructWidget<UVerticalBox>();
		ShelfList->AddChild(FridgeSection);
	}

	auto MakeCell = [this](int32 ShelfIdx, FName Id, int32 Q, float Thc) -> UShelfCell*
	{
		UShelfCell* C = WidgetTree->ConstructWidget<UShelfCell>();
		C->bShelfSide = true; C->ShelfIndex = ShelfIdx; C->ItemId = Id; C->Qty = Q; C->Thc = Thc; C->Owner = this;
		if (!Id.IsNone())
		{
			const FString S = Id.ToString();
			const bool bBag = UInventoryComponent::IsBag(Id);
			const bool bWeed = bBag || S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("Joint_")) || S.StartsWith(TEXT("WetBud_"));
			C->Badge = WeedUI::ItemQtyBadge(Id, Q);
			C->Tooltip = bBag ? FString::Printf(TEXT("%s\n%dx %dg bag  -  %.0f%% THC"), *WeedUI::PrettyItemName(Id), Q, UInventoryComponent::BagGrams(Id), Thc)
			           : (bWeed ? FString::Printf(TEXT("%s\n%dg  -  %.0f%% THC"), *WeedUI::PrettyItemName(Id), Q, Thc)
			                    : FString::Printf(TEXT("%s\nAmount: %d"), *WeedUI::PrettyItemName(Id), Q));
		}
		return C;
	};

	// Gewenste cellen: één per schap-item + één lege "sleep hierheen"-cel (drop-doel, ook bij leeg schap).
	const int32 N = Shelf ? Shelf->Contents.Num() : 0;
	const int32 Desired = N + 1;

	// Cel-pool op maat (alleen aan de STAART toevoegen/verwijderen -> ongewijzigde cellen blijven staan).
	while (ShelfCellBoxes.Num() < Desired)
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(78.f); Sz->SetHeightOverride(78.f);
		ShelfGrid->AddChildToWrapBox(Sz);
		ShelfCellBoxes.Add(Sz); ShelfCellSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}
	while (ShelfCellBoxes.Num() > Desired)
	{
		const int32 Last = ShelfCellBoxes.Num() - 1;
		if (ShelfCellBoxes[Last]) { ShelfCellBoxes[Last]->RemoveFromParent(); }
		ShelfCellBoxes.RemoveAt(Last); ShelfCellSigs.RemoveAt(Last);
	}

	// Per-cel diff: alleen een cel waarvan de inhoud ECHT wijzigde krijgt een nieuwe UShelfCell (geen flash).
	for (int32 i = 0; i < Desired; ++i)
	{
		const bool bEmpty = (i >= N);
		FName Id = NAME_None; int32 Q = 0; float Thc = 0.f; int32 ShelfIdx = -1;
		FString Sig = TEXT("E");
		if (!bEmpty)
		{
			const FShelfStack& S = Shelf->Contents[i];
			Id = S.ItemId; Q = S.Quantity; Thc = S.Thc; ShelfIdx = i;
			Sig = FString::Printf(TEXT("%s|%d|%.0f"), *Id.ToString(), Q, Thc);
		}
		if (!ShelfCellSigs.IsValidIndex(i) || !ShelfCellBoxes.IsValidIndex(i)) { continue; }
		if (Sig == ShelfCellSigs[i]) { continue; }
		ShelfCellSigs[i] = Sig;
		if (ShelfCellBoxes[i]) { ShelfCellBoxes[i]->SetContent(MakeCell(ShelfIdx, Id, Q, Thc)); }
	}

	RebuildFridgeSection(Shelf);
}

void UShelfWidget::RebuildFridgeSection(AStorageShelf* Shelf)
{
	// Koelkast: edibles maken (ButterMix -> Edible/Cookie/Gummy). Icoon-grid-picker + in-place status-teksten;
	// éénmalig gebouwd -> per refresh alleen SetItems/SetText/SetVisibility (geen ClearChildren-flash op een klik).
	if (!FridgeSection) { return; }
	if (!Shelf || !Shelf->IsFridge()) { FridgeSection->SetVisibility(ESlateVisibility::Collapsed); return; }
	FridgeSection->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Éénmalige opbouw van de fridge-sectie: kop, icoon-grid, hint- en status-regel.
	if (!FridgePick)
	{
		FridgeTitleText = WeedUI::Text(WidgetTree, TEXT("Make edibles"), 13, WeedUI::ColText(), false, true);
		FridgeSection->AddChildToVerticalBox(FridgeTitleText)->SetPadding(FMargin(0.f, 10.f, 0.f, 4.f));

		// Grid-config VOOR de eerste SetItems: geen selectie, kleinere cellen, max 2 rijen (rest scrollt).
		FridgePick = WidgetTree->ConstructWidget<UWeedItemPickGrid>();
		FridgePick->bShowSelection = false;
		FridgePick->CellSize = 72.f;
		FridgePick->IconSize = 46.f;
		FridgePick->MaxVisibleRows = 2;
		FridgePick->OnPick = [this](FName /*Id*/, int32 P)
		{
			// P = schap-slot-index (payload). Server hervalideert de index + ButterMix-check.
			if (PhoneComp.IsValid()) { PhoneComp->RequestShelfCook(P); LastSig.Reset(); }
		};
		FridgeSection->AddChildToVerticalBox(FridgePick)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));

		FridgeHintText = WeedUI::Text(WidgetTree, TEXT("Store butter mix here, then set it into edibles (add sugar+flour for cookies, sugar+gelatin for gummies)."), 11, WeedUI::ColTextDim());
		FridgeSection->AddChildToVerticalBox(FridgeHintText)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		FridgeStatusText = WeedUI::Text(WidgetTree, FString(), 11, WeedUI::ColTextDim());
		FridgeSection->AddChildToVerticalBox(FridgeStatusText)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}

	// Vul het grid: één cel per ButterMix_-stapel (Id=ItemId, Payload=schap-index, Badge="Ng").
	TArray<FWeedPickItem> Items;
	for (int32 i = 0; i < Shelf->Contents.Num(); ++i)
	{
		const FShelfStack& S = Shelf->Contents[i];
		if (!S.ItemId.ToString().StartsWith(TEXT("ButterMix_"))) { continue; }
		FWeedPickItem It;
		It.Id = S.ItemId;
		It.IconId = S.ItemId;
		It.Payload = i; // schap-slot-index -> RequestShelfCook
		It.Badge = FString::Printf(TEXT("%dg"), S.Quantity);
		It.Tooltip = FString::Printf(TEXT("Set %s  (%dg) into edibles"), *WeedUI::PrettyItemName(S.ItemId), S.Quantity);
		Items.Add(It);
	}
	const bool bAnyButter = Items.Num() > 0;
	FridgePick->SetItems(Items);
	FridgePick->SetVisibility(bAnyButter ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// Hint alleen tonen als er geen ButterMix ligt; status-regel alleen tijdens het zetten.
	if (FridgeHintText) { FridgeHintText->SetVisibility(bAnyButter ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); }
	if (FridgeStatusText)
	{
		const int32 C = Shelf->Cooking.Num();
		if (C > 0)
		{
			FridgeStatusText->SetText(FText::FromString(FString::Printf(TEXT("Setting %d batch%s... (~3 min, lands in the fridge)"), C, C == 1 ? TEXT("") : TEXT("es"))));
			FridgeStatusText->SetVisibility(ESlateVisibility::Visible);
		}
		else { FridgeStatusText->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

void UShelfWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsShelfOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	FString Sig;
	if (AStorageShelf* Shelf = PhoneComp->GetShelf())
	{
		Sig += FString::Printf(TEXT("S%d:"), Shelf->Contents.Num());
		for (const FShelfStack& S : Shelf->Contents) { Sig += FString::Printf(TEXT("%s%d|"), *S.ItemId.ToString(), S.Quantity); }
		Sig += FString::Printf(TEXT("C%d"), Shelf->Cooking.Num()); // koelkast: ververs als een edible-batch start/klaar is
	}
	// De shelf-grid toont ALLEEN de shelf-inhoud (de echte inventory staat los ernaast). Dus NIET meer op de
	// inventory-inhoud reageren - anders herbouwde de shelf bij elke backpack-sleep mee = flikker.
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

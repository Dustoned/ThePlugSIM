#include "UI/ShelfWidget.h"

#include "UI/WeedUiStyle.h"
#include "UI/WeedToast.h" // melding als de fridge een niet-eetbaar item weigert (zoals het droogrek)
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
		FSlateBrush ShRB = WeedUI::StorageSlotBrush(bHasItem, false, bHasItem ? WeedUI::ItemAccent(ItemId) : FLinearColor(0.f, 0.f, 0.f, 0.f), 10.f);
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
				Pill->SetBrush(WeedUI::ItemQtyPillBrush());
				Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
				Pill->SetContent(WeedUI::Text(WidgetTree, Badge, 10, WeedUI::ColText(), false, true));
				UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
				PS->SetHorizontalAlignment(HAlign_Right); PS->SetVerticalAlignment(VAlign_Top);
			}
			const FString Tag = WeedUI::ItemTagShort(ItemId);
			if (!Tag.IsEmpty())
			{
				UTextBlock* TagT = WeedUI::Text(WidgetTree, Tag, 10, FLinearColor::White, false, true);
				TagT->SetFont(WeedUI::ItemTagFont(10));
				TagT->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
				TagT->SetShadowOffset(FVector2D(1.f, 1.f));
				UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
				TagPill->SetBrush(WeedUI::ItemTagPillBrush(ItemId, 5.f));
				TagPill->SetPadding(FMargin(5.f, 0.f, 5.f, 1.f));
				TagPill->SetContent(TagT);
				TagPill->SetVisibility(ESlateVisibility::HitTestInvisible);
				UOverlaySlot* TagOS = Ov->AddChildToOverlay(TagPill);
				TagOS->SetHorizontalAlignment(HAlign_Center);
				TagOS->SetVerticalAlignment(VAlign_Bottom);
			}
		}
		// Lege slot: gewoon een leeg vakje (zoals de inventory), geen "+" in elk vakje (was rommelig bij een vol grid).
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
	// Alleen optimistisch de BRON-cel leegmaken als het schap AANTOONBAAR ruimte heeft (server weigert bij
	// Contents>=Capacity zonder mergebare stapel). Bij ruimte slaagt de store gegarandeerd -> veilig clearen;
	// bij twijfel (vol) niet, zodat een weigering nooit een cel ten onrechte leeg laat zonder OnInventoryChanged.
	const AStorageShelf* Shelf = PhoneComp->GetShelf();
	// De fridge accepteert alleen eetbaar-gerelateerde items (net als het droogrek alleen natte buds).
	// Weiger de rest METEEN met een melding -> niet opslaan EN niet clearen, zodat er geen ghost-cel
	// achterblijft (server weigert 'm anders stil en de optimistische clear laat de cel ten onrechte leeg).
	if (Shelf && Shelf->IsFridge() && !UInventoryComponent::IsFridgeItem(S.ItemId))
	{
		UWeedToast::NotifyPawn(P, -1, 3.f, FColor(255, 180, 90), TEXT("Only edibles and cooking items go in the fridge."));
		return;
	}
	const bool bHasRoom = Shelf && Shelf->Contents.Num() < Shelf->GetCapacity();
	PhoneComp->RequestShelfStore(S.ItemId, S.Quantity); // de hele gesleepte stapel het schap in
	// Optimistisch clearen tegen de flash: anders flitst de gesleepte (gedimde) cel na het loslaten eerst weer
	// op VOLLE opacity terug en verdwijnt 'ie pas als de server-store binnen is. Direct leegmaken = geen flash;
	// RebuildContent reconcilieert straks (sig "E" matcht -> cel wordt overgeslagen).
	if (bHasRoom && Op->FromCell >= 0)
	{
		if (UInventoryWidget* IW = PhoneComp->GetInventoryWidget()) { IW->OptimisticClearCell(Op->FromCell); }
	}
	// GEEN LastSig.Reset(): de store is server-authoritative -> Shelf->Contents wijzigt vanzelf, wat de tick-Sig
	// verandert en FillBody triggert (rebuild alleen de gewijzigde cel via de per-cel-sig).
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

	// VAST slot-grid (net als de inventory): toon ALLE capaciteit-slots, gevuld + leeg. Zo is een lege fridge
	// gewoon 8 lege vakjes i.p.v. "1 vakje met +", en zie je in een oogopslag hoeveel ruimte er is.
	const int32 N = Shelf ? Shelf->Contents.Num() : 0;
	const int32 Desired = Shelf ? FMath::Max(N, Shelf->GetCapacity()) : 1;

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
		FridgePick->CellSize = 86.f;
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
	const int32 CookingCount = Shelf->Cooking.Num();
	const bool bShowEdibleTools = bAnyButter || CookingCount > 0;
	FridgeSection->SetVisibility(bShowEdibleTools ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (FridgeTitleText) { FridgeTitleText->SetVisibility(bShowEdibleTools ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	FridgePick->SetItems(Items);
	FridgePick->SetVisibility(bAnyButter ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// Lege fridge toont alleen de lege slots. De edible-tools verschijnen pas bij ButterMix of lopende batches.
	if (FridgeHintText) { FridgeHintText->SetVisibility(ESlateVisibility::Collapsed); }
	if (FridgeStatusText)
	{
		const int32 C = CookingCount;
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

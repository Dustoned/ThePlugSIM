#include "UI/ShelfWidget.h"

#include "UI/WeedUiStyle.h"
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
		Root->SetBrush(WeedUI::Rounded(bHasItem ? FLinearColor(0.11f, 0.13f, 0.17f, 0.96f) : FLinearColor(0.13f, 0.14f, 0.18f, 0.55f), 8.f));
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
			UOverlaySlot* HS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, TEXT("+"), 20, FLinearColor(0.4f, 0.45f, 0.55f), true));
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
	UShelfDragOp* Op = Cast<UShelfDragOp>(InOperation);
	if (!Op || !Owner.IsValid()) { return false; }
	Owner->HandleShelfDrop(bShelfSide, Op); // bShelfSide = de kolom waar je losliet
	return true;
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
	UShelfDragOp* Op = Cast<UShelfDragOp>(InOperation);
	if (Op && Owner.IsValid()) { Owner->HandleShelfDrop(bShelfSide, Op); return true; }
	return false;
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
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.06f, 0.08f, 0.99f), 18.f));
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(680.f, 470.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("STORAGE"), 18, FLinearColor(0.6f, 0.85f, 1.f), false, true);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	HeadRow->AddChildToHorizontalBox(ShelfBtn(WidgetTree, TEXT("Exit"), FLinearColor(0.4f, 0.2f, 0.2f),
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseShelf(); } }));
	Outer->AddChildToVerticalBox(HeadRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	Outer->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Drag items between the shelf and your inventory."), 11, FLinearColor(0.6f, 0.65f, 0.78f)))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UHorizontalBox* Cols = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* ColsSlot = Outer->AddChildToVerticalBox(Cols);
	ColsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto MakeColumn = [this](const FString& Title, const FLinearColor& Col, TObjectPtr<UScrollBox>& OutScroll, bool bShelfSide) -> UWidget*
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		B->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 1.f), 10.f));
		B->SetPadding(FMargin(8.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		B->SetContent(VB);
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Title, 13, Col, false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		OutScroll = WidgetTree->ConstructWidget<UScrollBox>();
		VB->AddChildToVerticalBox(OutScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		// Hele kolom als drop-doel (droppen ergens in het vak telt, niet alleen op een cel).
		UShelfDropZone* DZ = WidgetTree->ConstructWidget<UShelfDropZone>();
		DZ->bShelfSide = bShelfSide; DZ->Owner = this; DZ->Inner = B;
		return DZ;
	};

	UWidget* ShelfCol = MakeColumn(TEXT("On the shelf"), FLinearColor(0.6f, 0.85f, 1.f), ShelfList, true);
	UWidget* InvCol = MakeColumn(TEXT("Your inventory"), FLinearColor(0.7f, 1.f, 0.75f), InvList, false);
	UHorizontalBoxSlot* L = Cols->AddChildToHorizontalBox(ShelfCol); L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); L->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
	UHorizontalBoxSlot* R = Cols->AddChildToHorizontalBox(InvCol);  R->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); R->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
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

void UShelfWidget::FillBody()
{
	if (!ShelfList || !InvList || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	AStorageShelf* Shelf = Ph->GetShelf();
	ShelfList->ClearChildren();
	InvList->ClearChildren();

	if (TitleText && Shelf)
	{
		TitleText->SetText(FText::FromString(FString::Printf(TEXT("%s   (%d/%d)"), *Shelf->GetTitle(), Shelf->Contents.Num(), Shelf->GetCapacity())));
	}

	auto MakeCell = [this](bool bShelfSide, int32 ShelfIdx, FName Id, int32 Q, float Thc) -> UShelfCell*
	{
		UShelfCell* C = WidgetTree->ConstructWidget<UShelfCell>();
		C->bShelfSide = bShelfSide; C->ShelfIndex = ShelfIdx; C->ItemId = Id; C->Qty = Q; C->Thc = Thc; C->Owner = this;
		if (!Id.IsNone())
		{
			const FString S = Id.ToString();
			const bool bBag = UInventoryComponent::IsBag(Id);
			const bool bWeed = bBag || S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("Joint_")) || S.StartsWith(TEXT("WetBud_"));
			C->Badge = WeedUI::ItemQtyBadge(Id, Q);
			C->Tooltip = bBag ? FString::Printf(TEXT("%s\n%dx %dg bag  -  %.0f%% THC"), *WeedUI::PrettyItemName(Id), Q, UInventoryComponent::BagGrams(Id), Thc)
			           : (bWeed ? FString::Printf(TEXT("%s\n%dg  -  %.0f%% THC"), *WeedUI::PrettyItemName(Id), Q, Thc)
			                    : FString::Printf(TEXT("%s\nAantal: %d"), *WeedUI::PrettyItemName(Id), Q));
		}
		return C;
	};
	auto AddGrid = [this](UScrollBox* Into) -> UWrapBox*
	{
		UWrapBox* W = WidgetTree->ConstructWidget<UWrapBox>();
		W->SetInnerSlotPadding(FVector2D(5.f, 5.f));
		Into->AddChild(W);
		return W;
	};

	// --- Schap-kolom ---
	{
		UWrapBox* Grid = AddGrid(ShelfList);
		if (Shelf)
		{
			for (int32 i = 0; i < Shelf->Contents.Num(); ++i)
			{
				const FShelfStack& S = Shelf->Contents[i];
				UShelfCell* C = MakeCell(true, i, S.ItemId, S.Quantity, S.Thc);
				USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
				Sz->SetWidthOverride(78.f); Sz->SetHeightOverride(78.f); Sz->SetContent(C);
				Grid->AddChildToWrapBox(Sz);
			}
		}
		// Lege "drag here"-cel (drop-doel, ook als het schap leeg is).
		UShelfCell* Empty = MakeCell(true, -1, NAME_None, 0, 0.f);
		USizeBox* ESz = WidgetTree->ConstructWidget<USizeBox>();
		ESz->SetWidthOverride(78.f); ESz->SetHeightOverride(78.f); ESz->SetContent(Empty);
		Grid->AddChildToWrapBox(ESz);
	}

	// --- Inventory-kolom (per item samengevoegd) ---
	{
		UWrapBox* Grid = AddGrid(InvList);
		APawn* P = GetOwningPlayerPawn();
		const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
		if (Inv)
		{
			TArray<FName> Order; TMap<FName, int32> Totals;
			for (const FInventoryStack& St : Inv->GetStacks())
			{
				if (St.ItemId == FName(TEXT("Cash")) || St.Quantity <= 0) { continue; }
				if (!Totals.Contains(St.ItemId)) { Order.Add(St.ItemId); }
				Totals.FindOrAdd(St.ItemId) += St.Quantity;
			}
			for (const FName& Id : Order)
			{
				UShelfCell* C = MakeCell(false, -1, Id, Totals[Id], Inv->GetItemQuality(Id));
				USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
				Sz->SetWidthOverride(78.f); Sz->SetHeightOverride(78.f); Sz->SetContent(C);
				Grid->AddChildToWrapBox(Sz);
			}
		}
		UShelfCell* Empty = MakeCell(false, -1, NAME_None, 0, 0.f);
		USizeBox* ESz = WidgetTree->ConstructWidget<USizeBox>();
		ESz->SetWidthOverride(78.f); ESz->SetHeightOverride(78.f); ESz->SetContent(Empty);
		Grid->AddChildToWrapBox(ESz);
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
	}
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			Sig += TEXT("I:");
			for (const FInventoryStack& St : Inv->GetStacks()) { Sig += FString::Printf(TEXT("%s%d|"), *St.ItemId.ToString(), St.Quantity); }
		}
	}
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

#include "UI/InventoryWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Inventory/InventoryComponent.h"

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
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CellRoot"));
		Root->SetBrush(WeedUI::Rounded(Bg, Radius));
		Root->SetPadding(FMargin(6.f, 4.f, 6.f, 4.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->SetContent(VB);

		if (!Line1.IsEmpty())
		{
			UTextBlock* T1 = WeedUI::Text(WidgetTree, Line1, 11, FLinearColor(0.9f, 0.93f, 1.f));
			T1->SetClipping(EWidgetClipping::ClipToBounds);
			VB->AddChildToVerticalBox(T1);
		}
		if (!Line2.IsEmpty())
		{
			VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Line2, 10, FLinearColor(0.75f, 0.8f, 0.7f)));
		}
		if (bShowMerge)
		{
			UWeedActionButton* M = WidgetTree->ConstructWidget<UWeedActionButton>();
			M->OnClicked.AddDynamic(M, &UWeedActionButton::Handle);
			TFunction<void()> Fn = MergeFn;
			M->OnAction.BindLambda([Fn](int32, int32) { if (Fn) { Fn(); } });
			FButtonStyle S;
			S.Normal = WeedUI::Rounded(FLinearColor(0.5f, 0.4f, 0.1f), 6.f);
			S.Hovered = WeedUI::Rounded(FLinearColor(0.65f, 0.52f, 0.13f), 6.f);
			S.Pressed = WeedUI::Rounded(FLinearColor(0.4f, 0.32f, 0.08f), 6.f);
			S.NormalPadding = FMargin(6.f, 3.f); S.PressedPadding = FMargin(6.f, 3.f);
			M->SetStyle(S);
			M->SetContent(WeedUI::Text(WidgetTree, TEXT("Merge"), 10, FLinearColor::White, true));
			VB->AddChildToVerticalBox(M)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		}
	}
	// Hit-test zichtbaar zodat de cel muis/drag-events ontvangt.
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

FReply UInvCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggable && StackId != 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
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
	Op->Pivot = EDragPivot::MouseDown;

	// Klein sleep-visueel: het label in een afgeronde kaart.
	UBorder* Vis = WidgetTree->ConstructWidget<UBorder>();
	Vis->SetBrush(WeedUI::Rounded(FLinearColor(0.18f, 0.4f, 0.55f, 0.95f), 8.f));
	Vis->SetPadding(FMargin(8.f, 5.f, 8.f, 5.f));
	Vis->SetContent(WeedUI::Text(WidgetTree, Line1.IsEmpty() ? TEXT("item") : Line1, 11, FLinearColor::White, true));
	Op->DefaultDragVisual = Vis;

	OutOperation = Op;
}

bool UInvCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UInvDragOp* Op = Cast<UInvDragOp>(InOperation);
	if (!Op || !Inv.IsValid() || Op->StackId == 0) { return false; }

	if (SlotIndex >= 0)
	{
		// Drop op een hotbar-slot -> toewijzen (Assign wisselt netjes als 'ie al ergens stond).
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
	CS->SetSize(FVector2D(580.f, 460.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

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

	// Item-tegels (wrap) in een scrollbox zodat een vol rooster scrollt.
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* GS = VB->AddChildToVerticalBox(Scroll);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Grid = WidgetTree->ConstructWidget<UWrapBox>();
	Grid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	Scroll->AddChild(Grid);

	// Hotbar-rij.
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Hotbar"), 12, FLinearColor(0.7f, 0.7f, 0.8f)))
		->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));
	HotbarBox = WidgetTree->ConstructWidget<UHorizontalBox>();
	VB->AddChildToVerticalBox(HotbarBox);
}

void UInventoryWidget::RebuildContent()
{
	UInventoryComponent* Inv = GetInv();
	if (!Inv || !Grid || !HotbarBox) { return; }
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
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(126.f); Sz->SetHeightOverride(54.f);

		UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
		Cell->SlotIndex = -1; Cell->GridCell = cell;
		Cell->Inv = Inv; Cell->Owner = this;

		const int32 Idx = Inv->FindStackById(StackId);
		if (StackId != 0 && Stacks.IsValidIndex(Idx))
		{
			const FInventoryStack& S = Stacks[Idx];
			const FName ItemId = S.ItemId;
			const bool bWeed = ItemId.ToString().StartsWith(TEXT("Bud_")) || ItemId.ToString().StartsWith(TEXT("Joint_"));
			const bool bCash = (ItemId == TEXT("Cash"));
			const bool bOnHotbar = Inv->IsStackOnHotbar(StackId);

			Cell->StackId = StackId;
			Cell->bDraggable = !bCash; // briefgeld kun je niet op de hotbar slepen
			if (bCash)
			{
				// Briefgeld: groen/goud, toon het bedrag i.p.v. een aantal.
				Cell->Bg = FLinearColor(0.10f, 0.16f, 0.10f, 0.97f);
				Cell->Line1 = TEXT("Cash");
				Cell->Line2 = FString::Printf(TEXT("EUR %d"), S.Quantity);
			}
			else
			{
				// Items die ook op de hotbar staan krijgen een blauwige tint zodat je ze herkent.
				Cell->Bg = bOnHotbar ? FLinearColor(0.12f, 0.18f, 0.26f, 0.97f) : FLinearColor(0.11f, 0.12f, 0.16f, 0.95f);
				FString Nm = WeedUI::PrettyItemName(ItemId);
				if (Nm.Len() > 16) { Nm = Nm.Left(15) + TEXT("."); }
				Cell->Line1 = bOnHotbar ? (Nm + TEXT("  *")) : Nm;
				Cell->Line2 = bWeed
					? FString::Printf(TEXT("x%d  THC%.0f%% Q%.0f%%"), S.Quantity, S.Quality, S.QualityPct)
					: FString::Printf(TEXT("x%d"), S.Quantity);
				if (bWeed && Ph && Inv->CountStacksOf(ItemId) > 1)
				{
					Cell->bShowMerge = true;
					Cell->MergeFn = [Ph, ItemId]() { Ph->MergeNow(ItemId); };
				}
			}
		}
		else
		{
			// Lege cel: drop-doel (verplaatsen binnen rooster / van hotbar halen), niet sleepbaar.
			Cell->StackId = 0; Cell->bDraggable = false;
			Cell->Bg = FLinearColor(0.10f, 0.10f, 0.13f, 0.35f);
		}
		Sz->SetContent(Cell);
		Grid->AddChildToWrapBox(Sz);
	}

	// --- Hotbar-rij (elk slot = drop-doel; bezet slot is ook sleepbaar) ---
	HotbarBox->ClearChildren();
	for (int32 h = 0; h < UInventoryComponent::HotbarSize; ++h)
	{
		const int32 SlotStackId = Inv->GetHotbarStackId(h);
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(64.f); Sz->SetHeightOverride(46.f);

		UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
		Cell->StackId = SlotStackId; Cell->SlotIndex = h; Cell->bDraggable = (SlotStackId != 0);
		Cell->Inv = Inv; Cell->Owner = this;
		Cell->Radius = 6.f;
		Cell->Bg = (h == Inv->GetActiveSlot()) ? FLinearColor(0.2f, 0.3f, 0.16f) : FLinearColor(0.10f, 0.11f, 0.14f);
		Cell->Line1 = FString::Printf(TEXT("%d"), h + 1);
		const int32 Idx = Inv->FindStackById(SlotStackId);
		if (Stacks.IsValidIndex(Idx))
		{
			FString Nm = WeedUI::PrettyItemName(Stacks[Idx].ItemId);
			if (Nm.Len() > 9) { Nm = Nm.Left(8) + TEXT("."); }
			Cell->Line2 = Nm;
		}
		Sz->SetContent(Cell);
		UHorizontalBoxSlot* HS = HotbarBox->AddChildToHorizontalBox(Sz);
		HS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
	}
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

	if (bDirty)
	{
		bDirty = false;
		RebuildContent();
	}
}

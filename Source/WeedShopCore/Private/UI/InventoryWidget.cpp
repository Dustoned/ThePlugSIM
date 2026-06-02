#include "UI/InventoryWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Inventory/InventoryComponent.h"
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

		if (bHotbar)
		{
			// Compacte hotbar-cel: icoon gecentreerd; slotnummer linksboven; aantal rechtsonder.
			UOverlaySlot* IconOS = Ov->AddChildToOverlay(
				bHasIcon ? WeedUI::ItemIcon(WidgetTree, IconId, IconSize)
				         : Cast<UWidget>(WeedUI::Text(WidgetTree, TEXT(""), 8, FLinearColor::Transparent)));
			IconOS->SetHorizontalAlignment(HAlign_Center);
			IconOS->SetVerticalAlignment(VAlign_Center);
		}
		else
		{
			// Rooster-cel: icoon links, naam (mag afbreken) + sub-regel rechts.
			UHorizontalBox* HB = WidgetTree->ConstructWidget<UHorizontalBox>();
			Ov->AddChildToOverlay(HB);

			if (bHasIcon)
			{
				USizeBox* IconSz = WidgetTree->ConstructWidget<USizeBox>();
				IconSz->SetWidthOverride(IconSize); IconSz->SetHeightOverride(IconSize);
				IconSz->SetContent(WeedUI::ItemIcon(WidgetTree, IconId, IconSize));
				UHorizontalBoxSlot* IS = HB->AddChildToHorizontalBox(IconSz);
				IS->SetVerticalAlignment(VAlign_Center);
				IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			}

			UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
			UHorizontalBoxSlot* VBS = HB->AddChildToHorizontalBox(VB);
			VBS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			VBS->SetVerticalAlignment(VAlign_Center);

			if (!Line1.IsEmpty())
			{
				UTextBlock* T1 = WeedUI::Text(WidgetTree, Line1, 12, FLinearColor(0.93f, 0.95f, 1.f), false, true);
				T1->SetAutoWrapText(true);
				VB->AddChildToVerticalBox(T1);
			}
			if (!Line2.IsEmpty())
			{
				VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Line2, 10, FLinearColor(0.68f, 0.72f, 0.82f)));
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
				S.NormalPadding = FMargin(6.f, 2.f); S.PressedPadding = FMargin(6.f, 2.f);
				M->SetStyle(S);
				M->SetContent(WeedUI::Text(WidgetTree, TEXT("Merge"), 10, FLinearColor::White, true));
				VB->AddChildToVerticalBox(M)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
			}
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
	CS->SetSize(FVector2D(900.f, 520.f));
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

	// --- Rechts: slots (wrap, scrollbaar) + hotbar ---
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* RS = Body->AddChildToHorizontalBox(Right);
	RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* GS = Right->AddChildToVerticalBox(Scroll);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Grid = WidgetTree->ConstructWidget<UWrapBox>();
	Grid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	Scroll->AddChild(Grid);

	// Hotbar-rij.
	Right->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Hotbar"), 12, FLinearColor(0.7f, 0.7f, 0.8f)))
		->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));
	HotbarBox = WidgetTree->ConstructWidget<UHorizontalBox>();
	Right->AddChildToVerticalBox(HotbarBox);
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
		if (bWeed) { Sub = FString::Printf(TEXT("%dg   %.0f%% THC%s"), N, Thc, bWet ? TEXT("  (wet)") : TEXT("")); }
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
		const int32 Idx = Inv->FindStackById(StackId);
		// Items die op de hotbar staan tonen we GEEN cel (ze staan in de hotbar-rij). Zo zijn de lege
		// vakjes precies de echt vrije slots (en niet ook nog de plekken van hotbar-items).
		if (StackId != 0 && Stacks.IsValidIndex(Idx) && Inv->IsStackOnHotbar(StackId)) { continue; }

		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(176.f); Sz->SetHeightOverride(72.f);

		UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
		Cell->SlotIndex = -1; Cell->GridCell = cell;
		Cell->Inv = Inv; Cell->Owner = this;
		Cell->IconSize = 40.f;

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
				Cell->Line2 = FString::Printf(TEXT("EUR %d"), S.Quantity);
				Cell->Badge.Empty();
				Cell->Tooltip += FString::Printf(TEXT("\nEUR %d contant"), S.Quantity);
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
				Cell->Badge = bBud ? FString::Printf(TEXT("%dg"), S.Quantity) : FString::Printf(TEXT("x%d"), S.Quantity);
				Cell->Tooltip += bWeed
					? FString::Printf(TEXT("\n%dg  -  THC %.0f%%   Kwaliteit %.0f%%"), S.Quantity, S.Quality, S.QualityPct)
					: FString::Printf(TEXT("\nAantal: %d"), S.Quantity);
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

	// --- Hotbar-rij (elk slot = drop-doel; bezet slot is ook sleepbaar) ---
	HotbarBox->ClearChildren();
	for (int32 h = 0; h < UInventoryComponent::HotbarSize; ++h)
	{
		const int32 SlotStackId = Inv->GetHotbarStackId(h);
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(58.f); Sz->SetHeightOverride(54.f);

		UInvCell* Cell = WidgetTree->ConstructWidget<UInvCell>();
		Cell->StackId = SlotStackId; Cell->SlotIndex = h; Cell->bDraggable = (SlotStackId != 0);
		Cell->Inv = Inv; Cell->Owner = this;
		Cell->Radius = 7.f;
		Cell->IconSize = 30.f;
		Cell->bSlotNumber = true; Cell->SlotNumber = h + 1;
		const bool bActive = (h == Inv->GetActiveSlot());
		Cell->Bg = bActive ? FLinearColor(0.18f, 0.30f, 0.15f, 0.98f) : FLinearColor(0.10f, 0.11f, 0.14f, 0.96f);
		const int32 Idx = Inv->FindStackById(SlotStackId);
		if (Stacks.IsValidIndex(Idx))
		{
			const FInventoryStack& S = Stacks[Idx];
			const FString IdStr = S.ItemId.ToString();
			const bool bBud = IdStr.StartsWith(TEXT("Bud_")) || IdStr.StartsWith(TEXT("WetBud_"));
			Cell->IconId = S.ItemId;
			Cell->Accent = bActive ? FLinearColor(0.55f, 0.95f, 0.5f) : WeedUI::ItemAccent(S.ItemId);
			const FString HbName = WeedUI::PrettyItemName(S.ItemId);
			if (S.ItemId == TEXT("Cash"))
			{
				Cell->Tooltip = FString::Printf(TEXT("%s\nEUR %d contant"), *HbName, S.Quantity);
			}
			else
			{
				Cell->Badge = bBud ? FString::Printf(TEXT("%dg"), S.Quantity) : FString::Printf(TEXT("x%d"), S.Quantity);
				Cell->Tooltip = FString::Printf(TEXT("%s  (%s)"), *HbName, *Cell->Badge);
			}
		}
		Sz->SetContent(Cell);
		UHorizontalBoxSlot* HS = HotbarBox->AddChildToHorizontalBox(Sz);
		HS->SetPadding(FMargin(3.f, 0.f, 3.f, 0.f));
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

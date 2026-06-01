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
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"

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
	CS->SetSize(FVector2D(640.f, 460.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop-balk.
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("STORAGE SHELF"), 18, FLinearColor(0.6f, 0.85f, 1.f), false, true);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(TitleText);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	HeadRow->AddChildToHorizontalBox(ShelfBtn(WidgetTree, TEXT("Exit"), FLinearColor(0.4f, 0.2f, 0.2f),
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseShelf(); } }));
	Outer->AddChildToVerticalBox(HeadRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	// Twee kolommen.
	UHorizontalBox* Cols = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* ColsSlot = Outer->AddChildToVerticalBox(Cols);
	ColsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto MakeColumn = [this](const FString& Title, const FLinearColor& Col, TObjectPtr<UScrollBox>& OutScroll) -> UWidget*
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		B->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 1.f), 10.f));
		B->SetPadding(FMargin(8.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		B->SetContent(VB);
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Title, 13, Col, false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		OutScroll = WidgetTree->ConstructWidget<UScrollBox>();
		VB->AddChildToVerticalBox(OutScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return B;
	};

	UWidget* ShelfCol = MakeColumn(TEXT("On the shelf  (take)"), FLinearColor(0.6f, 0.85f, 1.f), ShelfList);
	UWidget* InvCol = MakeColumn(TEXT("Your inventory  (store)"), FLinearColor(0.7f, 1.f, 0.75f), InvList);
	UHorizontalBoxSlot* L = Cols->AddChildToHorizontalBox(ShelfCol); L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); L->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
	UHorizontalBoxSlot* R = Cols->AddChildToHorizontalBox(InvCol);  R->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); R->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
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
		TitleText->SetText(FText::FromString(FString::Printf(TEXT("STORAGE SHELF   (%d/%d)"), Shelf->Contents.Num(), AStorageShelf::Capacity)));
	}

	auto MakeRow = [this](UScrollBox* Into, const FString& Name, const FString& Sub, TFunction<void()> OneFn, const FString& OneLbl,
		TFunction<void()> AllFn, const FString& AllLbl, const FLinearColor& BtnCol)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(WeedUI::Rounded(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 7.f));
		Card->SetPadding(FMargin(7.f, 5.f, 7.f, 5.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(VB);
		UTextBlock* NameT = WeedUI::Text(WidgetTree, Name, 12, FLinearColor(0.95f, 0.97f, 1.f));
		NameT->SetClipping(EWidgetClipping::ClipToBounds);
		VB->AddChildToVerticalBox(NameT);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* SubS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Sub, 10, FLinearColor(0.62f, 0.66f, 0.76f)));
		SubS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SubS->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(ShelfBtn(WidgetTree, OneLbl, BtnCol, OneFn))->SetPadding(FMargin(2.f, 0.f, 0.f, 0.f));
		Row->AddChildToHorizontalBox(ShelfBtn(WidgetTree, AllLbl, BtnCol, AllFn))->SetPadding(FMargin(3.f, 0.f, 0.f, 0.f));
		VB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		Into->AddChild(Card);
		Into->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
	};

	// --- Schap-inhoud (take) ---
	if (Shelf)
	{
		if (Shelf->Contents.Num() == 0)
		{
			ShelfList->AddChild(WeedUI::Text(WidgetTree, TEXT("Empty."), 12, FLinearColor::Gray));
		}
		for (int32 i = 0; i < Shelf->Contents.Num(); ++i)
		{
			const FShelfStack& S = Shelf->Contents[i];
			const FString Name = WeedUI::PrettyItemName(S.ItemId);
			const bool bWeed = S.ItemId.ToString().StartsWith(TEXT("Bag_")) || S.ItemId.ToString().StartsWith(TEXT("Bud_")) || S.ItemId.ToString().StartsWith(TEXT("Joint_"));
			const FString Sub = bWeed ? FString::Printf(TEXT("x%d   %.0f%% THC"), S.Quantity, S.Thc) : FString::Printf(TEXT("x%d"), S.Quantity);
			const int32 Idx = i; const int32 Qty = S.Quantity;
			MakeRow(ShelfList, Name, Sub,
				[Ph, Idx]() { Ph->RequestShelfTake(Idx, 1); }, TEXT("Take 1"),
				[Ph, Idx, Qty]() { Ph->RequestShelfTake(Idx, Qty); }, TEXT("All"),
				FLinearColor(0.2f, 0.4f, 0.55f));
		}
	}

	// --- Eigen voorraad (store) ---
	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (Inv)
	{
		// Per item-id samenvoegen (totaal aantal).
		TArray<FName> Order; TMap<FName, int32> Totals;
		for (const FInventoryStack& St : Inv->GetStacks())
		{
			if (St.ItemId == FName(TEXT("Cash")) || St.Quantity <= 0) { continue; }
			if (!Totals.Contains(St.ItemId)) { Order.Add(St.ItemId); }
			Totals.FindOrAdd(St.ItemId) += St.Quantity;
		}
		if (Order.Num() == 0) { InvList->AddChild(WeedUI::Text(WidgetTree, TEXT("Nothing to store."), 12, FLinearColor::Gray)); }
		for (const FName& Id : Order)
		{
			const int32 Have = Totals[Id];
			const FString Name = WeedUI::PrettyItemName(Id);
			const bool bWeed = Id.ToString().StartsWith(TEXT("Bag_")) || Id.ToString().StartsWith(TEXT("Bud_")) || Id.ToString().StartsWith(TEXT("Joint_")) || Id.ToString().StartsWith(TEXT("WetBud_"));
			const FString Sub = bWeed ? FString::Printf(TEXT("x%d   %.0f%% THC"), Have, Inv->GetItemQuality(Id)) : FString::Printf(TEXT("x%d"), Have);
			const FName PickId = Id; const int32 Qty = Have;
			MakeRow(InvList, Name, Sub,
				[Ph, PickId]() { Ph->RequestShelfStore(PickId, 1); }, TEXT("Store 1"),
				[Ph, PickId, Qty]() { Ph->RequestShelfStore(PickId, Qty); }, TEXT("All"),
				FLinearColor(0.2f, 0.45f, 0.28f));
		}
	}
}

void UShelfWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsShelfOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Signature uit schap-inhoud + eigen voorraad; alleen herbouwen bij wijziging (geen flicker).
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

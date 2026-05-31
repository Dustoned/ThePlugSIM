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
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

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
	// Knop met vrije callback (lokaal, voor inventory-tegels).
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
	UWeedActionButton* CloseBtn = TileButton(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->ToggleInventory(); } });
	CloseBtn->SetContent(WeedUI::Text(WidgetTree, TEXT("Close (I)"), 12, FLinearColor::White, true));
	Head->AddChildToHorizontalBox(CloseBtn)->SetVerticalAlignment(VAlign_Center);
	VB->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Item-tegels (wrap).
	Grid = WidgetTree->ConstructWidget<UWrapBox>();
	Grid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	UVerticalBoxSlot* GS = VB->AddChildToVerticalBox(Grid);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Hotbar-rij.
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Hotbar  (click an item to add, a slot to remove)"), 11, FLinearColor(0.7f, 0.7f, 0.8f)))
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

	// --- Vrije items (niet op de hotbar) ---
	Grid->ClearChildren();
	for (int32 i = 0; i < Stacks.Num(); ++i)
	{
		if (Inv->IsStackOnHotbar(Stacks[i].StackId)) { continue; }
		const FInventoryStack& S = Stacks[i];
		const int32 StackId = S.StackId;
		const FName ItemId = S.ItemId;
		const bool bWeed = ItemId.ToString().StartsWith(TEXT("Bud_")) || ItemId.ToString().StartsWith(TEXT("Joint_"));

		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(126.f); Sz->SetHeightOverride(54.f);

		UWeedActionButton* Tile = TileButton(WidgetTree, FLinearColor(0.11f, 0.12f, 0.16f, 0.95f), 8.f,
			[Inv, StackId]()
			{
				int32 Empty = Inv->GetActiveSlot();
				for (int32 h = 0; h < UInventoryComponent::HotbarSize; ++h) { if (Inv->GetHotbarStackId(h) == 0) { Empty = h; break; } }
				Inv->AssignHotbarStack(Empty, StackId);
			});
		Sz->SetContent(Tile);

		UVerticalBox* TVB = WidgetTree->ConstructWidget<UVerticalBox>();
		Tile->SetContent(TVB);
		FString Nm = WeedUI::PrettyItemName(ItemId);
		if (Nm.Len() > 16) { Nm = Nm.Left(15) + TEXT("."); }
		UTextBlock* NameT = WeedUI::Text(WidgetTree, Nm, 11, FLinearColor(0.9f, 0.93f, 1.f));
		NameT->SetClipping(EWidgetClipping::ClipToBounds);
		TVB->AddChildToVerticalBox(NameT);
		TVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, bWeed
			? FString::Printf(TEXT("x%d  THC%.0f%% Q%.0f%%"), S.Quantity, S.Quality, S.QualityPct)
			: FString::Printf(TEXT("x%d"), S.Quantity), 10, FLinearColor(0.75f, 0.8f, 0.7f)));

		// Merge-knopje als er meerdere batches van dit wiet-product zijn.
		if (bWeed && Ph && Inv->CountStacksOf(ItemId) > 1)
		{
			UWeedActionButton* M = TileButton(WidgetTree, FLinearColor(0.5f, 0.4f, 0.1f), 6.f, [Ph, ItemId]() { Ph->MergeNow(ItemId); });
			M->SetContent(WeedUI::Text(WidgetTree, TEXT("Merge"), 10, FLinearColor::White, true));
			TVB->AddChildToVerticalBox(M);
		}

		Grid->AddChildToWrapBox(Sz);
	}

	// Ghost-vakjes voor resterende ruimte.
	const int32 Free = (Inv->MaxStacks > 0) ? FMath::Max(0, Inv->MaxStacks - Inv->GetUsedSlots()) : 0;
	for (int32 g = 0; g < FMath::Min(Free, 12); ++g)
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(126.f); Sz->SetHeightOverride(54.f);
		UBorder* Ghost = WidgetTree->ConstructWidget<UBorder>();
		Ghost->SetBrush(WeedUI::Rounded(FLinearColor(0.10f, 0.10f, 0.13f, 0.35f), 8.f));
		Sz->SetContent(Ghost);
		Grid->AddChildToWrapBox(Sz);
	}

	// --- Hotbar-rij (klik = van hotbar halen) ---
	HotbarBox->ClearChildren();
	for (int32 h = 0; h < UInventoryComponent::HotbarSize; ++h)
	{
		const int32 SlotStackId = Inv->GetHotbarStackId(h);
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(64.f); Sz->SetHeightOverride(46.f);
		UWeedActionButton* SlotBtn = TileButton(WidgetTree, (h == Inv->GetActiveSlot()) ? FLinearColor(0.2f, 0.3f, 0.16f) : FLinearColor(0.10f, 0.11f, 0.14f), 6.f,
			[Inv, SlotStackId]() { if (SlotStackId != 0) { Inv->UnassignHotbarStack(SlotStackId); } });
		Sz->SetContent(SlotBtn);
		UVerticalBox* SV = WidgetTree->ConstructWidget<UVerticalBox>();
		SlotBtn->SetContent(SV);
		SV->AddChildToVerticalBox(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), h + 1), 9, FLinearColor(0.5f, 0.5f, 0.6f)));
		const int32 Idx = Inv->FindStackById(SlotStackId);
		if (Stacks.IsValidIndex(Idx))
		{
			FString Nm = WeedUI::PrettyItemName(Stacks[Idx].ItemId);
			if (Nm.Len() > 9) { Nm = Nm.Left(8) + TEXT("."); }
			UTextBlock* NT = WeedUI::Text(WidgetTree, Nm, 10, FLinearColor(0.9f, 0.93f, 1.f));
			NT->SetClipping(EWidgetClipping::ClipToBounds);
			SV->AddChildToVerticalBox(NT);
		}
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

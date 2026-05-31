#include "UI/HotbarWidget.h"

#include "UI/WeedUiStyle.h"
#include "Inventory/InventoryComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UHotbarWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UHotbarWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Bar);
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(0.5f, 1.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -18.f));

	const int32 N = UInventoryComponent::HotbarSize;
	for (int32 i = 0; i < N; ++i)
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(80.f);
		Sz->SetHeightOverride(54.f);

		UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
		Box->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 0.9f), 8.f));
		Box->SetPadding(FMargin(6.f, 4.f, 6.f, 4.f));
		Sz->SetContent(Box);

		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Box->SetContent(VB);

		UTextBlock* Num = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), i + 1), 9, FLinearColor(0.5f, 0.5f, 0.6f));
		VB->AddChildToVerticalBox(Num);
		UTextBlock* Name = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.9f, 0.93f, 1.f));
		Name->SetClipping(EWidgetClipping::ClipToBounds);
		VB->AddChildToVerticalBox(Name);
		UTextBlock* Info = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.75f, 0.85f, 0.7f));
		VB->AddChildToVerticalBox(Info);

		UHorizontalBoxSlot* BS = Bar->AddChildToHorizontalBox(Sz);
		BS->SetPadding(FMargin(3.f, 0.f, 3.f, 0.f));

		SlotBoxes.Add(Box);
		SlotNames.Add(Name);
		SlotInfos.Add(Info);
	}
}

void UHotbarWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }

	const int32 Active = Inv->GetActiveSlot();
	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();

	for (int32 i = 0; i < SlotBoxes.Num(); ++i)
	{
		const bool bActive = (i == Active);
		SlotBoxes[i]->SetBrush(WeedUI::Rounded(bActive ? FLinearColor(0.20f, 0.30f, 0.16f, 0.96f) : FLinearColor(0.08f, 0.09f, 0.12f, 0.88f), 8.f));

		const int32 Idx = Inv->FindStackById(Inv->GetHotbarStackId(i));
		if (Stacks.IsValidIndex(Idx))
		{
			const FInventoryStack& S = Stacks[Idx];
			FString Nm = WeedUI::PrettyItemName(S.ItemId);
			if (Nm.Len() > 12) { Nm = Nm.Left(11) + TEXT("."); }
			SlotNames[i]->SetText(FText::FromString(Nm));
			const bool bWeed = S.ItemId.ToString().StartsWith(TEXT("Bud_")) || S.ItemId.ToString().StartsWith(TEXT("Joint_"));
			SlotInfos[i]->SetText(FText::FromString(bWeed
				? FString::Printf(TEXT("x%d  %.0f%%"), S.Quantity, S.Quality)
				: FString::Printf(TEXT("x%d"), S.Quantity)));
		}
		else
		{
			SlotNames[i]->SetText(FText::GetEmpty());
			SlotInfos[i]->SetText(FText::GetEmpty());
		}
	}
}

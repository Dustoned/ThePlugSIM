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
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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
		Sz->SetWidthOverride(66.f);
		Sz->SetHeightOverride(66.f);

		UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
		Box->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 0.9f), 8.f));
		Box->SetPadding(FMargin(4.f));
		Sz->SetContent(Box);

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Box->SetContent(Ov);

		// Icoon, gecentreerd (inhoud wordt in de tick gezet/gewisseld).
		USizeBox* IcoBox = WidgetTree->ConstructWidget<USizeBox>();
		IcoBox->SetWidthOverride(34.f); IcoBox->SetHeightOverride(34.f);
		UOverlaySlot* IcoOS = Ov->AddChildToOverlay(IcoBox);
		IcoOS->SetHorizontalAlignment(HAlign_Center);
		IcoOS->SetVerticalAlignment(VAlign_Center);

		// Slotnummer linksboven.
		UOverlaySlot* NumOS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), i + 1), 9, FLinearColor(0.55f, 0.58f, 0.7f), false, true));
		NumOS->SetHorizontalAlignment(HAlign_Left);
		NumOS->SetVerticalAlignment(VAlign_Top);

		// Aantal/gram-badge rechtsonder.
		UTextBlock* Badge = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.92f, 0.95f, 1.f), false, true);
		UOverlaySlot* BadgeOS = Ov->AddChildToOverlay(Badge);
		BadgeOS->SetHorizontalAlignment(HAlign_Right);
		BadgeOS->SetVerticalAlignment(VAlign_Bottom);

		// Naam onderaan, klein (gecentreerd, afgekapt).
		UTextBlock* Name = WeedUI::Text(WidgetTree, TEXT(""), 8, FLinearColor(0.85f, 0.88f, 0.96f), true);
		Name->SetClipping(EWidgetClipping::ClipToBounds);
		UOverlaySlot* NameOS = Ov->AddChildToOverlay(Name);
		NameOS->SetHorizontalAlignment(HAlign_Center);
		NameOS->SetVerticalAlignment(VAlign_Bottom);

		UHorizontalBoxSlot* BS = Bar->AddChildToHorizontalBox(Sz);
		BS->SetPadding(FMargin(3.f, 0.f, 3.f, 0.f));

		SlotBoxes.Add(Box);
		SlotIconBoxes.Add(IcoBox);
		SlotNames.Add(Name);
		SlotBadges.Add(Badge);
		SlotLastIcon.Add(NAME_None);
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
			const FString IdStr = S.ItemId.ToString();
			const bool bWet = IdStr.StartsWith(TEXT("WetBud_"));
			const bool bBud = bWet || IdStr.StartsWith(TEXT("Bud_"));
			const bool bCash = (S.ItemId == TEXT("Cash"));

			// Icoon alleen (her)bouwen als het item in dit slot veranderd is.
			if (SlotLastIcon[i] != S.ItemId)
			{
				SlotLastIcon[i] = S.ItemId;
				SlotIconBoxes[i]->SetContent(WeedUI::ItemIcon(WidgetTree, S.ItemId, 34.f));
			}

			FString Nm = WeedUI::PrettyItemName(S.ItemId);
			if (Nm.Len() > 10) { Nm = Nm.Left(9) + TEXT("."); }
			SlotNames[i]->SetText(FText::FromString(Nm));
			SlotNames[i]->SetColorAndOpacity(FSlateColor(WeedUI::ItemAccent(S.ItemId)));

			SlotBadges[i]->SetText(bCash
				? FText::GetEmpty()
				: FText::FromString(bBud ? FString::Printf(TEXT("%dg"), S.Quantity) : FString::Printf(TEXT("x%d"), S.Quantity)));
		}
		else
		{
			if (SlotLastIcon[i] != NAME_None)
			{
				SlotLastIcon[i] = NAME_None;
				SlotIconBoxes[i]->SetContent(WeedUI::Text(WidgetTree, TEXT(""), 8, FLinearColor::Transparent));
			}
			SlotNames[i]->SetText(FText::GetEmpty());
			SlotBadges[i]->SetText(FText::GetEmpty());
		}
	}
}

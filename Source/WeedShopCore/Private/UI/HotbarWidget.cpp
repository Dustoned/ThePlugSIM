#include "UI/HotbarWidget.h"

#include "UI/WeedUiStyle.h"
#include "UI/InventoryWidget.h" // UInvCell (sleep/drop)
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"

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
		Sz->SetWidthOverride(78.f);
		Sz->SetHeightOverride(78.f);

		// Buitenste overlay: visuele cel (onder) + transparante sleep/drop-cel (boven).
		UOverlay* SlotOv = WidgetTree->ConstructWidget<UOverlay>();
		Sz->SetContent(SlotOv);

		UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
		Box->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.09f, 0.12f, 0.9f), 8.f));
		Box->SetPadding(FMargin(4.f, 3.f, 4.f, 3.f));
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* BoxOS = SlotOv->AddChildToOverlay(Box);
		BoxOS->SetHorizontalAlignment(HAlign_Fill); BoxOS->SetVerticalAlignment(VAlign_Fill);

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Box->SetContent(Ov);

		// Icoon, iets boven het midden (laat onderaan ruimte voor de naam).
		USizeBox* IcoBox = WidgetTree->ConstructWidget<USizeBox>();
		IcoBox->SetWidthOverride(38.f); IcoBox->SetHeightOverride(38.f);
		UOverlaySlot* IcoOS = Ov->AddChildToOverlay(IcoBox);
		IcoOS->SetHorizontalAlignment(HAlign_Center);
		IcoOS->SetVerticalAlignment(VAlign_Top);
		IcoOS->SetPadding(FMargin(0.f, 9.f, 0.f, 0.f));

		// Slotnummer linksboven.
		UOverlaySlot* NumOS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), i + 1), 9, FLinearColor(0.55f, 0.58f, 0.7f), false, true));
		NumOS->SetHorizontalAlignment(HAlign_Left);
		NumOS->SetVerticalAlignment(VAlign_Top);

		// Aantal/gram-badge RECHTSBOVEN als pilletje (los van de naam onderaan, zodat niets overlapt).
		UTextBlock* Badge = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.95f, 0.97f, 1.f), false, true);
		UBorder* BadgePill = WidgetTree->ConstructWidget<UBorder>();
		BadgePill->SetBrush(WeedUI::Rounded(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), 6.f));
		BadgePill->SetPadding(FMargin(4.f, 0.f, 4.f, 0.f));
		BadgePill->SetContent(Badge);
		UOverlaySlot* BadgeOS = Ov->AddChildToOverlay(BadgePill);
		BadgeOS->SetHorizontalAlignment(HAlign_Right);
		BadgeOS->SetVerticalAlignment(VAlign_Top);

		// Naam onderaan, klein (gecentreerd, afgekapt).
		UTextBlock* Name = WeedUI::Text(WidgetTree, TEXT(""), 8, FLinearColor(0.85f, 0.88f, 0.96f), true);
		Name->SetClipping(EWidgetClipping::ClipToBounds);
		UOverlaySlot* NameOS = Ov->AddChildToOverlay(Name);
		NameOS->SetHorizontalAlignment(HAlign_Center);
		NameOS->SetVerticalAlignment(VAlign_Bottom);

		// Transparante cel bovenop die drag (vanaf dit slot) en drop (toewijzen) afhandelt. Alleen
		// actief als de inventory open is (dan zetten we de hele hotbar hit-testbaar).
		UInvCell* Drop = WidgetTree->ConstructWidget<UInvCell>();
		Drop->SlotIndex = i; Drop->GridCell = -1;
		Drop->Bg = FLinearColor(0.f, 0.f, 0.f, 0.f); Drop->Radius = 8.f;
		UOverlaySlot* DropOS = SlotOv->AddChildToOverlay(Drop);
		DropOS->SetHorizontalAlignment(HAlign_Fill); DropOS->SetVerticalAlignment(VAlign_Fill);
		DropCells.Add(Drop);

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

	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;

	// Hit-testbaar (sleep/drop) zolang de inventory open is; anders louter weergave (geen input).
	UPhoneClientComponent* Phone = P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
	const bool bInvOpen = Phone && Phone->IsInventoryOpen();
	SetVisibility(bInvOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);

	if (!Inv) { return; }

	const int32 Active = Inv->GetActiveSlot();
	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();

	for (int32 i = 0; i < SlotBoxes.Num(); ++i)
	{
		const bool bActive = (i == Active);
		SlotBoxes[i]->SetBrush(WeedUI::Rounded(bActive ? FLinearColor(0.20f, 0.30f, 0.16f, 0.96f) : FLinearColor(0.08f, 0.09f, 0.12f, 0.88f), 8.f));

		const int32 SlotSid = Inv->GetHotbarStackId(i);
		// Sleep/drop-cel up-to-date houden (geen rebuild nodig: velden worden bij het event gelezen).
		if (DropCells.IsValidIndex(i))
		{
			DropCells[i]->Inv = Inv;
			DropCells[i]->StackId = SlotSid;
			DropCells[i]->bDraggable = (SlotSid != 0);
		}

		const int32 Idx = Inv->FindStackById(SlotSid);
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

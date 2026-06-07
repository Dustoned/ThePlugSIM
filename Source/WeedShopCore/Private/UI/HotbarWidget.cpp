#include "UI/HotbarWidget.h"

#include "UI/WeedUiStyle.h"
#include "UI/InventoryWidget.h" // UInvCell (sleep/drop)
#include "Inventory/InventoryComponent.h"
#include "Cultivation/WaterCanComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Phone/ContactsComponent.h"
#include "Game/WeedShopGameState.h"

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
	// SelfHitTestInvisible (niet HitTestInvisible): de canvas vangt zelf niets, maar de drop-cellen
	// (kinderen) MOGEN muis-events ontvangen wanneer de inventory open is.
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

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
		SlotBadgePills.Add(BadgePill);
		SlotLastIcon.Add(NAME_None);
		SlotLastWaterState.Add(-1);
	}

	// --- Telefoon-icoon rechts van de hotbar met notificatie-bubble (aantal gemiste berichten) ---
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(48.f); Sz->SetHeightOverride(48.f); // strak om het icoon -> badge valt op de hoek
		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Sz->SetContent(Ov);

		// Alleen het telefoon-icoon (geen doos/achtergrond), gecentreerd.
		PhoneIconBox = WidgetTree->ConstructWidget<USizeBox>();
		PhoneIconBox->SetWidthOverride(46.f); PhoneIconBox->SetHeightOverride(46.f);
		PhoneIconBox->SetContent(WeedUI::UiGlyph(WidgetTree, TEXT("phone"), 46.f, FLinearColor(0.85f, 0.92f, 1.f), WeedUI::EIcon::Message));
		PhoneIconBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f)); // midden-pivot voor de tril-animatie
		UOverlaySlot* IconOS = Ov->AddChildToOverlay(PhoneIconBox);
		IconOS->SetHorizontalAlignment(HAlign_Center); IconOS->SetVerticalAlignment(VAlign_Center);

		MsgBadge = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor::White, true, true);
		MsgBadgePill = WidgetTree->ConstructWidget<UBorder>();
		MsgBadgePill->SetBrush(WeedUI::Rounded(FLinearColor(0.90f, 0.16f, 0.16f, 0.98f), 8.f));
		MsgBadgePill->SetPadding(FMargin(4.f, 0.f, 4.f, 0.f));
		MsgBadgePill->SetContent(MsgBadge);
		MsgBadgePill->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* BadgeOS = Ov->AddChildToOverlay(MsgBadgePill);
		BadgeOS->SetHorizontalAlignment(HAlign_Right); BadgeOS->SetVerticalAlignment(VAlign_Top);
		BadgeOS->SetPadding(FMargin(0.f, 7.f, 4.f, 0.f)); // op de rechterbovenhoek van de telefoon (lager)

		UHorizontalBoxSlot* PS = Bar->AddChildToHorizontalBox(Sz);
		PS->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f)); // ruimte tussen de hotbar en het telefoon-icoon
	}
}

void UHotbarWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;

	// De drop-cellen zijn alleen hit-testbaar (sleep/drop) zolang de inventory open is; daarbuiten
	// puur weergave zodat ze geen muis-input opvangen tijdens het spelen.
	UPhoneClientComponent* Phone = P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
	// Sleepbaar als de inventory OF het droogrek open is (vanuit de hotbar het rek in slepen).
	const bool bInvOpen = Phone && (Phone->IsInventoryOpen() || Phone->IsDryRackOpen());
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	for (UInvCell* DC : DropCells)
	{
		if (DC) { DC->SetVisibility(bInvOpen ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible); }
	}

	if (!Inv) { return; }

	const int32 Active = Inv->GetActiveSlot();
	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();

	// (Fles vol/leeg wordt nu PER SLOT bepaald uit de stack-Quality, niet meer globaal van de actieve fles.)
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
			const int32 DIdx = Inv->FindStackById(SlotSid);
			DropCells[i]->IconId = Stacks.IsValidIndex(DIdx) ? Stacks[DIdx].ItemId : NAME_None;
		}

		const int32 Idx = Inv->FindStackById(SlotSid);
		if (Stacks.IsValidIndex(Idx))
		{
			const FInventoryStack& S = Stacks[Idx];
			const FString IdStr = S.ItemId.ToString();

			// Icoon (her)bouwen als het item veranderde, of als 't een fles is en DEZE fles z'n vol/leeg-staat flipte.
			const bool bIsWater = IdStr.StartsWith(TEXT("WaterBottle"));
			const int32 SlotWaterState = bIsWater ? (S.Quality <= 0.f ? 1 : 0) : -1; // water zit in de stack-Quality
			if (SlotLastIcon[i] != S.ItemId || (bIsWater && SlotWaterState != SlotLastWaterState[i]))
			{
				SlotLastIcon[i] = S.ItemId;
				SlotLastWaterState[i] = SlotWaterState;
				// Geef per fles z'n eigen water mee (FMath::RoundToInt(S.Quality)) zodat vol/leeg per slot klopt.
				SlotIconBoxes[i]->SetContent(WeedUI::ItemIcon(WidgetTree, S.ItemId, 34.f, bIsWater ? FMath::RoundToInt(S.Quality) : -1));
			}

			FString Nm = WeedUI::PrettyItemName(S.ItemId);
			if (Nm.Len() > 10) { Nm = Nm.Left(9) + TEXT("."); }
			SlotNames[i]->SetText(FText::FromString(Nm));
			SlotNames[i]->SetColorAndOpacity(FSlateColor(WeedUI::ItemAccent(S.ItemId)));

			// Aantal-badge: zakjes "Nx Xg", wiet "Xg", overig stapelbaar "xN", niets voor cash.
			const FString BadgeStr = WeedUI::ItemQtyBadge(S.ItemId, S.Quantity);
			SlotBadges[i]->SetText(BadgeStr.IsEmpty() ? FText::GetEmpty() : FText::FromString(BadgeStr));
			// Pill-achtergrond alleen tonen als er ook echt een aantal staat (anders een leeg dot-je).
			if (SlotBadgePills.IsValidIndex(i)) { SlotBadgePills[i]->SetVisibility(BadgeStr.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
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
			if (SlotBadgePills.IsValidIndex(i)) { SlotBadgePills[i]->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}

	// Telefoon-notificatie: ongelezen inkomende berichten. De bubble gaat pas weg als je de CHAT van die
	// persoon echt opent (MarkChatSeen), niet zomaar bij het openen van de telefoon.
	if (MsgBadge && MsgBadgePill)
	{
		const int32 Unread = Phone ? Phone->GetUnreadMessageCount() : 0;
		// Nieuw bericht (ongelezen steeg) -> korte tril-burst van 2 sec; daarna stil (badge blijft tot gelezen).
		if (Unread > PhoneLastUnread) { PhoneVibeTimer = 2.0f; }
		PhoneLastUnread = Unread;
		PhoneVibeTimer = FMath::Max(0.f, PhoneVibeTimer - DeltaTime);
		const int32 Vibe = (PhoneVibeTimer > 0.f) ? 1 : 0;

		// Telefoon-icoon wisselen bij flip: trillend (phone_vibrate, warm getint) tijdens de burst,
		// anders het normale (phone). Beide vallen terug op de message-glyph als de PNG ontbreekt.
		if (PhoneIconBox && Vibe != PhoneVibeState)
		{
			PhoneVibeState = Vibe;
			PhoneIconBox->SetContent(WeedUI::UiGlyph(WidgetTree, Vibe ? TEXT("phone_vibrate") : TEXT("phone"), 46.f,
				Vibe ? FLinearColor(1.f, 0.85f, 0.4f) : FLinearColor(0.85f, 0.92f, 1.f), WeedUI::EIcon::Message));
		}
		// Tril-animatie zolang er ongelezen berichten zijn.
		if (PhoneIconBox)
		{
			if (Vibe)
			{
				PhoneShakeT += DeltaTime * 28.f;
				PhoneIconBox->SetRenderTransformAngle(FMath::Sin(PhoneShakeT) * 8.f); // +-8 graden wiebel
			}
			else
			{
				PhoneIconBox->SetRenderTransformAngle(0.f);
			}
		}

		if (Unread > 0)
		{
			MsgBadge->SetText(FText::FromString(Unread > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), Unread)));
			MsgBadgePill->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			MsgBadgePill->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

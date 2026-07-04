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

	// Rij = [tray met de 9 slots] + [los telefoon-icoon]. De tray geeft de slots een bakje i.p.v. zwevende losse slots.
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Row);
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(0.5f, 1.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -14.f));

	UBorder* Tray = WidgetTree->ConstructWidget<UBorder>();
	FSlateBrush TrayBr = WeedUI::Rounded(WeedUI::Hex(0x252B3A, 0.94f), 14.f);
	TrayBr.OutlineSettings.Width = 1.f;
	TrayBr.OutlineSettings.Color = FSlateColor(WeedUI::Hex(0x3A4152, 0.5f));
	Tray->SetBrush(TrayBr);
	Tray->SetPadding(FMargin(8.f, 7.f, 8.f, 7.f));
	Row->AddChildToHorizontalBox(Tray)->SetVerticalAlignment(VAlign_Center);

	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
	Tray->SetContent(Bar);

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
		Box->SetBrush(WeedUI::Rounded(WeedUI::Hex(0x2A3140, 0.9f), 10.f));
		Box->SetPadding(FMargin(7.f)); // zelfde binnen-inset als de inventory-cel -> badge/tag op dezelfde plek
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* BoxOS = SlotOv->AddChildToOverlay(Box);
		BoxOS->SetHorizontalAlignment(HAlign_Fill); BoxOS->SetVerticalAlignment(VAlign_Fill);

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		Box->SetContent(Ov);

		// Icoon gecentreerd (geen naam-tekst meer onderaan -> mag groot/in het midden).
		USizeBox* IcoBox = WidgetTree->ConstructWidget<USizeBox>();
		IcoBox->SetWidthOverride(52.f); IcoBox->SetHeightOverride(52.f);
		UOverlaySlot* IcoOS = Ov->AddChildToOverlay(IcoBox);
		IcoOS->SetHorizontalAlignment(HAlign_Center);
		IcoOS->SetVerticalAlignment(VAlign_Center);

		// Slotnummer linksboven.
		UOverlaySlot* NumOS = Ov->AddChildToOverlay(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), i + 1), 9, FLinearColor(0.55f, 0.58f, 0.7f), false, true));
		NumOS->SetHorizontalAlignment(HAlign_Left);
		NumOS->SetVerticalAlignment(VAlign_Top);

		// Aantal/gram-badge RECHTSBOVEN als pilletje (los van de naam onderaan, zodat niets overlapt).
		UTextBlock* Badge = WeedUI::Text(WidgetTree, TEXT(""), 10, WeedUI::ColText(), false, true);
		UBorder* BadgePill = WidgetTree->ConstructWidget<UBorder>();
		BadgePill->SetBrush(WeedUI::Rounded(WeedUI::ColBg(0.85f), 7.f)); // zelfde badge-pill als inventory/pickers
		BadgePill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
		BadgePill->SetContent(Badge);
		UOverlaySlot* BadgeOS = Ov->AddChildToOverlay(BadgePill);
		BadgeOS->SetHorizontalAlignment(HAlign_Right);
		BadgeOS->SetVerticalAlignment(VAlign_Top);

		// TAG-bubble onderaan: korte code (OG, GSC, II, 100g, ...) in een pilletje i.p.v. de hele naam.
		// Iets groter + dunne donkere outline: op size 9 oogde de (Exo-)tekst te dun voor snelle herkenning.
		UTextBlock* Name = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.98f, 1.f, 0.99f), false, true);
		{
			FSlateFontInfo TagFont = WeedUI::Font(10, true);
			TagFont.OutlineSettings.OutlineSize = 1;
			TagFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);
			Name->SetFont(TagFont);
		}
		UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
		TagPill->SetBrush(WeedUI::Rounded(WeedUI::ColAccentDim(0.96f), 6.f)); // tag-bubble (default; per-item hue via TagColorForItem in RefreshSlots)
		TagPill->SetPadding(FMargin(5.f, 0.f, 5.f, 1.f));
		TagPill->SetContent(Name);
		TagPill->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* NameOS = Ov->AddChildToOverlay(TagPill);
		NameOS->SetHorizontalAlignment(HAlign_Center);
		NameOS->SetVerticalAlignment(VAlign_Bottom);
		NameOS->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f)); // zelfde tag-onderrand als inventory/pickers

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
		SlotTagPills.Add(TagPill);
		SlotBadges.Add(Badge);
		SlotBadgePills.Add(BadgePill);
		SlotLastIcon.Add(NAME_None);
		SlotLastWaterState.Add(-1);
		SlotLastKey.Add(FString());
	}

	// --- Telefoon-icoon rechts van de hotbar met notificatie-bubble (aantal gemiste berichten) ---
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(78.f); Sz->SetHeightOverride(78.f); // ZELFDE maat als de hotbar-slots
		UOverlay* SlotOv = WidgetTree->ConstructWidget<UOverlay>();
		Sz->SetContent(SlotOv);

		// GEEN doos achter de telefoon: gewoon het kale telefoon-icoon (zoals gevraagd).
		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
		UOverlaySlot* OvOS = SlotOv->AddChildToOverlay(Ov);
		OvOS->SetHorizontalAlignment(HAlign_Fill); OvOS->SetVerticalAlignment(VAlign_Fill);

		// Telefoon-icoon gecentreerd op NATUURLIJKE verhouding (UiGlyph is aspect-fit -> niet uitgerekt).
		PhoneIconBox = WidgetTree->ConstructWidget<USizeBox>();
		PhoneIconBox->SetWidthOverride(60.f); PhoneIconBox->SetHeightOverride(60.f);
		PhoneIconBox->SetContent(WeedUI::UiGlyph(WidgetTree, TEXT("phone"), 60.f, FLinearColor(0.85f, 0.92f, 1.f), WeedUI::EIcon::Message));
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

		UHorizontalBoxSlot* PS = Row->AddChildToHorizontalBox(Sz);
		PS->SetVerticalAlignment(VAlign_Center);
		PS->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f)); // ruimte tussen de tray en het telefoon-icoon
	}
}

void UHotbarWidget::OnInvChanged()
{
	// Voorraad wijzigde (bv. een drop op/van de hotbar) -> meteen verversen, in HETZELFDE frame als de mutatie
	// (AssignHotbarStack broadcast direct) -> geen 1-frame-gat -> geen icon-pop.
	RefreshSlots();
}

bool UHotbarWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Drop in de gap TUSSEN twee hotbar-slots -> snap naar het dichtstbijzijnde slot. Alleen zolang de
	// slots ook echt drop-doelen zijn (inventory/droogrek open; anders staan ze op HitTestInvisible).
	TArray<UInvCell*> Cells;
	Cells.Reserve(DropCells.Num());
	for (UInvCell* C : DropCells)
	{
		if (C && C->GetVisibility() == ESlateVisibility::Visible) { Cells.Add(C); }
	}
	if (UInvCell* Nearest = UInvCell::FindNearestCell(Cells, InDragDropEvent.GetScreenSpacePosition()))
	{
		return Nearest->HandleDropOp(InOperation);
	}
	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

void UHotbarWidget::RefreshSlots()
{
	APawn* P = GetOwningPlayerPawn();
	UpdateComponentCache(P);
	UInventoryComponent* Inv = CachedInv.Get();
	UPhoneClientComponent* Phone = CachedPhone.Get();
	if (!Inv) { return; }
	UInventoryWidget* InvW = Phone ? Phone->GetInventoryWidget() : nullptr; // voor de hover-details vanaf een hotbar-slot

	const int32 Active = Inv->GetActiveSlot();
	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
	const bool bRollLoadedUI = Phone && Phone->IsRollLoadedUI();

	// (Fles vol/leeg wordt nu PER SLOT bepaald uit de stack-Quality, niet meer globaal van de actieve fles.)
	for (int32 i = 0; i < SlotBoxes.Num(); ++i)
	{
		const bool bActive = (i == Active);
		const int32 SlotSid = Inv->GetHotbarStackId(i);
		const int32 Idx = Inv->FindStackById(SlotSid);

		// Goedkope pointer-writes altijd (kunnen wisselen zonder key-wissel, bv. na pawn-respawn).
		if (DropCells.IsValidIndex(i))
		{
			DropCells[i]->Inv = Inv;
			DropCells[i]->DetailsOwner = InvW; // hover op een hotbar-slot vult het inventory-details-paneel (zoals grid-cellen)
		}

		// Per-slot change-key: dure updates (brush/tekst/tooltip/icoon) alleen bij een echte wijziging.
		FString Key;
		if (Stacks.IsValidIndex(Idx))
		{
			const FInventoryStack& S = Stacks[Idx];
			Key = FString::Printf(TEXT("%d|%s|%d|%.3f|%.3f|%d|%d"), SlotSid, *S.ItemId.ToString(), S.Quantity, S.Quality, S.QualityPct, bActive ? 1 : 0, bRollLoadedUI ? 1 : 0);
		}
		else
		{
			Key = FString::Printf(TEXT("leeg|%d"), bActive ? 1 : 0);
		}
		if (SlotLastKey.IsValidIndex(i))
		{
			if (SlotLastKey[i] == Key) { continue; }
			SlotLastKey[i] = Key;
		}

		FSlateBrush HB = WeedUI::Rounded(bActive ? WeedUI::Hex(0x3A2B52, 0.96f) : WeedUI::Hex(0x2A3140, 0.9f), 10.f);
		if (bActive) { HB.OutlineSettings.Width = 1.5f; HB.OutlineSettings.Color = FSlateColor(WeedUI::Hex(0xB98CFF, 0.9f)); }
		SlotBoxes[i]->SetBrush(HB);

		// Sleep/drop-cel up-to-date houden (geen rebuild nodig: velden worden bij het event gelezen).
		if (DropCells.IsValidIndex(i))
		{
			DropCells[i]->StackId = SlotSid;
			DropCells[i]->bDraggable = (SlotSid != 0);
			DropCells[i]->IconId = Stacks.IsValidIndex(Idx) ? Stacks[Idx].ItemId : NAME_None;
			// Rijke hover-tooltip + dezelfde velden die het details-paneel leest (naam/tooltip/water).
			// Tooltip = info-BODY zonder naam (het details-paneel toont de naam al groot); de zwevende
			// tooltip krijgt naam + body.
			if (Stacks.IsValidIndex(Idx))
			{
				const FInventoryStack& DS = Stacks[Idx];
				const FString Nm = WeedUI::PrettyItemName(DS.ItemId);
				const FString Body = WeedUI::ItemInfoBody(DS.ItemId, DS.Quantity, DS.Quality, DS.QualityPct);
				DropCells[i]->SetToolTipText(FText::FromString(Body.IsEmpty() ? Nm : (Nm + TEXT("\n") + Body)));
				DropCells[i]->Tooltip = Body;
				DropCells[i]->Line1 = Nm;
				DropCells[i]->WaterOverride = DS.ItemId.ToString().StartsWith(TEXT("WaterBottle")) ? FMath::RoundToInt(DS.Quality) : -1;
			}
			else { DropCells[i]->SetToolTipText(FText::GetEmpty()); DropCells[i]->Tooltip.Empty(); }
		}

		if (Stacks.IsValidIndex(Idx))
		{
			const FInventoryStack& S = Stacks[Idx];
			const FString IdStr = S.ItemId.ToString();

			// Icoon (her)bouwen als het item veranderde, of als de icon-VARIANT flipte: een fles vol<->leeg,
			// of een vloei ongeladen<->geladen (geladen paper toont het handen-rol-icoon i.p.v. het boekje).
			const bool bIsWater = IdStr.StartsWith(TEXT("WaterBottle"));
			const bool bRollLoaded = IdStr.StartsWith(TEXT("Papers_")) && bRollLoadedUI;
			const int32 SlotWaterState = bIsWater ? (S.Quality <= 0.f ? 1 : 0) : (bRollLoaded ? 2 : 0);
			if (SlotLastIcon[i] != S.ItemId || SlotWaterState != SlotLastWaterState[i])
			{
				SlotLastIcon[i] = S.ItemId;
				SlotLastWaterState[i] = SlotWaterState;
				// Geef per fles z'n eigen water mee (FMath::RoundToInt(S.Quality)) zodat vol/leeg per slot klopt.
				SlotIconBoxes[i]->SetContent(WeedUI::ItemIcon(WidgetTree, S.ItemId, 48.f, bIsWater ? FMath::RoundToInt(S.Quality) : -1));
			}

			// TAG-bubble: korte code (OG, GSC, II, 100g, ...) i.p.v. de volledige naam. Leeg = geen bubble
			// (unieke iconen spreken voor zich -> minder rommel).
			const FString Tag = WeedUI::ItemTagShort(S.ItemId);
			if (Tag.IsEmpty())
			{
				if (SlotTagPills.IsValidIndex(i)) { SlotTagPills[i]->SetVisibility(ESlateVisibility::Collapsed); }
			}
			else
			{
				SlotNames[i]->SetText(FText::FromString(Tag));
				if (SlotTagPills.IsValidIndex(i)) { SlotTagPills[i]->SetBrush(WeedUI::Rounded(WeedUI::TagColorForItem(S.ItemId), 6.f)); SlotTagPills[i]->SetVisibility(ESlateVisibility::HitTestInvisible); } // strain -> kleur, standaard -> grijs
			}

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
			if (SlotTagPills.IsValidIndex(i)) { SlotTagPills[i]->SetVisibility(ESlateVisibility::Collapsed); }
			SlotBadges[i]->SetText(FText::GetEmpty());
			if (SlotBadgePills.IsValidIndex(i)) { SlotBadgePills[i]->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}
}

void UHotbarWidget::UpdateComponentCache(APawn* P)
{
	// Weak+pawn-check: FindComponentByClass alleen bij pawn-wissel of een weggevallen component.
	if (CachedCompPawn.Get() != P || (P && (!CachedInv.IsValid() || !CachedPhone.IsValid())))
	{
		CachedCompPawn = P;
		CachedInv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
		CachedPhone = P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
	}
}

void UHotbarWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	APawn* P = GetOwningPlayerPawn();
	UpdateComponentCache(P);
	UInventoryComponent* Inv = CachedInv.Get();
	UPhoneClientComponent* Phone = CachedPhone.Get();

	// De drop-cellen zijn alleen hit-testbaar (sleep/drop) zolang de inventory open is; daarbuiten
	// puur weergave zodat ze geen muis-input opvangen tijdens het spelen.
	// Sleepbaar als de inventory OF het droogrek open is (vanuit de hotbar het rek in slepen).
	const bool bInvOpen = Phone && (Phone->IsInventoryOpen() || Phone->IsDryRackOpen());
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (LastInvOpen != (bInvOpen ? 1 : 0))
	{
		LastInvOpen = bInvOpen ? 1 : 0;
		for (UInvCell* DC : DropCells)
		{
			if (DC) { DC->SetVisibility(bInvOpen ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible); }
		}
	}

	// Bind aan voorraad-wijzigingen -> de hotbar DIRECT (zelfde frame als de drop) verversen i.p.v. pas de volgende
	// tick. Anders zit er 1 frame tussen "drag-visual weg" en "slot gevuld" -> dat is de icon-pop tussen hotbar en inv.
	if (Inv && Inv != BoundInv.Get())
	{
		if (UInventoryComponent* Old = BoundInv.Get()) { Old->OnInventoryChanged.RemoveDynamic(this, &UHotbarWidget::OnInvChanged); }
		Inv->OnInventoryChanged.AddDynamic(this, &UHotbarWidget::OnInvChanged);
		BoundInv = Inv;
	}
	RefreshSlots(); // per-tick (actieve-slot-highlight, fles vol/leeg); óók synchroon vanuit OnInvChanged bij een drop

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
			PhoneIconBox->SetContent(WeedUI::UiGlyph(WidgetTree, Vibe ? TEXT("phone_vibrate") : TEXT("phone"), 60.f,
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

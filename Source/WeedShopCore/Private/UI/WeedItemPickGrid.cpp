#include "UI/WeedItemPickGrid.h"
#include "UI/WeedUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"

// ---------------------------------------------------------------------------
//  UWeedItemPickGrid — herbruikbaar icoon-keuze-grid
// ---------------------------------------------------------------------------

TSharedRef<SWidget> UWeedItemPickGrid::RebuildWidget()
{
	BuildShell();
	return Super::RebuildWidget();
}

void UWeedItemPickGrid::BuildShell()
{
	if (!WidgetTree || RootSizeBox) { return; }

	// SizeBox -> ScrollBox -> WrapBox. De SizeBox capt (optioneel) de hoogte; de ScrollBox laat de rest
	// scrollen; de WrapBox legt de 86x86-cellen naast/onder elkaar (padding 6,6 per cel).
	USizeBox* SB = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PickGridRoot"));
	if (MaxVisibleRows > 0)
	{
		// Rij-hoogte = cel + de 6px onder-padding van de WrapBox-slot.
		SB->SetMaxDesiredHeight(MaxVisibleRows * (CellSize + 6.f));
	}
	WidgetTree->RootWidget = SB;

	UScrollBox* Sc = WidgetTree->ConstructWidget<UScrollBox>();
	SB->SetContent(Sc);

	UWrapBox* WB = WidgetTree->ConstructWidget<UWrapBox>();
	WB->SetInnerSlotPadding(FVector2D(6.f, 6.f)); // 6px tussen de cellen (zelfde als de drying-rack-grid)
	Sc->AddChild(WB);

	RootSizeBox = SB;
	Scroll = Sc;
	Wrap = WB;
}

UWidget* UWeedItemPickGrid::MakeCellContent(const FWeedPickItem& Item) const
{
	// Icoon-id: expliciet IconId, anders de logische Id.
	const FName UseIcon = Item.IconId.IsNone() ? Item.Id : Item.IconId;
	const bool bHasIcon = !UseIcon.IsNone();

	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();

	// Icoon-grootte: 0 = auto = mee schalen met de cel op de inventory-ratio (68/86 = 0.79),
	// zodat de picker-iconen even groot ogen als in de inventory (ongeacht CellSize).
	const float IconPx = (IconSize > 0.f) ? IconSize : CellSize * 0.79f;

	// Icoon gecentreerd (zelfde look als UInvCell: naam/details in de tooltip, aantal als badge in de hoek).
	{
		UOverlaySlot* IconOS = Ov->AddChildToOverlay(
			bHasIcon ? WeedUI::ItemIcon(WidgetTree, UseIcon, IconPx)
			         : Cast<UWidget>(WeedUI::Text(WidgetTree, TEXT(""), 8, FLinearColor::Transparent)));
		IconOS->SetHorizontalAlignment(HAlign_Center);
		IconOS->SetVerticalAlignment(VAlign_Center);
	}

	// Aantal/gram-badge als pill rechtsboven.
	if (!Item.Badge.IsEmpty())
	{
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
		Pill->SetBrush(WeedUI::ItemQtyPillBrush()); // zelfde badge-pill als de inventory-cel
		Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
		Pill->SetContent(WeedUI::Text(WidgetTree, Item.Badge, 10, WeedUI::ColText(), false, true));
		Pill->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
		PS->SetHorizontalAlignment(HAlign_Right);
		PS->SetVerticalAlignment(VAlign_Top);
		PS->SetPadding(FMargin(0.f, 7.f, 7.f, 0.f)); // 7px inset vanaf rechtsboven = zelfde als de inventory-cel
	}

	// Onderaan gestapeld: (optioneel) klein SubLine-regeltje boven de tag-pill.
	{
		const FString UseTag = Item.Tag.IsEmpty() ? WeedUI::ItemTagShort(UseIcon) : Item.Tag;
		const bool bHasSub = !Item.SubLine.IsEmpty();
		const bool bHasTag = !UseTag.IsEmpty();
		if (bHasSub || bHasTag)
		{
			UVerticalBox* Bottom = WidgetTree->ConstructWidget<UVerticalBox>();

			if (bHasSub)
			{
				// Klein 9pt bold regeltje in de meegegeven kleur (bv. prijs).
				UTextBlock* Sub = WeedUI::Text(WidgetTree, Item.SubLine, 9, Item.SubCol, true, true);
				{
					FSlateFontInfo SubFont = WeedUI::Font(9, true);
					SubFont.OutlineSettings.OutlineSize = 1;
					SubFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);
					Sub->SetFont(SubFont);
				}
				UVerticalBoxSlot* SubS = Bottom->AddChildToVerticalBox(Sub);
				SubS->SetHorizontalAlignment(HAlign_Center);
				SubS->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
			}

			if (bHasTag)
			{
				// Strain/variant-TAG-bubble: gedeelde hoge-contrast tagstijl.
				UTextBlock* TagT = WeedUI::Text(WidgetTree, UseTag, 11, FLinearColor::White, false, true);
				TagT->SetFont(WeedUI::ItemTagFont(11));
				TagT->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
				TagT->SetShadowOffset(FVector2D(1.f, 1.f));
				UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
				TagPill->SetBrush(WeedUI::ItemTagPillBrush(UseIcon, 6.f));
				TagPill->SetPadding(FMargin(6.f, 0.f, 6.f, 2.f));
				TagPill->SetContent(TagT);
				UVerticalBoxSlot* TagS = Bottom->AddChildToVerticalBox(TagPill);
				TagS->SetHorizontalAlignment(HAlign_Center);
			}

			Bottom->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* BS = Ov->AddChildToOverlay(Bottom);
			BS->SetHorizontalAlignment(HAlign_Center);
			BS->SetVerticalAlignment(VAlign_Bottom);
			BS->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f)); // tag netjes onderin (3px van de celrand)
		}
	}

	// Forceer dat de inhoud de HELE cel vult: de button centreert anders de kleine overlay -> lege ruimte
	// onderin + de tag zweeft. SizeBox op celmaat -> icoon gecentreerd, badge rechtsboven, tag onderin.
	USizeBox* Fill = WidgetTree->ConstructWidget<USizeBox>();
	Fill->SetWidthOverride(CellSize);
	Fill->SetHeightOverride(CellSize);
	Fill->SetContent(Ov);
	return Fill;
}

void UWeedItemPickGrid::StyleCell(int32 i, bool bSel)
{
	if (!Cells.IsValidIndex(i) || !Cells[i]) { return; }

	// Geselecteerd = duidelijke accent-rand; niet-geselecteerd blijft vol leesbaar zoals inventory/hotbar.
	FButtonStyle S;
	FSlateBrush Base = bSel
		? WeedUI::StorageSlotBrushWithFill(WeedUI::ColAccentDim(0.92f), true, true, WeedUI::ColAccent(1.f), 8.f)
		: WeedUI::StorageSlotBrush(true, false, WeedUI::ColAccent(0.9f), 8.f);
	if (bSel)
	{
		Base.OutlineSettings.Width = 2.6f;
		Base.OutlineSettings.Color = FSlateColor(WeedUI::ColAccent(1.f));
	}
	S.Normal = Base;

	// Hover/pressed: iets lichter dan de basis, rand behouden bij selectie.
	FSlateBrush Hover = WeedUI::StorageSlotBrushWithFill(bSel ? WeedUI::ColAccentDim(1.06f) : WeedUI::ColInner(0.92f), true, bSel, WeedUI::ColAccent(1.f), 8.f);
	if (bSel)
	{
		Hover.OutlineSettings.Width = 2.6f;
		Hover.OutlineSettings.Color = FSlateColor(WeedUI::ColAccent(1.f));
	}
	S.Hovered = Hover;

	FSlateBrush Pressed = WeedUI::StorageSlotBrushWithFill(bSel ? WeedUI::ColAccentDim(0.94f) : WeedUI::ColSlot(0.72f), true, bSel, WeedUI::ColAccent(0.9f), 8.f);
	if (bSel)
	{
		Pressed.OutlineSettings.Width = 2.6f;
		Pressed.OutlineSettings.Color = FSlateColor(WeedUI::ColAccent(1.f));
	}
	S.Pressed = Pressed;

	// Geen button-padding: de inhoud (SizeBox op celmaat) vult de hele cel zelf; badge/tag krijgen hun
	// inset via hun eigen overlay-slot (7px rechtsboven / 3px onder), gelijk aan de inventory-cel.
	S.NormalPadding = FMargin(0.f);
	S.PressedPadding = FMargin(0.f);
	Cells[i]->SetStyle(S);
	Cells[i]->SetRenderOpacity((bShowSelection && !SelId.IsNone() && !bSel) ? 0.90f : 1.f);
}

void UWeedItemPickGrid::SetItems(const TArray<FWeedPickItem>& Items, FName SelectedId)
{
	BuildShell();
	if (!Wrap) { return; }

	SelId = SelectedId;

	// Pool aan de STAART laten groeien: elke nieuwe cel is een UWeedActionButton in een vaste-maat USizeBox,
	// met een klik-lambda die de VASTE pool-index i vangt en Id/Payload uit de parallelle arrays leest.
	while (Cells.Num() < Items.Num())
	{
		const int32 i = Cells.Num();

		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([this, i](int32, int32)
		{
			if (OnPick && CellIds.IsValidIndex(i) && CellPayloads.IsValidIndex(i))
			{
				OnPick(CellIds[i], CellPayloads[i]);
			}
		});

		// Vaste 86x86-maat via een SizeBox rond de knop.
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(CellSize);
		Box->SetHeightOverride(CellSize);
		Box->SetContent(B);

		Wrap->AddChildToWrapBox(Box); // tussenruimte komt van de WrapBox InnerSlotPadding (6,6)

		Cells.Add(B);
		CellSigs.Add(TEXT("\x01")); // sentinel -> forceert eerste content-vulling
		CellIds.Add(NAME_None);
		CellPayloads.Add(0);
	}

	// Pool krimpen: overtollige cellen (met hun SizeBox) uit de WrapBox halen.
	while (Cells.Num() > Items.Num())
	{
		const int32 Last = Cells.Num() - 1;
		if (Cells[Last])
		{
			// De knop zit in een SizeBox die in de WrapBox hangt -> de SizeBox (parent) verwijderen.
			if (UWidget* Parent = Cells[Last]->GetParent()) { Parent->RemoveFromParent(); }
			else { Cells[Last]->RemoveFromParent(); }
		}
		Cells.RemoveAt(Last);
		CellSigs.RemoveAt(Last);
		CellIds.RemoveAt(Last);
		CellPayloads.RemoveAt(Last);
	}

	// Per-cel diff: content alleen vervangen bij een sig-verschil (sig uit ALLE velden BEHALVE selectie).
	// Selectie mag EXACT EEN cel raken: als er (per abuis) meerdere cellen dezelfde Id hebben - bv. twee
	// stapels van dezelfde strain met net-andere THC% - highlight alleen de EERSTE match, anders lijkt de
	// hele strain "geselecteerd". bSelSeen bewaakt dat.
	bool bSelSeen = false;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FWeedPickItem& It = Items[i];
		const FString Sig = FString::Printf(TEXT("%s|%s|%d|%s|%s|%s|%08x|%s"),
			*It.Id.ToString(), *It.IconId.ToString(), It.Payload,
			*It.Badge, *It.Tag, *It.SubLine, It.SubCol.ToFColor(true).ToPackedARGB(),
			*It.Tooltip);

		if (CellSigs[i] != Sig)
		{
			CellSigs[i] = Sig;
			if (Cells[i]) { Cells[i]->SetContent(MakeCellContent(It)); }
			// Id/Payload bijwerken zodat de (index-gevangen) klik-lambda de juiste keuze doorgeeft.
			CellIds[i] = It.Id;
			CellPayloads[i] = It.Payload;
			if (Cells[i]) { Cells[i]->SetToolTipText(It.Tooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(It.Tooltip)); }
		}

		const bool bSel = bShowSelection && !SelectedId.IsNone() && It.Id == SelectedId && !bSelSeen;
		if (bSel) { bSelSeen = true; }
		StyleCell(i, bSel);
	}
}

void UWeedItemPickGrid::SetSelected(FName Id)
{
	if (SelId == Id) { return; }

	// Alleen de oude + nieuwe geselecteerde cel opnieuw stylen (geen content-rebuild).
	const FName Old = SelId;
	SelId = Id;
	if (!bShowSelection) { return; }

	// Selectie raakt EXACT EEN cel: bij dubbele Ids (twee stapels van dezelfde strain) highlight alleen de
	// EERSTE match - anders licht de hele strain op. Elke oude match wordt sowieso ge-de-highlight.
	bool bSelSeen = false;
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (!CellIds.IsValidIndex(i)) { continue; }
		const FName CellId = CellIds[i];
		if (!Id.IsNone() && CellId == Id && !bSelSeen) { StyleCell(i, true); bSelSeen = true; }
		else if (CellId == Old || CellId == Id) { StyleCell(i, false); }
	}
}

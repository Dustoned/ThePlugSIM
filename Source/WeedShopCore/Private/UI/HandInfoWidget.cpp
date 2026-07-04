#include "UI/HandInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Inventory/InventoryComponent.h"
#include "Cultivation/WaterCanComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Cultivation/PotTypes.h" // IsPotItem (Qty-logica: pot = geen aantal tonen)

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

namespace
{
	// "SilverHaze" -> "Silver Haze": spaties voor binnenste hoofdletters/cijfers.
	FString PrettyName(const FString& In)
	{
		FString Out;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (i > 0)
			{
				const TCHAR Prev = In[i - 1];
				const bool bBoundary = (FChar::IsUpper(C) && !FChar::IsUpper(Prev) && Prev != TEXT(' '))
					|| (FChar::IsDigit(C) && !FChar::IsDigit(Prev) && Prev != TEXT(' '));
				if (bBoundary) { Out.AppendChar(TEXT(' ')); }
			}
			Out.AppendChar(C);
		}
		return Out;
	}
}

TSharedRef<SWidget> UHandInfoWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UHandInfoWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Kaart LINKS-midden (zo valt 'ie nooit achter de telefoon, die rechts opent).
	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HandCard"));
	// Massief paneel (kit-look, zelfde als de inventory quick-view) i.p.v. transparant: leesbaarder + nettere kaart.
	CardB->SetBrush(WeedUI::Rounded(WeedUI::ColPanel(0.95f), 12.f));
	CardB->SetPadding(FMargin(0.f));
	Card = CardB;
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	// LinksONDER (boven de hotbar), niet links-midden: zo botst de vastgehouden-item-kaart niet meer
	// met de status-HUD linksboven.
	CS->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));
	CS->SetAlignment(FVector2D(0.f, 1.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(24.f, -28.f));

	// Accent-balk links + tekstkolom (vaste, smalle breedte -> kaart wordt verticaler dan breed).
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	CardB->SetContent(Row);

	AccentBar = WidgetTree->ConstructWidget<UBorder>();
	AccentBar->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(), 3.f));
	AccentBar->SetPadding(FMargin(3.f, 0.f, 3.f, 0.f));
	UHorizontalBoxSlot* AS = Row->AddChildToHorizontalBox(AccentBar);
	AS->SetPadding(FMargin(0.f)); AS->SetVerticalAlignment(VAlign_Fill);

	USizeBox* ColSize = WidgetTree->ConstructWidget<USizeBox>();
	ColSize->SetWidthOverride(180.f); // smal -> tekst wrapt, kaart wordt hoger
	UHorizontalBoxSlot* SzS = Row->AddChildToHorizontalBox(ColSize);
	SzS->SetVerticalAlignment(VAlign_Center);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	ColSize->SetContent(Col);

	// Type-tag (klein, hoofdletters, gekleurd). Bold projectfont + dunne donkere outline: de letters waren
	// te dun/onleesbaar zonder paneel-achtergrond (de kaart is transparant).
	TypeText = WeedUI::Text(WidgetTree, TEXT(""), 11, FLinearColor(0.6f, 0.95f, 0.65f), false, true);
	{
		FSlateFontInfo TagFont = WeedUI::Font(11, true);
		TagFont.OutlineSettings.OutlineSize = 1;
		TagFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);
		TypeText->SetFont(TagFont);
	}
	TypeText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(TypeText)->SetPadding(FMargin(14.f, 12.f, 14.f, 1.f));

	// Naam (groot).
	NameText = WeedUI::Text(WidgetTree, TEXT(""), 20, WeedUI::ColText(), false, true);
	NameText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(NameText)->SetPadding(FMargin(14.f, 0.f, 14.f, 2.f));

	// Aantal/gram - groot en gekleurd, direct onder de titel.
	QtyText = WeedUI::Text(WidgetTree, TEXT(""), 22, WeedUI::ColGood(), false, true);
	Col->AddChildToVerticalBox(QtyText)->SetPadding(FMargin(14.f, 0.f, 14.f, 8.f));

	// Dun scheidingslijntje.
	Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrush(WeedUI::Rounded(WeedUI::ColStroke(0.6f), 1.f));
	Divider->SetPadding(FMargin(0.f, 0.7f, 0.f, 0.7f));
	Col->AddChildToVerticalBox(Divider)->SetPadding(FMargin(14.f, 0.f, 14.f, 8.f));

	// Nette label/waarde-rijen.
	StatBox = WidgetTree->ConstructWidget<UVerticalBox>();
	Col->AddChildToVerticalBox(StatBox)->SetPadding(FMargin(14.f, 0.f, 14.f, 6.f));

	// Korte hint (dim) onderaan.
	HintText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, false);
	HintText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(HintText)->SetPadding(FMargin(14.f, 2.f, 14.f, 12.f));

	// (Geen zware tekst-schaduw meer: op het massieve paneel is de tekst zo al leesbaar, net als de quick-view.)

	Card->SetRenderOpacity(0.f);
}

void UHandInfoWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!Card) { return; }

	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	const UPhoneClientComponent* Ph = P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;

	// Verberg de popup als een vol-scherm menu open is (telefoon-apps/pauze/titel).
	const bool bUiBlocking = Ph && (Ph->IsOpen() || Ph->IsPauseOpen() || Ph->IsMainMenuOpen() || Ph->IsSettingsOpen());

	FName Id = Inv ? Inv->GetActiveItemId() : NAME_None;
	const FString IdStr = Id.ToString();
	const bool bHasItem = Inv && !Id.IsNone() && !bUiBlocking; // Cash mag nu wél (toont het bedrag)

	// Geladen joint: hou je vloei vast met wiet erin geladen -> die info hoort HIER bij de hand-preview
	// (niet bij de controls-kaart). RollDesc is de opgeslagen snapshot ("2g Silver Haze - 11% THC, ...").
	const bool bRollLoaded = Ph && IdStr.StartsWith(TEXT("Papers_")) && Ph->IsRollLoadedUI();
	const FString RollDesc = bRollLoaded ? Ph->GetRollLoadDesc() : FString();

	// Fade in/uit.
	const float Target = bHasItem ? 1.f : 0.f;
	Shown = FMath::FInterpTo(Shown, Target, DeltaTime, 12.f);
	Card->SetRenderOpacity(Shown);
	// Volledig dichtklappen als 'ie weg is -> nooit een leeg kader/blok dat blijft hangen.
	Card->SetVisibility(Shown > 0.02f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (Shown <= 0.02f && !bHasItem) { return; }

	if (!bHasItem) { return; }

	// Bouw een sleutel zodat we de tekst alleen bij wijziging updaten. Neem de ACTIEVE stack-id + diens
	// Quality mee, zodat wisselen tussen 2 gelijke items (bv. 2 flessen) én het water-niveau live verversen.
	const int32 Qty = Inv->GetQuantity(Id);
	const float Thc = Inv->GetItemQuality(Id);
	const float Qpct = Inv->GetItemQualityPct(Id);
	const int32 ActiveSid = Inv->GetActiveStackId();
	const float ActiveQ = Inv->GetStackQualityById(ActiveSid);
	const FString Key = FString::Printf(TEXT("%s|%d|%.0f|%.0f|%d|%.0f|%s"), *IdStr, Qty, Thc, Qpct, ActiveSid, ActiveQ, *RollDesc);
	if (Key == LastKey) { return; }
	LastKey = Key;

	// Gedeelde detail-DATA (type-tag + kleur + hint + stat-rijen). Zelfde bron als de inventory-quick-view,
	// zodat de twee NOOIT uit elkaar lopen. De rendering (AddStat/Qty/naam-kleur) blijft hier in de widget.
	const WeedUI::FItemDetailInfo Detail = WeedUI::BuildItemDetail(this, Id, Qty, Thc, Qpct);
	FString Type = Detail.Type, Hint = Detail.Hint; FLinearColor Col = Detail.TypeColor;

	// Alleen de NAAM kleurt mee met de PER-STRAIN tag-kleur (zelfde hue als de tag-pills in de inventory/
	// hotbar), iets helderder voor leesbaarheid als losse tekst; de type-tekst erboven ("SEED"/"JOINT") houdt
	// z'n normale categorie-kleur uit ClassifyItem. Niet-strain-items geven bij TagColorForItem één vaste
	// neutrale pill-kleur terug (Value/Sat genegeerd) -> die herkennen we door twee varianten te vergelijken;
	// dan blijft de naam gewoon wit.
	FLinearColor NameCol = WeedUI::ColText();
	{
		const FLinearColor StrainCol = WeedUI::TagColorForItem(Id, 0.9f, 0.72f);
		if (!StrainCol.Equals(WeedUI::TagColorForItem(Id))) { NameCol = StrainCol; }
	}

	if (bRollLoaded) { Hint = TEXT("Loaded and ready to roll"); }

	if (AccentBar) { AccentBar->SetBrush(WeedUI::Rounded(Col, 3.f)); }
	if (TypeText) { TypeText->SetText(FText::FromString(Type)); TypeText->SetColorAndOpacity(FSlateColor(Col)); } // categorie-kleur (bold+outline blijven)
	if (NameText) { NameText->SetText(FText::FromString(PrettyName(WeedUI::PrettyItemName(Id)))); NameText->SetColorAndOpacity(FSlateColor(NameCol)); }
	if (HintText) { HintText->SetText(FText::FromString(Hint)); }

	// Aantal groot bij de titel: gram voor wiet/baggies, anders "x N". Voor gereedschap/plaatsbare
	// dingen (fles, pot, rek, bench, meubels) is een aantal zinloos -> verbergen.
	if (QtyText)
	{
		const bool bBottle = IdStr.StartsWith(TEXT("WaterBottle"));
		const bool bEquip = IsPotItem(Id)
			|| IdStr.StartsWith(TEXT("DryRack_")) || IdStr.StartsWith(TEXT("Bench_"))
			|| IdStr.StartsWith(TEXT("Lamp")) || IdStr.StartsWith(TEXT("Tent"))
			|| Id == TEXT("Shelf") || Id == TEXT("Chest") || Id == TEXT("Table")
			|| Id == TEXT("Mattress") || Id == TEXT("Fridge") || Id == TEXT("Atm");
		if (bBottle)
		{
			// Fles: toon GROOT hoe vol 'ie is (water / capaciteit), live tijdens vullen.
			int32 Cur = 0, Max = 0;
			if (const APawn* Pw = GetOwningPlayerPawn())
			{
				if (const UWaterCanComponent* Can = Pw->FindComponentByClass<UWaterCanComponent>())
				{
					Cur = Can->GetCharges(); Max = Can->GetMaxCharges();
				}
			}
			QtyText->SetVisibility(ESlateVisibility::HitTestInvisible);
			QtyText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Cur, FMath::Max(1, Max))));
			QtyText->SetColorAndOpacity(FSlateColor((Cur <= 0) ? WeedUI::ColWarn() : WeedUI::ColGood()));
		}
		else if (bEquip)
		{
			QtyText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			QtyText->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UInventoryComponent::IsBag(Id))
			{
				// Zakjes: "Nx Xg" (aantal x grootte), bv. 2x 2g.
				QtyText->SetText(FText::FromString(WeedUI::ItemQtyBadge(Id, Qty)));
			}
			else
			{
				const bool bGrams = IdStr.StartsWith(TEXT("WetBud_")) || IdStr.StartsWith(TEXT("Bud_"));
				QtyText->SetText(FText::FromString(bGrams ? FString::Printf(TEXT("%d g"), Qty) : FString::Printf(TEXT("x%d"), Qty)));
			}
			QtyText->SetColorAndOpacity(FSlateColor(Col));
		}
	}

	// Nette label/waarde-rijen opbouwen.
	if (StatBox)
	{
		StatBox->ClearChildren();
			if (bRollLoaded)
			{
				// Geladen vloei -> toon de geladen-omschrijving (snapshot uit GetRollLoadDesc) bovenaan,
				// volle breedte (wrapt). De "2g loaded"-info hoort hier bij de hand-preview, niet bij de controls.
				UTextBlock* Ld = WeedUI::Text(WidgetTree, RollDesc, 13, WeedUI::ColGood(), false, true);
				Ld->SetAutoWrapText(true);
				StatBox->AddChildToVerticalBox(Ld)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));
			}
		auto AddStat = [this](const FString& Label, const FString& Value)
		{
			UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* L = WeedUI::Text(WidgetTree, Label, 12, WeedUI::ColTextDim());
			UHorizontalBoxSlot* LS = RowH->AddChildToHorizontalBox(L);
			LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
			UTextBlock* V = WeedUI::Text(WidgetTree, Value, 13, WeedUI::ColText(), false, true);
			V->SetJustification(ETextJustify::Right);
			UHorizontalBoxSlot* VS = RowH->AddChildToHorizontalBox(V);
			VS->SetVerticalAlignment(VAlign_Center);
			StatBox->AddChildToVerticalBox(RowH)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		};

		// Aantal staat al groot bij de titel - hier alleen de echte eigenschappen (gedeelde stat-rijen).
		for (const TPair<FString, FString>& Stat : Detail.Stats) { AddStat(Stat.Key, Stat.Value); }
	}
}

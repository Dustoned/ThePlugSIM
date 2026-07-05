#include "UI/DealWidget.h"
#include "WeedShopCore.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "World/CityDoor.h" // FriendlyNpcName fallback

#include "UI/WeedUiStyle.h"
#include "UI/WeedItemPickGrid.h"
#include "Phone/PhoneClientComponent.h"
#include "Customer/CustomerBase.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "Save/SaveGameSubsystem.h" // StablePlayerId: per-speler tier/cooldown-reads in competitive

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Pawn.h"

namespace
{
	// D20 — nette productnaam voor de deal-teksten: volledige catalogus-naam via WeedUI::PrettyItemName,
	// met het " bag"-suffix gestript (zelfde patroon als ContactsComponent): "Critical Mass" i.p.v.
	// "CriticalMass_Bag2_2 bag". Hash_/Edible_/Bud_/Seed_ komen er via PrettyItemName al netjes uit.
	FString PrettyDealName(FName Id)
	{
		return WeedUI::PrettyItemName(Id).Replace(TEXT(" bag"), TEXT(""), ESearchCase::IgnoreCase);
	}
}

// ===================== UDealBagCell (geef-interactie-cel) =====================
TSharedRef<SWidget> UDealBagCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		if (Mode == 1)
		{
			// Geef-zone = drop-DOEL met een subtiel kader; de geef-grid (of de lege-hint) zit als Content erin.
			FSlateBrush Br = WeedUI::StorageSlotBrushWithFill(WeedUI::ColSlotEmpty(0.50f), false, false, WeedUI::ColAccent(0.30f), 9.f);
			Br.OutlineSettings.Width = 1.f;
			Br.OutlineSettings.Color = FSlateColor(WeedUI::ColAccent(0.30f));
			B->SetBrush(Br);
			B->SetPadding(FMargin(8.f, 8.f));
			// Content bovenaan (VAlign_Top) i.p.v. gecentreerd: het geef-vak is nu een HOOG vak; label/hint horen
			// bovenaan te staan (net als het bags-vak links), met de vrije drop-ruimte eronder.
			B->SetHorizontalAlignment(HAlign_Fill); B->SetVerticalAlignment(VAlign_Top);
			if (Content) { B->SetContent(Content); }
		}
		else
		{
			// Bron-zakje (mode 0) of gegeven bag (mode 2): canonieke inventory-icon-cel als Content. SOLIDE cel-bg
			// (zoals de inventory-cel) - NIET transparant: een transparant kader is niet hit-testbaar, dan kun je de
			// cel niet vastpakken om te slepen (alleen het icoon-pixel). Solide bg = de HELE cel grijpbaar + inventory-look.
			B->SetBrush(WeedUI::StorageSlotBrush(true, false, WeedUI::ColAccent(0.6f), 9.f));
			B->SetPadding(FMargin(0.f));
			if (Content) { B->SetContent(Content); }
		}
		WidgetTree->RootWidget = B;
	}
	// KRITISCH: hit-testbaar maken, anders start de sleep/drop/klik nooit (dat was de bug van de eerste poging).
	SetVisibility(ESlateVisibility::Visible);
	return Super::RebuildWidget();
}

FReply UDealBagCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (Mode == 0 && Avail > 0 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	if (Mode == 2 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) // klik op een gegeven bag = 1 terug
	{
		if (Owner.IsValid()) { Owner->OnGivenBagClicked(Strain, Gram); }
		return FReply::Handled();
	}
	if (Mode == 1 && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) // tik op de LEGE geef-zone = leegmaken
	{
		if (Owner.IsValid()) { Owner->OnGiveZoneClicked(); }
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UDealBagCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (Mode != 0 || Avail <= 0) { return; }
	UDealBagDragOp* Op = NewObject<UDealBagDragOp>(GetTransientPackage(), UDealBagDragOp::StaticClass());
	Op->Strain = Strain; Op->Gram = Gram; Op->Avail = Avail;
	Op->Pivot = EDragPivot::CenterCenter;
	// Sleep-visual = het ECHTE zakje-icoon dat aan de muis hangt (exact hetzelfde patroon als de inventory-drag,
	// UInvCell::NativeOnDragDetected): een SizeBox met WeedUI::ItemIcon op de canonieke bag-id. De vorige poging
	// hing een LEGE UDealBagCell op de muis (geen Content) -> je zag een grijs/leeg blok i.p.v. het zakje.
	if (WidgetTree)
	{
		const FName BagId = UInventoryComponent::MakeBagId(Strain, NAME_None, Gram);
		USizeBox* Vis = WidgetTree->ConstructWidget<USizeBox>();
		Vis->SetWidthOverride(58.f); Vis->SetHeightOverride(58.f);
		Vis->SetContent(WeedUI::ItemIcon(WidgetTree, BagId, 58.f));
		Op->DefaultDragVisual = Vis;
	}
	OutOperation = Op;
}

bool UDealBagCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (Mode == 1) // geef-zone: accepteer een gesleept zakje
	{
		if (UDealBagDragOp* Op = Cast<UDealBagDragOp>(InOperation))
		{
			if (Owner.IsValid()) { Owner->OnBagDroppedOnGive(Op->Strain, Op->Gram, Op->Avail); }
			return true;
		}
	}
	return false;
}

void UDealWidget::SetPhone(UPhoneClientComponent* InPhone)
{
	PhoneComp = InPhone;
}

UPhoneClientComponent* UDealWidget::GetPhone() const
{
	if (PhoneComp.IsValid()) { return PhoneComp.Get(); }
	APawn* P = GetOwningPlayerPawn();
	return P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
}

TSharedRef<SWidget> UDealWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UDealWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Klik-vang-achtergrond (transparant, wel hit-testbaar): als EERSTE kind (achter de kaart) vangt 'ie alleen
	// klikken die de kaart MISSEN -> op leegte klikken sluit de deal (speler-wens: wegklikbaar). De kaart ligt
	// erbovenop; de "hoeveel?"-modal ligt er weer bovenop (die vangt z'n eigen klikken).
	{
		UButton* Bd = WidgetTree->ConstructWidget<UButton>();
		FButtonStyle St;
		FSlateBrush Clear; Clear.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		St.Normal = Clear; St.Hovered = Clear; St.Pressed = Clear; St.Disabled = Clear;
		Bd->SetStyle(St);
		Bd->OnClicked.AddDynamic(this, &UDealWidget::OnBackdropClicked);
		UCanvasPanelSlot* BS = Root->AddChildToCanvas(Bd);
		BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BS->SetOffsets(FMargin(0.f));
		// KRITISCH: start Collapsed. De widget hangt ALTIJD in de viewport (ZOrder 30); een zichtbare full-screen
		// klik-vanger zou anders ALLE UI eronder (inventory/packing bench/telefoon) blokkeren als er geen deal is.
		// NativeTick zet 'm alleen Visible zolang de deal-kaart open staat.
		Bd->SetVisibility(ESlateVisibility::Collapsed);
		Backdrop = Bd;
	}

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DealCard"));
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.98f), 26.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(14.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	// Niet meer pal in het midden: onderaan (boven de hotbar) zodat de NPC in beeld vrij blijft.
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(0.5f, 1.f)); // onderrand = ankerpunt
	// AutoSize: de kaart krimpt naar zijn inhoud -> geen groot leeg grijs vlak bij niet-kopers.
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -104.f)); // net BOVEN de hotbar: kaart valt niet over de hotbar (speler-wens)

	// Vaste breedte (zodat de Fill-knoppen netjes uitlijnen), hoogte volgt de inhoud. De breedte is DYNAMISCH:
	// smal (420) bij losse dialogen/gram-deals, breed (740) bij bag-deals -> de bag-kolommen flankeren de gauges
	// (bags links, geef-vak rechts) als een brede strook onderaan i.p.v. het paneel hoog te maken.
	CardWidthBox = WidgetTree->ConstructWidget<USizeBox>();
	CardWidthBox->SetWidthOverride(420.f);
	CurrentCardWidth = 420.f;
	CardB->SetContent(CardWidthBox);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardWidthBox->SetContent(VB);

	const FLinearColor Money(0.4f, 0.85f, 0.5f); // geld-groen (zelfde tint als de Cash-kleur)

	// --- Kop: alleen identiteit. Deal-data staat in een vaste summary-strip eronder. ---
	{
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
		NameText = WeedUI::Text(WidgetTree, TEXT("Customer"), 15, WeedUI::ColAccent(), false, true);
		UHorizontalBoxSlot* NS = Head->AddChildToHorizontalBox(NameText);
		NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NS->SetVerticalAlignment(VAlign_Center);

		// Tier-pill (accent-vlak, alleen de tier-NAAM). TierText wordt hergebruikt als pill-tekst.
		TierPill = WidgetTree->ConstructWidget<UBorder>();
		TierPill->SetBrush(WeedUI::Rounded(WeedUI::ColAccentDim(0.88f), 6.f));
		TierPill->SetPadding(FMargin(7.f, 2.f));
		TierText = WeedUI::Text(WidgetTree, TEXT(""), 10, WeedUI::ColAccent(), false, true);
		TierPill->SetContent(TierText);
		UHorizontalBoxSlot* PS = Head->AddChildToHorizontalBox(TierPill);
		PS->SetHorizontalAlignment(HAlign_Right);
		PS->SetVerticalAlignment(VAlign_Center);
		PS->SetPadding(FMargin(0.f, 0.f, 9.f, 0.f));
		VB->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	// Compacte dealstrip met vaste informatieblokken en een full-width prijsrail eronder.
	{
		WantsRow = WidgetTree->ConstructWidget<UHorizontalBox>();

		UBorder* Summary = WidgetTree->ConstructWidget<UBorder>();
		{
			FSlateBrush Br = WeedUI::Rounded(WeedUI::ColSlotEmpty(0.42f), 8.f);
			Br.OutlineSettings.Width = 1.f;
			Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.28f));
			Summary->SetBrush(Br);
		}
		Summary->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));

		UVerticalBox* SummaryVB = WidgetTree->ConstructWidget<UVerticalBox>();
		Summary->SetContent(SummaryVB);

		UHorizontalBox* Metrics = WidgetTree->ConstructWidget<UHorizontalBox>();
		SummaryVB->AddChildToVerticalBox(Metrics);

		USizeBox* RequestBox = WidgetTree->ConstructWidget<USizeBox>();
		RequestBox->SetMinDesiredWidth(185.f);
		{
			UVerticalBox* RequestVB = WidgetTree->ConstructWidget<UVerticalBox>();
			RequestVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("WANTS"), 8, WeedUI::ColTextDim(0.78f), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
			UHorizontalBox* LabelHB = WidgetTree->ConstructWidget<UHorizontalBox>();
			WantsText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColText(), false, true);
			WantsStrainText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColText(), false, true);
			{ UHorizontalBoxSlot* WTS = LabelHB->AddChildToHorizontalBox(WantsText); WTS->SetVerticalAlignment(VAlign_Center); }
			{ UHorizontalBoxSlot* WSS = LabelHB->AddChildToHorizontalBox(WantsStrainText); WSS->SetVerticalAlignment(VAlign_Center); }
			RequestVB->AddChildToVerticalBox(LabelHB);
			RequestBox->SetContent(RequestVB);
		}
		{ UHorizontalBoxSlot* RS = Metrics->AddChildToHorizontalBox(RequestBox); RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); RS->SetVerticalAlignment(VAlign_Center); RS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f)); }

		DealMetricBox = WidgetTree->ConstructWidget<UHorizontalBox>();

		USizeBox* BidBox = WidgetTree->ConstructWidget<USizeBox>();
		BidBox->SetMinDesiredWidth(92.f);
		{
			UVerticalBox* BidVB = WidgetTree->ConstructWidget<UVerticalBox>();
			BidVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("BID"), 8, WeedUI::ColTextDim(0.78f), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
			PriceText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, true);
			BidVB->AddChildToVerticalBox(PriceText);
			BidBox->SetContent(BidVB);
		}
		{ UHorizontalBoxSlot* BS = DealMetricBox->AddChildToHorizontalBox(BidBox); BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f)); }

		USizeBox* ChanceBox = WidgetTree->ConstructWidget<USizeBox>();
		ChanceBox->SetMinDesiredWidth(52.f);
		{
			UVerticalBox* ChanceVB = WidgetTree->ConstructWidget<UVerticalBox>();
			ChanceVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("CHANCE"), 8, WeedUI::ColTextDim(0.78f), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
			ChanceText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColGood(), false, true);
			ChanceVB->AddChildToVerticalBox(ChanceText);
			ChanceBox->SetContent(ChanceVB);
		}
		{ UHorizontalBoxSlot* ChS = DealMetricBox->AddChildToHorizontalBox(ChanceBox); ChS->SetVerticalAlignment(VAlign_Center); ChS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f)); }

		USizeBox* TotalBox = WidgetTree->ConstructWidget<USizeBox>();
		TotalBox->SetMinDesiredWidth(72.f);
		{
			UVerticalBox* TotalVB = WidgetTree->ConstructWidget<UVerticalBox>();
			TotalVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("TOTAL"), 8, WeedUI::ColTextDim(0.78f), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
			PriceTotalText = WeedUI::Text(WidgetTree, TEXT(""), 15, Money, true, true);
			TotalVB->AddChildToVerticalBox(PriceTotalText);
			TotalBox->SetContent(TotalVB);
		}
		{ UHorizontalBoxSlot* TS = DealMetricBox->AddChildToHorizontalBox(TotalBox); TS->SetVerticalAlignment(VAlign_Center); }
		{ UHorizontalBoxSlot* DMS = Metrics->AddChildToHorizontalBox(DealMetricBox); DMS->SetVerticalAlignment(VAlign_Center); }

		PriceSlider = WidgetTree->ConstructWidget<USlider>();
		PriceSlider->SetSliderHandleColor(WeedUI::ColAccent());
		PriceSlider->SetSliderBarColor(WeedUI::ColSlot());
		{
			FSliderStyle SS = PriceSlider->GetWidgetStyle();
			SS.SetBarThickness(8.f);
			PriceSlider->SetWidgetStyle(SS);
		}
		PriceSlider->OnValueChanged.AddDynamic(this, &UDealWidget::OnPriceSlider);
		USizeBox* SliderH = WidgetTree->ConstructWidget<USizeBox>();
		SliderH->SetHeightOverride(16.f);
		SliderH->SetContent(PriceSlider);
		PriceRailBox = SliderH;
		{ UVerticalBoxSlot* SS = SummaryVB->AddChildToVerticalBox(SliderH); SS->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f)); SS->SetHorizontalAlignment(HAlign_Fill); }

		{ UHorizontalBoxSlot* SumS = WantsRow->AddChildToHorizontalBox(Summary); SumS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
		VB->AddChildToVerticalBox(WantsRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 0.f));
	}

	// Dunne accent-balk ONDER de header (naam + request + prijs) -> scheidt de header van de 3 kolommen.
	TierBar = WidgetTree->ConstructWidget<USizeBox>();
	TierBar->SetHeightOverride(3.f);
	{
		UBorder* Fill = WidgetTree->ConstructWidget<UBorder>();
		Fill->SetBrush(WeedUI::Rounded(WeedUI::ColStroke(0.45f), 1.f));
		TierBar->SetContent(Fill);
	}
	VB->AddChildToVerticalBox(TierBar)->SetPadding(FMargin(0.f, 5.f, 0.f, 7.f));

	// --- HOOFD-INDELING: 3 kolommen (speler-wens). LINKS je bags, MIDDEN de deal-info + ring-gauges, RECHTS het
	// geef-vak. De 2 inventory-kolommen FLANKEREN de progress-circles; brede strook onderaan, aansluitend op de hotbar.
	UHorizontalBox* MainCols = WidgetTree->ConstructWidget<UHorizontalBox>();
	VB->AddChildToVerticalBox(MainCols);

	// LINKER KOLOM (SellPane, togglebaar met de bag-deal): al je bags (inv + hotbar), sleepbaar. Vaste breedte
	// zodat de bag-cellen netjes 2-per-rij wrappen i.p.v. de kaart uit te rekken.
	SellPane = WidgetTree->ConstructWidget<USizeBox>();
	SellPane->SetWidthOverride(176.f);
	SellPane->SetMinDesiredHeight(172.f); // hoog vak (gauges centreren ernaast), maar NIET tot in de knoppen (gap eronder)
	{
		// Gehighlight vak: dezelfde rand-look als de geef-zone, maar om het HELE bags-vak (speler-wens).
		UBorder* SellBox = WidgetTree->ConstructWidget<UBorder>();
		{
			FSlateBrush Br = WeedUI::StorageSlotBrushWithFill(WeedUI::ColSlotEmpty(0.50f), false, false, WeedUI::ColAccent(0.24f), 9.f);
			Br.OutlineSettings.Width = 1.f;
			Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.34f));
			SellBox->SetBrush(Br);
		}
		SellBox->SetPadding(FMargin(8.f));
		SellPane->SetContent(SellBox);
		UVerticalBox* SellVB = WidgetTree->ConstructWidget<UVerticalBox>();
		SellBox->SetContent(SellVB);
		SellVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("BAGS"), 9, WeedUI::ColTextDim(0.85f), false, true));
		SellGrid = WidgetTree->ConstructWidget<UWrapBox>();
		SellGrid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
		SellVB->AddChildToVerticalBox(SellGrid)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
	}
	{ UHorizontalBoxSlot* SPS = MainCols->AddChildToHorizontalBox(SellPane); SPS->SetVerticalAlignment(VAlign_Fill); SPS->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f)); }

	// MIDDEN KOLOM (Fill): de deal-info (gauges/dialoog/prijs/kans). Fill -> centreert tussen de twee kolommen.
	UVerticalBox* MidCol = WidgetTree->ConstructWidget<UVerticalBox>();
	{ UHorizontalBoxSlot* MPS = MainCols->AddChildToHorizontalBox(MidCol); MPS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MPS->SetVerticalAlignment(VAlign_Center); }

	// RECHTER KOLOM (GivePane, togglebaar): gram-teller + het geef-vak (mode-1 drop-cel met geef-grid + lege-hint).
	GivePane = WidgetTree->ConstructWidget<USizeBox>();
	GivePane->SetWidthOverride(176.f);
	GivePane->SetMinDesiredHeight(172.f); // even hoog als het bags-vak links
	{
		// Het HELE vak is de drop-zone (mode-1 cel = de highlight-rand, zelfde look als links). Alles zit erbinnen:
		// het label, de gram-teller, de "Drop bags here"-hint en de gegeven bags.
		GiveZone = WidgetTree->ConstructWidget<UDealBagCell>();
		GiveZone->Owner = this; GiveZone->Mode = 1;
		UVerticalBox* GiveVB = WidgetTree->ConstructWidget<UVerticalBox>();
		GiveVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("GIVING"), 9, WeedUI::ColTextDim(0.85f), false, true));
		GiveGramsText = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColTextDim(), false, true);
		GiveVB->AddChildToVerticalBox(GiveGramsText)->SetPadding(FMargin(0.f, 1.f, 0.f, 3.f));
		GiveHint = WeedUI::Text(WidgetTree, TEXT("Drop bags"), 12, WeedUI::ColTextDim(), true, true);
		GiveHint->SetAutoWrapText(true);
		{ UVerticalBoxSlot* HS = GiveVB->AddChildToVerticalBox(GiveHint); HS->SetHorizontalAlignment(HAlign_Center); }
		GiveGrid = WidgetTree->ConstructWidget<UWrapBox>();
		GiveGrid->SetInnerSlotPadding(FVector2D(6.f, 6.f));
		GiveVB->AddChildToVerticalBox(GiveGrid);
		GiveZone->Content = GiveVB; // RebuildWidget (mode 1) wraps dit in de rand-cel
		GivePane->SetContent(GiveZone);
	}
	{ UHorizontalBoxSlot* GPS = MainCols->AddChildToHorizontalBox(GivePane); GPS->SetVerticalAlignment(VAlign_Fill); GPS->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f)); }

	StateText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColHighlight(), false, true);
	MidCol->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	// RelationText vervalt visueel (de 3 ringen tonen R/L/A nu). Member blijft bestaan (Collapsed) -> kleinste diff.
	RelationText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, true);
	MidCol->AddChildToVerticalBox(RelationText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	RelationText->SetVisibility(ESlateVisibility::Collapsed);

	// --- C.4: 3 ring-gauges (respect / loyalty / addiction), spiegel van PlantInfoWidget's MakeGauge-mechanisme.
	// Radiaal-materiaal 1x laden; elke gauge = SizeBox 88x88 -> Overlay{ ring-image (Fill) + icoon (Center) } + waarde + label.
	RadialMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/UI/M_RadialProgress.M_RadialProgress"));
	{
		auto MakeGauge = [this](const FString& IcoStem, const FLinearColor& IcoTint, const FString& Label,
			UImage*& OutRing, UTextBlock*& OutVal, UTextBlock*& OutDelta, const FString& SubLabel = FString(), UTextBlock** OutSub = nullptr) -> UWidget*
		{
			UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
			// Ring 96px + icoon 44px -> exact de plant-gauge-maat (speler-wens: weer net zo groot als bij de plants).
			USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(96.f); Sz->SetHeightOverride(96.f);
			UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>(); Sz->SetContent(Ov);
			OutRing = WidgetTree->ConstructWidget<UImage>();
			if (RadialMat) { OutRing->SetBrushFromMaterial(RadialMat); }
			OutRing->SetBrushSize(FVector2D(96.f, 96.f));
			UOverlaySlot* ROS = Ov->AddChildToOverlay(OutRing); ROS->SetHorizontalAlignment(HAlign_Fill); ROS->SetVerticalAlignment(VAlign_Fill);
			USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(44.f); IcoSz->SetHeightOverride(44.f);
			IcoSz->SetContent(WeedUI::KitIcon(WidgetTree, IcoStem, 44.f, IcoTint));
			UOverlaySlot* IS = Ov->AddChildToOverlay(IcoSz); IS->SetHorizontalAlignment(HAlign_Center); IS->SetVerticalAlignment(VAlign_Center);
			UVerticalBoxSlot* SzS = Box->AddChildToVerticalBox(Sz); SzS->SetHorizontalAlignment(HAlign_Center);
			OutVal = WeedUI::Text(WidgetTree, TEXT(""), 17, WeedUI::ColText(), true, true);
			UVerticalBoxSlot* VS = Box->AddChildToVerticalBox(OutVal); VS->SetHorizontalAlignment(HAlign_Center); VS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
			// LIVE deal-delta "+N" (groen, bold) direct onder de waarde: hoeveel deze deal deze stat oplevert.
			// 1x gebouwd; UpdateLive zet de tekst + toggelt de visibility (0/geen deal -> Collapsed). Vervangt PreviewText.
			OutDelta = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColGood(), true, true);
			UVerticalBoxSlot* DS = Box->AddChildToVerticalBox(OutDelta); DS->SetHorizontalAlignment(HAlign_Center);
			OutDelta->SetVisibility(ESlateVisibility::Collapsed);
			// D13a — optioneel mini-label direct onder de delta (bv. "to contact"): 1x gebouwd,
			// UpdateLive toggelt alleen de visibility (persistent, geen rebuild).
			if (OutSub)
			{
				*OutSub = WeedUI::Text(WidgetTree, SubLabel, 9, WeedUI::ColTextDim(), false, false);
				UVerticalBoxSlot* MS = Box->AddChildToVerticalBox(*OutSub); MS->SetHorizontalAlignment(HAlign_Center);
				(*OutSub)->SetVisibility(ESlateVisibility::Collapsed);
			}
			UTextBlock* Lbl = WeedUI::Text(WidgetTree, Label, 10, WeedUI::ColTextDim(), false, true);
			UVerticalBoxSlot* LS = Box->AddChildToVerticalBox(Lbl); LS->SetHorizontalAlignment(HAlign_Center);
			return Box;
		};

		StatGaugeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		UImage* Rr = nullptr; UTextBlock* Rt = nullptr; UTextBlock* Rd = nullptr; UTextBlock* Rs = nullptr;
		// D13a — respect-gauge krijgt het "to contact"-mini-label: zo leest de speler de voortgang naar
		// het telefoonnummer (waarde toont dan "X/45" in UpdateLive).
		StatGaugeRow->AddChildToHorizontalBox(MakeGauge(TEXT("t_medal_128"), FLinearColor::White, TEXT("Respect"), Rr, Rt, Rd, TEXT("to contact"), &Rs))->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
		RespectRing = Rr; RespectVal = Rt; RespectDelta = Rd; RespectSub = Rs;
		UImage* Lr = nullptr; UTextBlock* Lt = nullptr; UTextBlock* Ld = nullptr;
		StatGaugeRow->AddChildToHorizontalBox(MakeGauge(TEXT("t_heart_red_128"), FLinearColor::White, TEXT("Loyalty"), Lr, Lt, Ld))->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
		LoyaltyRing = Lr; LoyaltyVal = Lt; LoyaltyDelta = Ld;
		UImage* Ar = nullptr; UTextBlock* At = nullptr; UTextBlock* Ad = nullptr;
		// D13b — label "Hooked": dit is de haak die van een prospect een koper maakt.
		StatGaugeRow->AddChildToHorizontalBox(MakeGauge(TEXT("t_flame_128"), FLinearColor::White, TEXT("Hooked"), Ar, At, Ad));
		AddictRing = Ar; AddictVal = At; AddictDelta = Ad;
		UVerticalBoxSlot* SGS = MidCol->AddChildToVerticalBox(StatGaugeRow);
		SGS->SetHorizontalAlignment(HAlign_Center); SGS->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
	}

	// --- Dialoog-kader: wat de NPC zegt ---
	{
		UBorder* DB = WidgetTree->ConstructWidget<UBorder>();
		DB->SetBrush(WeedUI::Rounded(WeedUI::ColInner(), 10.f));
		DB->SetPadding(FMargin(12.f, 10.f));
		DialogueText = WeedUI::Text(WidgetTree, TEXT("..."), 14, WeedUI::ColText(), false, false);
		DialogueText->SetAutoWrapText(true);
		DB->SetContent(DialogueText);
		DialogueBox = DB;
		MidCol->AddChildToVerticalBox(DB)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	// (De "Wants Xg <strain>"-request staat nu BOVENAAN, boven de kolommen - zie de request-rij hierboven.)

	// SubText vervalt als los element (substituut-info zit nu in de ChanceText-suffix). Member blijft,
	// maar permanent verborgen + leeg -> kleinste diff, geen ruis in de layout.
	SubText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim());
	MidCol->AddChildToVerticalBox(SubText);
	SubText->SetVisibility(ESlateVisibility::Collapsed);

	// (PriceText + prijs-slider staan nu compact BOVENAAN in de header - zie het prijs-blok daar.)

	// Hoeveelheid: bag-offers krijgen de tastbare GEEF-TRAY (sleep zakjes). Losse gram-items
	// (concentraten/edibles) vallen terug op deze AmountText-regel (dan geef je automatisch de gevraagde hoeveelheid).
	AmountText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColText(), false, true);
	MidCol->AddChildToVerticalBox(AmountText)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
	// (De geef-tray staat NIET hier maar onderaan bij "Selling:" - zo staat de deal-kans vlak onder de prijs-slider.)

	StockText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim());
	MidCol->AddChildToVerticalBox(StockText)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));

	// (De deal-kans staat nu als mee-kleurend % in de request-rij BOVENAAN; geen aparte kans-bar meer.)

	// PreviewText is dood (spec): de +respect/+loyalty/+hooked worden nu LIVE als delta op de 3 gauges getoond.
	// Member blijft (kleinste diff) maar permanent Collapsed + leeg.
	PreviewText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim());
	MidCol->AddChildToVerticalBox(PreviewText);
	PreviewText->SetVisibility(ESlateVisibility::Collapsed);

	// Grote, duidelijke melding als je niets te verkopen hebt (verbergt de hele prijs-flow).
	NoWeedText = WeedUI::Text(WidgetTree, TEXT("No bagged weed.\nGrow -> dry -> bag first."), 14, WeedUI::ColWarn(), false, true);
	MidCol->AddChildToVerticalBox(NoWeedText)->SetPadding(FMargin(0.f, 14.f, 0.f, 14.f));

	// (De sell-grid + geef-zone staan nu in de LINKER/RECHTER kolom hierboven, geflankeerd om de gauges.)

	// Joint-kiezer (verborgen tot je "Give joint" klikt): kies WELKE joint je geeft. Geen selectie (elke klik = geven).
	JointPickerBox = WidgetTree->ConstructWidget<UVerticalBox>();
	MidCol->AddChildToVerticalBox(JointPickerBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));
	JointPickerBox->SetVisibility(ESlateVisibility::Collapsed);
	{
		JointGrid = WidgetTree->ConstructWidget<UWeedItemPickGrid>();
		JointGrid->bShowSelection = false;
		JointGrid->OnPick = [this](FName Id, int32 /*P*/)
		{
			if (UPhoneClientComponent* Ph = GetPhone()) { Ph->RequestGiveJointId(Ph->GetDealCustomer(), Id); }
			if (JointPickerBox) { JointPickerBox->SetVisibility(ESlateVisibility::Collapsed); }
		};
		JointPickerBox->AddChildToVerticalBox(JointGrid);
	}

	// Knoppen: Offer deal (primair, alleen kopers) / Give joint / Leave.
	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakeBtn = [this](const FString& Label, const FLinearColor& Col, int32 Act) -> UWeedActionButton*
	{
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->Action = Act;
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([this](int32 A, int32 /*P*/)
		{
			UPhoneClientComponent* Ph = GetPhone();
			if (!Ph) { return; }
			if (A == 1) { Ph->ConfirmDeal(); }
			else if (A == 2) { GiveJointPressed(); }
			else { Ph->CloseDeal(); }
		});
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(12.f, 8.f); St.PressedPadding = FMargin(12.f, 8.f);
		B->SetStyle(St);
		B->SetContent(WeedUI::Text(WidgetTree, Label, 14, WeedUI::ColText(), true, true));
		return B;
	};
	UWeedActionButton* OB = MakeBtn(TEXT("Offer deal"), WeedUI::ColAccent(), 1);
	UHorizontalBoxSlot* OS = Btns->AddChildToHorizontalBox(OB);
	OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	OfferBtn = OB;
	UWeedActionButton* GB = MakeBtn(TEXT("Give joint"), WeedUI::ColAccentDim(), 2);
	UHorizontalBoxSlot* GS = Btns->AddChildToHorizontalBox(GB);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	GiveBtn = GB;
	UHorizontalBoxSlot* CcS = Btns->AddChildToHorizontalBox(MakeBtn(TEXT("Leave"), WeedUI::ColSlot(), 0));
	CcS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	VB->AddChildToVerticalBox(Btns)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f)); // gap: highlight-vakken lopen NIET tot in de knoppen

	// "Hoeveel geven?"-modal LAATST op de root-canvas bouwen -> hoogste paint-order binnen het deal-scherm.
	BuildAmountPopup(Root);
}

void UDealWidget::OnBackdropClicked()
{
	// Klik op de leegte naast de kaart -> deal sluiten (zelfde als de "Leave"-knop).
	if (UPhoneClientComponent* Ph = GetPhone()) { Ph->CloseDeal(); }
}

void UDealWidget::OnPriceSlider(float Value)
{
	// Snappy: klem de waarde naar 20 discrete stappen -> de handle "klikt" naar posities i.p.v. vloeiend te
	// glijden. SetValue(Snapped) laat de handle meteen naar de stap springen; de guard voorkomt recursie
	// (de her-broadcast levert dezelfde Snapped -> geen tweede SetValue).
	const float Snapped = FMath::RoundToFloat(Value * 20.f) / 20.f;
	if (PriceSlider && !FMath::IsNearlyEqual(PriceSlider->GetValue(), Snapped)) { PriceSlider->SetValue(Snapped); }
	bSliderHeld = true;
	UPhoneClientComponent* Ph = GetPhone();
	if (!Ph) { return; }
	const int32 Market = Ph->GetOfferMarketCents();
	if (Market <= 0) { return; }
	const int32 Ask = FMath::RoundToInt(Market * (0.40f + 1.60f * Snapped));
	Ph->SetDealAskCents(Ask);
}

void UDealWidget::OnAmountSlider(float Value)
{
	bAmountHeld = true;
	UPhoneClientComponent* Ph = GetPhone();
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Ph || !Inv) { return; }
	float Thc = 0.f, QPct = 0.f;
	const FName Off = Ph->GetOfferedProduct();
	const int32 Stock = Off.ToString().StartsWith(TEXT("Bag_"))
		? Inv->BagStockGrams(UInventoryComponent::BagStrain(Off), Thc, QPct)
		: Inv->GetQuantity(Off);
	if (Stock <= 0) { return; }
	int32 Grams = FMath::Clamp(FMath::RoundToInt(Value * Stock), 1, Stock);
	if (Off.ToString().StartsWith(TEXT("Bag_"))) // bags per heel zakje -> snap naar wat je echt levert
	{
		const int32 Real = Inv->BagGramsForTarget(UInventoryComponent::BagStrain(Off), Grams);
		if (Real > 0) { Grams = Real; }
	}
	Ph->SetDealGiveGrams(Grams);
}

void UDealWidget::GiveJointPressed()
{
	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Ph || !C || !Inv || !JointPickerBox) { return; }

	// Verzamel je joints (elke joint-stack = eigen strain/gram/kwaliteit).
	TArray<FName> Joints;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.Quantity > 0 && St.ItemId.ToString().StartsWith(TEXT("Joint_"))) { Joints.AddUnique(St.ItemId); }
	}

	if (Joints.Num() == 1)
	{
		// Eén joint -> meteen geven (geen keuze nodig); de reactie verschijnt in dit venster.
		Ph->RequestGiveJointId(C, Joints[0]);
		JointPickerBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// 0 of meerdere -> toon de kiezer (0 toont "roll one first"); nogmaals klikken sluit 'm weer.
	if (JointPickerBox->GetVisibility() == ESlateVisibility::Visible)
	{
		JointPickerBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	RebuildJointPicker();
	JointPickerBox->SetVisibility(ESlateVisibility::Visible);
}

void UDealWidget::RebuildJointPicker()
{
	if (!JointPickerBox || !JointGrid) { return; }
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }

	// Grid-items opbouwen: per joint-id één cel. Icoon = de joint-id, badge = aantal, subline = gram +
	// kwaliteit + de sample-opbrengst (D21): zo ziet de speler dat een dikkere joint meer oplevert
	// (de afnemende meeropbrengst + caps zitten in ComputeSampleEffect, zelfde formule als het echte geven).
	TArray<FWeedPickItem> Items;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.Quantity <= 0 || !St.ItemId.ToString().StartsWith(TEXT("Joint_"))) { continue; }
		const FName Id = St.ItemId;
		const FName Strain = UInventoryComponent::JointStrain(Id);
		const int32 Grams = UInventoryComponent::JointGrams(Id);
		const float QPct = Inv->GetItemQualityPct(Id);

		// D21 — verwachte opbrengst van DEZE joint (kwaliteit 0..1, zelfde schaal als GiveSampleCore's WeedQ).
		float Inten = 0.f, AddG = 0.f, LoyG = 0.f, RespG = 0.f;
		UPhoneClientComponent::ComputeSampleEffect((float)Grams, FMath::Clamp(QPct / 100.f, 0.f, 1.f), Inten, AddG, LoyG, RespG);
		FString Gains = FString::Printf(TEXT("+%.0f hooked"), AddG);
		if (FMath::RoundToInt(LoyG) != 0) { Gains += FString::Printf(TEXT(" +%.0f loy"), LoyG); }

		FWeedPickItem It;
		It.Id = Id;
		It.IconId = Id;
		It.Badge = FString::Printf(TEXT("x%d"), St.Quantity);
		// Twee subregels in één tekstblok (het grid rendert \n gewoon; sig-diff dekt beide regels).
		It.SubLine = FString::Printf(TEXT("%dg Q%.0f%%\n%s"), Grams, QPct, *Gains);
		It.Tooltip = FString::Printf(TEXT("%s  %dg  Quality %.0f%%  (x%d)\nSample effect: +%.0f hooked, +%.0f loyalty, +%.0f respect"),
			Strain.IsNone() ? TEXT("Joint") : *Strain.ToString(), Grams, QPct, St.Quantity, AddG, LoyG, RespG);
		Items.Add(It);
	}

	// Persistent: het grid diff't intern (geen ClearChildren). Geen selectie (elke klik = joint geven).
	JointGrid->SetItems(Items);
	JointGrid->SetVisibility(Items.Num() > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// Lege-melding tonen/verbergen zonder ClearChildren.
	if (Items.Num() == 0)
	{
		if (!JointEmptyText)
		{
			JointEmptyText = WeedUI::Text(WidgetTree, TEXT("No joints - roll one first (R)."), 12, WeedUI::ColWarn());
			JointPickerBox->AddChildToVerticalBox(JointEmptyText);
		}
		JointEmptyText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (JointEmptyText)
	{
		JointEmptyText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ===================== Geef-tray-implementatie =====================

int32 UDealWidget::PileTotalGrams() const
{
	int32 T = 0;
	for (const TPair<int32, int32>& P : PileCounts) { T += P.Key * P.Value; }
	return T;
}

int32 UDealWidget::PileAvailFor(FName Strain, int32 Gram) const
{
	int32 Owned = 0;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			TArray<TPair<int32, int32>> Sizes; Inv->ListBagSizes(Strain, Sizes);
			for (const TPair<int32, int32>& S : Sizes) { if (S.Key == Gram) { Owned = S.Value; break; } }
		}
	}
	// Alleen aftrekken wat al in de pile ligt ALS de pile diezelfde strain is; voor een ANDERE strain (die de
	// pile straks vervangt) is het volledige bezit beschikbaar (anders trek je per ongeluk een vreemde-strain-pile af).
	const int32 InPile = (Strain == PileStrain && PileCounts.Contains(Gram)) ? PileCounts[Gram] : 0;
	return FMath::Max(0, Owned - InPile);
}

void UDealWidget::SetCardWidth(float W)
{
	// Alleen echt herschalen bij een wijziging -> geen per-tick re-layout-flits (UpdateLive draait elke refresh).
	if (CardWidthBox && !FMath::IsNearlyEqual(CurrentCardWidth, W))
	{
		CardWidthBox->SetWidthOverride(W);
		CurrentCardWidth = W;
	}
}

void UDealWidget::SetTrayVisible(bool bVis)
{
	// De twee inventory-kolommen (bags links, geef-vak rechts) samen tonen/verbergen. Collapsed = geen breedte
	// (zodat de midden-kolom bij een losse gram-deal netjes centreert in de smalle kaart).
	const ESlateVisibility V = bVis ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (SellPane) { SellPane->SetVisibility(V); }
	if (GivePane) { GivePane->SetVisibility(V); }
}


// Canonieke inventory-icon-cel-content (spiegel van UWeedItemPickGrid::MakeCellContent): bag-icoon (68px)
// gecentreerd in een SizeBox op celmaat 86, count-badge rechtsboven (ColBg-pill "xN", 7px inset),
// strain-tag onderaan (TagColorForItem, 3px van onder).
UWidget* UDealWidget::MakeBagCellContent(FName Strain, int32 Gram, int32 Count) const
{
	const float CellSize = 74.f;
	const float IconPx = 58.f;
	// Canonieke bag-id (container-loos) zodat icoon/tag kloppen: Bag_<strain>_<G>. BagGrams() leest de laatste
	// token als getal (dus GEEN "g"-suffix), en het icoon schaalt op die gram-maat (weed_bag/jar/block/sack).
	const FName BagId = UInventoryComponent::MakeBagId(Strain, NAME_None, Gram);

	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();

	// Icoon gecentreerd.
	{
		UOverlaySlot* IconOS = Ov->AddChildToOverlay(WeedUI::ItemIcon(WidgetTree, BagId, IconPx));
		IconOS->SetHorizontalAlignment(HAlign_Center);
		IconOS->SetVerticalAlignment(VAlign_Center);
	}

	// Count-badge rechtsboven ("xN").
	{
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
		Pill->SetBrush(WeedUI::ItemQtyPillBrush());
		Pill->SetPadding(FMargin(5.f, 1.f, 5.f, 1.f));
		Pill->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%dx %dg"), Count, Gram), 10, WeedUI::ColText(), false, true));
		Pill->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* PS = Ov->AddChildToOverlay(Pill);
		PS->SetHorizontalAlignment(HAlign_Right);
		PS->SetVerticalAlignment(VAlign_Top);
		PS->SetPadding(FMargin(0.f, 7.f, 7.f, 0.f));
	}

	// Strain-tag-pill onderaan (grootte-code als er geen strain-tag is, bv. "5g" — zelfde onderrij als de inventory).
	{
		FString Tag = WeedUI::ItemTagShort(BagId);
		if (Tag.IsEmpty()) { Tag = FString::Printf(TEXT("%dg"), Gram); }
		UTextBlock* TagT = WeedUI::Text(WidgetTree, Tag, 11, FLinearColor::White, false, true);
		TagT->SetFont(WeedUI::ItemTagFont(11));
		TagT->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
		TagT->SetShadowOffset(FVector2D(1.f, 1.f));
		UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
		TagPill->SetBrush(WeedUI::ItemTagPillBrush(BagId, 6.f));
		TagPill->SetPadding(FMargin(6.f, 0.f, 6.f, 2.f));
		TagPill->SetContent(TagT);
		TagPill->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* TS = Ov->AddChildToOverlay(TagPill);
		TS->SetHorizontalAlignment(HAlign_Center);
		TS->SetVerticalAlignment(VAlign_Bottom);
		TS->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
	}

	USizeBox* Fill = WidgetTree->ConstructWidget<USizeBox>();
	Fill->SetWidthOverride(CellSize);
	Fill->SetHeightOverride(CellSize);
	Fill->SetContent(Ov);
	return Fill;
}

void UDealWidget::RebuildSellGrid()
{
	if (!SellGrid) { return; }
	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }

	// ALLE bag-stacks (inventory + hotbar zitten in dezelfde component), gegroepeerd per (strain, gram) -> bezit.
	// Vaste, deterministische volgorde (strain-naam, dan grote maat eerst) zodat de sig stabiel is.
	struct FBagKey { FName Strain; int32 Gram; };
	TArray<FBagKey> Keys;
	TMap<FString, int32> Owned; // "strain|gram" -> aantal zakjes
	auto KeyStr = [](FName S, int32 G) { return FString::Printf(TEXT("%s|%d"), *S.ToString(), G); };
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.Quantity <= 0 || !UInventoryComponent::IsBag(St.ItemId)) { continue; }
		const FName S = UInventoryComponent::BagStrain(St.ItemId);
		const int32 G = UInventoryComponent::BagGrams(St.ItemId);
		const FString K = KeyStr(S, G);
		if (!Owned.Contains(K)) { Keys.Add({ S, G }); }
		Owned.FindOrAdd(K) += St.Quantity;
	}
	Keys.Sort([](const FBagKey& A, const FBagKey& B)
	{
		const int32 Cmp = A.Strain.Compare(B.Strain);
		return Cmp != 0 ? Cmp < 0 : A.Gram > B.Gram; // strain A->Z, binnen strain groot->klein
	});

	// Beschikbaar = bezit - wat al in de pile ligt (van de huidige pile-strain). Sig over strain|gram|avail.
	FString Sig;
	for (const FBagKey& K : Keys)
	{
		const int32 Owned0 = Owned[KeyStr(K.Strain, K.Gram)];
		const int32 InPile = (K.Strain == PileStrain && PileCounts.Contains(K.Gram)) ? PileCounts[K.Gram] : 0;
		const int32 Avail = FMath::Max(0, Owned0 - InPile);
		Sig += FString::Printf(TEXT("%s:%d:%d|"), *K.Strain.ToString(), K.Gram, Avail);
	}
	Sig += FString::Printf(TEXT("pile=%s"), *PileStrain.ToString());
	if (Sig == SellGridSig) { return; } // sig-gate: alleen bij een voorraad-/pile-wijziging herbouwen
	SellGridSig = Sig;

	SellGrid->ClearChildren();
	for (const FBagKey& K : Keys)
	{
		const int32 Owned0 = Owned[KeyStr(K.Strain, K.Gram)];
		const int32 InPile = (K.Strain == PileStrain && PileCounts.Contains(K.Gram)) ? PileCounts[K.Gram] : 0;
		const int32 Avail = FMath::Max(0, Owned0 - InPile);
		UDealBagCell* Cell = WidgetTree->ConstructWidget<UDealBagCell>();
		Cell->Owner = this; Cell->Mode = 0; Cell->Strain = K.Strain; Cell->Gram = K.Gram; Cell->Avail = Avail;
		Cell->Content = MakeBagCellContent(K.Strain, K.Gram, Avail);
		// Op = uitverkocht (alles in de pile): dimmen zodat de speler ziet dat 'ie niks meer kan slepen.
		Cell->SetRenderOpacity(Avail > 0 ? 1.f : 0.35f);
		SellGrid->AddChildToWrapBox(Cell);
	}
}

void UDealWidget::RefreshGiveZone()
{
	// De geef-grid bij een geef/verwijder-actie (of strain-wissel) opnieuw vullen = user-actie, geen per-tick rebuild.
	if (GiveGrid)
	{
		GiveGrid->ClearChildren();
		TArray<int32> Grams; PileCounts.GetKeys(Grams); Grams.Sort([](const int32& A, const int32& B) { return A > B; });
		for (int32 G : Grams)
		{
			const int32 N = PileCounts[G];
			if (N <= 0) { continue; }
			UDealBagCell* Cell = WidgetTree->ConstructWidget<UDealBagCell>();
			Cell->Owner = this; Cell->Mode = 2; Cell->Strain = PileStrain; Cell->Gram = G; Cell->Avail = N;
			Cell->Content = MakeBagCellContent(PileStrain, G, N); // count = hoeveel van die maat je geeft
			GiveGrid->AddChildToWrapBox(Cell);
		}
	}
	const int32 Total = PileTotalGrams();
	int32 Qty = 0;
	if (UPhoneClientComponent* Ph0 = GetPhone())
	{
		if (ACustomerBase* C0 = Ph0->GetDealCustomer()) { Qty = C0->DesiredQuantity; }
	}
	// "Drop bags here"-hint (statische tekst, in BuildShell gezet) alleen tonen als de pile leeg is; zodra je
	// zakjes geeft spreken de bag-iconen voor zich.
	if (GiveHint)
	{
		GiveHint->SetVisibility(Total > 0 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	// DUIDELIJKE gram-teller onder het geef-label: "Giving Xg / Ng" (groen zodra je genoeg geeft).
	if (GiveGramsText)
	{
		GiveGramsText->SetText(FText::FromString(Qty > 0
			? FString::Printf(TEXT("Giving %dg / %dg"), Total, Qty)
			: FString::Printf(TEXT("Giving %dg"), Total)));
		GiveGramsText->SetColorAndOpacity(FSlateColor((Qty > 0 && Total >= Qty) ? WeedUI::ColGood() : WeedUI::ColTextDim()));
	}
	if (UPhoneClientComponent* Ph = GetPhone()) { Ph->SetDealGiveGrams(FMath::Max(0, Total)); }
	// De sell-grid moet mee-updaten: het beschikbare aantal per maat is nu bezit - pile.
	RebuildSellGrid();
}

void UDealWidget::SyncPileToOffered()
{
	UPhoneClientComponent* Ph = GetPhone();
	if (!Ph) { return; }
	// De pile-strain volgt uit de aangeboden strain; bij een strain-wissel begin je LEEG opnieuw (1 strain tegelijk).
	const FName Strain = UInventoryComponent::BagStrain(Ph->GetOfferedProduct());
	if (Strain == PileStrain) { return; }
	PileStrain = Strain;
	PileCounts.Reset();
	RefreshGiveZone();
}

void UDealWidget::OnBagDroppedOnGive(FName Strain, int32 Gram, int32 /*Avail*/)
{
	UPhoneClientComponent* Ph = GetPhone();
	if (!Ph) { return; }
	// Staat de "hoeveel?"-popup open? Negeer nieuwe drops (voorkomt inconsistente pending-state vs zichtbare pile).
	if (AmountRoot && AmountRoot->GetVisibility() != ESlateVisibility::Collapsed) { return; }

	// Beschikbaar van deze (strain,maat). PileAvailFor telt de pile alleen mee bij dezelfde strain, dus een
	// andere strain geeft hier het VOLLE bezit (de pile-reset gebeurt pas bij ApplyGive, zie onder).
	const int32 Avail = PileAvailFor(Strain, Gram);
	if (Avail <= 0) { return; }
	if (Avail == 1) { ApplyGive(Strain, Gram, 1); return; } // enkel zakje -> direct geven, geen popup
	OpenAmountPopup(Strain, Gram, Avail);                   // meerdere -> vraag HOEVEEL
}

void UDealWidget::ApplyGive(FName Strain, int32 Gram, int32 N)
{
	UPhoneClientComponent* Ph = GetPhone();
	if (!Ph) { return; }
	// De pile-reset + strain-switch gebeurt PAS hier (bij het echt toevoegen), niet al bij de drop: zo wist het
	// ANNULEREN van de hoeveel-popup je bestaande pile niet. SetOfferedProduct schaalt prijs/kans mee
	// (basis-id Bag_<strain>, matcht de substituut-detectie tegen C->DesiredProductId).
	if (Strain != PileStrain)
	{
		Ph->SetOfferedProduct(FName(*FString::Printf(TEXT("Bag_%s"), *Strain.ToString())));
		PileStrain = Strain;
		PileCounts.Reset();
	}
	// Klem tegen het actuele bezit (defensief: voorraad kan tussen open en confirm theoretisch wijzigen).
	N = FMath::Clamp(N, 1, FMath::Max(1, PileAvailFor(Strain, Gram)));
	PileCounts.FindOrAdd(Gram) += N;
	RefreshGiveZone();
}

void UDealWidget::OnGivenBagClicked(FName Strain, int32 Gram)
{
	if (Strain != PileStrain) { return; }
	int32* N = PileCounts.Find(Gram);
	if (!N) { return; }
	*N -= 1;
	if (*N <= 0) { PileCounts.Remove(Gram); }
	RefreshGiveZone();
}

void UDealWidget::OnGiveZoneClicked()
{
	// Bewust GEEN "alles wissen" meer: nu het HELE rechter vak de drop-zone is, zou een klik op het label/hint
	// anders per ongeluk de hele pile wissen. Je haalt zakjes er 1-voor-1 af door op een gegeven bag te klikken.
}

// ===================== "Hoeveel geven?"-popup (modal, spiegel van de inventory-split-popup) =====================

void UDealWidget::BuildAmountPopup(UCanvasPanel* Root)
{
	if (!Root) { return; }
	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();

	// Full-screen hit-test-blocker (dim): vangt ALLE klikken zodat de rest van het deal-scherm modaal geblokkeerd
	// is (de deal-root is SelfHitTestInvisible -> zonder blocker blijven sell-grid/knoppen klikbaar onder de popup).
	UBorder* Blocker = WidgetTree->ConstructWidget<UBorder>();
	Blocker->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f));
	Blocker->SetVisibility(ESlateVisibility::Visible);
	UOverlaySlot* BS = Ov->AddChildToOverlay(Blocker);
	BS->SetHorizontalAlignment(HAlign_Fill); BS->SetVerticalAlignment(VAlign_Fill);

	// Gecentreerd paneel (zelfde look als de split-popup: SizeBox -> Border -> VBox).
	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(300.f);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 16.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.7f));
		Panel->SetBrush(Br);
	}
	Panel->SetPadding(FMargin(16.f, 14.f));
	UVerticalBox* PV = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(PV);
	PV->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("How many to give?"), 15, WeedUI::ColAccent(), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	AmountPickLabel = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColText(), false, true);
	PV->AddChildToVerticalBox(AmountPickLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	AmountPickSlider = WidgetTree->ConstructWidget<USlider>();
	AmountPickSlider->SetSliderHandleColor(WeedUI::ColAccent());
	AmountPickSlider->SetSliderBarColor(WeedUI::ColSlot());
	AmountPickSlider->SetMinValue(0.f); AmountPickSlider->SetMaxValue(1.f); AmountPickSlider->SetValue(1.f);
	{ FSliderStyle SS = AmountPickSlider->GetWidgetStyle(); SS.SetBarThickness(8.f); AmountPickSlider->SetWidgetStyle(SS); }
	AmountPickSlider->OnValueChanged.AddDynamic(this, &UDealWidget::OnAmountPickChanged);
	{
		USizeBox* SH = WidgetTree->ConstructWidget<USizeBox>(); SH->SetHeightOverride(20.f); SH->SetContent(AmountPickSlider);
		PV->AddChildToVerticalBox(SH)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	// Knoppen: Give (confirm) + Cancel -- recept als BuildShell's MakeBtn (UWeedActionButton + OnAction-lambda).
	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakePopBtn = [this](const FString& Label, const FLinearColor& Col, bool bConfirm) -> UWeedActionButton*
	{
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->Action = 0;
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([this, bConfirm](int32, int32) { if (bConfirm) { ConfirmAmount(); } else { CancelAmount(); } });
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(12.f, 7.f); St.PressedPadding = FMargin(12.f, 7.f);
		B->SetStyle(St);
		B->SetContent(WeedUI::Text(WidgetTree, Label, 14, WeedUI::ColText(), true, true));
		return B;
	};
	UHorizontalBoxSlot* GBS = Btns->AddChildToHorizontalBox(MakePopBtn(TEXT("Give"), WeedUI::ColAccent(), true));
	GBS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GBS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	UHorizontalBoxSlot* CBS = Btns->AddChildToHorizontalBox(MakePopBtn(TEXT("Cancel"), WeedUI::ColSlot(), false));
	CBS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PV->AddChildToVerticalBox(Btns);

	Sz->SetContent(Panel);
	UOverlaySlot* PS = Ov->AddChildToOverlay(Sz);
	PS->SetHorizontalAlignment(HAlign_Center); PS->SetVerticalAlignment(VAlign_Center);

	// Op de ROOT-canvas (niet de kaart-VerticalBox): Collapsed<->Visible op een canvas-kind herlayout de kaart NIET.
	AmountRoot = Ov;
	UCanvasPanelSlot* CPS = Root->AddChildToCanvas(Ov);
	CPS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	CPS->SetOffsets(FMargin(0.f));
	Ov->SetVisibility(ESlateVisibility::Collapsed);
}

void UDealWidget::OpenAmountPopup(FName Strain, int32 Gram, int32 MaxN)
{
	if (!AmountRoot || !AmountPickSlider) { return; }
	PendingStrain = Strain; PendingGram = Gram; PendingMax = FMath::Max(1, MaxN);
	AmountPickSlider->SetValue(1.f);
	OnAmountPickChanged(1.f);
	AmountRoot->SetVisibility(ESlateVisibility::Visible);
}

void UDealWidget::OnAmountPickChanged(float V)
{
	if (!AmountPickLabel) { return; }
	const int32 N = FMath::Clamp(FMath::RoundToInt(V * PendingMax), 1, FMath::Max(1, PendingMax));
	AmountPickLabel->SetText(FText::FromString(FString::Printf(TEXT("Give: %d  (of %d)  =  %dg"), N, PendingMax, N * PendingGram)));
}

void UDealWidget::ConfirmAmount()
{
	if (AmountPickSlider && PendingGram > 0 && PendingMax > 0)
	{
		const int32 N = FMath::Clamp(FMath::RoundToInt(AmountPickSlider->GetValue() * PendingMax), 1, PendingMax);
		ApplyGive(PendingStrain, PendingGram, N);
	}
	CancelAmount();
}

void UDealWidget::CancelAmount()
{
	PendingStrain = NAME_None; PendingGram = 0; PendingMax = 0;
	if (AmountRoot) { AmountRoot->SetVisibility(ESlateVisibility::Collapsed); }
}

void UDealWidget::UpdateLive()
{
	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	// Deal weg -> een eventueel open "hoeveel?"-popup mee sluiten (anders blokkeert 'ie de volgende deal).
	if (!Ph || !C) { if (AmountRoot && AmountRoot->GetVisibility() != ESlateVisibility::Collapsed) { CancelAmount(); } return; }

	// --- Value-key rond de TEKST-updates: alle bron-waarden die hieronder getoond worden. Gelijk aan de
	// vorige tick = geen enkele SetText/visibility-call nodig -> hele body overslaan (geen visueel verschil).
	{
		int32 KStock = 0; float KThc = 0.f, KQPct = 0.f; bool bKHasWeed = false;
		if (APawn* P = GetOwningPlayerPawn())
		{
			if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
			{
				for (const FInventoryStack& St : Inv->GetStacks())
				{
					if (St.ItemId.ToString().StartsWith(TEXT("Bag_")) && St.Quantity > 0) { bKHasWeed = true; break; }
				}
				KStock = Inv->BagStockGrams(UInventoryComponent::BagStrain(Ph->GetOfferedProduct()), KThc, KQPct);
			}
		}
		int32 KUnlocked = -1, KTier = -1;
		if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
			{
				// Tier per-speler (competitive): de tier van MIJN relatie met deze klant, niet de gedeelde base.
				if (!C->NpcId.IsNone()) { KUnlocked = Reg->IsUnlocked(C->NpcId) ? 1 : 0; KTier = Reg->GetCustomerTier(C->NpcId, USaveGameSubsystem::StablePlayerId(GetOwningPlayerPawn())); }
			}
		}
		// DealGiveGrams meenemen: een drop/verwijder in de geef-zone verandert de gegeven grammen -> de deal-kans
		// en de gauge-DELTA's moeten dan mee-updaten (anders zou de key gelijk blijven en de body overslaan).
		const FString Key = FString::Printf(TEXT("%llu|%d|%.2f|%.2f|%.2f|%.2f|%s|%d|%s|%s|%d|%d|%d|%d|%d|%.2f|%.2f|%d|%d|%d|%d"),
			(unsigned long long)(UPTRINT)C, (int32)C->State, C->Respect, C->Loyalty, C->Addiction, C->AddictionToBuy,
			*C->SpeechLine, C->DesiredQuantity, *C->DesiredProductId.ToString(),
			*Ph->GetOfferedProduct().ToString(), Ph->IsOfferingSubstitute() ? 1 : 0,
			Ph->GetOfferMarketCents(), Ph->GetDealAskCents(),
			bKHasWeed ? 1 : 0, KStock, KThc, KQPct, bSliderHeld ? 1 : 0, KUnlocked, KTier, Ph->GetDealGiveGrams());
		if (Key == LastLiveKey) { return; }
		LastLiveKey = Key;
	}

	// --- Kop (altijd, voor ELKE NPC): naam, status, stats, dialoog ---
	FString NpcName = ACityDoor::FriendlyNpcName(C->NpcId); // nette fallback i.p.v. ruwe "Resident_0121"
	// D13a — contact-unlock-status voor de respect-ring: onder de drempel toont die "X/45" + "to contact".
	bool bUnlocked = true; float UnlockAt = 45.f;
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
		{
			float r = 0.f, l = 0.f, a = 0.f; FText N;
			if (Reg->GetStats(C->NpcId, r, l, a, N) && !N.IsEmpty()) { NpcName = N.ToString(); }
			if (!C->NpcId.IsNone()) { bUnlocked = Reg->IsUnlocked(C->NpcId); UnlockAt = Reg->UnlockRespect; }
		}
	}
	if (NameText) { NameText->SetText(FText::FromString(NpcName)); }

	const bool bBuyer = (C->State == ECustomerState::WantsToOrder || C->State == ECustomerState::Negotiating);

	// Status-regel: bij kopers zegt de Wants-regel al genoeg -> verbergen. Anders 1 korte regel.
	if (StateText)
	{
		if (bBuyer)
		{
			StateText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			FString StateStr;
			switch (C->State)
			{
			case ECustomerState::Prospect: StateStr = TEXT("Not a customer yet"); break;
			case ECustomerState::Served:   StateStr = TEXT("Satisfied"); break;
			default:                       StateStr = TEXT("Leaving"); break;
			}
			StateText->SetText(FText::FromString(StateStr));
			StateText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	// C.4 — stats als 3 ring-gauges (respect / loyalty / addiction). Huisnummer-hint vervalt.
	// Ring-update via het dynamische materiaal, met per-ring delta-gate (spiegel van PlantInfoWidget::SetRing).
	{
		auto SetRing = [](UImage* Ring, float Frac, const FLinearColor& Col, float& LastFrac, FLinearColor& LastCol)
		{
			if (!Ring) { return; }
			const float F = FMath::Clamp(Frac, 0.f, 1.f);
			if (FMath::Abs(F - LastFrac) <= 0.001f && Col == LastCol) { return; }
			LastFrac = F; LastCol = Col;
			if (UMaterialInstanceDynamic* MID = Ring->GetDynamicMaterial())
			{
				MID->SetScalarParameterValue(TEXT("Percent"), F);
				MID->SetVectorParameterValue(TEXT("Color"), Col);
			}
		};
		const float RespFrac  = C->Respect / 100.f;
		const float LoyalFrac = C->Loyalty / 100.f;
		const float AddictFrac = FMath::Clamp(C->Addiction / FMath::Max(1.f, C->AddictionToBuy), 0.f, 1.f);
		SetRing(RespectRing,  RespFrac,   WeedUI::ColAccent(), LastRespFrac,   LastRespCol);
		SetRing(LoyaltyRing,  LoyalFrac,  WeedUI::ColGood(),   LastLoyalFrac,  LastLoyalCol);
		SetRing(AddictRing,   AddictFrac, WeedUI::ColWarn(),   LastAddictFrac, LastAddictCol);
		// D13a — respect: zolang het contact niet ontgrendeld is toont de ring de voortgang naar het
		// telefoonnummer ("X/45" + mini-label "to contact"); na unlock gewoon de kale waarde.
		if (RespectVal)
		{
			RespectVal->SetText(FText::FromString(bUnlocked
				? FString::Printf(TEXT("%.0f"), C->Respect)
				: FString::Printf(TEXT("%.0f/%.0f"), C->Respect, UnlockAt)));
		}
		if (RespectSub) { RespectSub->SetVisibility(bUnlocked ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
		if (LoyaltyVal) { LoyaltyVal->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), C->Loyalty))); }
		// D13b — Hooked: "X/drempel" zolang de NPC nog geen koper is; een koper (drempel gehaald of al
		// aan het bestellen) toont alleen de kale waarde — de voortgang is dan niet meer interessant.
		const bool bIsHooked = bBuyer || C->Addiction >= C->AddictionToBuy;
		if (AddictVal)
		{
			AddictVal->SetText(FText::FromString(bIsHooked
				? FString::Printf(TEXT("%.0f"), C->Addiction)
				: FString::Printf(TEXT("%.0f/%.0f"), C->Addiction, C->AddictionToBuy)));
		}
	}

	// Klant-tier: alleen de NAAM in de pill (voortgang/next vervalt).
	if (TierText)
	{
		FString TLbl = TEXT("Casual");
		if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
			{
				if (!C->NpcId.IsNone())
				{
					// Per-speler tier (competitive): elke speler ziet zijn EIGEN relatie-tier in de pill.
					const int32 Tier = Reg->GetCustomerTier(C->NpcId, USaveGameSubsystem::StablePlayerId(GetOwningPlayerPawn()));
					TLbl = UNpcRegistryComponent::TierName(Tier);
				}
			}
		}
		TierText->SetText(FText::FromString(TLbl));
	}

	// Dialoog: de server-regel (reactie op joint/deal), anders een begroeting per status.
	// Kopers krijgen GEEN canned greeting (de Wants-regel + prijs-flow zeggen het al) -> box alleen bij een echte regel.
	FString Line = C->SpeechLine;
	if (Line.IsEmpty() && !bBuyer)
	{
		switch (C->State)
		{
		case ECustomerState::Prospect: Line = TEXT("Yo... you holding? Hook me up with a taste."); break;
		case ECustomerState::Served: Line = TEXT("Appreciate it, man. I'm good for now."); break;
		default: Line = TEXT("..."); break;
		}
	}
	if (DialogueText) { DialogueText->SetText(FText::FromString(Line)); }
	if (DialogueBox) { DialogueBox->SetVisibility(Line.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
	if (OfferBtn) { OfferBtn->SetVisibility(bBuyer ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }

	if (!bBuyer)
	{
		// Geen koper: verberg de deal-sectie; alleen kop + dialoog + Give joint + Leave.
		auto Hide = [](UWidget* W) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } };
		Hide(WantsRow); Hide(DealMetricBox); Hide(SubText); Hide(PriceText); Hide(PriceTotalText); Hide(PriceRailBox); Hide(PriceSlider); Hide(AmountSlider); Hide(AmountText); Hide(StockText); SetTrayVisible(false);
		Hide(ChanceText); Hide(PreviewText); Hide(NoWeedText);
		Hide(RespectDelta); Hide(LoyaltyDelta); Hide(AddictDelta); // geen deal -> geen delta's
		SetCardWidth(420.f); // niet-koper: smalle kaart (geen geef-tray)
		return;
	}

	const int32 Qty = C->DesiredQuantity;
	const FName Offered = Ph->GetOfferedProduct();
	const bool bSub = Ph->IsOfferingSubstitute();
	const bool bBagOffer = Offered.ToString().StartsWith(TEXT("Bag_"));
	const int32 Market = FMath::Max(1, Ph->GetOfferMarketCents());
	const int32 Ask = Ph->GetDealAskCents();

	// Heb je überhaupt verpakte wiet (Bag_) om te verkopen? Zo niet: toon alleen een duidelijke
	// melding en verberg de hele prijs/kans/preview-flow.
	bool bHasWeed = false;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			if (bBagOffer)
			{
				float DummyThc = 0.f, DummyQ = 0.f;
				bHasWeed = Inv->BagStockGrams(UInventoryComponent::BagStrain(Offered), DummyThc, DummyQ) > 0;
			}
		}
	}
	const ESlateVisibility DealVis = bHasWeed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	const ESlateVisibility SliderVis = bHasWeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (NoWeedText) { NoWeedText->SetVisibility(bHasWeed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
	if (PriceText)     { PriceText->SetVisibility(DealVis); }
	if (PriceTotalText){ PriceTotalText->SetVisibility(DealVis); }
	if (DealMetricBox) { DealMetricBox->SetVisibility(DealVis); }
	if (PriceRailBox)  { PriceRailBox->SetVisibility(SliderVis); }
	if (PriceSlider)   { PriceSlider->SetVisibility(SliderVis); }
	if (AmountSlider) { AmountSlider->SetVisibility(SliderVis); }
	if (AmountText)   { AmountText->SetVisibility(DealVis); }
	SetTrayVisible(bHasWeed);
	if (ChanceText)   { ChanceText->SetVisibility(DealVis); }
	// PreviewText blijft altijd Collapsed (dood); de gauge-delta's tonen de +R/+L/+A nu.
	// StockText: default verborgen; alleen de WARN-tak (tekort) zet 'm zichtbaar.
	if (StockText)    { StockText->SetVisibility(ESlateVisibility::Collapsed); }
	if (WantsRow)     { WantsRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } // SelfHitTestInvisible (NIET HitTestInvisible!) zodat de SLIDER in deze rij wel klikbaar/sleepbaar blijft
	if (!bHasWeed)
	{
		// Alleen "Wants" + de melding tonen; de rest is verborgen. Klaar.
		WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants %dg "), Qty)));
		// Alleen de STRAIN-naam in de strain-tagkleur (niet de hele regel).
		WantsStrainText->SetText(FText::FromString(PrettyDealName(C->DesiredProductId)));
		WantsStrainText->SetColorAndOpacity(FSlateColor(WeedUI::TagColorForItem(C->DesiredProductId, 0.85f, 0.75f)));
		if (NoWeedText)
		{
			NoWeedText->SetText(FText::FromString(TEXT("No bagged weed.\nGrow -> dry -> bag first.")));
		}
		if (RespectDelta) { RespectDelta->SetVisibility(ESlateVisibility::Collapsed); }
		if (LoyaltyDelta) { LoyaltyDelta->SetVisibility(ESlateVisibility::Collapsed); }
		if (AddictDelta)  { AddictDelta->SetVisibility(ESlateVisibility::Collapsed); }
		SetCardWidth(420.f); // geen wiet: smalle kaart (alleen de melding)
		return;
	}

	WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants %dg "), Qty)));
	// Alleen de STRAIN-naam in de strain-tagkleur (niet de hele regel).
	WantsStrainText->SetText(FText::FromString(PrettyDealName(C->DesiredProductId)));
	WantsStrainText->SetColorAndOpacity(FSlateColor(WeedUI::TagColorForItem(C->DesiredProductId, 0.85f, 0.75f)));

	const float Pct = float(Ask) / Market * 100.f;
	// Compacte BID: bod/marktpercentage is secundair grijs; alleen TOTAL rechts blijft geld-groen.
	PriceText->SetText(FText::FromString(FString::Printf(TEXT("EUR %d/g  %.0f%%"),
		(int32)(WeedRoundEuros((int64)Ask) / 100), Pct)));
	if (PriceTotalText)
	{
		PriceTotalText->SetText(FText::FromString(FString::Printf(TEXT("EUR %d"), (int32)(WeedRoundEuros((int64)Ask * Qty) / 100))));
	}
	// Slider volgt het bod als de speler 'm niet vasthoudt.
	if (PriceSlider && !bSliderHeld)
	{
		PriceSlider->SetValue(FMath::Clamp((float(Ask) / Market - 0.40f) / 1.60f, 0.f, 1.f));
	}

	// Voorraad + kwaliteit van het aangeboden product.
	// Voorraad in GRAMMEN (zakjes van die strain), gewogen THC/kwaliteit. Zo klopt het met wat de klant in
	// grammen vraagt en met de echte deal-afwikkeling (RemoveBagsForGrams) - geen "not enough" meer terwijl je het wel hebt.
	float Q01 = -1.f, Thc = 0.f, QPct = 0.f; int32 Stock = 0;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			if (bBagOffer)
			{
				Stock = Inv->BagStockGrams(UInventoryComponent::BagStrain(Offered), Thc, QPct);
			}
			else // hasj/edibles/concentraten: losse gram-voorraad van precies dit product
			{
				Stock = Inv->GetQuantity(Offered);
				for (const FInventoryStack& St : Inv->GetStacks()) { if (St.ItemId == Offered) { Thc = St.Quality; QPct = St.QualityPct; break; } }
			}
			if (Stock > 0) { Q01 = FMath::Clamp(QPct / 100.f, 0.f, 1.f); }
		}
	}
	// StockText: alleen tonen bij tekort (ingekort, ColWarn). Genoeg voorraad -> verborgen (blijft Collapsed).
	if (Stock < Qty)
	{
		StockText->SetColorAndOpacity(FSlateColor(WeedUI::ColWarn()));
		StockText->SetText(FText::FromString(FString::Printf(TEXT("Not enough %s: %dg / %dg"), *PrettyDealName(Offered), Stock, Qty)));
		StockText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// Hoeveel je GEEFT. Bag-offers -> tastbare geef-interactie (de pile drijft DealGiveGrams). Losse gram-items
	// (concentraten/edibles) -> auto = gevraagd, geklemd op voorraad (geen grid, geen discrete zakjes).
	int32 GiveG;
	if (bBagOffer)
	{
		SetTrayVisible(true);
		if (AmountText) { AmountText->SetVisibility(ESlateVisibility::Collapsed); }
		SetCardWidth(740.f); // bag-deal: brede strook zodat bags LINKS + geef-vak RECHTS om de gauges passen
		SyncPileToOffered(); // reset de pile bij strain-wissel (zet DealGiveGrams via RefreshGiveZone)
		RebuildSellGrid();   // sig-gated: alleen bij een bag-voorraad-/pile-wijziging
		GiveG = FMath::Max(1, PileTotalGrams());
	}
	else
	{
		SetTrayVisible(false);
		if (AmountText) { AmountText->SetVisibility(ESlateVisibility::HitTestInvisible); }
		SetCardWidth(420.f); // losse gram-deal (concentraat/edible): smal, geen geef-kolommen
		PileStrain = NAME_None; // reset zodat een volgende bag-offer opnieuw synct
		GiveG = FMath::Clamp(Qty, 1, FMath::Max(1, Stock));
		Ph->SetDealGiveGrams(GiveG);
		if (AmountText)
		{
			const FString Note = (GiveG > Qty) ? FString::Printf(TEXT("  (+%dg extra)"), GiveG - Qty)
				: (GiveG < Qty ? FString::Printf(TEXT("  (%dg short)"), Qty - GiveG) : FString(TEXT("  (exactly asked)")));
			AmountText->SetText(FText::FromString(FString::Printf(TEXT("Give  %dg%s"), GiveG, *Note)));
			AmountText->SetColorAndOpacity(FSlateColor(GiveG >= Qty ? WeedUI::ColText() : FLinearColor(1.f, 0.8f, 0.2f)));
		}
	}

	const float OffThc = (Stock > 0) ? Thc : -1.f;
	float Chance = bSub ? C->GetSubstituteAcceptance(Offered, Ask, Q01, OffThc) : C->GetAcceptanceChance(Ask, Q01, OffThc);
	Chance = FMath::Clamp(Chance + ACustomerBase::QuantityAcceptMod(GiveG, Qty), 0.f, 100.f);
	const FLinearColor CCol = Chance >= 66.f ? WeedUI::ColGood() : (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : WeedUI::ColWarn());
	ChanceText->SetColorAndOpacity(FSlateColor(CCol));
	ChanceText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%%s"), Chance, bSub ? TEXT(" sub") : TEXT(""))));

	// LIVE gauge-delta's: hoeveel deze deal (bij deze prijs/kwaliteit/GiveG) elke stat oplevert. Uit dezelfde
	// preview-berekening die vroeger PreviewText vulde. 0 of geen deal -> delta verborgen.
	float pR = 0.f, pL = 0.f, pA = 0.f;
	C->PreviewDealOutcome(Ask, Q01, (Stock > 0 ? Thc : -1.f), pR, pL, pA, bSub, GiveG);
	auto SetDelta = [](UTextBlock* T, float D)
	{
		if (!T) { return; }
		const int32 N = FMath::RoundToInt(D);
		if (N == 0) { T->SetVisibility(ESlateVisibility::Collapsed); return; }
		// Winst = groen "+N"; een (zeldzaam) verlies = warn-kleur "-N".
		T->SetColorAndOpacity(FSlateColor(N > 0 ? WeedUI::ColGood() : WeedUI::ColWarn()));
		T->SetText(FText::FromString(FString::Printf(TEXT("%+d"), N)));
		T->SetVisibility(ESlateVisibility::HitTestInvisible);
	};
	SetDelta(RespectDelta, pR - C->Respect);
	SetDelta(LoyaltyDelta, pL - C->Loyalty);
	SetDelta(AddictDelta,  pA - C->Addiction);
}

void UDealWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	bool bOpen = Ph && Ph->IsDealOpen() && C != nullptr;
	// Zolang dit HUD open staat: de klant pauzeert z'n wandeling (en loopt daarna weer door).
	if (bOpen && GetWorld())
	{
		C->ConversationHoldUntil = GetWorld()->GetRealTimeSeconds() + 0.6f;
		// DIRECT stilzetten (de patrouille-tik komt pas tot een seconde later): beweging op nul
		// zodat de walk-animatie meteen naar idle klapt zodra je het gesprek opent.
		if (AAIController* AI = Cast<AAIController>(C->GetController())) { AI->StopMovement(); }
		if (UCharacterMovementComponent* Mv = C->GetCharacterMovement()) { Mv->StopMovementImmediately(); }
		C->ForceIdleAnimNow(); // voorbij de walk-naijler: animatie klapt dit frame naar idle
	}

	// Loop je weg -> sluit (alleen dichtbij dealen).
	if (bOpen)
	{
		if (APawn* P = GetOwningPlayerPawn())
		{
			if (FVector::DistSquared(P->GetActorLocation(), C->GetActorLocation()) > FMath::Square(300.f))
			{
				Ph->CloseDeal();
				bOpen = false;
			}
		}
	}

	// De widget zelf blijft altijd ticken.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (!bOpen)
	{
		if (Card) { Card->SetVisibility(ESlateVisibility::Collapsed); }
		if (Backdrop) { Backdrop->SetVisibility(ESlateVisibility::Collapsed); } // geen deal -> klik-vanger UIT (anders blokkeert 'ie alle UI)
		LastCustomer = nullptr; LastLiveKey.Reset(); return;
	}

	// EERST de inhoud (en dus de hoogte) vullen, DAARNA pas de kaart tonen -> geen 1-frame
	// "flits van midden naar onder": de AutoSize/onderrand-slot zou anders op een lege/oude
	// hoogte opmeten en pas de volgende frame omlaag settelen.
	const FName Offered = Ph->GetOfferedProduct();
	const bool bNewCustomer = (C != LastCustomer.Get());
	if (bNewCustomer)
	{
		// Nieuwe klant: slider mag het nieuwe bod volgen; joint-kiezer dicht. De pile-strain resetten zodat
		// SyncPileToOffered de geef-zone leeg begint voor deze klant.
		LastCustomer = C; bSliderHeld = false; bAmountHeld = false;
		PileStrain = NAME_None; PileCounts.Reset();
		SellGridSig.Reset(); // sell-grid mag herbouwen voor de nieuwe klant/voorraad
		if (JointPickerBox) { JointPickerBox->SetVisibility(ESlateVisibility::Collapsed); } // kiezer dicht bij nieuwe klant
	}
	if (Offered != LastOffered) { bSliderHeld = false; bAmountHeld = false; } // ander product gekozen -> sliders mogen weer volgen
	LastOffered = Offered;

	// De aangeboden strain volgt uit de bag die je in de geef-zone legt; er is geen aparte strain-picker meer.
	// De sell-grid + geef-grid worden in UpdateLive (bag-offer-tak) gevuld/ge-sig-gate.
	UpdateLive();

	// C.6a/C.6c — verberg "Give joint" tijdens de sample-cooldown (server weigert dan stil; knop zou nutteloos zijn).
	// Buiten de UpdateLive-key-gate: die returned vroeg bij een gelijke key, waardoor de knop niet zou terugkomen
	// zodra de cooldown afloopt. Collapsed (niet Hidden) zodat de knoppenrij herschikt.
	if (GiveBtn && C)
	{
		UNpcRegistryComponent* Reg = nullptr;
		if (UWorld* W = GetWorld())
		{
			if (AWeedShopGameState* GS = W->GetGameState<AWeedShopGameState>()) { Reg = GS->GetNpcRegistry(); }
		}
		// Per-speler sample-cooldown (competitive): MIJN give-knop hangt aan MIJN cooldown, niet die van de rivaal.
		const bool bCd = (Reg && !C->NpcId.IsNone() && Reg->IsOnSampleCooldown(C->NpcId, USaveGameSubsystem::StablePlayerId(GetOwningPlayerPawn())));
		GiveBtn->SetVisibility(bCd ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (Card) { Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } // nu pas zichtbaar, al op echte hoogte
	if (Backdrop) { Backdrop->SetVisibility(ESlateVisibility::Visible); } // deal open -> klik-vanger AAN (wegklikbaar naast de kaart)

	// Reset de "slider held"-vlag als de muisknop los is (zodat 'ie het bod weer kan volgen).
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton)) { bSliderHeld = false; bAmountHeld = false; }
	}
}

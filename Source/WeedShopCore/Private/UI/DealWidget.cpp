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
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DealCard"));
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.98f), 26.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(20.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	// Niet meer pal in het midden: onderaan (boven de hotbar) zodat de NPC in beeld vrij blijft.
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(0.5f, 1.f)); // onderrand = ankerpunt
	// AutoSize: de kaart krimpt naar zijn inhoud -> geen groot leeg grijs vlak bij niet-kopers.
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -120.f));

	// Vaste breedte (zodat de Fill-knoppen netjes uitlijnen), hoogte volgt de inhoud.
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetWidthOverride(440.f);
	CardB->SetContent(Width);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Width->SetContent(VB);

	// --- Kop-rij: naam links + tier-PILL rechts (altijd zichtbaar voor ELKE NPC) ---
	{
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
		NameText = WeedUI::Text(WidgetTree, TEXT("Customer"), 20, WeedUI::ColAccent(), false, true);
		UHorizontalBoxSlot* NS = Head->AddChildToHorizontalBox(NameText);
		NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NS->SetVerticalAlignment(VAlign_Center);

		// Tier-pill (accent-vlak, alleen de tier-NAAM). TierText wordt hergebruikt als pill-tekst.
		TierPill = WidgetTree->ConstructWidget<UBorder>();
		TierPill->SetBrush(WeedUI::Rounded(WeedUI::ColAccentDim(), 8.f));
		TierPill->SetPadding(FMargin(8.f, 3.f));
		TierText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColAccent(), false, true);
		TierPill->SetContent(TierText);
		UHorizontalBoxSlot* PS = Head->AddChildToHorizontalBox(TierPill);
		PS->SetHorizontalAlignment(HAlign_Right);
		PS->SetVerticalAlignment(VAlign_Center);
		VB->AddChildToVerticalBox(Head);
	}

	// Dunne accent-balk onder de kop-rij (fungeert als tier-voortgangsindicator via de breedte niet, puur scheiding-accent).
	TierBar = WidgetTree->ConstructWidget<USizeBox>();
	TierBar->SetHeightOverride(3.f);
	{
		UBorder* Fill = WidgetTree->ConstructWidget<UBorder>();
		Fill->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(0.8f), 2.f));
		TierBar->SetContent(Fill);
	}
	VB->AddChildToVerticalBox(TierBar)->SetPadding(FMargin(0.f, 4.f, 0.f, 6.f));

	StateText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColHighlight(), false, true);
	VB->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	// RelationText vervalt visueel (de 3 ringen tonen R/L/A nu). Member blijft bestaan (Collapsed) -> kleinste diff.
	RelationText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, true);
	VB->AddChildToVerticalBox(RelationText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	RelationText->SetVisibility(ESlateVisibility::Collapsed);

	// --- C.4: 3 ring-gauges (respect / loyalty / addiction), spiegel van PlantInfoWidget's MakeGauge-mechanisme.
	// Radiaal-materiaal 1x laden; elke gauge = SizeBox 88x88 -> Overlay{ ring-image (Fill) + icoon (Center) } + waarde + label.
	RadialMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/UI/M_RadialProgress.M_RadialProgress"));
	{
		auto MakeGauge = [this](const FString& IcoStem, const FLinearColor& IcoTint, const FString& Label,
			UImage*& OutRing, UTextBlock*& OutVal, const FString& SubLabel = FString(), UTextBlock** OutSub = nullptr) -> UWidget*
		{
			UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
			USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(88.f); Sz->SetHeightOverride(88.f);
			UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>(); Sz->SetContent(Ov);
			OutRing = WidgetTree->ConstructWidget<UImage>();
			if (RadialMat) { OutRing->SetBrushFromMaterial(RadialMat); }
			OutRing->SetBrushSize(FVector2D(88.f, 88.f));
			UOverlaySlot* ROS = Ov->AddChildToOverlay(OutRing); ROS->SetHorizontalAlignment(HAlign_Fill); ROS->SetVerticalAlignment(VAlign_Fill);
			USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(40.f); IcoSz->SetHeightOverride(40.f);
			IcoSz->SetContent(WeedUI::KitIcon(WidgetTree, IcoStem, 40.f, IcoTint));
			UOverlaySlot* IS = Ov->AddChildToOverlay(IcoSz); IS->SetHorizontalAlignment(HAlign_Center); IS->SetVerticalAlignment(VAlign_Center);
			UVerticalBoxSlot* SzS = Box->AddChildToVerticalBox(Sz); SzS->SetHorizontalAlignment(HAlign_Center);
			OutVal = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::ColText(), true, true);
			UVerticalBoxSlot* VS = Box->AddChildToVerticalBox(OutVal); VS->SetHorizontalAlignment(HAlign_Center); VS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
			// D13a — optioneel mini-label direct onder de waarde (bv. "to contact"): 1x gebouwd,
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
		UImage* Rr = nullptr; UTextBlock* Rt = nullptr; UTextBlock* Rs = nullptr;
		// D13a — respect-gauge krijgt het "to contact"-mini-label: zo leest de speler de voortgang naar
		// het telefoonnummer (waarde toont dan "X/45" in UpdateLive).
		StatGaugeRow->AddChildToHorizontalBox(MakeGauge(TEXT("t_medal_128"), FLinearColor::White, TEXT("Respect"), Rr, Rt, TEXT("to contact"), &Rs))->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
		RespectRing = Rr; RespectVal = Rt; RespectSub = Rs;
		UImage* Lr = nullptr; UTextBlock* Lt = nullptr;
		StatGaugeRow->AddChildToHorizontalBox(MakeGauge(TEXT("t_heart_red_128"), FLinearColor::White, TEXT("Loyalty"), Lr, Lt))->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
		LoyaltyRing = Lr; LoyaltyVal = Lt;
		UImage* Ar = nullptr; UTextBlock* At = nullptr;
		// D13b — label "Hooked": dit is de haak die van een prospect een koper maakt.
		StatGaugeRow->AddChildToHorizontalBox(MakeGauge(TEXT("t_flame_128"), FLinearColor::White, TEXT("Hooked"), Ar, At));
		AddictRing = Ar; AddictVal = At;
		UVerticalBoxSlot* SGS = VB->AddChildToVerticalBox(StatGaugeRow);
		SGS->SetHorizontalAlignment(HAlign_Center); SGS->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));
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
		VB->AddChildToVerticalBox(DB)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	// Held: waar de klant om vraagt. "Wants Xg " in normale kleur; ALLEEN de strain-naam in de strain-
	// tagkleur (op verzoek: niet de hele regel kleuren). Twee blokken in een rij (1 blok kan maar 1 kleur).
	WantsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	WantsText = WeedUI::Text(WidgetTree, TEXT(""), 17, WeedUI::ColText(), false, true);
	WantsStrainText = WeedUI::Text(WidgetTree, TEXT(""), 17, WeedUI::ColText(), false, true);
	WantsRow->AddChildToHorizontalBox(WantsText);
	WantsRow->AddChildToHorizontalBox(WantsStrainText);
	VB->AddChildToVerticalBox(WantsRow)->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));

	// SubText vervalt als los element (substituut-info zit nu in de ChanceText-suffix). Member blijft,
	// maar permanent verborgen + leeg -> kleinste diff, geen ruis in de layout.
	SubText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(SubText);
	SubText->SetVisibility(ESlateVisibility::Collapsed);

	PriceText = WeedUI::Text(WidgetTree, TEXT(""), 15, WeedUI::ColText(), false, true);
	VB->AddChildToVerticalBox(PriceText)->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));

	PriceSlider = WidgetTree->ConstructWidget<USlider>();
	PriceSlider->SetSliderHandleColor(WeedUI::ColAccent());
	PriceSlider->SetSliderBarColor(WeedUI::ColSlot());
	PriceSlider->OnValueChanged.AddDynamic(this, &UDealWidget::OnPriceSlider);
	VB->AddChildToVerticalBox(PriceSlider)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Hoeveelheid-slider: kies zelf hoeveel gram je geeft (meer -> hogere accept-kans + stats; minder -> lager).
	AmountText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColText(), false, true);
	VB->AddChildToVerticalBox(AmountText)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
	AmountSlider = WidgetTree->ConstructWidget<USlider>();
	AmountSlider->SetSliderHandleColor(WeedUI::ColAccent());
	AmountSlider->SetSliderBarColor(WeedUI::ColSlot());
	AmountSlider->OnValueChanged.AddDynamic(this, &UDealWidget::OnAmountSlider);
	VB->AddChildToVerticalBox(AmountSlider)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	StockText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(StockText)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));

	ChanceText = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColGood(), false, true);
	VB->AddChildToVerticalBox(ChanceText);
	ChanceBar = WidgetTree->ConstructWidget<UProgressBar>();
	ChanceBar->SetFillColorAndOpacity(WeedUI::ColGood());
	VB->AddChildToVerticalBox(ChanceBar)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	PreviewText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(PreviewText)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Grote, duidelijke melding als je niets te verkopen hebt (verbergt de hele prijs-flow).
	NoWeedText = WeedUI::Text(WidgetTree, TEXT("No bagged weed.\nGrow -> dry -> bag first."), 14, WeedUI::ColWarn(), false, true);
	VB->AddChildToVerticalBox(NoWeedText)->SetPadding(FMargin(0.f, 14.f, 0.f, 14.f));

	// Strain-keuze-grid (B.11): welke strain bied je aan. Selectie-highlight aan; hoogte-cap 2 rijen (rest scrollt).
	OfferLabel = WeedUI::Text(WidgetTree, TEXT("Selling:"), 11, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(OfferLabel);
	StrainBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(StrainBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));
	{
		StrainGrid = WidgetTree->ConstructWidget<UWeedItemPickGrid>();
		StrainGrid->CellSize = 78.f;
		StrainGrid->MaxVisibleRows = 2;
		StrainGrid->bShowSelection = true;
		StrainGrid->OnPick = [this](FName Id, int32 /*P*/)
		{
			if (UPhoneClientComponent* Ph = GetPhone()) { Ph->SetOfferedProduct(Id); }
		};
		StrainBox->AddChildToVerticalBox(StrainGrid);
	}

	// Joint-kiezer (verborgen tot je "Give joint" klikt): kies WELKE joint je geeft. Geen selectie (elke klik = geven).
	JointPickerBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(JointPickerBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));
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
	VB->AddChildToVerticalBox(Btns);
}

void UDealWidget::OnPriceSlider(float Value)
{
	bSliderHeld = true;
	UPhoneClientComponent* Ph = GetPhone();
	if (!Ph) { return; }
	const int32 Market = Ph->GetOfferMarketCents();
	if (Market <= 0) { return; }
	const int32 Ask = FMath::RoundToInt(Market * (0.40f + 1.60f * Value));
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
	Ph->SetDealGiveGrams(FMath::Clamp(FMath::RoundToInt(Value * Stock), 1, Stock));
}

FString UDealWidget::ComputeStrainListSig() const
{
	// Signatuur van de aangeboden strain-LIJST: de set Bag_<strain>-ids + hun grammen/thc + wat de klant wil.
	// Wijzigt die -> RebuildStrains (cel-diff). Wijzigt alleen het gekozen product -> alleen restyle.
	UPhoneClientComponent* Ph = const_cast<UDealWidget*>(this)->GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Ph || !C || !Inv) { return FString(); }

	TArray<FName> Buds;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (!UInventoryComponent::IsBag(St.ItemId)) { continue; }
		const FName Base = FName(*FString::Printf(TEXT("Bag_%s"), *UInventoryComponent::BagStrain(St.ItemId).ToString()));
		if (!Buds.Contains(Base)) { Buds.Add(Base); }
	}
	FString Sig;
	for (const FName& Bud : Buds)
	{
		const FName StrainNm = UInventoryComponent::BagStrain(Bud);
		const int32 Avail = Inv->BagGramsAvailable(StrainNm);
		float QThc = 0.f;
		for (const FInventoryStack& St2 : Inv->GetStacks()) { if (UInventoryComponent::IsBag(St2.ItemId) && UInventoryComponent::BagStrain(St2.ItemId) == StrainNm) { QThc = St2.Quality; break; } }
		Sig += FString::Printf(TEXT("%s:%d:%.0f:%d|"), *Bud.ToString(), Avail, QThc, (Bud == C->DesiredProductId) ? 1 : 0);
	}
	return Sig;
}

void UDealWidget::RebuildStrains()
{
	if (!StrainBox || !StrainGrid) { return; }
	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Ph || !C || !Inv) { return; }

	// Klanten kopen verpakte wiet; bied PER STRAIN aan (basis-id Bag_<strain>), ongeacht de zakje-maten.
	TArray<FName> Buds;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (!UInventoryComponent::IsBag(St.ItemId)) { continue; }
		const FName Base = FName(*FString::Printf(TEXT("Bag_%s"), *UInventoryComponent::BagStrain(St.ItemId).ToString()));
		if (!Buds.Contains(Base)) { Buds.Add(Base); }
	}
	const FName Offered = Ph->GetOfferedProduct();

	// Lege-melding tonen/verbergen zonder ClearChildren (persistent tekstblok, 1x gebouwd).
	if (Buds.Num() == 0)
	{
		if (!StrainEmptyText)
		{
			StrainEmptyText = WeedUI::Text(WidgetTree, TEXT("(no weed in your inventory)"), 12, WeedUI::ColTextDim());
			StrainBox->AddChildToVerticalBox(StrainEmptyText);
		}
		StrainEmptyText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (StrainEmptyText)
	{
		StrainEmptyText->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Grid-items opbouwen: per strain één cel. Icoon = de Bag_<strain>-basis-id, badge = grammen op voorraad,
	// subline = THC%. De gewenste strain krijgt een "* "-prefix in groen zodat 'ie meteen opvalt.
	TArray<FWeedPickItem> Items;
	Items.Reserve(Buds.Num());
	for (const FName& Bud : Buds)
	{
		const FName StrainNm = UInventoryComponent::BagStrain(Bud);
		const int32 Avail = Inv->BagGramsAvailable(StrainNm);
		float QThc = 0.f;
		for (const FInventoryStack& St2 : Inv->GetStacks()) { if (UInventoryComponent::IsBag(St2.ItemId) && UInventoryComponent::BagStrain(St2.ItemId) == StrainNm) { QThc = St2.Quality; break; } }
		const bool bWanted = (Bud == C->DesiredProductId);

		FWeedPickItem It;
		It.Id = Bud;
		It.IconId = Bud;
		It.Badge = FString::Printf(TEXT("%dg"), Avail);
		It.SubLine = bWanted ? FString::Printf(TEXT("* T%.0f%%"), QThc) : FString::Printf(TEXT("T%.0f%%"), QThc);
		if (bWanted) { It.SubCol = WeedUI::ColGood(); }
		It.Tooltip = FString::Printf(TEXT("%s  %dg  THC %.0f%%%s"), *PrettyDealName(Bud), Avail, QThc, bWanted ? TEXT("  (what they want)") : TEXT(""));
		Items.Add(It);
	}

	// Persistent: het grid diff't intern (geen ClearChildren). Selectie = het aangeboden product.
	StrainGrid->SetItems(Items, Offered);
	StrainSelectedId = Offered;
}

void UDealWidget::RefreshStrainSelection()
{
	if (!StrainGrid) { return; }
	UPhoneClientComponent* Ph = GetPhone();
	const FName Offered = Ph ? Ph->GetOfferedProduct() : NAME_None;
	if (Offered == StrainSelectedId) { return; }
	// Pure selectie-wissel: alleen de oude + nieuwe cel herstylen (het grid regelt dit intern) -> geen rebuild.
	StrainGrid->SetSelected(Offered);
	StrainSelectedId = Offered;
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

void UDealWidget::UpdateLive()
{
	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	if (!Ph || !C) { return; }

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
		const FString Key = FString::Printf(TEXT("%llu|%d|%.2f|%.2f|%.2f|%.2f|%s|%d|%s|%s|%d|%d|%d|%d|%d|%.2f|%.2f|%d|%d|%d"),
			(unsigned long long)(UPTRINT)C, (int32)C->State, C->Respect, C->Loyalty, C->Addiction, C->AddictionToBuy,
			*C->SpeechLine, C->DesiredQuantity, *C->DesiredProductId.ToString(),
			*Ph->GetOfferedProduct().ToString(), Ph->IsOfferingSubstitute() ? 1 : 0,
			Ph->GetOfferMarketCents(), Ph->GetDealAskCents(),
			bKHasWeed ? 1 : 0, KStock, KThc, KQPct, bSliderHeld ? 1 : 0, KUnlocked, KTier);
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
		Hide(WantsRow); Hide(SubText); Hide(PriceText); Hide(PriceSlider); Hide(AmountSlider); Hide(AmountText); Hide(StockText);
		Hide(ChanceText); Hide(ChanceBar); Hide(PreviewText); Hide(NoWeedText); Hide(OfferLabel); Hide(StrainBox);
		return;
	}

	const int32 Qty = C->DesiredQuantity;
	const FName Offered = Ph->GetOfferedProduct();
	const bool bSub = Ph->IsOfferingSubstitute();
	const int32 Market = FMath::Max(1, Ph->GetOfferMarketCents());
	const int32 Ask = Ph->GetDealAskCents();

	// Heb je überhaupt verpakte wiet (Bag_) om te verkopen? Zo niet: toon alleen een duidelijke
	// melding en verberg de hele prijs/kans/preview-flow.
	bool bHasWeed = false;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			for (const FInventoryStack& St : Inv->GetStacks())
			{
				if (St.ItemId.ToString().StartsWith(TEXT("Bag_")) && St.Quantity > 0) { bHasWeed = true; break; }
			}
		}
	}
	const ESlateVisibility DealVis = bHasWeed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	const ESlateVisibility SliderVis = bHasWeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (NoWeedText) { NoWeedText->SetVisibility(bHasWeed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
	if (PriceText)    { PriceText->SetVisibility(DealVis); }
	if (PriceSlider)  { PriceSlider->SetVisibility(SliderVis); }
	if (AmountSlider) { AmountSlider->SetVisibility(SliderVis); }
	if (AmountText)   { AmountText->SetVisibility(DealVis); }
	if (ChanceText)   { ChanceText->SetVisibility(DealVis); }
	if (ChanceBar)    { ChanceBar->SetVisibility(DealVis); }
	if (PreviewText)  { PreviewText->SetVisibility(DealVis); }
	if (OfferLabel)   { OfferLabel->SetVisibility(DealVis); }
	if (StrainBox)    { StrainBox->SetVisibility(bHasWeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	// StockText: default verborgen; alleen de WARN-tak (tekort) zet 'm zichtbaar.
	if (StockText)    { StockText->SetVisibility(ESlateVisibility::Collapsed); }
	if (WantsRow)     { WantsRow->SetVisibility(ESlateVisibility::HitTestInvisible); } // koper: altijd tonen (kan Collapsed staan door een vorige niet-koper)
	if (!bHasWeed)
	{
		// Alleen "Wants" + de melding tonen; de rest is verborgen. Klaar.
		WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants %dg "), Qty)));
		// Alleen de STRAIN-naam in de strain-tagkleur (niet de hele regel).
		WantsStrainText->SetText(FText::FromString(PrettyDealName(C->DesiredProductId)));
		WantsStrainText->SetColorAndOpacity(FSlateColor(WeedUI::TagColorForItem(C->DesiredProductId, 0.85f, 0.75f)));
		return;
	}

	WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants %dg "), Qty)));
	// Alleen de STRAIN-naam in de strain-tagkleur (niet de hele regel).
	WantsStrainText->SetText(FText::FromString(PrettyDealName(C->DesiredProductId)));
	WantsStrainText->SetColorAndOpacity(FSlateColor(WeedUI::TagColorForItem(C->DesiredProductId, 0.85f, 0.75f)));

	const float Pct = float(Ask) / Market * 100.f;
	PriceText->SetText(FText::FromString(FString::Printf(TEXT("Your price  EUR %d/g   -   %.0f%%   -   total EUR %d"),
		(int32)(WeedRoundEuros((int64)Ask) / 100), Pct, (int32)(WeedRoundEuros((int64)Ask * Qty) / 100))));
	// Slider volgt het bod als de speler 'm niet vasthoudt.
	if (PriceSlider && !bSliderHeld)
	{
		PriceSlider->SetValue(FMath::Clamp((float(Ask) / Market - 0.40f) / 1.60f, 0.f, 1.f));
	}

	// Voorraad + kwaliteit van het aangeboden product.
	// Voorraad in GRAMMEN (zakjes van die strain), gewogen THC/kwaliteit. Zo klopt het met wat de klant in
	// grammen vraagt en met de echte deal-afwikkeling (RemoveBagsForGrams) - geen "not enough" meer terwijl je het wel hebt.
	float Q01 = -1.f, Thc = 0.f, QPct = 0.f; int32 Stock = 0;
	const bool bBagOffer = Offered.ToString().StartsWith(TEXT("Bag_"));
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

	// Hoeveel je GEEFT (amount-slider); default = gevraagd, geklemd op je voorraad. Accept-kans + preview schalen mee.
	int32 GiveG = Ph->GetDealGiveGrams(); if (GiveG <= 0) { GiveG = Qty; }
	if (Stock > 0) { GiveG = FMath::Clamp(GiveG, 1, Stock); }
	if (AmountSlider && !bAmountHeld && Stock > 0) { AmountSlider->SetValue(FMath::Clamp((float)GiveG / (float)Stock, 0.f, 1.f)); }
	if (AmountText)
	{
		const FString Note = (GiveG > Qty) ? FString::Printf(TEXT("  (+%dg extra)"), GiveG - Qty)
			: (GiveG < Qty ? FString::Printf(TEXT("  (%dg short)"), Qty - GiveG) : FString(TEXT("  (exactly asked)")));
		AmountText->SetText(FText::FromString(FString::Printf(TEXT("Give  %dg%s"), GiveG, *Note)));
		AmountText->SetColorAndOpacity(FSlateColor(GiveG >= Qty ? WeedUI::ColText() : FLinearColor(1.f, 0.8f, 0.2f)));
	}

	const float OffThc = (Stock > 0) ? Thc : -1.f;
	float Chance = bSub ? C->GetSubstituteAcceptance(Offered, Ask, Q01, OffThc) : C->GetAcceptanceChance(Ask, Q01, OffThc);
	Chance = FMath::Clamp(Chance + ACustomerBase::QuantityAcceptMod(GiveG, Qty), 0.f, 100.f);
	const FLinearColor CCol = Chance >= 66.f ? WeedUI::ColGood() : (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : WeedUI::ColWarn());
	ChanceText->SetColorAndOpacity(FSlateColor(CCol));
	ChanceText->SetText(FText::FromString(FString::Printf(TEXT("Deal chance  %.0f%%%s"), Chance, bSub ? TEXT("  (sub - harder sell)") : TEXT(""))));
	ChanceBar->SetPercent(Chance / 100.f);
	ChanceBar->SetFillColorAndOpacity(CCol);

	float pR = 0.f, pL = 0.f, pA = 0.f;
	C->PreviewDealOutcome(Ask, Q01, (Stock > 0 ? Thc : -1.f), pR, pL, pA, bSub, GiveG);
	PreviewText->SetText(FText::FromString(FString::Printf(TEXT("If accepted:  R %+.0f   L %+.0f   A %+.0f"),
		pR - C->Respect, pL - C->Loyalty, pA - C->Addiction)));
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
		LastCustomer = nullptr; LastLiveKey.Reset(); return;
	}

	// EERST de inhoud (en dus de hoogte) vullen, DAARNA pas de kaart tonen -> geen 1-frame
	// "flits van midden naar onder": de AutoSize/onderrand-slot zou anders op een lege/oude
	// hoogte opmeten en pas de volgende frame omlaag settelen.
	const FName Offered = Ph->GetOfferedProduct();
	const bool bNewCustomer = (C != LastCustomer.Get());
	const FString ListSig = ComputeStrainListSig();
	if (bNewCustomer)
	{
		// Nieuwe klant: slider mag het nieuwe bod volgen; joint-kiezer dicht. StrainSelectedId NIET resetten,
		// zodat RefreshStrainSelection ook de vorige highlight netjes wist (oud + nieuw = 2 knoppen restyle).
		LastCustomer = C; bSliderHeld = false; bAmountHeld = false;
		if (JointPickerBox) { JointPickerBox->SetVisibility(ESlateVisibility::Collapsed); } // kiezer dicht bij nieuwe klant
	}
	if (Offered != LastOffered) { bSliderHeld = false; bAmountHeld = false; } // ander product gekozen -> sliders mogen weer volgen
	LastOffered = Offered;

	// Alleen de cel-pool (her)vullen als de klant OF de strain-lijst zelf wijzigt (strain toegevoegd/uitverkocht).
	// Een pure selectie-wissel (lijst identiek, alleen Offered anders) raakt de pool niet -> geen ClearChildren, geen flash.
	if (bNewCustomer || ListSig != StrainListSig)
	{
		StrainListSig = ListSig;
		RebuildStrains(); // diff't per cel + zet de selectie-highlight (RefreshStrainSelection aan het eind)
	}
	else
	{
		RefreshStrainSelection(); // enkel de 2 betrokken knoppen herstylen
	}
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

	// Reset de "slider held"-vlag als de muisknop los is (zodat 'ie het bod weer kan volgen).
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton)) { bSliderHeld = false; bAmountHeld = false; }
	}
}

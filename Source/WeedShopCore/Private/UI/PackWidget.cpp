#include "UI/PackWidget.h"

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
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

void UPackWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	FButtonStyle PackStyle(const FLinearColor& Col)
	{
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 8.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		S.NormalPadding = FMargin(8.f, 4.f); S.PressedPadding = FMargin(8.f, 4.f);
		return S;
	}

	UWeedActionButton* PackBtn(UWidgetTree* Tree, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		B->SetStyle(PackStyle(Col));
		return B;
	}

	// Herkleur een bestaande rij-knop IN PLACE (selectie <-> deselectie) zonder de rij te herbouwen.
	void Recolour(UWeedActionButton* B, const FLinearColor& Col)
	{
		if (B) { B->SetStyle(PackStyle(Col)); }
	}

	UInventoryComponent* GetInv(APawn* P) { return P ? P->FindComponentByClass<UInventoryComponent>() : nullptr; }

	// Knop-inhoud: item-icoon links + tekst rechts (vult de breedte) - geeft de rijen een herkenbaar
	// beeld i.p.v. kale tekst, in dezelfde stijl als hotbar/inventory/shop.
	UWidget* IconText(UWidgetTree* Tree, FName ItemId, const FString& Txt, int32 TxtSize)
	{
		UHorizontalBox* HB = Tree->ConstructWidget<UHorizontalBox>();
		USizeBox* IB = Tree->ConstructWidget<USizeBox>();
		IB->SetWidthOverride(30.f); IB->SetHeightOverride(30.f);
		IB->SetContent(WeedUI::ItemIcon(Tree, ItemId, 30.f));
		UHorizontalBoxSlot* IS = HB->AddChildToHorizontalBox(IB);
		IS->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f)); IS->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* TS2 = HB->AddChildToHorizontalBox(WeedUI::Text(Tree, Txt, TxtSize, FLinearColor::White, false, true));
		TS2->SetVerticalAlignment(VAlign_Center); TS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return HB;
	}

	// Vaste container-lijst voor het pack-scherm (zelfde volgorde/labels als voorheen).
	static const TCHAR* const kConts[6] = { TEXT("Cont_Bag2"), TEXT("Cont_Bag5"), TEXT("Cont_Jar10"), TEXT("Cont_Jar15"), TEXT("Cont_Block100"), TEXT("Cont_Garbage500") };

	const FLinearColor SelCol(0.22f, 0.52f, 0.32f);   // strain/container geselecteerd
	const FLinearColor UnselCol(0.15f, 0.16f, 0.21f); // strain/container niet-geselecteerd
	const FLinearColor USelCol(0.42f, 0.30f, 0.18f);  // unpack-bag geselecteerd
	const FLinearColor UUnselCol(0.15f, 0.16f, 0.21f);// unpack-bag niet-geselecteerd
}

TSharedRef<SWidget> UPackWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPackWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PackCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), 22.f));
	CardB->SetPadding(FMargin(18.f));
	Card = CardB;

	// Vast bovenanker + autosize: de kaart groeit naar de inhoud (NOOIT scrollen) en blijft met z'n
	// bovenkant op een vaste plek staan - dus hij verschuift niet als er rijen bij/af komen (bv. na het
	// inpakken van een bag verschijnt de unpack-sectie onderaan; de pack-knop blijft op z'n plek).
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.08f, 0.5f, 0.08f));
	CS->SetAlignment(FVector2D(0.5f, 0.0f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, 0.f));

	// Vaste breedte zodat de kaart niet in breedte meeademt met de tekst.
	USizeBox* WidthBox = WidgetTree->ConstructWidget<USizeBox>();
	WidthBox->SetWidthOverride(540.f);
	CardB->SetContent(WidthBox);

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	WidthBox->SetContent(Outer);

	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* TS = Head->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("PACKING BENCH"), 18, FLinearColor(0.6f, 1.f, 0.6f), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	// Aparte UNPACK-tab: grote knop naast Close die wisselt tussen de pack-flow en de uitpak-lijst.
	// IN PLACE: alleen de pane-Visibility + het tab-label flippen (geen ClearChildren/rebuild).
	UWeedActionButton* TabB = PackBtn(WidgetTree, FLinearColor(0.20f, 0.40f, 0.52f), [this]()
	{
		bUnpackTab = !bUnpackTab;
		if (PackPane)   { PackPane->SetVisibility(bUnpackTab ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }
		if (UnpackPane) { UnpackPane->SetVisibility(bUnpackTab ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
		if (TabBtnLabel) { TabBtnLabel->SetText(FText::FromString(bUnpackTab ? TEXT("Back to packing") : TEXT("Unpack bags"))); }
		FillBody(); // alleen de nu-zichtbare pane in place bijwerken
	});
	TabBtnLabel = WeedUI::Text(WidgetTree, TEXT("Unpack bags"), 13, FLinearColor::White, true);
	TabB->SetContent(TabBtnLabel);
	Head->AddChildToHorizontalBox(TabB)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	UWeedActionButton* CloseB = PackBtn(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), [this]() { if (PhoneComp.IsValid()) { PhoneComp->ClosePack(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 12, FLinearColor::White, true));
	Head->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Geen ScrollBox meer: de body staat direct in de kaart en de kaart groeit mee.
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Outer->AddChildToVerticalBox(Body);

	// Twee wederzijds-uitsluitende panes, ÉÉN keer opgebouwd. Alleen hun Visibility flipt op de tab-toggle.
	PackPane = WidgetTree->ConstructWidget<UVerticalBox>();
	Body->AddChildToVerticalBox(PackPane);
	BuildPackPane(PackPane);

	UnpackPane = WidgetTree->ConstructWidget<UVerticalBox>();
	Body->AddChildToVerticalBox(UnpackPane);
	BuildUnpackPane(UnpackPane);

	PackPane->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UnpackPane->SetVisibility(ESlateVisibility::Collapsed);
}

// === Bouwt het volledige pack-scherm ÉÉN keer op (alle secties + slider + steppers). Daarna updatet
//     RefreshPack alleen waardes/zichtbaarheid/rij-pool in place. ===
void UPackWidget::BuildPackPane(UVerticalBox* Parent)
{
	auto Row = [Parent](UWidget* W, const FMargin& P) { Parent->AddChildToVerticalBox(W)->SetPadding(P); };

	// === 1) Kies een gedroogde strain (Bud_) ===
	Row(WeedUI::Text(WidgetTree, TEXT("1.  Pick dried weed"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 0, 0, 4));
	StrainList = WidgetTree->ConstructWidget<UVerticalBox>();
	Row(StrainList, FMargin(0, 0, 0, 0));
	NoBudLabel = WeedUI::Text(WidgetTree, TEXT("No dried weed. Dry it on a rack first."), 11, FLinearColor::Gray);
	Row(NoBudLabel, FMargin(0, 6, 0, 6));
	NoBudLabel->SetVisibility(ESlateVisibility::Collapsed);

	// === 2) Kies een container ===
	ContSection = WidgetTree->ConstructWidget<UVerticalBox>();
	Row(ContSection, FMargin(0, 0, 0, 0));
	{
		auto CRow = [this](UWidget* W, const FMargin& P) { ContSection->AddChildToVerticalBox(W)->SetPadding(P); };
		CRow(WeedUI::Text(WidgetTree, TEXT("2.  Pick a bag/jar"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 4));
		ContList = WidgetTree->ConstructWidget<UVerticalBox>();
		CRow(ContList, FMargin(0, 0, 0, 0));
		NoContLabel = WeedUI::Text(WidgetTree, TEXT("No bags/jars. Buy them in the Grow shop."), 11, FLinearColor(1.f, 0.7f, 0.5f));
		CRow(NoContLabel, FMargin(0, 4, 0, 0));
		NoContLabel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// === 2.b) Gram per zakje (alleen tonen als de container meer dan 1g kan) ===
	GpbSection = WidgetTree->ConstructWidget<UVerticalBox>();
	Row(GpbSection, FMargin(0, 0, 0, 0));
	{
		auto GRow = [this](UWidget* W, const FMargin& P) { GpbSection->AddChildToVerticalBox(W)->SetPadding(P); };
		GRow(WeedUI::Text(WidgetTree, TEXT("2.b  Grams per bag"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 2));
		GpbLabel = WeedUI::Text(WidgetTree, TEXT(""), 16, FLinearColor::White, false, true);
		GRow(GpbLabel, FMargin(0, 0, 0, 4));

		// -/+ stepper voor de gram-per-zakje. Wijziging => hergebruikt RefreshPack (recompute + in place).
		UHorizontalBox* GpbRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		{
			UWeedActionButton* Minus = PackBtn(WidgetTree, FLinearColor(0.20f, 0.27f, 0.22f),
				[this]() { SelGrams = FMath::Clamp(SelGrams - 1, 1, PackCap); RefreshPack(); });
			Minus->SetContent(WeedUI::Text(WidgetTree, TEXT("-"), 20, FLinearColor::White, true, true));
			USizeBox* MB = WidgetTree->ConstructWidget<USizeBox>(); MB->SetWidthOverride(44.f); MB->SetContent(Minus);
			GpbRow->AddChildToHorizontalBox(MB)->SetVerticalAlignment(VAlign_Fill);

			UWeedActionButton* OneB = PackBtn(WidgetTree, FLinearColor(0.18f, 0.22f, 0.30f),
				[this]() { SelGrams = FMath::Clamp(1, 1, PackCap); RefreshPack(); });
			OneB->SetContent(WeedUI::Text(WidgetTree, TEXT("1g"), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* O1 = GpbRow->AddChildToHorizontalBox(OneB);
			O1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); O1->SetPadding(FMargin(8.f, 0.f, 4.f, 0.f)); O1->SetVerticalAlignment(VAlign_Fill);

			UWeedActionButton* MaxGB = PackBtn(WidgetTree, FLinearColor(0.18f, 0.22f, 0.30f),
				[this]() { SelGrams = PackCap; RefreshPack(); });
			MaxGB->SetContent(WeedUI::Text(WidgetTree, TEXT("Max"), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* G1 = GpbRow->AddChildToHorizontalBox(MaxGB);
			G1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); G1->SetPadding(FMargin(4.f, 0.f, 8.f, 0.f)); G1->SetVerticalAlignment(VAlign_Fill);

			UWeedActionButton* Plus = PackBtn(WidgetTree, FLinearColor(0.20f, 0.27f, 0.22f),
				[this]() { SelGrams = FMath::Clamp(SelGrams + 1, 1, PackCap); RefreshPack(); });
			Plus->SetContent(WeedUI::Text(WidgetTree, TEXT("+"), 20, FLinearColor::White, true, true));
			USizeBox* PB = WidgetTree->ConstructWidget<USizeBox>(); PB->SetWidthOverride(44.f); PB->SetContent(Plus);
			GpbRow->AddChildToHorizontalBox(PB)->SetVerticalAlignment(VAlign_Fill);
		}
		GRow(GpbRow, FMargin(0, 0, 0, 6));
	}

	// === 3) Hoeveel bags? (slider + steppers + presets + pack-knop) ===
	BagsSection = WidgetTree->ConstructWidget<UVerticalBox>();
	Row(BagsSection, FMargin(0, 0, 0, 0));
	{
		auto BRow = [this](UWidget* W, const FMargin& P) { BagsSection->AddChildToVerticalBox(W)->SetPadding(P); };
		BRow(WeedUI::Text(WidgetTree, TEXT("3.  How many bags?"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 2));
		GramLabel = WeedUI::Text(WidgetTree, TEXT(""), 16, FLinearColor::White, false, true);
		BRow(GramLabel, FMargin(0, 0, 0, 4));

		GramSlider = WidgetTree->ConstructWidget<USlider>();
		GramSlider->SetMinValue(0.f);
		GramSlider->SetMaxValue(1.f);
		GramSlider->SetSliderHandleColor(FLinearColor(0.5f, 1.f, 0.6f));
		GramSlider->SetSliderBarColor(FLinearColor(0.25f, 0.4f, 0.3f));

		// -/+ stepper rond de slider voor precieze controle.
		UHorizontalBox* GramRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		{
			UWeedActionButton* Minus = PackBtn(WidgetTree, FLinearColor(0.20f, 0.27f, 0.22f), [this]() { SetB(SelBags - 1); });
			Minus->SetContent(WeedUI::Text(WidgetTree, TEXT("-"), 20, FLinearColor::White, true, true));
			USizeBox* MB = WidgetTree->ConstructWidget<USizeBox>(); MB->SetWidthOverride(44.f); MB->SetContent(Minus);
			GramRow->AddChildToHorizontalBox(MB)->SetVerticalAlignment(VAlign_Fill);

			USizeBox* SliderBox = WidgetTree->ConstructWidget<USizeBox>();
			SliderBox->SetHeightOverride(24.f);
			SliderBox->SetContent(GramSlider);
			UHorizontalBoxSlot* SbS = GramRow->AddChildToHorizontalBox(SliderBox);
			SbS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SbS->SetVerticalAlignment(VAlign_Center);
			SbS->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));

			UWeedActionButton* Plus = PackBtn(WidgetTree, FLinearColor(0.20f, 0.27f, 0.22f), [this]() { SetB(SelBags + 1); });
			Plus->SetContent(WeedUI::Text(WidgetTree, TEXT("+"), 20, FLinearColor::White, true, true));
			USizeBox* PB = WidgetTree->ConstructWidget<USizeBox>(); PB->SetWidthOverride(44.f); PB->SetContent(Plus);
			GramRow->AddChildToHorizontalBox(PB)->SetVerticalAlignment(VAlign_Fill);
		}
		BRow(GramRow, FMargin(0, 0, 0, 6));

		// Snelknoppen: Half / Max (aantal bags).
		UHorizontalBox* PresetRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		{
			UWeedActionButton* HalfB = PackBtn(WidgetTree, FLinearColor(0.18f, 0.22f, 0.30f), [this]() { SetB(FMath::Max(1, MaxBags / 2)); });
			HalfB->SetContent(WeedUI::Text(WidgetTree, TEXT("Half"), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* H1 = PresetRow->AddChildToHorizontalBox(HalfB); H1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H1->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			UWeedActionButton* MaxB = PackBtn(WidgetTree, FLinearColor(0.18f, 0.22f, 0.30f), [this]() { SetB(MaxBags); });
			MaxB->SetContent(WeedUI::Text(WidgetTree, TEXT("Max"), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* M1 = PresetRow->AddChildToHorizontalBox(MaxB); M1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); M1->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		}
		BRow(PresetRow, FMargin(0, 0, 0, 8));

		PackButton = PackBtn(WidgetTree, FLinearColor(0.2f, 0.5f, 0.3f), [this]()
		{
			if (!PhoneComp.IsValid()) { return; }
			UPhoneClientComponent* Ph = PhoneComp.Get();
			for (int32 i = 0; i < FMath::Max(1, SelBags); ++i) { Ph->RequestPackGrams(SelStrain, SelContainer, SelGrams); }
			RefreshPack(); // voorraad wijzigt -> in place bijwerken (geen teardown)
		});
		PackBtnLabel = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor::White, true);
		PackButton->SetContent(PackBtnLabel);
		BRow(PackButton, FMargin(0, 2, 0, 2));
	}
	// (Uitpakken zit in de aparte "Unpack bags"-tab in de header, niet meer inline onder de pack-knop.)
}

// === Bouwt het volledige unpack-scherm ÉÉN keer op. Daarna updatet RefreshUnpack in place. ===
void UPackWidget::BuildUnpackPane(UVerticalBox* Parent)
{
	auto Row = [Parent](UWidget* W, const FMargin& P) { Parent->AddChildToVerticalBox(W)->SetPadding(P); };

	UnpackEmptyLabel = WeedUI::Text(WidgetTree, TEXT("No packed bags to unpack - pack some first."), 11, FLinearColor::Gray);
	Row(UnpackEmptyLabel, FMargin(0, 8, 0, 6));
	UnpackEmptyLabel->SetVisibility(ESlateVisibility::Collapsed);

	Row(WeedUI::Text(WidgetTree, TEXT("Unpack bags (back to loose weed)"), 12, FLinearColor(1.f, 0.85f, 0.6f), false, true), FMargin(0, 14, 0, 4));

	// Lijst met bag-rijen (klik = selecteren) - net als de strain/container-keuze bij packen.
	UnpackList = WidgetTree->ConstructWidget<UVerticalBox>();
	Row(UnpackList, FMargin(0, 0, 0, 0));

	// Bedieningsblok (slider/steppers/knop): verborgen als er geen bags zijn.
	UnpackControls = WidgetTree->ConstructWidget<UVerticalBox>();
	Row(UnpackControls, FMargin(0, 0, 0, 0));
	{
		auto CRow = [this](UWidget* W, const FMargin& P) { UnpackControls->AddChildToVerticalBox(W)->SetPadding(P); };

		UnpackSlider = WidgetTree->ConstructWidget<USlider>();
		UnpackSlider->SetMinValue(0.f);
		UnpackSlider->SetMaxValue(1.f);
		UnpackSlider->SetSliderHandleColor(FLinearColor(0.95f, 0.75f, 0.45f));
		UnpackSlider->SetSliderBarColor(FLinearColor(0.4f, 0.3f, 0.2f));

		UnpackLabel = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor::White, false);
		CRow(UnpackLabel, FMargin(0, 6, 0, 2));

		// -/+ stepper rond de slider.
		UHorizontalBox* QRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		{
			UWeedActionButton* Minus = PackBtn(WidgetTree, FLinearColor(0.27f, 0.22f, 0.18f), [this]() { SetUB(SelBags - 1); });
			Minus->SetContent(WeedUI::Text(WidgetTree, TEXT("-"), 20, FLinearColor::White, true, true));
			USizeBox* MB = WidgetTree->ConstructWidget<USizeBox>(); MB->SetWidthOverride(44.f); MB->SetContent(Minus);
			QRow->AddChildToHorizontalBox(MB)->SetVerticalAlignment(VAlign_Fill);

			USizeBox* SliderBox = WidgetTree->ConstructWidget<USizeBox>();
			SliderBox->SetHeightOverride(24.f);
			SliderBox->SetContent(UnpackSlider);
			UHorizontalBoxSlot* SbS = QRow->AddChildToHorizontalBox(SliderBox);
			SbS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SbS->SetVerticalAlignment(VAlign_Center);
			SbS->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));

			UWeedActionButton* Plus = PackBtn(WidgetTree, FLinearColor(0.27f, 0.22f, 0.18f), [this]() { SetUB(SelBags + 1); });
			Plus->SetContent(WeedUI::Text(WidgetTree, TEXT("+"), 20, FLinearColor::White, true, true));
			USizeBox* PB = WidgetTree->ConstructWidget<USizeBox>(); PB->SetWidthOverride(44.f); PB->SetContent(Plus);
			QRow->AddChildToHorizontalBox(PB)->SetVerticalAlignment(VAlign_Fill);
		}
		CRow(QRow, FMargin(0, 0, 0, 6));

		// Snelknoppen: Half / Max.
		UHorizontalBox* PresetRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		{
			UWeedActionButton* HalfB = PackBtn(WidgetTree, FLinearColor(0.30f, 0.24f, 0.18f), [this]() { SetUB(FMath::Max(1, MaxBags / 2)); });
			HalfB->SetContent(WeedUI::Text(WidgetTree, TEXT("Half"), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* H1 = PresetRow->AddChildToHorizontalBox(HalfB); H1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H1->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			UWeedActionButton* MaxB = PackBtn(WidgetTree, FLinearColor(0.30f, 0.24f, 0.18f), [this]() { SetUB(MaxBags); });
			MaxB->SetContent(WeedUI::Text(WidgetTree, TEXT("Max"), 12, FLinearColor::White, true));
			UHorizontalBoxSlot* M1 = PresetRow->AddChildToHorizontalBox(MaxB); M1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); M1->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		}
		CRow(PresetRow, FMargin(0, 0, 0, 8));

		UnpackButton = PackBtn(WidgetTree, FLinearColor(0.5f, 0.34f, 0.18f), [this]()
		{
			if (!PhoneComp.IsValid()) { return; }
			PhoneComp->RequestUnpack(SelUnpackBag, SelBags);
			RefreshUnpack(); // voorraad wijzigt -> in place bijwerken (geen teardown)
		});
		UnpackBtnLabel = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor::White, true);
		UnpackButton->SetContent(UnpackBtnLabel);
		CRow(UnpackButton, FMargin(0, 2, 0, 2));
	}
}

// Dispatcher: geen ClearChildren meer - kiest de zichtbare pane en werkt die in place bij.
void UPackWidget::FillBody()
{
	if (!Body || !PhoneComp.IsValid()) { return; }
	if (TabBtnLabel) { TabBtnLabel->SetText(FText::FromString(bUnpackTab ? TEXT("Back to packing") : TEXT("Unpack bags"))); }
	if (bUnpackTab) { RefreshUnpack(); } else { RefreshPack(); }
}

// Zet het aantal bags EN werkt slider + labels meteen bij (geen herbouw -> slider springt niet). Pack.
void UPackWidget::SetB(int32 N)
{
	SelBags = FMath::Clamp(N, 1, FMath::Max(1, MaxBags));
	const int32 G = FMath::Min(PackBudHave, SelBags * SelGrams);
	if (GramSlider)   { GramSlider->SetValue(MaxBags > 1 ? float(SelBags - 1) / float(MaxBags - 1) : 1.f); }
	if (GramLabel)    { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("%d bag%s   (uses %dg, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G, MaxBags))); }
	if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
}

// Zet het aantal bags EN werkt slider + labels meteen bij. Unpack.
void UPackWidget::SetUB(int32 N)
{
	SelBags = FMath::Clamp(N, 1, FMath::Max(1, MaxBags));
	const int32 G = SelBags * UnpackPerBag;
	if (UnpackSlider)   { UnpackSlider->SetValue(MaxBags > 1 ? float(SelBags - 1) / float(MaxBags - 1) : 1.f); }
	if (UnpackLabel)    { UnpackLabel->SetText(FText::FromString(FString::Printf(TEXT("%d bag%s   (%dg -> loose, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G, MaxBags))); }
	if (UnpackBtnLabel) { UnpackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Unpack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
}

// Werkt de pack-flow IN PLACE bij: diff de strain/container-rij-pools, herkleur enkel gewijzigde rijen,
// toggle de vervolg-secties, en push slider/labels. NOOIT ClearChildren op de lijsten.
void UPackWidget::RefreshPack()
{
	if (!PackPane || !PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	// --- 1) Strain-rijen (persistent pool + per-rij signature) ---
	struct FBudRow { FName Id; int32 Qty; float Quality; float QualityPct; };
	TArray<FBudRow> Buds;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (!S.ItemId.ToString().StartsWith(TEXT("Bud_"))) { continue; }
		Buds.Add({ S.ItemId, S.Quantity, S.Quality, S.QualityPct });
	}
	const bool bAnyBud = Buds.Num() > 0;
	if (NoBudLabel) { NoBudLabel->SetVisibility(bAnyBud ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }

	// Pool op maat brengen (geen ClearChildren).
	while (StrainRowBtns.Num() < Buds.Num())
	{
		UWeedActionButton* B = PackBtn(WidgetTree, UnselCol, nullptr);
		StrainList->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 2, 0, 2));
		StrainRowBtns.Add(B); StrainRowIds.Add(NAME_None); StrainRowSigs.Add(TEXT("\x01"));
	}
	while (StrainRowBtns.Num() > Buds.Num())
	{
		const int32 Last = StrainRowBtns.Num() - 1;
		if (StrainRowBtns[Last]) { StrainRowBtns[Last]->RemoveFromParent(); }
		StrainRowBtns.RemoveAt(Last); StrainRowIds.RemoveAt(Last); StrainRowSigs.RemoveAt(Last);
	}
	for (int32 i = 0; i < Buds.Num(); ++i)
	{
		const FBudRow& R = Buds[i];
		UWeedActionButton* B = StrainRowBtns[i];
		if (!B) { continue; }
		const bool bSel = (R.Id == SelStrain);
		// Content-signature ZONDER selectie: alleen rij-DATA. Zo herbouwen we de rij-inhoud niet bij een
		// simpele selectie-flip - dan herkleuren we alleen (SetStyle), precies zoals gevraagd.
		const FString Sig = FString::Printf(TEXT("%s|%d|%.1f|%.1f"), *R.Id.ToString(), R.Qty, R.Quality, R.QualityPct);
		if (Sig != StrainRowSigs[i])
		{
			StrainRowSigs[i] = Sig;
			StrainRowIds[i] = R.Id;
			B->SetContent(IconText(WidgetTree, R.Id, FString::Printf(TEXT("%s   %dg  (THC %.0f%%, Q %.0f%%)"),
				*WeedUI::PrettyItemName(R.Id), R.Qty, R.Quality, R.QualityPct), 12));
		}
		Recolour(B, bSel ? SelCol : UnselCol); // herkleur-in-place (goedkoop; alleen de style verandert)
		// (Re)bind de klik-actie zodat de captured id klopt met de huidige rij-index.
		const FName Bud = R.Id;
		B->OnAction.Unbind();
		B->OnAction.BindLambda([this, Bud](int32, int32) { SelStrain = Bud; RefreshPack(); });
	}

	const int32 BudHave = SelStrain.IsNone() ? 0 : Inv->GetQuantity(SelStrain);
	const bool bStrainChosen = bAnyBud && !SelStrain.IsNone() && BudHave > 0;

	// --- 2) Container-rijen (persistent pool) ---
	struct FContRow { FName Id; int32 Cap; int32 Owned; };
	TArray<FContRow> Conts;
	if (bStrainChosen)
	{
		for (int32 i = 0; i < 6; ++i)
		{
			const FName ContId(kConts[i]);
			const int32 Owned = Inv->GetQuantity(ContId);
			if (Owned <= 0) { continue; }
			Conts.Add({ ContId, UPhoneClientComponent::ContainerCapacity(ContId), Owned });
		}
	}
	const bool bAnyCont = Conts.Num() > 0;
	if (ContSection) { ContSection->SetVisibility(bStrainChosen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (NoContLabel) { NoContLabel->SetVisibility((bStrainChosen && !bAnyCont) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	while (ContRowBtns.Num() < Conts.Num())
	{
		UWeedActionButton* B = PackBtn(WidgetTree, UnselCol, nullptr);
		ContList->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 2, 0, 2));
		ContRowBtns.Add(B); ContRowIds.Add(NAME_None); ContRowSigs.Add(TEXT("\x01"));
	}
	while (ContRowBtns.Num() > Conts.Num())
	{
		const int32 Last = ContRowBtns.Num() - 1;
		if (ContRowBtns[Last]) { ContRowBtns[Last]->RemoveFromParent(); }
		ContRowBtns.RemoveAt(Last); ContRowIds.RemoveAt(Last); ContRowSigs.RemoveAt(Last);
	}
	for (int32 i = 0; i < Conts.Num(); ++i)
	{
		const FContRow& R = Conts[i];
		UWeedActionButton* B = ContRowBtns[i];
		if (!B) { continue; }
		const bool bSel = (R.Id == SelContainer);
		const FString Sig = FString::Printf(TEXT("%s|%d|%d"), *R.Id.ToString(), R.Cap, R.Owned);
		if (Sig != ContRowSigs[i])
		{
			ContRowSigs[i] = Sig;
			ContRowIds[i] = R.Id;
			B->SetContent(IconText(WidgetTree, R.Id, FString::Printf(TEXT("%s   up to %dg   x%d"),
				*WeedUI::PrettyItemName(R.Id), R.Cap, R.Owned), 12));
		}
		Recolour(B, bSel ? SelCol : UnselCol); // herkleur-in-place
		const FName ContId = R.Id;
		B->OnAction.Unbind();
		B->OnAction.BindLambda([this, ContId](int32, int32) { SelContainer = ContId; SelBags = MaxBags; RefreshPack(); });
	}

	// --- Vervolg-secties zichtbaar zodra strain + container gekozen zijn ---
	const bool bContChosen = bStrainChosen && !SelContainer.IsNone() && Inv->HasItem(SelContainer, 1);
	if (GpbSection)  { GpbSection->SetVisibility(ESlateVisibility::Collapsed); }
	if (BagsSection) { BagsSection->SetVisibility(bContChosen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bContChosen) { return; }

	// === 3) Rekenkern (identiek aan voorheen) ===
	PackCap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(SelContainer));
	if (SelGrams <= 0) { SelGrams = PackCap; }
	SelGrams = FMath::Clamp(SelGrams, 1, PackCap);
	PackBudHave = BudHave;
	const int32 OwnedCont = Inv->GetQuantity(SelContainer);
	MaxBags = FMath::Max(1, FMath::Min(OwnedCont, BudHave / FMath::Max(1, SelGrams)));
	SelBags = FMath::Clamp(SelBags, 1, MaxBags);

	// 2.b Gram per zakje: alleen tonen als de container meer dan 1g kan.
	if (GpbSection) { GpbSection->SetVisibility(PackCap > 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (GpbLabel)   { GpbLabel->SetText(FText::FromString(FString::Printf(TEXT("%d g per bag   (max %d)"), SelGrams, PackCap))); }

	// Slider + labels + pack-knop in place (via de gedeelde helper).
	SetB(SelBags);
}

// Werkt de unpack-flow IN PLACE bij: diff de bag-rij-pool, herkleur enkel gewijzigde rijen, toggle het
// bedieningsblok, en push slider/labels. NOOIT ClearChildren op de lijst.
void UPackWidget::RefreshUnpack()
{
	if (!UnpackPane || !PhoneComp.IsValid()) { return; }
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	// Welke verpakte bags heb je?
	struct FBagRow { FName Id; int32 Owned; };
	TArray<FBagRow> Bags;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (S.ItemId.ToString().StartsWith(TEXT("Bag_")) && S.Quantity > 0) { Bags.Add({ S.ItemId, S.Quantity }); }
	}
	const bool bAnyBag = Bags.Num() > 0;
	if (UnpackEmptyLabel) { UnpackEmptyLabel->SetVisibility(bAnyBag ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }
	if (UnpackControls)   { UnpackControls->SetVisibility(bAnyBag ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	if (bAnyBag && (SelUnpackBag.IsNone() || !Bags.ContainsByPredicate([this](const FBagRow& R) { return R.Id == SelUnpackBag; })))
	{
		SelUnpackBag = Bags[0].Id;
	}

	// Pool op maat brengen (geen ClearChildren).
	while (UnpackRowBtns.Num() < Bags.Num())
	{
		UWeedActionButton* B = PackBtn(WidgetTree, UUnselCol, nullptr);
		UnpackList->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 2, 0, 2));
		UnpackRowBtns.Add(B); UnpackRowIds.Add(NAME_None); UnpackRowSigs.Add(TEXT("\x01"));
	}
	while (UnpackRowBtns.Num() > Bags.Num())
	{
		const int32 Last = UnpackRowBtns.Num() - 1;
		if (UnpackRowBtns[Last]) { UnpackRowBtns[Last]->RemoveFromParent(); }
		UnpackRowBtns.RemoveAt(Last); UnpackRowIds.RemoveAt(Last); UnpackRowSigs.RemoveAt(Last);
	}
	for (int32 i = 0; i < Bags.Num(); ++i)
	{
		const FBagRow& R = Bags[i];
		UWeedActionButton* B = UnpackRowBtns[i];
		if (!B) { continue; }
		const bool bSel = (R.Id == SelUnpackBag);
		const FString Sig = FString::Printf(TEXT("%s|%d"), *R.Id.ToString(), R.Owned);
		if (Sig != UnpackRowSigs[i])
		{
			UnpackRowSigs[i] = Sig;
			UnpackRowIds[i] = R.Id;
			B->SetContent(IconText(WidgetTree, R.Id, FString::Printf(TEXT("%s   x%d"), *WeedUI::PrettyItemName(R.Id), R.Owned), 11));
		}
		Recolour(B, bSel ? USelCol : UUnselCol); // herkleur-in-place
		const FName Bag = R.Id;
		B->OnAction.Unbind();
		B->OnAction.BindLambda([this, Bag](int32, int32) { if (Bag != SelUnpackBag) { SelUnpackBag = Bag; SelBags = 1; RefreshUnpack(); } });
	}

	if (!bAnyBag) { return; }

	// Hoeveelheid-selector voor de gekozen bag (mirror van de pack-selector).
	const int32 OwnedSel = Inv->GetQuantity(SelUnpackBag);
	MaxBags = FMath::Max(1, OwnedSel);
	UnpackPerBag = FMath::Max(1, UInventoryComponent::BagGrams(SelUnpackBag));
	SelBags = FMath::Clamp(SelBags, 1, MaxBags);

	// Slider + labels + unpack-knop in place (via de gedeelde helper).
	SetUB(SelBags);
}

void UPackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsPackOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Live de bag-slider uitlezen (zonder herbouw, anders springt de slider).
	USlider* ActiveSlider = bUnpackTab ? UnpackSlider.Get() : GramSlider.Get();
	if (ActiveSlider)
	{
		const int32 NewN = (MaxBags <= 1) ? 1 : FMath::Clamp(1 + FMath::RoundToInt(ActiveSlider->GetValue() * float(MaxBags - 1)), 1, MaxBags);
		if (NewN != SelBags)
		{
			SelBags = NewN;
			if (bUnpackTab)
			{
				const int32 G = SelBags * UnpackPerBag;
				if (UnpackLabel)    { UnpackLabel->SetText(FText::FromString(FString::Printf(TEXT("%d bag%s   (%dg -> loose, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G, MaxBags))); }
				if (UnpackBtnLabel) { UnpackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Unpack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
			}
			else
			{
				const int32 G = FMath::Min(PackBudHave, SelBags * SelGrams);
				if (GramLabel)    { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("%d bag%s   (uses %dg, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G, MaxBags))); }
				if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
			}
		}
	}

	// Diff-refresh als de relevante voorraad of de strain-keuze wijzigt (NIET bij slider-bewegen).
	// De Refresh-functies bouwen niets af: ze updaten/voegen-toe/verwijderen alleen gewijzigde rijen.
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	FString Sig = SelStrain.ToString() + TEXT("/") + SelContainer.ToString() + TEXT("/") + SelUnpackBag.ToString() + (bUnpackTab ? TEXT("/U") : TEXT("/P"));
	if (Inv) { for (const FInventoryStack& S : Inv->GetStacks()) { const FString Id = S.ItemId.ToString(); if (Id.StartsWith(TEXT("Bud_")) || Id.StartsWith(TEXT("Cont_")) || Id.StartsWith(TEXT("Bag_"))) { Sig += FString::Printf(TEXT("|%s:%d"), *Id, S.Quantity); } } }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

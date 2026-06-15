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
	UWeedActionButton* PackBtn(UWidgetTree* Tree, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 8.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		S.NormalPadding = FMargin(8.f, 4.f); S.PressedPadding = FMargin(8.f, 4.f);
		B->SetStyle(S);
		return B;
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
	// inpakken van een bag verschijnt de unpack-sectie onderaan; de pack-knпop blijft op z'n plek).
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
	UWeedActionButton* CloseB = PackBtn(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), [this]() { if (PhoneComp.IsValid()) { PhoneComp->ClosePack(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 12, FLinearColor::White, true));
	Head->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Geen ScrollBox meer: de body staat direct in de kaart en de kaart groeit mee.
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Outer->AddChildToVerticalBox(Body);
}

void UPackWidget::FillBody()
{
	if (!Body || !PhoneComp.IsValid()) { return; }
	Body->ClearChildren();
	GramSlider = nullptr; GramLabel = nullptr; PackBtnLabel = nullptr;
	UPhoneClientComponent* Ph = PhoneComp.Get();
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	auto Row = [this](UWidget* W, const FMargin& P) { Body->AddChildToVerticalBox(W)->SetPadding(P); };

	// === 1) Kies een gedroogde strain (Bud_) ===
	Row(WeedUI::Text(WidgetTree, TEXT("1.  Pick dried weed"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 0, 0, 4));
	bool bAnyBud = false;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (!S.ItemId.ToString().StartsWith(TEXT("Bud_"))) { continue; }
		bAnyBud = true;
		const FName Bud = S.ItemId;
		const bool bSel = (Bud == SelStrain);
		UWeedActionButton* B = PackBtn(WidgetTree, bSel ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f),
			[this, Bud]() { SelStrain = Bud; LastSig.Reset(); FillBody(); });
		B->SetContent(IconText(WidgetTree, Bud, FString::Printf(TEXT("%s   %dg  (THC %.0f%%, Q %.0f%%)"),
			*WeedUI::PrettyItemName(Bud), S.Quantity, S.Quality, S.QualityPct), 12));
		Row(B, FMargin(0, 2, 0, 2));
	}
	if (!bAnyBud) { Row(WeedUI::Text(WidgetTree, TEXT("No dried weed. Dry it on a rack first."), 11, FLinearColor::Gray), FMargin(0, 6, 0, 6)); BuildUnpackSection(); return; }

	const int32 BudHave = SelStrain.IsNone() ? 0 : Inv->GetQuantity(SelStrain);
	if (SelStrain.IsNone() || BudHave <= 0) { BuildUnpackSection(); return; }

	// === 2) Kies een container ===
	Row(WeedUI::Text(WidgetTree, TEXT("2.  Pick a bag/jar"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 4));
	static const TCHAR* Conts[6] = { TEXT("Cont_Bag2"), TEXT("Cont_Bag5"), TEXT("Cont_Jar10"), TEXT("Cont_Jar15"), TEXT("Cont_Block100"), TEXT("Cont_Garbage500") };
	bool bAnyCont = false;
	for (int32 i = 0; i < 6; ++i)
	{
		const FName ContId(Conts[i]);
		const int32 Owned = Inv->GetQuantity(ContId);
		if (Owned <= 0) { continue; }
		bAnyCont = true;
		const int32 Cap = UPhoneClientComponent::ContainerCapacity(ContId);
		const bool bSel = (ContId == SelContainer);
		UWeedActionButton* B = PackBtn(WidgetTree, bSel ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f),
			[this, ContId]() { SelContainer = ContId; SelBags = MaxBags; LastSig.Reset(); FillBody(); });
		B->SetContent(IconText(WidgetTree, ContId, FString::Printf(TEXT("%s   up to %dg   x%d"),
			*WeedUI::PrettyItemName(ContId), Cap, Owned), 12));
		Row(B, FMargin(0, 2, 0, 2));
	}
	if (!bAnyCont) { Row(WeedUI::Text(WidgetTree, TEXT("No bags/jars. Buy them in the Grow shop."), 11, FLinearColor(1.f, 0.7f, 0.5f)), FMargin(0, 4, 0, 0)); BuildUnpackSection(); return; }

	// === 3) Hoeveel bags? (elk bakje tot capaciteit; laatste mag een restje zijn) ===
	if (SelContainer.IsNone() || !Inv->HasItem(SelContainer, 1)) { BuildUnpackSection(); return; }
	PackCap = FMath::Max(1, UPhoneClientComponent::ContainerCapacity(SelContainer));
	PackBudHave = BudHave;
	const int32 OwnedCont = Inv->GetQuantity(SelContainer);
	MaxBags = FMath::Max(1, FMath::Min(OwnedCont, FMath::DivideAndRoundUp(BudHave, PackCap)));
	SelBags = FMath::Clamp(SelBags, 1, MaxBags);
	const int32 UsedG = FMath::Min(BudHave, SelBags * PackCap);

	Row(WeedUI::Text(WidgetTree, TEXT("3.  How many bags?"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 2));
	GramLabel = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d bag%s   (uses %dg, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), UsedG, MaxBags), 16, FLinearColor::White, false, true);
	Row(GramLabel, FMargin(0, 0, 0, 4));

	GramSlider = WidgetTree->ConstructWidget<USlider>();
	GramSlider->SetMinValue(0.f);
	GramSlider->SetMaxValue(1.f);
	GramSlider->SetValue(MaxBags > 1 ? float(SelBags - 1) / float(MaxBags - 1) : 1.f);
	GramSlider->SetSliderHandleColor(FLinearColor(0.5f, 1.f, 0.6f));
	GramSlider->SetSliderBarColor(FLinearColor(0.25f, 0.4f, 0.3f));

	// Eén plek die het aantal bags zet EN slider + labels meteen bijwerkt (geen herbouw -> slider springt niet).
	auto SetB = [this](int32 N)
	{
		SelBags = FMath::Clamp(N, 1, FMath::Max(1, MaxBags));
		const int32 G = FMath::Min(PackBudHave, SelBags * PackCap);
		if (GramSlider)   { GramSlider->SetValue(MaxBags > 1 ? float(SelBags - 1) / float(MaxBags - 1) : 1.f); }
		if (GramLabel)    { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("%d bag%s   (uses %dg, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G, MaxBags))); }
		if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
	};

	// -/+ stepper rond de slider voor precieze controle.
	UHorizontalBox* GramRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UWeedActionButton* Minus = PackBtn(WidgetTree, FLinearColor(0.20f, 0.27f, 0.22f), [this, SetB]() { SetB(SelBags - 1); });
		Minus->SetContent(WeedUI::Text(WidgetTree, TEXT("-"), 20, FLinearColor::White, true, true));
		USizeBox* MB = WidgetTree->ConstructWidget<USizeBox>(); MB->SetWidthOverride(44.f); MB->SetContent(Minus);
		GramRow->AddChildToHorizontalBox(MB)->SetVerticalAlignment(VAlign_Fill);

		USizeBox* SliderBox = WidgetTree->ConstructWidget<USizeBox>();
		SliderBox->SetHeightOverride(24.f);
		SliderBox->SetContent(GramSlider);
		UHorizontalBoxSlot* SbS = GramRow->AddChildToHorizontalBox(SliderBox);
		SbS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SbS->SetVerticalAlignment(VAlign_Center);
		SbS->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));

		UWeedActionButton* Plus = PackBtn(WidgetTree, FLinearColor(0.20f, 0.27f, 0.22f), [this, SetB]() { SetB(SelBags + 1); });
		Plus->SetContent(WeedUI::Text(WidgetTree, TEXT("+"), 20, FLinearColor::White, true, true));
		USizeBox* PB = WidgetTree->ConstructWidget<USizeBox>(); PB->SetWidthOverride(44.f); PB->SetContent(Plus);
		GramRow->AddChildToHorizontalBox(PB)->SetVerticalAlignment(VAlign_Fill);
	}
	Row(GramRow, FMargin(0, 0, 0, 6));

	// Snelknoppen: Half / Max (aantal bags).
	UHorizontalBox* PresetRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UWeedActionButton* HalfB = PackBtn(WidgetTree, FLinearColor(0.18f, 0.22f, 0.30f), [this, SetB]() { SetB(FMath::Max(1, MaxBags / 2)); });
		HalfB->SetContent(WeedUI::Text(WidgetTree, TEXT("Half"), 12, FLinearColor::White, true));
		UHorizontalBoxSlot* H1 = PresetRow->AddChildToHorizontalBox(HalfB); H1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H1->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		UWeedActionButton* MaxB = PackBtn(WidgetTree, FLinearColor(0.18f, 0.22f, 0.30f), [this, SetB]() { SetB(MaxBags); });
		MaxB->SetContent(WeedUI::Text(WidgetTree, TEXT("Max"), 12, FLinearColor::White, true));
		UHorizontalBoxSlot* M1 = PresetRow->AddChildToHorizontalBox(MaxB); M1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); M1->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
	}
	Row(PresetRow, FMargin(0, 0, 0, 8));

	UWeedActionButton* PackB = PackBtn(WidgetTree, FLinearColor(0.2f, 0.5f, 0.3f),
		[this, Ph]() { Ph->ServerPack(SelStrain, SelContainer, SelBags); LastSig.Reset(); });
	PackBtnLabel = WeedUI::Text(WidgetTree, FString::Printf(TEXT("Pack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), UsedG), 13, FLinearColor::White, true);
	PackB->SetContent(PackBtnLabel);
	Row(PackB, FMargin(0, 2, 0, 2));

	// Uitpakken onderaan: nieuwe bags verschijnen hier, ONDER de pack-knop, zodat die niet verschuift.
	BuildUnpackSection();
}

void UPackWidget::BuildUnpackSection()
{
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	auto Row = [this](UWidget* W, const FMargin& P) { Body->AddChildToVerticalBox(W)->SetPadding(P); };

	bool bAnyBag = false;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (!S.ItemId.ToString().StartsWith(TEXT("Bag_"))) { continue; }
		if (!bAnyBag) { Row(WeedUI::Text(WidgetTree, TEXT("Unpack a bag (back to loose weed)"), 12, FLinearColor(1.f, 0.85f, 0.6f), false, true), FMargin(0, 14, 0, 4)); }
		bAnyBag = true;
		const FName Bag = S.ItemId;
		UWeedActionButton* B = PackBtn(WidgetTree, FLinearColor(0.42f, 0.30f, 0.18f),
			[this, Ph, Bag]() { Ph->RequestUnpack(Bag); LastSig.Reset(); });
		B->SetContent(IconText(WidgetTree, Bag, FString::Printf(TEXT("Unpack %s  (%dg -> loose)"), *WeedUI::PrettyItemName(Bag), S.Quantity), 11));
		Row(B, FMargin(0, 2, 0, 2));
	}
}

void UPackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsPackOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Live de bag-slider uitlezen (zonder herbouw, anders springt de slider).
	if (GramSlider)
	{
		const int32 NewN = (MaxBags <= 1) ? 1 : FMath::Clamp(1 + FMath::RoundToInt(GramSlider->GetValue() * float(MaxBags - 1)), 1, MaxBags);
		if (NewN != SelBags)
		{
			SelBags = NewN;
			const int32 G = FMath::Min(PackBudHave, SelBags * PackCap);
			if (GramLabel) { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("%d bag%s   (uses %dg, max %d)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G, MaxBags))); }
			if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %d bag%s   (%dg)"), SelBags, SelBags == 1 ? TEXT("") : TEXT("s"), G))); }
		}
	}

	// Herbouw als de relevante voorraad of de strain-keuze wijzigt (NIET bij slider-bewegen).
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	FString Sig = SelStrain.ToString() + TEXT("/") + SelContainer.ToString();
	if (Inv) { for (const FInventoryStack& S : Inv->GetStacks()) { const FString Id = S.ItemId.ToString(); if (Id.StartsWith(TEXT("Bud_")) || Id.StartsWith(TEXT("Cont_")) || Id.StartsWith(TEXT("Bag_"))) { Sig += FString::Printf(TEXT("|%s:%d"), *Id, S.Quantity); } } }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

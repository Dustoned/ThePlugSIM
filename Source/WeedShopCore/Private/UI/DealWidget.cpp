#include "UI/DealWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Customer/CustomerBase.h"
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
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

namespace
{
	FString PrettyName(FName Id)
	{
		FString S = Id.ToString();
		if (S.StartsWith(TEXT("Bag_"))) { S = S.RightChop(4) + TEXT(" bag"); }
		else if (S.StartsWith(TEXT("Bud_"))) { S = S.RightChop(4); }
		else if (S.StartsWith(TEXT("Seed_"))) { S = S.RightChop(5) + TEXT(" seed"); }
		return S;
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
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), 26.f));
	CardB->SetPadding(FMargin(20.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(440.f, 470.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("DEAL"), 20, FLinearColor(0.6f, 1.f, 0.6f), false, true));

	WantsText = WeedUI::Text(WidgetTree, TEXT(""), 14, FLinearColor::White);
	VB->AddChildToVerticalBox(WantsText)->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));

	SubText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(1.f, 0.7f, 0.4f));
	VB->AddChildToVerticalBox(SubText);

	PriceText = WeedUI::Text(WidgetTree, TEXT(""), 14, FLinearColor(1.f, 0.95f, 0.6f));
	VB->AddChildToVerticalBox(PriceText)->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));

	PriceSlider = WidgetTree->ConstructWidget<USlider>();
	PriceSlider->SetSliderHandleColor(FLinearColor::White);
	PriceSlider->SetSliderBarColor(FLinearColor(0.2f, 0.22f, 0.28f));
	PriceSlider->OnValueChanged.AddDynamic(this, &UDealWidget::OnPriceSlider);
	VB->AddChildToVerticalBox(PriceSlider)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	StockText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.8f, 0.85f, 1.f));
	VB->AddChildToVerticalBox(StockText)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));

	ChanceText = WeedUI::Text(WidgetTree, TEXT(""), 14, FLinearColor::Green, false, true);
	VB->AddChildToVerticalBox(ChanceText);
	ChanceBar = WidgetTree->ConstructWidget<UProgressBar>();
	ChanceBar->SetFillColorAndOpacity(FLinearColor::Green);
	VB->AddChildToVerticalBox(ChanceBar)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	RelationText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.7f, 0.7f, 0.8f));
	VB->AddChildToVerticalBox(RelationText);
	PreviewText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.55f, 0.95f, 0.6f));
	VB->AddChildToVerticalBox(PreviewText)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Grote, duidelijke melding als je niets te verkopen hebt (verbergt de hele prijs-flow).
	NoWeedText = WeedUI::Text(WidgetTree, TEXT("You have no packaged weed to sell.\nGrow it, dry it, then bag it first."), 14, FLinearColor(1.f, 0.6f, 0.45f), false, true);
	VB->AddChildToVerticalBox(NoWeedText)->SetPadding(FMargin(0.f, 14.f, 0.f, 14.f));

	OfferLabel = WeedUI::Text(WidgetTree, TEXT("Offer another strain:"), 12, FLinearColor(0.75f, 0.8f, 0.95f));
	VB->AddChildToVerticalBox(OfferLabel);
	StrainBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(StrainBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Offer / Cancel.
	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakeBtn = [this](const FString& Label, const FLinearColor& Col, int32 Act) -> UWeedActionButton*
	{
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->Action = Act;
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([this](int32 A, int32 /*P*/)
		{
			if (UPhoneClientComponent* Ph = GetPhone())
			{
				if (A == 1) { Ph->ConfirmDeal(); } else { Ph->CloseDeal(); }
			}
		});
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(14.f, 8.f); St.PressedPadding = FMargin(14.f, 8.f);
		B->SetStyle(St);
		B->SetContent(WeedUI::Text(WidgetTree, Label, 14, FLinearColor::White, true, true));
		return B;
	};
	UHorizontalBoxSlot* OS = Btns->AddChildToHorizontalBox(MakeBtn(TEXT("Offer deal"), FLinearColor(0.2f, 0.5f, 0.28f), 1));
	OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	UHorizontalBoxSlot* CcS = Btns->AddChildToHorizontalBox(MakeBtn(TEXT("Cancel"), FLinearColor(0.45f, 0.3f, 0.2f), 0));
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

void UDealWidget::RebuildStrains()
{
	if (!StrainBox) { return; }
	StrainBox->ClearChildren();
	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Ph || !C || !Inv) { return; }

	// Klanten kopen alleen VERPAKTE wiet (Bag_<strain>); bied dus je verpakte voorraad aan.
	TArray<FName> Buds;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.ItemId.ToString().StartsWith(TEXT("Bag_")) && !Buds.Contains(St.ItemId)) { Buds.Add(St.ItemId); }
	}
	const FName Offered = Ph->GetOfferedProduct();

	UHorizontalBox* RowBox = nullptr;
	int32 col = 0;
	for (int32 i = 0; i < Buds.Num(); ++i)
	{
		if (col == 0) { RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(); StrainBox->AddChildToVerticalBox(RowBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f)); }
		const bool bThis = (Buds[i] == Offered);
		const bool bWanted = (Buds[i] == C->DesiredProductId);
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->Param = i;
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		const FName Pick = Buds[i];
		B->OnAction.BindLambda([this, Pick](int32, int32) { if (UPhoneClientComponent* X = GetPhone()) { X->SetOfferedProduct(Pick); } });
		const FLinearColor Col = bThis ? FLinearColor(0.22f, 0.5f, 0.3f) : FLinearColor(0.14f, 0.16f, 0.22f);
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 8.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		St.NormalPadding = FMargin(8.f, 5.f); St.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(St);
		B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s%s T%.0f%%"), *PrettyName(Buds[i]), bWanted ? TEXT(" *") : TEXT(""), Inv->GetItemQuality(Buds[i])), 12, FLinearColor::White, true));
		UHorizontalBoxSlot* S = RowBox->AddChildToHorizontalBox(B);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		col = (col + 1) % 2;
	}
	if (Buds.Num() == 0)
	{
		StrainBox->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("(no weed in your inventory)"), 12, FLinearColor::Gray));
	}
}

void UDealWidget::UpdateLive()
{
	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	if (!Ph || !C) { return; }

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
	if (StockText)    { StockText->SetVisibility(DealVis); }
	if (ChanceText)   { ChanceText->SetVisibility(DealVis); }
	if (ChanceBar)    { ChanceBar->SetVisibility(DealVis); }
	if (RelationText) { RelationText->SetVisibility(DealVis); }
	if (PreviewText)  { PreviewText->SetVisibility(DealVis); }
	if (OfferLabel)   { OfferLabel->SetVisibility(DealVis); }
	if (StrainBox)    { StrainBox->SetVisibility(bHasWeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (SubText)      { SubText->SetVisibility(DealVis); }
	if (!bHasWeed)
	{
		// Alleen "Wants" + de melding tonen; de rest is verborgen. Klaar.
		WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants: %dx %s  (market EUR %.2f)"),
			Qty, *PrettyName(C->DesiredProductId), C->GetMarketPriceCents() / 100.f)));
		return;
	}

	WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants: %dx %s  (market EUR %.2f)"),
		Qty, *PrettyName(C->DesiredProductId), C->GetMarketPriceCents() / 100.f)));
	SubText->SetText(FText::FromString(bSub ? FString::Printf(TEXT("Offering instead: %s  (substitute)"), *PrettyName(Offered)) : FString()));

	const float Pct = float(Ask) / Market * 100.f;
	PriceText->SetText(FText::FromString(FString::Printf(TEXT("Your price: EUR %.2f / unit  (%.0f%%)   Total EUR %.2f"),
		Ask / 100.f, Pct, (Ask * Qty) / 100.f)));
	// Slider volgt het bod als de speler 'm niet vasthoudt.
	if (PriceSlider && !bSliderHeld)
	{
		PriceSlider->SetValue(FMath::Clamp((float(Ask) / Market - 0.40f) / 1.60f, 0.f, 1.f));
	}

	// Voorraad + kwaliteit van het aangeboden product.
	float Q01 = -1.f, Thc = 0.f, QPct = 0.f; int32 Stock = 0;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			Stock = Inv->GetQuantity(Offered);
			if (Stock > 0) { QPct = Inv->GetItemQualityPct(Offered); Thc = Inv->GetItemQuality(Offered); Q01 = FMath::Clamp(QPct / 100.f, 0.f, 1.f); }
		}
	}
	if (Stock >= Qty)
	{
		StockText->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.85f, 1.f)));
		StockText->SetText(FText::FromString(FString::Printf(TEXT("Your stock: %dg %s  -  THC %.0f%%  Quality %.0f%%"), Stock, *PrettyName(Offered), Thc, QPct)));
	}
	else
	{
		StockText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.5f, 0.4f)));
		StockText->SetText(FText::FromString(FString::Printf(TEXT("Your stock: %dg of %d needed - not enough %s!"), Stock, Qty, *PrettyName(Offered))));
	}

	const float Chance = bSub ? C->GetSubstituteAcceptance(Offered, Ask, Q01) : C->GetAcceptanceChance(Ask, Q01);
	const FLinearColor CCol = Chance >= 66.f ? FLinearColor::Green : (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f));
	ChanceText->SetColorAndOpacity(FSlateColor(CCol));
	ChanceText->SetText(FText::FromString(FString::Printf(TEXT("Chance they accept: %.0f%%%s"), Chance, bSub ? TEXT("  (substitute)") : TEXT(""))));
	ChanceBar->SetPercent(Chance / 100.f);
	ChanceBar->SetFillColorAndOpacity(CCol);

	RelationText->SetText(FText::FromString(FString::Printf(TEXT("Respect %.0f   Loyalty %.0f   Addiction %.0f"), C->Respect, C->Loyalty, C->Addiction)));
	float pR = 0.f, pL = 0.f, pA = 0.f;
	C->PreviewDealOutcome(Ask, Q01, (Stock > 0 ? Thc : -1.f), pR, pL, pA, bSub);
	PreviewText->SetText(FText::FromString(FString::Printf(TEXT("If accepted:  R %.0f->%.0f   L %.0f->%.0f   A %.0f->%.0f"),
		C->Respect, pR, C->Loyalty, pL, C->Addiction, pA)));
}

void UDealWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	UPhoneClientComponent* Ph = GetPhone();
	ACustomerBase* C = Ph ? Ph->GetDealCustomer() : nullptr;
	bool bOpen = Ph && Ph->IsDealOpen() && C != nullptr;

	// Loop je weg -> sluit (alleen dichtbij dealen).
	if (bOpen)
	{
		if (APawn* P = GetOwningPlayerPawn())
		{
			if (FVector::DistSquared(P->GetActorLocation(), C->GetActorLocation()) > FMath::Square(450.f))
			{
				Ph->CloseDeal();
				bOpen = false;
			}
		}
	}

	// De widget zelf blijft altijd ticken; alleen de kaart tonen/verbergen.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastCustomer = nullptr; return; }

	const FName Offered = Ph->GetOfferedProduct();
	if (C != LastCustomer.Get() || Offered != LastOffered)
	{
		LastCustomer = C; LastOffered = Offered; bSliderHeld = false;
		RebuildStrains();
	}
	UpdateLive();

	// Reset de "slider vastgehouden"-vlag als de muisknop los is (zodat 'ie het bod weer kan volgen).
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton)) { bSliderHeld = false; }
	}
}

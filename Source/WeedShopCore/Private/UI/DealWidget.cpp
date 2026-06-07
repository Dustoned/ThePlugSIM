#include "UI/DealWidget.h"
#include "World/CityDoor.h" // FriendlyNpcName fallback

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Customer/CustomerBase.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"

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
	// AutoSize: de kaart krimpt naar zijn inhoud -> geen groot leeg grijs vlak bij niet-kopers.
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, 0.f));

	// Vaste breedte (zodat de Fill-knoppen netjes uitlijnen), hoogte volgt de inhoud.
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetWidthOverride(440.f);
	CardB->SetContent(Width);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Width->SetContent(VB);

	// --- Kop: naam + status + stats (altijd zichtbaar voor ELKE NPC) ---
	NameText = WeedUI::Text(WidgetTree, TEXT("Customer"), 22, FLinearColor(0.85f, 0.95f, 1.f), false, true);
	VB->AddChildToVerticalBox(NameText);
	StateText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.65f, 0.7f, 0.8f), false, true);
	VB->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	RelationText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.75f, 0.82f, 1.f), false, true);
	VB->AddChildToVerticalBox(RelationText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// --- Dialoog-kader: wat de NPC zegt ---
	{
		UBorder* DB = WidgetTree->ConstructWidget<UBorder>();
		DB->SetBrush(WeedUI::Rounded(FLinearColor(0.10f, 0.12f, 0.16f, 1.f), 10.f));
		DB->SetPadding(FMargin(12.f, 10.f));
		DialogueText = WeedUI::Text(WidgetTree, TEXT("..."), 14, FLinearColor(0.95f, 0.95f, 0.85f), false, false);
		DialogueText->SetAutoWrapText(true);
		DB->SetContent(DialogueText);
		DialogueBox = DB;
		VB->AddChildToVerticalBox(DB)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

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

	PreviewText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.55f, 0.95f, 0.6f));
	VB->AddChildToVerticalBox(PreviewText)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Grote, duidelijke melding als je niets te verkopen hebt (verbergt de hele prijs-flow).
	NoWeedText = WeedUI::Text(WidgetTree, TEXT("You have no packaged weed to sell.\nGrow it, dry it, then bag it first."), 14, FLinearColor(1.f, 0.6f, 0.45f), false, true);
	VB->AddChildToVerticalBox(NoWeedText)->SetPadding(FMargin(0.f, 14.f, 0.f, 14.f));

	OfferLabel = WeedUI::Text(WidgetTree, TEXT("Offer another strain:"), 12, FLinearColor(0.75f, 0.8f, 0.95f));
	VB->AddChildToVerticalBox(OfferLabel);
	StrainBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(StrainBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Knoppen: Give joint (altijd) / Offer deal (alleen kopers) / Leave.
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
			else if (A == 2) { Ph->RequestGiveJoint(Ph->GetDealCustomer()); }
			else { Ph->CloseDeal(); }
		});
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(12.f, 8.f); St.PressedPadding = FMargin(12.f, 8.f);
		B->SetStyle(St);
		B->SetContent(WeedUI::Text(WidgetTree, Label, 14, FLinearColor::White, true, true));
		return B;
	};
	UWeedActionButton* GB = MakeBtn(TEXT("Give joint"), FLinearColor(0.45f, 0.35f, 0.15f), 2);
	UHorizontalBoxSlot* GS = Btns->AddChildToHorizontalBox(GB);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	GiveBtn = GB;
	UWeedActionButton* OB = MakeBtn(TEXT("Offer deal"), FLinearColor(0.2f, 0.5f, 0.28f), 1);
	UHorizontalBoxSlot* OS = Btns->AddChildToHorizontalBox(OB);
	OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	OfferBtn = OB;
	UHorizontalBoxSlot* CcS = Btns->AddChildToHorizontalBox(MakeBtn(TEXT("Leave"), FLinearColor(0.4f, 0.28f, 0.22f), 0));
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

	// Klanten kopen verpakte wiet; bied PER STRAIN aan (basis-id Bag_<strain>), ongeacht de zakje-maten.
	TArray<FName> Buds;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (!UInventoryComponent::IsBag(St.ItemId)) { continue; }
		const FName Base = FName(*FString::Printf(TEXT("Bag_%s"), *UInventoryComponent::BagStrain(St.ItemId).ToString()));
		if (!Buds.Contains(Base)) { Buds.Add(Base); }
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
		const FName StrainNm = UInventoryComponent::BagStrain(Buds[i]);
		const int32 Avail = Inv->BagGramsAvailable(StrainNm);
		float QThc = 0.f;
		for (const FInventoryStack& St2 : Inv->GetStacks()) { if (UInventoryComponent::IsBag(St2.ItemId) && UInventoryComponent::BagStrain(St2.ItemId) == StrainNm) { QThc = St2.Quality; break; } }
		B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s%s  %dg  T%.0f%%"), *PrettyName(Buds[i]), bWanted ? TEXT(" *") : TEXT(""), Avail, QThc), 12, FLinearColor::White, true));
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

	// --- Kop (altijd, voor ELKE NPC): naam, status, stats, dialoog ---
	FString NpcName = ACityDoor::FriendlyNpcName(C->NpcId); // nette fallback i.p.v. ruwe "Resident_0121"
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
		{
			float r = 0.f, l = 0.f, a = 0.f; FText N;
			if (Reg->GetStats(C->NpcId, r, l, a, N) && !N.IsEmpty()) { NpcName = N.ToString(); }
		}
	}
	if (NameText) { NameText->SetText(FText::FromString(NpcName)); }

	const bool bBuyer = (C->State == ECustomerState::WantsToOrder || C->State == ECustomerState::Negotiating);
	FString StateStr;
	switch (C->State)
	{
	case ECustomerState::WantsToOrder: StateStr = TEXT("Wants to buy"); break;
	case ECustomerState::Negotiating:  StateStr = TEXT("Negotiating"); break;
	case ECustomerState::Prospect:     StateStr = FString::Printf(TEXT("Not hooked yet  (addiction %.0f/%.0f)"), C->Addiction, C->AddictionToBuy); break;
	case ECustomerState::Served:       StateStr = TEXT("Satisfied"); break;
	default:                           StateStr = TEXT("Leaving"); break;
	}
	if (StateText) { StateText->SetText(FText::FromString(StateStr)); }
	if (RelationText)
	{
		FString Rel = FString::Printf(TEXT("Respect %.0f     Loyalty %.0f     Addiction %.0f"), C->Respect, C->Loyalty, C->Addiction);
		// Duidelijk: hoeveel respect nog nodig voor hun telefoonnummer (zodat je ze kunt appen).
		if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
			{
				if (!C->NpcId.IsNone())
				{
					if (Reg->IsUnlocked(C->NpcId)) { Rel += TEXT("\nNumber saved - you can text them"); }
					else { Rel += FString::Printf(TEXT("\nNumber: respect %.0f / %.0f to get it"), C->Respect, Reg->UnlockRespect); }
				}
			}
		}
		RelationText->SetText(FText::FromString(Rel));
	}

	// Dialoog: de server-regel (reactie op joint/deal), anders een begroeting per status.
	FString Line = C->SpeechLine;
	if (Line.IsEmpty())
	{
		switch (C->State)
		{
		case ECustomerState::Prospect: Line = TEXT("Yo... you holding? Hook me up with a taste."); break;
		case ECustomerState::WantsToOrder:
		case ECustomerState::Negotiating: Line = FString::Printf(TEXT("What's up. I need %dx %s - you got it?"), C->DesiredQuantity, *PrettyName(C->DesiredProductId)); break;
		case ECustomerState::Served: Line = TEXT("Appreciate it, man. I'm good for now."); break;
		default: Line = TEXT("..."); break;
		}
	}
	if (DialogueText) { DialogueText->SetText(FText::FromString(Line)); }
	if (OfferBtn) { OfferBtn->SetVisibility(bBuyer ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }

	if (!bBuyer)
	{
		// Geen koper: verberg de deal-sectie; alleen kop + dialoog + Give joint + Leave.
		auto Hide = [](UWidget* W) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } };
		Hide(WantsText); Hide(SubText); Hide(PriceText); Hide(PriceSlider); Hide(StockText);
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
	if (StockText)    { StockText->SetVisibility(DealVis); }
	if (ChanceText)   { ChanceText->SetVisibility(DealVis); }
	if (ChanceBar)    { ChanceBar->SetVisibility(DealVis); }
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

	const float OffThc = (Stock > 0) ? Thc : -1.f;
	const float Chance = bSub ? C->GetSubstituteAcceptance(Offered, Ask, Q01, OffThc) : C->GetAcceptanceChance(Ask, Q01, OffThc);
	const FLinearColor CCol = Chance >= 66.f ? FLinearColor::Green : (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f));
	ChanceText->SetColorAndOpacity(FSlateColor(CCol));
	ChanceText->SetText(FText::FromString(FString::Printf(TEXT("Chance they accept: %.0f%%%s"), Chance, bSub ? TEXT("  (substitute)") : TEXT(""))));
	ChanceBar->SetPercent(Chance / 100.f);
	ChanceBar->SetFillColorAndOpacity(CCol);

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

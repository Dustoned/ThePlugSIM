#include "UI/DealWidget.h"
#include "WeedShopCore.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	// --- Kop: naam + status + stats (altijd zichtbaar voor ELKE NPC) ---
	NameText = WeedUI::Text(WidgetTree, TEXT("Customer"), 22, WeedUI::ColAccent(), false, true);
	VB->AddChildToVerticalBox(NameText);
	StateText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColTextDim(), false, true);
	VB->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	RelationText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim(), false, true);
	VB->AddChildToVerticalBox(RelationText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// Klant-tier + XP-balk naar de volgende tier.
	TierText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColTextDim(), false, true);
	VB->AddChildToVerticalBox(TierText)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	TierBar = WidgetTree->ConstructWidget<UProgressBar>();
	TierBar->SetFillColorAndOpacity(WeedUI::ColAccent());
	VB->AddChildToVerticalBox(TierBar)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	{ USizeBox* DivSz = WidgetTree->ConstructWidget<USizeBox>(); DivSz->SetHeightOverride(2.f); UBorder* Div = WidgetTree->ConstructWidget<UBorder>(); Div->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(0.75f), 1.f)); DivSz->SetContent(Div); VB->AddChildToVerticalBox(DivSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f)); }

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

	WantsText = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColText());
	VB->AddChildToVerticalBox(WantsText)->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));

	SubText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(SubText);

	PriceText = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColText());
	VB->AddChildToVerticalBox(PriceText)->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));

	PriceSlider = WidgetTree->ConstructWidget<USlider>();
	PriceSlider->SetSliderHandleColor(WeedUI::ColAccent());
	PriceSlider->SetSliderBarColor(WeedUI::ColSlot());
	PriceSlider->OnValueChanged.AddDynamic(this, &UDealWidget::OnPriceSlider);
	VB->AddChildToVerticalBox(PriceSlider)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	StockText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(StockText)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));

	ChanceText = WeedUI::Text(WidgetTree, TEXT(""), 14, WeedUI::ColGood(), false, true);
	VB->AddChildToVerticalBox(ChanceText);
	ChanceBar = WidgetTree->ConstructWidget<UProgressBar>();
	ChanceBar->SetFillColorAndOpacity(WeedUI::ColGood());
	VB->AddChildToVerticalBox(ChanceBar)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	PreviewText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColGood());
	VB->AddChildToVerticalBox(PreviewText)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Grote, duidelijke melding als je niets te verkopen hebt (verbergt de hele prijs-flow).
	NoWeedText = WeedUI::Text(WidgetTree, TEXT("No bagged weed.\nGrow -> dry -> bag first."), 14, WeedUI::ColWarn(), false, true);
	VB->AddChildToVerticalBox(NoWeedText)->SetPadding(FMargin(0.f, 14.f, 0.f, 14.f));

	OfferLabel = WeedUI::Text(WidgetTree, TEXT("Offer another strain:"), 12, WeedUI::ColTextDim());
	VB->AddChildToVerticalBox(OfferLabel);
	StrainBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(StrainBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));

	// Joint-kiezer (verborgen tot je "Give joint" klikt): kies WELKE joint je geeft, zonder te scrollen.
	JointPickerBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(JointPickerBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));
	JointPickerBox->SetVisibility(ESlateVisibility::Collapsed);

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
	UWeedActionButton* GB = MakeBtn(TEXT("Give joint"), WeedUI::ColAccentDim(), 2);
	UHorizontalBoxSlot* GS = Btns->AddChildToHorizontalBox(GB);
	GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	GiveBtn = GB;
	UWeedActionButton* OB = MakeBtn(TEXT("Offer deal"), WeedUI::ColAccent(), 1);
	UHorizontalBoxSlot* OS = Btns->AddChildToHorizontalBox(OB);
	OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	OfferBtn = OB;
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

void UDealWidget::StyleStrainCell(UWeedActionButton* B, bool bSelected)
{
	if (!B) { return; }
	const FLinearColor Col = bSelected ? WeedUI::ColAccentDim() : WeedUI::ColSlotEmpty();
	FButtonStyle St;
	St.Normal = WeedUI::Rounded(Col, 8.f);
	St.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
	St.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
	St.NormalPadding = FMargin(8.f, 5.f); St.PressedPadding = FMargin(8.f, 5.f);
	B->SetStyle(St);
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
	if (!StrainBox) { return; }
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

	// Rij-containers op maat brengen: elke rij bevat max 2 cellen. Groeien/krimpen -> geen ClearChildren.
	const int32 NeededRows = (Buds.Num() + 1) / 2;
	while (StrainRows.Num() < NeededRows)
	{
		UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
		StrainBox->AddChildToVerticalBox(RowBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		StrainRows.Add(RowBox);
	}
	while (StrainRows.Num() > NeededRows)
	{
		const int32 Last = StrainRows.Num() - 1;
		if (StrainRows[Last]) { StrainRows[Last]->RemoveFromParent(); }
		StrainRows.RemoveAt(Last);
	}

	// Cel-pool op maat brengen (persistent). Nieuwe cellen krijgen een sentinel-sig zodat ze eerst gevuld worden.
	while (StrainCells.Num() < Buds.Num())
	{
		const int32 idx = StrainCells.Num();
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		// De lambda leest bij klik de actuele id uit de pool (StrainCellIds), zodat een herbruikte cel de juiste strain kiest.
		B->OnAction.BindLambda([this, idx](int32, int32)
		{
			if (UPhoneClientComponent* X = GetPhone())
			{
				if (StrainCellIds.IsValidIndex(idx) && !StrainCellIds[idx].IsNone()) { X->SetOfferedProduct(StrainCellIds[idx]); }
			}
		});
		UHorizontalBox* RowBox = StrainRows.IsValidIndex(idx / 2) ? StrainRows[idx / 2].Get() : nullptr;
		if (RowBox)
		{
			UHorizontalBoxSlot* S = RowBox->AddChildToHorizontalBox(B);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		StrainCells.Add(B); StrainCellIds.Add(NAME_None); StrainCellSigs.Add(TEXT("\x01"));
	}
	while (StrainCells.Num() > Buds.Num())
	{
		const int32 Last = StrainCells.Num() - 1;
		if (StrainCells[Last]) { StrainCells[Last]->RemoveFromParent(); }
		StrainCells.RemoveAt(Last); StrainCellIds.RemoveAt(Last); StrainCellSigs.RemoveAt(Last);
	}

	// Per-cel diff: alleen de label opnieuw zetten als grammen/thc/wanted (of de getoonde strain) wijzigde.
	// De selectie-stijl wordt hier NIET meegediffed -> die gaat via RefreshStrainSelection (2 knoppen restyle).
	for (int32 i = 0; i < Buds.Num(); ++i)
	{
		UWeedActionButton* B = StrainCells.IsValidIndex(i) ? StrainCells[i].Get() : nullptr;
		if (!B) { continue; }
		const FName StrainNm = UInventoryComponent::BagStrain(Buds[i]);
		const int32 Avail = Inv->BagGramsAvailable(StrainNm);
		float QThc = 0.f;
		for (const FInventoryStack& St2 : Inv->GetStacks()) { if (UInventoryComponent::IsBag(St2.ItemId) && UInventoryComponent::BagStrain(St2.ItemId) == StrainNm) { QThc = St2.Quality; break; } }
		const bool bWanted = (Buds[i] == C->DesiredProductId);
		const FString Sig = FString::Printf(TEXT("%s|%d|%.0f|%d"), *Buds[i].ToString(), Avail, QThc, bWanted ? 1 : 0);
		if (StrainCellIds[i] != Buds[i] || StrainCellSigs[i] != Sig)
		{
			StrainCellIds[i] = Buds[i];
			StrainCellSigs[i] = Sig;
			B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s%s  %dg  T%.0f%%"), *PrettyName(Buds[i]), bWanted ? TEXT(" *") : TEXT(""), Avail, QThc), 12, WeedUI::ColText(), true));
			StyleStrainCell(B, Buds[i] == Offered); // stijl meteen goed bij (her)vulling
		}
	}

	// Selectie-highlight bijwerken (in-place restyle van de betrokken knoppen).
	RefreshStrainSelection();
}

void UDealWidget::RefreshStrainSelection()
{
	UPhoneClientComponent* Ph = GetPhone();
	const FName Offered = Ph ? Ph->GetOfferedProduct() : NAME_None;
	if (Offered == StrainSelectedId) { return; }
	// Alleen de vorige- en de nieuwe-selectie herstylen (rest blijft ongemoeid) -> geen rebuild, geen flash.
	for (int32 i = 0; i < StrainCells.Num(); ++i)
	{
		if (!StrainCellIds.IsValidIndex(i)) { continue; }
		const FName Id = StrainCellIds[i];
		if (Id == StrainSelectedId || Id == Offered) { StyleStrainCell(StrainCells[i].Get(), Id == Offered); }
	}
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
	if (!JointPickerBox) { return; }
	APawn* P = GetOwningPlayerPawn();
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }

	// Titel 1x bouwen (persistent) i.p.v. elke open opnieuw.
	if (!JointPickerTitle)
	{
		JointPickerTitle = WeedUI::Text(WidgetTree, TEXT("Give which joint?"), 12, WeedUI::ColTextDim());
		JointPickerBox->AddChildToVerticalBox(JointPickerTitle);
	}

	// Zichtbare joints verzamelen (id + label-data).
	struct FJointRow { FName Id; FString Label; };
	TArray<FJointRow> Rows;
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.Quantity <= 0 || !St.ItemId.ToString().StartsWith(TEXT("Joint_"))) { continue; }
		const FName Id = St.ItemId;
		const FName Strain = UInventoryComponent::JointStrain(Id);
		const int32 Grams = UInventoryComponent::JointGrams(Id);
		const float QPct = Inv->GetItemQualityPct(Id);
		Rows.Add({ Id, FString::Printf(TEXT("%s   %dg   Q%.0f%%   (x%d)"),
			Strain.IsNone() ? TEXT("Joint") : *Strain.ToString(), Grams, QPct, St.Quantity) });
	}

	// Cel-pool op maat brengen (persistent) -> geen ClearChildren bij (her)openen.
	while (JointCells.Num() < Rows.Num())
	{
		const int32 idx = JointCells.Num();
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		// De lambda vangt de pool-index en leest bij klik de actuele joint-id uit de parallelle JointCellIds
		// (zodat een herbruikte cel de juiste joint geeft, ook na een lijst-wijziging).
		B->Param = idx;
		B->OnAction.BindLambda([this, idx](int32, int32)
		{
			if (JointCellIds.IsValidIndex(idx) && !JointCellIds[idx].IsNone())
			{
				if (UPhoneClientComponent* X = GetPhone()) { X->RequestGiveJointId(X->GetDealCustomer(), JointCellIds[idx]); }
			}
			if (JointPickerBox) { JointPickerBox->SetVisibility(ESlateVisibility::Collapsed); }
		});
		const FLinearColor Col = WeedUI::ColSlotEmpty();
		FButtonStyle Sty;
		Sty.Normal = WeedUI::Rounded(Col, 8.f);
		Sty.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		Sty.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		Sty.NormalPadding = FMargin(8.f, 5.f); Sty.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(Sty);
		JointPickerBox->AddChildToVerticalBox(B)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		JointCells.Add(B); JointCellIds.Add(NAME_None); JointCellSigs.Add(TEXT("\x01"));
	}
	while (JointCells.Num() > Rows.Num())
	{
		const int32 Last = JointCells.Num() - 1;
		if (JointCells[Last]) { JointCells[Last]->RemoveFromParent(); } // echt losmaken -> geen orphan-cellen die ophopen
		JointCells.RemoveAt(Last); JointCellIds.RemoveAt(Last); JointCellSigs.RemoveAt(Last);
	}

	// Per-cel diff: alleen een gewijzigde joint-rij krijgt nieuwe inhoud.
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		UWeedActionButton* B = JointCells.IsValidIndex(i) ? JointCells[i].Get() : nullptr;
		if (!B) { continue; }
		B->SetVisibility(ESlateVisibility::Visible);
		const FString Sig = FString::Printf(TEXT("%s|%s"), *Rows[i].Id.ToString(), *Rows[i].Label);
		if (JointCellSigs[i] != Sig)
		{
			JointCellSigs[i] = Sig;
			JointCellIds[i] = Rows[i].Id;
			B->SetContent(WeedUI::Text(WidgetTree, Rows[i].Label, 12, WeedUI::ColText(), true));
		}
	}

	// Lege-melding tonen/verbergen zonder ClearChildren.
	if (Rows.Num() == 0)
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

	// Klant-tier + XP-balk naar de volgende tier (Casual -> Whale; bij Whale "(max)").
	if (TierText && TierBar)
	{
		float Frac = 0.f; FString TLbl = TEXT("Tier: Casual");
		if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
			{
				if (!C->NpcId.IsNone())
				{
					const int32 Tier = Reg->GetCustomerTier(C->NpcId);
					Frac = Reg->GetTierProgress01(C->NpcId);
					TLbl = (Tier >= 5)
						? FString::Printf(TEXT("Tier: %s  (max)"), *UNpcRegistryComponent::TierName(Tier))
						: FString::Printf(TEXT("Tier: %s  ->  %s"), *UNpcRegistryComponent::TierName(Tier), *UNpcRegistryComponent::TierName(Tier + 1));
				}
			}
		}
		TierText->SetText(FText::FromString(TLbl));
		TierBar->SetPercent(Frac);
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
	if (WantsText)    { WantsText->SetVisibility(ESlateVisibility::HitTestInvisible); } // koper: altijd tonen (kan Collapsed staan door een vorige niet-koper)
	if (!bHasWeed)
	{
		// Alleen "Wants" + de melding tonen; de rest is verborgen. Klaar.
		WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants: %dg %s  (market EUR %d)"),
			Qty, *PrettyName(C->DesiredProductId), (int32)(WeedRoundEuros((int64)C->GetMarketPriceCents()) / 100))));
		return;
	}

	WantsText->SetText(FText::FromString(FString::Printf(TEXT("Wants: %dx %s  (market EUR %d)"),
		Qty, *PrettyName(C->DesiredProductId), (int32)(WeedRoundEuros((int64)C->GetMarketPriceCents()) / 100))));
	SubText->SetText(FText::FromString(bSub ? FString::Printf(TEXT("Offering instead: %s  (substitute)"), *PrettyName(Offered)) : FString()));

	const float Pct = float(Ask) / Market * 100.f;
	PriceText->SetText(FText::FromString(FString::Printf(TEXT("Your price: EUR %d / unit  (%.0f%%)   Total EUR %d"),
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
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			Stock = Inv->BagStockGrams(UInventoryComponent::BagStrain(Offered), Thc, QPct);
			if (Stock > 0) { Q01 = FMath::Clamp(QPct / 100.f, 0.f, 1.f); }
		}
	}
	if (Stock >= Qty)
	{
		StockText->SetColorAndOpacity(FSlateColor(WeedUI::ColTextDim()));
		StockText->SetText(FText::FromString(FString::Printf(TEXT("Your stock: %dg %s  -  THC %.0f%%  Quality %.0f%%"), Stock, *PrettyName(Offered), Thc, QPct)));
	}
	else
	{
		StockText->SetColorAndOpacity(FSlateColor(WeedUI::ColWarn()));
		StockText->SetText(FText::FromString(FString::Printf(TEXT("Your stock: %dg of %d needed - not enough %s!"), Stock, Qty, *PrettyName(Offered))));
	}

	const float OffThc = (Stock > 0) ? Thc : -1.f;
	const float Chance = bSub ? C->GetSubstituteAcceptance(Offered, Ask, Q01, OffThc) : C->GetAcceptanceChance(Ask, Q01, OffThc);
	const FLinearColor CCol = Chance >= 66.f ? WeedUI::ColGood() : (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : WeedUI::ColWarn());
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
		LastCustomer = nullptr; return;
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
		LastCustomer = C; bSliderHeld = false;
		if (JointPickerBox) { JointPickerBox->SetVisibility(ESlateVisibility::Collapsed); } // kiezer dicht bij nieuwe klant
	}
	if (Offered != LastOffered) { bSliderHeld = false; } // ander product gekozen -> slider mag het bod weer volgen
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
	if (Card) { Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } // nu pas zichtbaar, al op echte hoogte

	// Reset de "slider held"-vlag als de muisknop los is (zodat 'ie het bod weer kan volgen).
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton)) { bSliderHeld = false; }
	}
}

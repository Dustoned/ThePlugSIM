#include "UI/RollWidget.h"

#include "UI/WeedUiStyle.h"
#include "UI/WeedItemPickGrid.h"
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
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Pawn.h"

void URollWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

void URollWidget::OnGramSlider(float V)
{
	if (!PhoneComp.IsValid() || !GramSlider) { return; }
	const int32 MaxG = FMath::Max(1, PhoneComp->GetMaxJointGrams());
	const int32 g = FMath::Clamp(FMath::RoundToInt(V), 1, MaxG);
	if (!FMath::IsNearlyEqual(GramSlider->GetValue(), (float)g)) { GramSlider->SetValue((float)g); } // snap de handle op hele grammen
	PhoneComp->SetRollGrams(g);
}

namespace
{
	UWeedActionButton* RollBtn(UWidgetTree* Tree, const FLinearColor& Col, float Radius, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, Radius);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, Radius);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, Radius);
		S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(S);
		return B;
	}
}

TSharedRef<SWidget> URollWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void URollWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RollCard"));
	{ FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.98f), 24.f); Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f)); CardB->SetBrush(Br); }
	CardB->SetPadding(FMargin(20.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	// Hoger dan voorheen (320) om ook het "Pick your weed"-strain-grid (2 rijen, eigen scroll) te huisvesten
	// naast de grams-stepper + sterkte-preview + acties.
	CS->SetSize(FVector2D(520.f, 512.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Body);
}

void URollWidget::BuildContentOnce()
{
	// Bouw de VASTE structuur één keer op; daarna alleen nog in-place bijwerken (UpdateContent).
	if (!Body || FullPane) { return; } // FullPane != null => al gebouwd.

	// --- Kop (titel + divider), gedeeld door beide panes ---
	Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("ROLL JOINT"), 18, WeedUI::ColAccent(), false, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	{ USizeBox* DivSz = WidgetTree->ConstructWidget<USizeBox>(); DivSz->SetHeightOverride(2.f); UBorder* Div = WidgetTree->ConstructWidget<UBorder>(); Div->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(0.75f), 1.f)); DivSz->SetContent(Div); Body->AddChildToVerticalBox(DivSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f)); }

	// --- "No papers" pane (vooraf gebouwd; getoggeld via Visibility) ---
	NoPapersPane = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		NoPapersPane->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("No papers! Buy a pack from Suppliers (phone)."), 13, WeedUI::ColWarn()))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		UWeedActionButton* CloseB = RollBtn(WidgetTree, WeedUI::ColSlot(), 10.f, [this]() { if (PhoneComp.IsValid()) { PhoneComp->ToggleRollUI(); } });
		CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 13, WeedUI::ColText(), true));
		NoPapersPane->AddChildToVerticalBox(CloseB);
	}
	Body->AddChildToVerticalBox(NoPapersPane);

	// --- Volledige pane (strain-keuze + grams-keuze + sterkte + acties), vooraf gebouwd ---
	FullPane = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		// Strain-keuze: icoon-grid met alle Bud_-stacks van de speler (welke wiet rol je?).
		FullPane->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Pick your weed"), 13, WeedUI::ColText(), false, true))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		StrainGrid = WidgetTree->ConstructWidget<UWeedItemPickGrid>();
		StrainGrid->CellSize = 86.f;          // config-velden VOOR de eerste SetItems
		StrainGrid->MaxVisibleRows = 2;
		StrainGrid->bShowSelection = true;
		StrainGrid->OnPick = [this](FName Id, int32)
		{
			if (PhoneComp.IsValid()) { PhoneComp->SetRollStrain(Id); UpdateContent(); } // directe feedback; tick-sig ververst ook
		};
		FullPane->AddChildToVerticalBox(StrainGrid)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

		GramsLabel = WeedUI::Text(WidgetTree, FString(), 13, WeedUI::ColText());
		FullPane->AddChildToVerticalBox(GramsLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		// Gram-keuze: SLIDER (1..MaxG, snapt op hele grammen) - zelfde snappy look als de deal-sliders.
		GramsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		FullPane->AddChildToVerticalBox(GramsRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		GramSlider = WidgetTree->ConstructWidget<USlider>();
		GramSlider->SetSliderHandleColor(WeedUI::ColAccent());
		GramSlider->SetSliderBarColor(WeedUI::ColSlot());
		GramSlider->SetMinValue(1.f); GramSlider->SetMaxValue(10.f); GramSlider->SetStepSize(1.f); GramSlider->SetValue(1.f);
		{ FSliderStyle SS = GramSlider->GetWidgetStyle(); SS.SetBarThickness(8.f); GramSlider->SetWidgetStyle(SS); }
		GramSlider->OnValueChanged.AddDynamic(this, &URollWidget::OnGramSlider);
		{
			USizeBox* SH = WidgetTree->ConstructWidget<USizeBox>(); SH->SetHeightOverride(20.f); SH->SetContent(GramSlider);
			UHorizontalBoxSlot* GS = GramsRow->AddChildToHorizontalBox(SH);
			GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GS->SetVerticalAlignment(VAlign_Center);
		}

		// --- Sterkte (verwachte high) — schaalt mee met gram + THC% + kwaliteit% ---
		StrengthLabel = WeedUI::Text(WidgetTree, FString(), 13, WeedUI::ColText(), false, true);
		FullPane->AddChildToVerticalBox(StrengthLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		HintLabel = WeedUI::Text(WidgetTree, TEXT("More grams = stronger joint."), 10, WeedUI::ColTextDim());
		FullPane->AddChildToVerticalBox(HintLabel)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
		BarSz->SetHeightOverride(18.f);
		StrengthBar = WidgetTree->ConstructWidget<UProgressBar>();
		BarSz->SetContent(StrengthBar);
		FullPane->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

		// Actie-knoppen: Load (alleen zichtbaar met wiet) + Close.
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		LoadBtn = RollBtn(WidgetTree, WeedUI::ColAccent(), 10.f, [this]() { if (PhoneComp.IsValid()) { PhoneComp->LoadRoll(); } });
		LoadBtnText = WeedUI::Text(WidgetTree, FString(), 13, WeedUI::ColText(), true);
		LoadBtn->SetContent(LoadBtnText);
		UHorizontalBoxSlot* RS = Btns->AddChildToHorizontalBox(LoadBtn);
		RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); RS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));

		UWeedActionButton* CloseB = RollBtn(WidgetTree, WeedUI::ColSlot(), 10.f, [this]() { if (PhoneComp.IsValid()) { PhoneComp->ToggleRollUI(); } });
		CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 13, WeedUI::ColText(), true));
		Btns->AddChildToHorizontalBox(CloseB);
		FullPane->AddChildToVerticalBox(Btns);
	}
	Body->AddChildToVerticalBox(FullPane);
}

void URollWidget::UpdateContent()
{
	if (!Body || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	BuildContentOnce();

	const int32 MaxG = Ph->GetMaxJointGrams();

	// Geen papers -> alleen de "No papers"-pane tonen; de volle pane blijft verborgen (geen teardown).
	if (MaxG <= 0)
	{
		if (NoPapersPane) { NoPapersPane->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		if (FullPane)     { FullPane->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	if (NoPapersPane) { NoPapersPane->SetVisibility(ESlateVisibility::Collapsed); }
	if (FullPane)     { FullPane->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }

	// --- Strain-keuze: alle Bud_-stacks van de speler in het icoon-grid (SetItems diff't intern) ---
	if (StrainGrid)
	{
		TArray<FWeedPickItem> Items;
		if (const APawn* P = GetOwningPlayerPawn())
		{
			if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
			{
				for (const FInventoryStack& S : Inv->GetStacks())
				{
					if (!S.ItemId.ToString().StartsWith(TEXT("Bud_"))) { continue; }
					FWeedPickItem It;
					It.Id = S.ItemId;
					It.Badge = FString::Printf(TEXT("%dg"), S.Quantity);
					It.Tooltip = FString::Printf(TEXT("%s\n%dg - THC %.0f%% Q %.0f%%"),
						*WeedUI::PrettyItemName(S.ItemId), S.Quantity, S.Quality, S.QualityPct);
					Items.Add(It);
				}
			}
		}
		StrainGrid->SetItems(Items, Ph->GetRollStrain());
	}

	const int32 G = FMath::Clamp(Ph->GetRollGrams(), 1, MaxG);

	if (GramsLabel)
	{
		GramsLabel->SetText(FText::FromString(
			FString::Printf(TEXT("Grams per joint: %d   (your papers allow up to %dg)"), G, MaxG)));
	}

	// Slider-bereik + stand in-place bijwerken (SetValue/SetMaxValue vuren OnValueChanged NIET -> geen feedback-lus).
	if (GramSlider)
	{
		if (!FMath::IsNearlyEqual(GramSlider->GetMaxValue(), (float)MaxG)) { GramSlider->SetMaxValue((float)FMath::Max(1, MaxG)); }
		if (FMath::RoundToInt(GramSlider->GetValue()) != G) { GramSlider->SetValue((float)G); }
	}

	// --- Sterkte (verwachte high) — schaalt mee met gram + THC% + kwaliteit% ---
	float Thc = 0.f, Qpct = 0.f;
	const bool bHasWeed = Ph->GetRollWeedInfo(G, Thc, Qpct);
	const float Intensity = bHasWeed ? UPhoneClientComponent::JointIntensity(G, Thc, Qpct) : 0.f;

	if (StrengthLabel)
	{
		if (bHasWeed)
		{
			const FLinearColor LblCol = Intensity >= 0.6f ? FLinearColor(0.5f, 1.f, 0.55f)
				: (Intensity >= 0.3f ? FLinearColor(1.f, 0.75f, 0.3f) : FLinearColor(1.f, 0.55f, 0.45f));
			StrengthLabel->SetText(FText::FromString(
				FString::Printf(TEXT("Joint strength: %.0f%%   (%dg of %.0f%% quality weed - %.0f%% THC)"), Intensity * 100.f, G, Qpct, Thc)));
			StrengthLabel->SetColorAndOpacity(FSlateColor(LblCol));
		}
		else
		{
			StrengthLabel->SetText(FText::FromString(
				FString::Printf(TEXT("No weed for a %dg joint - grow/buy buds first."), G)));
			StrengthLabel->SetColorAndOpacity(FSlateColor(WeedUI::ColWarn()));
		}
	}
	// De "More grams"-hint hoort bij de wiet-tak (stond daar in de originele layout).
	if (HintLabel) { HintLabel->SetVisibility(bHasWeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }

	if (StrengthBar)
	{
		StrengthBar->SetFillColorAndOpacity(Intensity >= 0.6f ? FLinearColor(0.4f, 0.9f, 0.45f)
			: (Intensity >= 0.3f ? FLinearColor(0.95f, 0.7f, 0.25f) : FLinearColor(0.9f, 0.45f, 0.4f)));
		StrengthBar->SetPercent(Intensity);
	}

	// Load-knop: alleen tonen met wiet -> geen lege joint kunnen laden/rollen.
	if (LoadBtn)
	{
		LoadBtn->SetVisibility(bHasWeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bHasWeed && LoadBtnText)
		{
			LoadBtnText->SetText(FText::FromString(
				FString::Printf(TEXT("Load  (%dg)  -  then hold right-click to roll"), G)));
		}
	}
}

void URollWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsRollOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastGrams = -1; LastMaxG = -2; LastWeedSig = -1; return; }

	// Alleen bijwerken als er iets veranderde -> geen per-frame widget-werk (update is in-place, geen flash/scroll-sprong).
	// Naast grams + papers-capaciteit ook de WIET-VOORRAAD + de GEKOZEN STRAIN in de gate: komt er wiet bij / gaat weg,
	// of kiest de speler een andere strain terwijl het scherm open staat (droogrek klaar, oogst, delivery-pakket, klik in
	// het grid) dan moeten het strain-grid + de sterkte-preview + Load-knop mee-updaten, ook bij ongewijzigd G/MaxG.
	const int32 G = PhoneComp->GetRollGrams();
	const int32 MaxG = PhoneComp->GetMaxJointGrams();
	float Thc = 0.f, Qpct = 0.f;
	int32 WeedSig = PhoneComp->GetRollWeedInfo(G, Thc, Qpct)
		? (1 + (int32)(Thc * 10.f) * 131 + (int32)(Qpct * 10.f) * 17) : 0;
	// Gekozen strain in de sig (grid-selectie moet volgen bij een klik/save-load).
	WeedSig = WeedSig * 486187739 + GetTypeHash(PhoneComp->GetRollStrain());
	// Complete Bud_-voorraad in de sig (som van id-hash * qty): een strain die bijkomt/leegraakt ververst het grid,
	// ook als de nu-gekozen strain zelf niet wijzigt.
	if (const APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			for (const FInventoryStack& S : Inv->GetStacks())
			{
				if (!S.ItemId.ToString().StartsWith(TEXT("Bud_"))) { continue; }
				WeedSig = WeedSig * 33 + (int32)(GetTypeHash(S.ItemId) * (uint32)(S.Quantity + 1));
			}
		}
	}
	if (G != LastGrams || MaxG != LastMaxG || WeedSig != LastWeedSig)
	{
		LastGrams = G; LastMaxG = MaxG; LastWeedSig = WeedSig;
		UpdateContent();
	}
}

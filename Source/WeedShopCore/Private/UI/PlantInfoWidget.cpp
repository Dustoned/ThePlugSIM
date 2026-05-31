#include "UI/PlantInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/SoilTypes.h"

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
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UPlantInfoWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPlantInfoWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlantCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.07f, 0.06f, 0.95f), 18.f));
	CardB->SetPadding(FMargin(16.f, 12.f, 16.f, 12.f));
	CardB->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -60.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	TitleText = WeedUI::Text(WidgetTree, TEXT("Pot"), 15, FLinearColor(0.7f, 1.f, 0.7f), false, true);
	VB->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Groei-header: "Growth" links + resterende tijd rechts.
	UHorizontalBox* GHead = WidgetTree->ConstructWidget<UHorizontalBox>();
	GrowthLabel = WeedUI::Text(WidgetTree, TEXT("Growth"), 12, FLinearColor(0.85f, 0.9f, 1.f));
	UHorizontalBoxSlot* GLS = GHead->AddChildToHorizontalBox(GrowthLabel);
	GLS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); GLS->SetVerticalAlignment(VAlign_Center);
	GrowthTimeText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.7f, 0.85f, 1.f));
	UHorizontalBoxSlot* GTS = GHead->AddChildToHorizontalBox(GrowthTimeText);
	GTS->SetHorizontalAlignment(HAlign_Right); GTS->SetVerticalAlignment(VAlign_Center);
	GrowthHeader = GHead;
	VB->AddChildToVerticalBox(GHead);
	GrowthBox = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(GrowthBox);
	for (int32 i = 0; i < 6; ++i)
	{
		UProgressBar* B = WidgetTree->ConstructWidget<UProgressBar>();
		B->SetFillColorAndOpacity(FLinearColor(0.45f, 0.9f, 0.4f));
		UVerticalBoxSlot* S = GrowthBox->AddChildToVerticalBox(B);
		S->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		GrowthBars.Add(B);
	}

	auto MakeBarRow = [this](UVerticalBox* Parent, UProgressBar*& OutBar, UTextBlock*& OutText, const FLinearColor& Fill) -> UWidget*
	{
		UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>();
		OutText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.85f, 0.9f, 1.f));
		Row->AddChildToVerticalBox(OutText);
		OutBar = WidgetTree->ConstructWidget<UProgressBar>();
		OutBar->SetFillColorAndOpacity(Fill);
		Row->AddChildToVerticalBox(OutBar);
		UVerticalBoxSlot* RS = Parent->AddChildToVerticalBox(Row);
		RS->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		return Row;
	};
	UProgressBar* WB = nullptr; UTextBlock* WT = nullptr;
	WaterRow = MakeBarRow(VB, WB, WT, FLinearColor(0.3f, 0.7f, 1.f)); WaterBar = WB; WaterText = WT;
	UProgressBar* HB = nullptr; UTextBlock* HT = nullptr;
	HealthRow = MakeBarRow(VB, HB, HT, FLinearColor(0.4f, 0.9f, 0.4f)); HealthBar = HB; HealthText = HT;

	YieldText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.9f, 0.9f, 0.7f));
	VB->AddChildToVerticalBox(YieldText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	SoilText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.6f, 0.9f, 0.6f));
	VB->AddChildToVerticalBox(SoilText);
	HintText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(1.f, 0.95f, 0.5f));
	VB->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
}

void UPlantInfoWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	APawn* P = GetOwningPlayerPawn();
	if (!P) { if (Card) { Card->SetVisibility(ESlateVisibility::Collapsed); } return; }

	// Geen kaart als er een UI open is.
	bool bUiOpen = false;
	if (const UPhoneClientComponent* Ph = P->FindComponentByClass<UPhoneClientComponent>())
	{
		bUiOpen = Ph->IsOpen() || Ph->IsDealOpen() || Ph->IsInventoryOpen() || Ph->IsRollOpen() || Ph->IsPotUpgradeOpen();
	}

	AGrowPlant* Plant = nullptr;
	if (const UInteractionComponent* IC = P->FindComponentByClass<UInteractionComponent>())
	{
		Plant = Cast<AGrowPlant>(IC->GetFocusedActor());
	}

	const bool bShow = Plant && !bUiOpen;
	if (Card) { Card->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bShow) { return; }

	const bool bPlanted = Plant->IsPlanted();
	const int32 NumSlots = Plant->GetNumSlots();

	if (bPlanted)
	{
		// Titel = naam van de plant + basis-THC%, met aantal plekken als die >1 is.
		const FString StrainName = Plant->GetPrimaryStrainName().ToString();
		FString Title = FString::Printf(TEXT("%s   %.0f%% THC"), *StrainName, Plant->GetPrimaryBaseThc());
		if (NumSlots > 1) { Title += FString::Printf(TEXT("   (%d/%d)"), Plant->GetPlantedCount(), NumSlots); }
		TitleText->SetText(FText::FromString(Title));
		if (GrowthHeader) { GrowthHeader->SetVisibility(ESlateVisibility::HitTestInvisible); }
		if (GrowthTimeText)
		{
			const int32 Rem = FMath::CeilToInt(Plant->GetSecondsRemaining());
			GrowthTimeText->SetText(FText::FromString(Rem > 0
				? FString::Printf(TEXT("%d:%02d left"), Rem / 60, Rem % 60)
				: TEXT("READY")));
			GrowthTimeText->SetColorAndOpacity(FSlateColor(Rem > 0 ? FLinearColor(0.7f, 0.85f, 1.f) : FLinearColor(0.45f, 0.95f, 0.4f)));
		}
		GrowthBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		for (int32 i = 0; i < GrowthBars.Num(); ++i)
		{
			const bool bSlot = (i < NumSlots) && Plant->IsSlotPlanted(i);
			GrowthBars[i]->SetVisibility(bSlot ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (bSlot)
			{
				GrowthBars[i]->SetPercent(Plant->GetSlotFraction(i));
				GrowthBars[i]->SetFillColorAndOpacity(FLinearColor(0.45f, 0.9f, 0.4f));
			}
		}
		if (WaterRow) { WaterRow->SetVisibility(ESlateVisibility::HitTestInvisible); }
		if (HealthRow) { HealthRow->SetVisibility(ESlateVisibility::HitTestInvisible); }
		const float Wtr = Plant->GetWaterLevel();
		WaterBar->SetPercent(Wtr);
		WaterText->SetText(FText::FromString(FString::Printf(TEXT("Water  %.0f%%"), Wtr * 100.f)));
		const float Care = Plant->GetCareMultiplier();
		HealthBar->SetPercent(Care);
		HealthBar->SetFillColorAndOpacity(Care >= 0.8f ? FLinearColor(0.4f, 0.9f, 0.4f) : (Care >= 0.5f ? FLinearColor(1.f, 0.7f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f)));
		HealthText->SetText(FText::FromString(FString::Printf(TEXT("Health  %.0f%%"), Care * 100.f)));
		YieldText->SetVisibility(ESlateVisibility::HitTestInvisible);
		YieldText->SetText(FText::FromString(FString::Printf(TEXT("Expected: %.0fg @ %.0f%% THC"), Plant->GetEstimatedTotalYield(), Plant->GetEstimatedThcPercent())));
	}
	else
	{
		TitleText->SetText(FText::FromString(TEXT("Empty pot")));
		if (GrowthHeader) { GrowthHeader->SetVisibility(ESlateVisibility::Collapsed); }
		GrowthBox->SetVisibility(ESlateVisibility::Collapsed);
		if (WaterRow) { WaterRow->SetVisibility(ESlateVisibility::Collapsed); }
		if (HealthRow) { HealthRow->SetVisibility(ESlateVisibility::Collapsed); }
		YieldText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Plant->HasSoil())
	{
		FSoilDef Sd; const FString Sn = GetSoilDef(Plant->GetSoilId(), Sd) ? Sd.DisplayName : Plant->GetSoilId().ToString();
		SoilText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.9f, 0.6f)));
		SoilText->SetText(FText::FromString(FString::Printf(TEXT("Soil: %s  (%d harvests left)"), *Sn, Plant->GetSoilUsesLeft())));
	}
	else
	{
		SoilText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.7f, 0.3f)));
		SoilText->SetText(FText::FromString(TEXT("No soil - hold soil + E to fill")));
	}

	const FText Prompt = IInteractable::Execute_GetInteractionPrompt(Plant);
	HintText->SetText(FText::FromString(FString::Printf(TEXT("[E / left-click] %s"), *Prompt.ToString())));
}

#include "UI/LightDimmerWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "World/PackLightSwitch.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

void ULightDimmerWidget::SetPhone(UPhoneClientComponent* InPhone)
{
	Ph = InPhone;
}

TSharedRef<SWidget> ULightDimmerWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void ULightDimmerWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DimmerCard"));
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.98f), 22.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(22.f, 18.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	UTextBlock* Title = WeedUI::Text(WidgetTree, TEXT("Light brightness"), 18, WeedUI::ColAccent(), true, true);
	VB->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	{ USizeBox* DivSz = WidgetTree->ConstructWidget<USizeBox>(); DivSz->SetHeightOverride(2.f); UBorder* Div = WidgetTree->ConstructWidget<UBorder>(); Div->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(0.75f), 1.f)); DivSz->SetContent(Div); VB->AddChildToVerticalBox(DivSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f)); }

	// Verticale dimmer-slider (default = midden).
	Slider = WidgetTree->ConstructWidget<USlider>();
	Slider->SetOrientation(Orient_Vertical);
	Slider->SetMinValue(0.f);
	Slider->SetMaxValue(1.f);
	Slider->SetValue(0.5f);
	Slider->SetStepSize(0.02f);
	Slider->SetSliderBarColor(WeedUI::ColSlotEmpty());
	Slider->SetSliderHandleColor(WeedUI::ColAccent());
	Slider->OnValueChanged.AddDynamic(this, &ULightDimmerWidget::OnSlider);

	USizeBox* SSz = WidgetTree->ConstructWidget<USizeBox>();
	SSz->SetWidthOverride(64.f);
	SSz->SetHeightOverride(240.f);
	SSz->SetContent(Slider);
	UVerticalBoxSlot* SlotS = VB->AddChildToVerticalBox(SSz);
	SlotS->SetHorizontalAlignment(HAlign_Center);
	SlotS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	ValueText = WeedUI::Text(WidgetTree, TEXT("50%"), 16, WeedUI::ColAccent(), true, true);
	VB->AddChildToVerticalBox(ValueText)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// "Link lampen"-knop: sluit de dimmer en start de link-modus (klikbare groen/wit lamp-markers).
	UWeedActionButton* LinkB = WidgetTree->ConstructWidget<UWeedActionButton>();
	LinkB->OnClicked.AddDynamic(LinkB, &UWeedActionButton::Handle);
	LinkB->OnAction.BindLambda([this](int32, int32)
	{
		if (UPhoneClientComponent* P = Ph.Get())
		{
			if (P->IsLinkModeActive()) { P->ExitLinkMode(); P->CloseLightDimmer(); } // "Complete linking" -> stop + dicht
			else { P->EnterLinkMode(); }
		}
	});
	{
		const FLinearColor Col = WeedUI::ColAccentDim();
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(16.f, 8.f); St.PressedPadding = FMargin(16.f, 8.f);
		LinkB->SetStyle(St);
	}
	LinkButton = LinkB;
	LinkButtonText = WeedUI::Text(WidgetTree, TEXT("Link lampen"), 14, WeedUI::ColText(), true, true);
	LinkB->SetContent(LinkButtonText);
	UVerticalBoxSlot* SlotL = VB->AddChildToVerticalBox(LinkB);
	SlotL->SetHorizontalAlignment(HAlign_Center);
	SlotL->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Sluit-knop.
	UWeedActionButton* Close = WidgetTree->ConstructWidget<UWeedActionButton>();
	Close->OnClicked.AddDynamic(Close, &UWeedActionButton::Handle);
	Close->OnAction.BindLambda([this](int32, int32)
	{
		if (UPhoneClientComponent* P = Ph.Get()) { P->CloseLightDimmer(); }
	});
	{
		const FLinearColor Col = WeedUI::ColAccentDim();
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 10.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		St.NormalPadding = FMargin(16.f, 8.f); St.PressedPadding = FMargin(16.f, 8.f);
		Close->SetStyle(St);
	}
	Close->SetContent(WeedUI::Text(WidgetTree, TEXT("Done"), 14, WeedUI::ColText(), true, true));
	UVerticalBoxSlot* SlotC = VB->AddChildToVerticalBox(Close);
	SlotC->SetHorizontalAlignment(HAlign_Center);
}

void ULightDimmerWidget::OnSlider(float Value)
{
	bSliderHeld = true;
	if (APackLightSwitch* Sw = LastSwitch.Get()) { Sw->SetBrightness01(Value); }
	if (ValueText) { ValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Value * 100.f))); }
}

void ULightDimmerWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	UPhoneClientComponent* P = Ph.Get();
	APackLightSwitch* Sw = P ? P->GetDimmerSwitch() : nullptr;
	const bool bOpen = P && P->IsLightDimmerOpen() && Sw != nullptr;

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSwitch = nullptr; bSliderHeld = false; return; }

	// Pas NA de bOpen-gate: het dicht-paneel hoeft z'n link-label niet elke tick te zetten.
	if (LinkButtonText)
	{
		LinkButtonText->SetText(FText::FromString(P->IsLinkModeActive() ? TEXT("Complete linking") : TEXT("Link lampen")));
	}

	// Net geopend (of andere schakelaar): slider op de huidige helderheid van die schakelaar zetten.
	if (Sw != LastSwitch.Get())
	{
		LastSwitch = Sw;
		bSliderHeld = false;
		if (Slider) { Slider->SetValue(Sw->GetBrightness01()); }
		if (ValueText) { ValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Sw->GetBrightness01() * 100.f))); }
	}

	// Loop je weg van de schakelaar -> sluiten.
	if (APawn* Pawn = GetOwningPlayerPawn())
	{
		if (FVector::DistSquared(Pawn->GetActorLocation(), Sw->GetActorLocation()) > FMath::Square(450.f))
		{
			P->CloseLightDimmer();
		}
	}
}

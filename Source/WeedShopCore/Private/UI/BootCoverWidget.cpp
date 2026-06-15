#include "UI/BootCoverWidget.h"

#include "WeedShopCore.h"
#include "UI/WeedUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Math/UnrealMathUtility.h"
#if WITH_EDITOR
#include "ShaderCompiler.h" // GShaderCompilingManager: wacht tot de shaders klaar zijn (geen zwarte scene)
#endif

// LET OP: alles hier is bewust IDENTIEK aan de movie-loadingscreen (SWeedLoadingScreen in WeedShopCore.cpp):
// zelfde achtergrond, fonts (FCoreStyle), kleuren, layout, progress bar + de GEDEELDE laad-timer/tekst.
// Zo zie je geen verschil bij de overgang van het ene naar het andere laadscherm.

TSharedRef<SWidget> UBootCoverWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		Cover = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Cover"));
		Cover->SetBrush(WeedUI::Rounded(FLinearColor(0.025f, 0.05f, 0.035f, 1.f), 0.f)); // zelfde bg als de movie
		Cover->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Cover->SetHorizontalAlignment(HAlign_Center);
		Cover->SetVerticalAlignment(VAlign_Center);
		UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(Cover);
		CS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CS->SetOffsets(FMargin(0.f));

		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Cover->SetContent(VB);
		Content = VB; // bewaren om in NativeTick de DPI-schaal terug te rekenen (matcht de native movie)

		Title = WidgetTree->ConstructWidget<UTextBlock>();
		Title->SetText(FText::FromString(TEXT("THE PLUG")));
		Title->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 64));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.95f, 0.5f)));
		VB->AddChildToVerticalBox(Title)->SetHorizontalAlignment(HAlign_Center);

		Sub = WidgetTree->ConstructWidget<UTextBlock>();
		Sub->SetText(FText::FromString(TEXT("coffeeshop simulator")));
		Sub->SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 18));
		Sub->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.6f, 0.55f)));
		UVerticalBoxSlot* SubS = VB->AddChildToVerticalBox(Sub);
		SubS->SetHorizontalAlignment(HAlign_Center); SubS->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

		StatusText = WidgetTree->ConstructWidget<UTextBlock>();
		StatusText->SetText(FText::FromString(WeedShop_LoadLine(0)));
		StatusText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 15));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 0.62f)));
		UVerticalBoxSlot* SS = VB->AddChildToVerticalBox(StatusText);
		SS->SetHorizontalAlignment(HAlign_Center); SS->SetPadding(FMargin(0.f, 44.f, 0.f, 10.f));

		BarBox = WidgetTree->ConstructWidget<USizeBox>();
		BarBox->SetWidthOverride(360.f); BarBox->SetHeightOverride(10.f);
		Bar = WidgetTree->ConstructWidget<UProgressBar>();
		// EXACT dezelfde bar-stijl als de movie (SWeedLoadingScreen): donkere achtergrond + witte fill
		// (getint door FillColorAndOpacity), geen UMG-default-stijl.
		{
			FProgressBarStyle BarStyle;
			BarStyle.BackgroundImage = FSlateColorBrush(FLinearColor(0.06f, 0.11f, 0.07f, 1.f));
			BarStyle.FillImage = FSlateColorBrush(FLinearColor::White);
			BarStyle.MarqueeImage = FSlateColorBrush(FLinearColor::White);
			Bar->SetWidgetStyle(BarStyle);
		}
		Bar->SetPercent(0.05f);
		Bar->SetFillColorAndOpacity(FLinearColor(0.4f, 0.85f, 0.45f));
		BarBox->SetContent(Bar);
		VB->AddChildToVerticalBox(BarBox)->SetHorizontalAlignment(HAlign_Center);
	}
	return Super::RebuildWidget();
}

void UBootCoverWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// DPI-COMPENSATIE voor een NATIVE-SCHERPE weergave die exact matcht met de movie-loadingscreen (die
	// native tekent, zonder UMG-DPI). I.p.v. een render-transform (rasteriseert op DPI-grootte en rekt
	// daarna op -> blurry, ander 'kleur/stijl'-gevoel) delen we de font- en layout-MATEN door de DPI:
	// na de UMG-DPI-vermenigvuldiging komt alles weer op native pixels uit -> scherp, identiek aan de movie.
	const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
	if (DPI > 0.f && !FMath::IsNearlyEqual(DPI, LastDPI, 0.001f))
	{
		LastDPI = DPI;
		const float Inv = 1.f / DPI;
		auto SetSize = [Inv](UTextBlock* T, const ANSICHAR* Style, float Base)
		{
			if (T) { T->SetFont(FCoreStyle::GetDefaultFontStyle(Style, FMath::RoundToInt(Base * Inv))); }
		};
		SetSize(Title, "Bold", 64.f);
		SetSize(Sub, "Italic", 18.f);
		SetSize(StatusText, "Regular", 15.f);
		if (BarBox) { BarBox->SetWidthOverride(360.f * Inv); BarBox->SetHeightOverride(10.f * Inv); }
		if (Sub) { if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Sub->Slot)) { S->SetPadding(FMargin(0.f, 4.f * Inv, 0.f, 0.f)); } }
		if (StatusText) { if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(StatusText->Slot)) { S->SetPadding(FMargin(0.f, 44.f * Inv, 0.f, 10.f * Inv)); } }
	}

	const float E = (float)WeedShop_LoadElapsedSeconds(); // gedeeld met de movie -> doorloopt naadloos
	const bool bReady = WeedShop_IsRoomReady();
	if (bReady && ReadyAt < 0.f) { ReadyAt = E; }

	// Zelfde tekst-formule als de movie (deterministisch) -> bij de overgang exact dezelfde regel.
	const int32 Step = (int32)(E / 1.6f);
	if (Step != LastStep && StatusText)
	{
		LastStep = Step;
		StatusText->SetText(FText::FromString(WeedShop_LoadLine(Step)));
	}

	// Shader-compile-status (editor): of ze nog bezig zijn (bepaalt de fade-gate + cap).
#if WITH_EDITOR
	const bool bShadersBusy = (GShaderCompilingManager && GShaderCompilingManager->IsCompiling());
#else
	const bool bShadersBusy = false; // packaged build: shaders zijn al gecompileerd
#endif

	// Cover weghalen: kamer klaar + korte na-buffer (omgeving laat bijladen). MAAR niet zolang de
	// shaders nog compileren (editor) - anders zie je een zwarte/onafgewerkte scene met nog-niet-klare
	// materials. De harde cap is hoger als de shaders nog bezig zijn (eerste keer compileren duurt lang).
	const float HardCap = bShadersBusy ? 120.f : 30.f;
	// Terwijl de shaders nog compileren: duidelijke melding (niet eindeloos 'random' tekst).
	if (bShadersBusy && bReady && StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Compiling shaders...")));
		LastStep = -2;
	}
	const bool bBufferDone = (ReadyAt >= 0.f) && (E - ReadyAt > 1.6f) && !bShadersBusy;
	if (!bFading && (bBufferDone || E > HardCap)) { bFading = true; }

	// VLOEIENDE, MONOTONE progress-bar: puur op TIJD, NOOIT op de togglende shader-status (die wisselt
	// tussen shader-golven aan/uit -> dat liet de bar naar 100% en terug springen). Pas echt 100% als
	// de cover daadwerkelijk gaat wegfaden. Beide schermen delen dezelfde tijd -> naadloze overgang.
	if (Bar)
	{
		float Pct;
		if (bFading)      { Pct = 1.f; }
		else if (!bReady) { Pct = 0.55f * (1.f - FMath::Exp(-E / 6.f)); }
		else              { Pct = 0.58f + 0.40f * (1.f - FMath::Exp(-FMath::Max(0.f, E - ReadyAt) / 6.f)); }
		Bar->SetPercent(FMath::Clamp(Pct, 0.04f, 1.f));
	}

	if (bFading)
	{
		Fade = FMath::FInterpTo(Fade, 0.f, DeltaTime, 8.f);
		SetRenderOpacity(Fade);
		if (Fade <= 0.03f) { RemoveFromParent(); }
	}
}

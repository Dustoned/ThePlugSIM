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
#include "Engine/World.h" // GetNetMode: co-op client slaat de CrowdWarm-gate over
#include "Math/UnrealMathUtility.h"
#include "PipelineStateCache.h" // PSO-precaching-status: laadscherm wacht tot de pipeline-states klaar zijn
#if WITH_EDITOR
#include "ShaderCompiler.h" // GShaderCompilingManager: wacht tot de shaders klaar zijn (geen zwarte scene)
#endif

// LET OP: alles hier is bewust IDENTIEK aan de movie-loadingscreen (SWeedLoadingScreen in WeedShopCore.cpp):
// zelfde achtergrond, fonts (FCoreStyle), kleuren, layout, progress bar + de GEDEELDE laad-timer/tekst.
// Zo zie je geen verschil bij de overgang van het ene naar het andere laadscherm.

// Seconden per laad-regel. MOET identiek zijn aan GLoadLineSeconds in WeedShopCore.cpp: beide schermen delen
// dezelfde verstreken tijd E, dus een afwijkende deler zou de getoonde regels laten desyncen.
static const float GLoadLineSeconds = 3.0f;

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
			BarStyle.FillImage = FSlateColorBrush(FLinearColor(0.4f, 0.85f, 0.45f));   // PRE-getint groen -> nooit een witte flash voor de tint pakt
			BarStyle.MarqueeImage = FSlateColorBrush(FLinearColor(0.4f, 0.85f, 0.45f));
			Bar->SetWidgetStyle(BarStyle);
		}
		Bar->SetPercent(0.05f);
		Bar->SetFillColorAndOpacity(FLinearColor::White); // brush is al groen -> neutrale tint (geen dubbele tint, geen witte flash)
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
	if (bReady && ReadyAt < 0.f) { ReadyAt = E; UE_LOG(LogTemp, Verbose, TEXT("[COVER] floor-ready @E=%.1f"), E); }

	// Zelfde tekst-formule als de movie (deterministisch) -> bij de overgang exact dezelfde regel.
	const int32 Step = (int32)(E / GLoadLineSeconds);
	if (Step != LastStep && StatusText)
	{
		LastStep = Step;
		StatusText->SetText(FText::FromString(WeedShop_LoadLine(Step)));
	}

	// Shader-compile-status (editor): of ze nog bezig zijn (bepaalt de fade-gate + cap).
#if WITH_EDITOR
	const bool bShaderCompile = (GShaderCompilingManager && GShaderCompilingManager->IsCompiling());
#else
	const bool bShaderCompile = false; // packaged build: shader-bytecode is al gecompileerd
#endif
	// PSO-PRECACHING (editor EN packaged): wacht tot de pipeline-states klaar zijn -> dan compileren ze in HET
	// LAADSCHERM i.p.v. als "rendering shaders"-hitch tijdens het spelen. Drained zodra de geladen wereld klaar is.
	const bool bPSOBusy = (PipelineStateCache::NumActivePrecacheRequests() > 0);
	const bool bShadersBusy = bShaderCompile || bPSOBusy;

	// Cover weghalen: pas als de WERELD er echt staat. MAAR niet zolang de shaders nog compileren
	// (editor) - anders zie je een zwarte/onafgewerkte scene met nog-niet-klare materials. De harde
	// cap is hoger als de shaders nog bezig zijn (eerste keer compileren duurt lang). LET OP: de cap
	// gaat op TIJD-OP-BEELD (E - AppearAt), NIET op E: E deelt de timer met de movie en stond bij een
	// lange map-load (43-58s) al boven de 45 op het moment dat de cover VERSCHEEN -> de cap vuurde
	// direct en de fade negeerde de hele wereld-opbouw (precies de pop-in die de cover moet verbergen).
	const float HardCap = bShadersBusy ? 120.f : 45.f;
	// Terwijl de shaders nog compileren: duidelijke melding (niet eindeloos 'random' tekst).
	if (bShadersBusy && bReady && StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Compiling shaders...")));
		LastStep = -2;
	}
	// AppearAt = wanneer de cover voor 't eerst tickt (= op beeld komt; de movie heeft tot dan gedekt).
	if (AppearAt < 0.f) { AppearAt = E; UE_LOG(LogTemp, Verbose, TEXT("[COVER] appear @E=%.1f bReady=%d shaders=%d"), E, bReady ? 1 : 0, bShadersBusy ? 1 : 0); }
	// De WERELD is nog aan 't streamen/laden zolang er async package-loading loopt (precies de FlushAsyncLoading/
	// QueuedPackages-regels in de log). Daar moet de cover op WACHTEN: pas faden als die loading ~2,5s STIL is
	// (de initiele streaming-burst is dan klaar), EN de shaders klaar zijn, EN de cover minstens even op beeld stond.
	if (IsAsyncLoading()) { LastLoadAt = E; }
	const bool bLoadSettled = (E - FMath::Max(LastLoadAt, AppearAt)) > 2.5f;
	const bool bMinShown = (E - AppearAt) > 3.0f;
	// WERELD-OPBOUW-GATES (DoorRetrofitter, alleen op maps waar die draait - anders vrijgeven):
	//  - RoomReady:      de vloer onder de thuis-plek is ingestreamd (je staat echt in je kamer)
	//  - CityConverted:  BakedRooms-overlay zichtbaar + de ombouw-sweep vond een volle pass niks nieuws
	//  - CrowdWarm:      alle spawnbare walkers binnen bereik hebben een lichaam (geen in-druppel-show).
	//    Host-side: een co-op client slaat deze over (bodies komen daar via replicatie binnen).
	// De relatieve HardCap hierboven blijft de vangrail als een vlag om wat voor reden ook uitblijft.
	const bool bRetro = WeedShop_IsCityRetroActive();
	const bool bClient = (GetWorld() && GetWorld()->GetNetMode() == NM_Client);
	const bool bRoomOk = !bRetro || bReady;
	const bool bCityOk = !bRetro || WeedShop_IsCityConverted();
	const bool bCrowdOk = !bRetro || bClient || WeedShop_IsCrowdWarm();
	// FASE-TEKST: zolang een concrete opbouw-fase loopt, benoem die i.p.v. de random regel (zelfde
	// patroon als de "Compiling shaders..."-override hierboven, die wint als de shaders bezig zijn).
	if (StatusText && !bShadersBusy)
	{
		const TCHAR* Phase = nullptr;
		if (!bLoadSettled)  { Phase = TEXT("Streaming the rooms..."); }
		else if (!bCityOk)  { Phase = TEXT("Building the city..."); }
		else if (!bCrowdOk) { Phase = TEXT("Warming up the customers..."); }
		if (Phase) { StatusText->SetText(FText::FromString(Phase)); LastStep = -2; }
	}
	if (!bFading && ((bMinShown && bLoadSettled && !bShadersBusy && bRoomOk && bCityOk && bCrowdOk) || (E - AppearAt) > HardCap))
	{
		bFading = true;
		WeedShop_SetCrowdSpawned(true); // DoorRetrofitter terug naar 1-per-keer (smooth gameplay)
		WeedShop_SetCoverUp(false);     // cover is weg
		UE_LOG(LogTemp, Verbose, TEXT("[COVER] FADE @E=%.1f appearAt=%.1f lastLoad=%.1f shaders=%d room=%d city=%d crowd=%d hardcap=%.0f"), E, AppearAt, LastLoadAt, bShadersBusy ? 1 : 0, bRoomOk ? 1 : 0, bCityOk ? 1 : 0, bCrowdOk ? 1 : 0, HardCap);
	}

	// VLOEIENDE, MONOTONE progress-bar: puur op TIJD, NOOIT op de togglende shader-status (die wisselt
	// tussen shader-golven aan/uit -> dat liet de bar naar 100% en terug springen). Pas echt 100% als
	// de cover daadwerkelijk gaat wegfaden. Beide schermen delen dezelfde tijd -> naadloze overgang.
	if (Bar)
	{
		float Pct;
		if (bFading) { Pct = 1.f; }
		else { const float t = (AppearAt >= 0.f) ? (E - AppearAt) : 0.f; Pct = 0.55f + 0.42f * (1.f - FMath::Exp(-t / 4.f)); }
		Bar->SetPercent(FMath::Clamp(Pct, 0.04f, 1.f));
	}

	if (bFading)
	{
		Fade = FMath::FInterpTo(Fade, 0.f, DeltaTime, 8.f);
		SetRenderOpacity(Fade);
		if (Fade <= 0.03f) { RemoveFromParent(); }
	}
}

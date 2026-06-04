// WeedShopCore — module-implementatie. Registreert de module, de log-categorie EN een loading screen
// die het zwarte beeld afdekt tijdens een level-reload (New Game / Load / Continue doen OpenLevel()).

#include "WeedShopCore.h"

#include "MoviePlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogWeedShop);

// --- Slate loading screen (geen UObjects/UMG: draait veilig tijdens het laden) ---
class SWeedLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWeedLoadingScreen) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		// Donkergroen-zwarte achtergrond (sfeer past bij de game). Static: brush moet blijven leven.
		static const FSlateColorBrush BgBrush(FLinearColor(0.025f, 0.05f, 0.035f, 1.f));
		static const FSlateColorBrush BarBrush(FLinearColor(0.06f, 0.11f, 0.07f, 1.f));

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(&BgBrush)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("THE PLUG")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 64))
					.ColorAndOpacity(FLinearColor(0.45f, 0.95f, 0.5f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("coffeeshop simulator")))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 18))
					.ColorAndOpacity(FLinearColor(0.55f, 0.6f, 0.55f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 40.f, 0.f, 0.f)
				[
					SNew(SThrobber)
					.PieceImage(&BarBrush)
					.NumPieces(7)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 28.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("De stad wordt opgebouwd...")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
					.ColorAndOpacity(FLinearColor(0.5f, 0.55f, 0.5f))
				]
			]
		];
	}
};

// --- Module: hookt de map-load delegates om de loading screen te tonen ---
class FWeedShopCoreModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningDedicatedServer()) { return; }
		// Bind altijd (de slate/movieplayer-check gebeurt pas bij het tonen, niet hier).
		FCoreUObjectDelegates::PreLoadMap.AddRaw(this, &FWeedShopCoreModule::OnPreLoadMap);
	}

	virtual void ShutdownModule() override
	{
		FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	}

private:
	void OnPreLoadMap(const FString& MapName)
	{
		if (IsRunningDedicatedServer() || GetMoviePlayer() == nullptr) { return; }

		FLoadingScreenAttributes Attr;
		Attr.bAutoCompleteWhenLoadingCompletes = true;   // verdwijnt vanzelf zodra de map klaar is
		Attr.bMoviesAreSkippable = true;
		Attr.bWaitForManualStop = false;
		Attr.MinimumLoadingScreenDisplayTime = 1.2f;     // altijd kort tonen -> nooit een zwarte flits
		Attr.WidgetLoadingScreen = SNew(SWeedLoadingScreen);
		GetMoviePlayer()->SetupLoadingScreen(Attr);
	}
};

// Secundaire game-module (de primaire blijft 'ThePlugSIM' uit de template).
IMPLEMENT_MODULE(FWeedShopCoreModule, WeedShopCore);

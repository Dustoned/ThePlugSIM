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
#include "Widgets/Notifications/SProgressBar.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/FileHelper.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY(LogWeedShop);

// Alleen tonen bij de eerstvolgende IN-GAME level-reload (gezet door New Game/Load/Continue).
static bool GShowGameLoadingScreen = false;
static bool GRoomFloorReady = false;
static double GLoadStartSeconds = 0.0;
static uint32 GLoadSeed = 0; // per-launch seed -> elke keer andere laad-regels (gedeeld door movie + cover)
void WeedShop_RequestGameLoadingScreen()
{
	GShowGameLoadingScreen = true;
	GRoomFloorReady = false;
	GLoadStartSeconds = FPlatformTime::Seconds(); // gedeelde laad-timer voor movie + cover (naadloos)
	GLoadSeed = (uint32)FPlatformTime::Cycles() * 2654435761u + 12345u; // verandert elke game-start
}
void WeedShop_SetRoomReady(bool bReady) { GRoomFloorReady = bReady; }
bool WeedShop_IsRoomReady() { return GRoomFloorReady; }
double WeedShop_LoadElapsedSeconds() { return GLoadStartSeconds > 0.0 ? (FPlatformTime::Seconds() - GLoadStartSeconds) : 0.0; }

// Gedeelde grappige laad-regels. Beide laadschermen kiezen via WeedShop_LoadLine(step) exact dezelfde regel.
static const TArray<FString>& GLoadLines()
{
	static const TArray<FString> L = {
		TEXT("Building the city..."), TEXT("Watering the plants..."), TEXT("Trimming the buds..."),
		TEXT("Rolling the welcome joint..."), TEXT("Stocking the shelves..."), TEXT("Brewing the coffee..."),
		TEXT("Lighting the neon sign..."), TEXT("Counting the cash..."), TEXT("Calling the supplier..."),
		TEXT("Warming up the customers..."), TEXT("Polishing the bongs..."), TEXT("Hiding the good stuff..."),
		TEXT("Pressing the hash..."), TEXT("Baking the edibles..."), TEXT("Curing the jars..."),
		TEXT("Tipping the bouncer..."), TEXT("Charging the ATM..."), TEXT("Setting the mood lighting..."),
		TEXT("Bribing the parking meter..."), TEXT("Weighing the grams..."), TEXT("Sorting the strains..."),
		TEXT("Opening the shutters..."), TEXT("Refilling the lighter..."), TEXT("Streaming the rooms..."),
		// Extra regels voor meer variatie:
		TEXT("Grinding the kief..."), TEXT("Restocking the rolling papers..."), TEXT("Feeding the parking meter..."),
		TEXT("Spraying the air freshener..."), TEXT("Defrosting the gummies..."), TEXT("Untangling the grow lights..."),
		TEXT("Counting the seeds..."), TEXT("Checking the humidity..."), TEXT("Dimming the back room..."),
		TEXT("Wiping the display case..."), TEXT("Sweeping the floor..."), TEXT("Paying off the inspector..."),
		TEXT("Testing the smoke alarm..."), TEXT("Rolling out the welcome mat..."), TEXT("Loading the playlist..."),
		TEXT("Cracking a window..."), TEXT("Sealing the baggies..."), TEXT("Topping up the change drawer..."),
		TEXT("Greasing the door hinges..."), TEXT("Stacking the press blocks..."), TEXT("Labelling the jars..."),
		TEXT("Shooing the seagulls..."), TEXT("Booting the security cams..."), TEXT("Misting the seedlings..."),
	};
	return L;
}
FString WeedShop_LoadLine(int32 Step)
{
	const TArray<FString>& L = GLoadLines();
	if (L.Num() == 0) { return FString(); }
	// Step + per-launch seed door een mix-hash -> elke game-start een andere volgorde, maar deterministisch
	// per (Step, seed) zodat movie en cover exact dezelfde regel tonen. Eerste regel altijd "Building the
	// city..." (Step 0, seed 0-pad) zou saai zijn -> we mengen de seed er ook in Step 0 doorheen.
	uint32 H = (uint32)Step * 2654435761u + GLoadSeed;
	H ^= H >> 13; H *= 3266489917u; H ^= H >> 16;
	return L[(int32)(H % (uint32)L.Num())];
}

void WeedShop_StopGameLoadingScreen()
{
	if (GetMoviePlayer() && GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		GetMoviePlayer()->StopMovie();
	}
}

// Lumen (GI + reflecties) aan/uit. Zet de cvars DIRECT via de console-manager op de hoogste
// game-prioriteit, plus de harde Lumen-Allow-schakelaars (die deinst niets terug). Logt de
// werkelijke waardes na afloop zodat we kunnen verifieren dat het echt geschakeld is.
void WeedShop_ApplyLumen(bool bLumenOff)
{
	IConsoleManager& CM = IConsoleManager::Get();
	auto SetCV = [&](const TCHAR* Name, int32 Val)
	{
		if (IConsoleVariable* CV = CM.FindConsoleVariable(Name))
		{
			CV->Set(Val, ECVF_SetByConsole);
		}
	};
	SetCV(TEXT("r.DynamicGlobalIlluminationMethod"), bLumenOff ? 0 : 1);
	SetCV(TEXT("r.ReflectionMethod"), bLumenOff ? 0 : 1);
	SetCV(TEXT("r.Lumen.DiffuseIndirect.Allow"), bLumenOff ? 0 : 1);
	SetCV(TEXT("r.Lumen.Reflections.Allow"), bLumenOff ? 0 : 1);

	auto GetCV = [&](const TCHAR* Name) -> int32
	{
		const IConsoleVariable* CV = CM.FindConsoleVariable(Name);
		return CV ? CV->GetInt() : -999;
	};
	UE_LOG(LogWeedShop, Warning, TEXT("Lumen %s -> GIMethod=%d ReflMethod=%d DiffuseAllow=%d ReflAllow=%d"),
		bLumenOff ? TEXT("UIT") : TEXT("AAN"),
		GetCV(TEXT("r.DynamicGlobalIlluminationMethod")), GetCV(TEXT("r.ReflectionMethod")),
		GetCV(TEXT("r.Lumen.DiffuseIndirect.Allow")), GetCV(TEXT("r.Lumen.Reflections.Allow")));
}

// Grafische kwaliteit-tier. Tier -1 = Potato (onder Low): scalability 0 PLUS extra agressieve
// verlagingen voor hele zwakke pc's. Tier 0..3 = Low/Medium/High/Epic (puur scalability, cvars
// terug naar normaal zodat omhoog-schakelen vanuit Potato alles weer herstelt).
void WeedShop_ApplyGraphicsTier(int32 Tier)
{
	const bool bPotato = (Tier <= -1);
	const int32 Scal = bPotato ? 0 : FMath::Clamp(Tier, 0, 3);

	if (UGameUserSettings* GU = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		GU->SetOverallScalabilityLevel(Scal);
		GU->ApplySettings(false);
		GU->SaveSettings();
	}

	IConsoleManager& CM = IConsoleManager::Get();
	auto SetF = [&](const TCHAR* Name, float Val)
	{
		if (IConsoleVariable* CV = CM.FindConsoleVariable(Name)) { CV->Set(Val, ECVF_SetByConsole); }
	};
	// Potato = alles zo laag mogelijk BOVENOP scalability 0 (nog agressiever voor echt zwakke pc's);
	// anders terug naar normale waardes.
	SetF(TEXT("r.ScreenPercentage"),     bPotato ? 42.f   : 100.f);  // render op ~42% resolutie
	SetF(TEXT("r.Streaming.MipBias"),    bPotato ? 3.0f   : 0.f);    // veel lagere-res textures runtime
	SetF(TEXT("r.Streaming.PoolSize"),   bPotato ? 250.f  : 1000.f); // kleine texture-pool (VRAM)
	// View-distance GRADUAAL per tier: schaalt ALLE cull-afstanden mee (NPC's, gebouwen/props-HISMs, foliage,
	// algemene mesh-draw-distance). Lager = dichterbij cullen = meer FPS. Potato cullt agressief, Epic ziet ver.
	const float ViewDist = bPotato ? 0.35f : (Tier == 0 ? 0.6f : (Tier == 1 ? 0.85f : (Tier == 2 ? 1.0f : 1.3f)));
	SetF(TEXT("r.ViewDistanceScale"),    ViewDist);
	SetF(TEXT("foliage.DensityScale"),   bPotato ? 0.2f   : 1.f);
	SetF(TEXT("grass.DensityScale"),     bPotato ? 0.2f   : 1.f);
	SetF(TEXT("r.MaxAnisotropy"),        bPotato ? 0.f    : 4.f);

	// --- LUMEN: de #1 GPU-kost. Alleen op EPIC. Potato/Low/Medium/High draaien zonder (de speler vindt
	//     de Lumen-uit-look prima), wat op High ~20-25 FPS scheelt. Epic blijft de mooie-maar-dure optie.
	WeedShop_ApplyLumen(Tier < 3);

	// --- SCHADUWEN + dure GI-bijdragen per tier (grootste winst na Lumen) ---
	const bool bEpic = (Tier >= 3);
	// Distance-field shadows + volumetric fog: duur, weinig zichtbare meerwaarde -> alleen Epic.
	SetF(TEXT("r.DistanceFieldShadowing"), bEpic ? 1.f : 0.f);
	SetF(TEXT("r.VolumetricFog"),          bEpic ? 1.f : 0.f);
	// CSM (zonschaduw) resolutie + cascades per tier. Potato krijgt JUIST betere schaduwen (1024 i.p.v. de
	// blokkerige 256/1-cascade default) want dat was de zwakke plek; verder oplopend.
	const float CsmRes = bPotato ? 1024.f : (Tier <= 1 ? 1024.f : (Tier == 2 ? 2048.f : 4096.f));
	SetF(TEXT("r.Shadow.MaxCSMResolution"), CsmRes);
	SetF(TEXT("r.Shadow.MaxResolution"),    bPotato ? 512.f : (Tier == 2 ? 2048.f : (bEpic ? 4096.f : 1024.f)));
	SetF(TEXT("r.Shadow.CSM.MaxCascades"),  bPotato ? 2.f   : (Tier <= 1 ? 3.f : 4.f));
	// Kleine/verre schaduwen wegcullen (perf, nauwelijks zichtbaar): hoger = meer cullen.
	SetF(TEXT("r.Shadow.RadiusThreshold"),  bPotato ? 0.05f : (Tier == 2 ? 0.03f : 0.01f));

	UE_LOG(LogWeedShop, Warning, TEXT("Graphics-tier: %s (scalability %d)"),
		bPotato ? TEXT("POTATO") : (Scal == 0 ? TEXT("Low") : Scal == 1 ? TEXT("Medium") : Scal == 2 ? TEXT("High") : TEXT("Epic")), Scal);
}

// Motion blur aan/uit (post-process). Off = 0, On = standaard halve sterkte.
void WeedShop_ApplyMotionBlur(bool bOff)
{
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlur.Amount")))
	{
		CV->Set(bOff ? 0.f : 0.5f, ECVF_SetByConsole);
	}
}

// De cvar-gebaseerde grafische vlaggen (Lumen/Potato/MotionBlur) leven samen in Saved/GraphicsConfig.txt.
// Read/Write als geheel zodat het wijzigen van één vlag de andere niet wist.
void WeedShop_ReadGfxFlags(bool& bLumenOff, bool& bPotato, bool& bMotionBlurOff)
{
	FString T;
	FFileHelper::LoadFileToString(T, *(FPaths::ProjectSavedDir() / TEXT("GraphicsConfig.txt")));
	bLumenOff      = T.Contains(TEXT("LumenOff=1"));
	bPotato        = T.Contains(TEXT("Potato=1"));
	bMotionBlurOff = T.Contains(TEXT("MotionBlurOff=1"));
}

void WeedShop_WriteGfxFlags(bool bLumenOff, bool bPotato, bool bMotionBlurOff)
{
	const FString Out = FString::Printf(TEXT("LumenOff=%d\nPotato=%d\nMotionBlurOff=%d\n"),
		bLumenOff ? 1 : 0, bPotato ? 1 : 0, bMotionBlurOff ? 1 : 0);
	FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("GraphicsConfig.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

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
		// EXPLICIETE bar-stijl zodat de movie-bar EXACT gelijk is aan de UMG-cover-bar (UBootCoverWidget):
		// zelfde donkere achtergrond + witte fill (getint door FillColorAndOpacity), geen default-stijl.
		static const FSlateColorBrush BarBgBrush(FLinearColor(0.06f, 0.11f, 0.07f, 1.f));
		static const FSlateColorBrush BarFillBrush(FLinearColor::White);
		static const FProgressBarStyle BarStyle = FProgressBarStyle()
			.SetBackgroundImage(BarBgBrush)
			.SetFillImage(BarFillBrush)
			.SetMarqueeImage(BarFillBrush);

		SetCanTick(true); // we updaten zelf de progress bar + wisselende tekst

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
				// Wisselende grappige status-tekst.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 44.f, 0.f, 10.f)
				[
					SAssignNew(StatusText, STextBlock)
					.Text(FText::FromString(WeedShop_LoadLine(0)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
					.ColorAndOpacity(FLinearColor(0.6f, 0.85f, 0.62f))
				]
				// Progress bar onder de tekst.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(360.f).HeightOverride(10.f)
					[
						SAssignNew(Bar, SProgressBar)
						.Style(&BarStyle)
						.Percent(0.05f)
						.FillColorAndOpacity(FLinearColor(0.4f, 0.85f, 0.45f))
					]
				]
			]
		];
	}

	virtual void Tick(const FGeometry& Geo, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(Geo, InCurrentTime, InDeltaTime);
		const float E = (float)WeedShop_LoadElapsedSeconds();
		const bool bReady = WeedShop_IsRoomReady();
		if (bReady && ReadyAt < 0.f) { ReadyAt = E; }
		// Zelfde EERLIJKE creep als de cover (UBootCoverWidget): vloeiend tot ~55% terwijl de map streamt.
		// De movie verdwijnt zodra het level klaar is; de cover loopt vanaf exact deze stand verder.
		if (Bar.IsValid()) { Bar->SetPercent(bReady ? 1.f : FMath::Clamp(0.55f * (1.f - FMath::Exp(-E / 6.f)), 0.04f, 1.f)); }
		const int32 Step = (int32)(E / 1.6f);
		if (Step != LastStep && StatusText.IsValid())
		{
			LastStep = Step;
			StatusText->SetText(FText::FromString(WeedShop_LoadLine(Step)));
		}
		// EEN doorlopend scherm: stopt zodra de kamer klaar is (vloer onder de speler gevonden, gemeld
		// via WeedShop_SetRoomReady) + korte buffer. De harde cap (24s) is alleen een noodrem zodat het
		// nooit blijft hangen als room-ready om wat voor reden ook niet binnenkomt.
		const bool bBufferDone = (ReadyAt >= 0.f) && (E - ReadyAt > 1.5f);
		if (bBufferDone || E > 24.f)
		{
			if (IGameMoviePlayer* MP = GetMoviePlayer()) { MP->StopMovie(); }
		}
	}

private:
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SProgressBar> Bar;
	int32 LastStep = -1;
	float ReadyAt = -1.f;
};

// --- Module: hookt de map-load delegates om de loading screen te tonen ---
class FWeedShopCoreModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// Verse (packaged) install: herstel de gebakken wereld-data (markers, apartments, routes,
		// rent, ...) naar Saved/ zodat ook de directe ProjectSavedDir()-loads hun data vinden.
		// In de editor een no-op (Saved/ is al gevuld). Ook op de server (data-only) nuttig.
		WeedData::RestoreAll();

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
		// Alleen bij in-game gaan (New Game/Load/Continue). De boot naar het hoofdmenu krijgt geen laadscherm.
		if (!GShowGameLoadingScreen) { return; }
		GShowGameLoadingScreen = false;

		// TWEE SCHERMEN, NAADLOOS IN ELKAAR OVERLOPEND:
		// 1) Dit MOVIE-scherm dekt de engine-map-load en VERDWIJNT automatisch zodra het level klaar is
		//    (bAutoCompleteWhenLoadingCompletes + GEEN manual-wait -> nooit een hang). Z'n progress-bar
		//    staat op dat moment NIET op 100% maar op de gedeelde laad-stand (E/12).
		// 2) Daaronder ligt dan al het in-game COVER-scherm (UBootCoverWidget, PhoneClientComponent::
		//    EnsureWidget): exact dezelfde look + DEZELFDE gedeelde timer/bar/tekst. Het loopt dus
		//    gewoon DOOR vanaf waar de movie zat (geen 'klaar -> opnieuw') tot je stil in de kamer staat.
		FLoadingScreenAttributes Attr;
		Attr.bAutoCompleteWhenLoadingCompletes = true;  // movie weg zodra het level geladen is (geen hang)
		Attr.bMoviesAreSkippable = false;
		Attr.bWaitForManualStop = false;
		Attr.MinimumLoadingScreenDisplayTime = 1.0f;
		Attr.WidgetLoadingScreen = SNew(SWeedLoadingScreen);
		GetMoviePlayer()->SetupLoadingScreen(Attr);
	}
};

// Secundaire game-module (de primaire blijft 'ThePlugSIM' uit de template).
IMPLEMENT_MODULE(FWeedShopCoreModule, WeedShopCore);

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
static bool GCrowdSpawned = false; // nabije crowd-burst klaar (gezet door DoorRetrofitter) -> cover mag wegfaden
static bool GCoverUp = false;      // in-game cover staat op beeld -> de movie mag overdragen (geen laad-gat)
static bool GCityConverted = false;   // stad omgebouwd (BakedRooms zichtbaar + sweep idle) - gezet door DoorRetrofitter
static bool GCrowdWarm = false;       // crowd gematerialiseerd (alle spawnbare walkers in bereik hebben een lichaam)
static bool GCityRetroActive = false; // er draait een DoorRetrofitter in deze wereld (pack-map) -> stad/crowd-gates gelden
static bool GBootLoading = false;     // boot-laadscherm (eerste map -> hoofdmenu) actief -> EnsureWidget maakt geen cover
static double GLoadStartSeconds = 0.0;
static uint32 GLoadSeed = 0; // per-launch seed -> elke keer andere laad-regels (gedeeld door movie + cover)
void WeedShop_RequestGameLoadingScreen()
{
	GShowGameLoadingScreen = true;
	GRoomFloorReady = false;
	GCrowdSpawned = false; // verse load: crowd moet opnieuw materialiseren voordat de cover weg mag
	GCoverUp = false;      // verse load: de movie wacht weer op de nieuwe cover
	GCityConverted = false;   // verse load: stad moet opnieuw omgebouwd worden
	GCrowdWarm = false;       // verse load: crowd moet opnieuw warm draaien
	GCityRetroActive = false; // verse load: de nieuwe wereld meldt zich (of niet, op maps zonder retrofitter)
	GBootLoading = false;     // een echte game-load overschrijft de boot-staat
	GLoadStartSeconds = FPlatformTime::Seconds(); // gedeelde laad-timer voor movie + cover (naadloos)
	GLoadSeed = (uint32)FPlatformTime::Cycles() * 2654435761u + 12345u; // verandert elke game-start
}
void WeedShop_SetRoomReady(bool bReady) { GRoomFloorReady = bReady; }
bool WeedShop_IsRoomReady() { return GRoomFloorReady; }
void WeedShop_SetCrowdSpawned(bool bSpawned) { GCrowdSpawned = bSpawned; }
bool WeedShop_IsCrowdSpawned() { return GCrowdSpawned; }
void WeedShop_SetCoverUp(bool bUp) { GCoverUp = bUp; }
bool WeedShop_IsCoverUp() { return GCoverUp; }
void WeedShop_SetCityConverted(bool bConverted) { GCityConverted = bConverted; }
bool WeedShop_IsCityConverted() { return GCityConverted; }
void WeedShop_SetCrowdWarm(bool bWarm) { GCrowdWarm = bWarm; }
bool WeedShop_IsCrowdWarm() { return GCrowdWarm; }
void WeedShop_SetCityRetroActive(bool bActive) { GCityRetroActive = bActive; }
bool WeedShop_IsCityRetroActive() { return GCityRetroActive; }
void WeedShop_SetBootLoading(bool bBoot) { GBootLoading = bBoot; }
bool WeedShop_IsBootLoading() { return GBootLoading; }
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
	GBootLoading = false; // boot-laadscherm is (bij deze stop) sowieso voorbij
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
	UE_LOG(LogWeedShop, Log, TEXT("Lumen %s -> GIMethod=%d ReflMethod=%d DiffuseAllow=%d ReflAllow=%d"),
		bLumenOff ? TEXT("UIT") : TEXT("AAN"),
		GetCV(TEXT("r.DynamicGlobalIlluminationMethod")), GetCV(TEXT("r.ReflectionMethod")),
		GetCV(TEXT("r.Lumen.DiffuseIndirect.Allow")), GetCV(TEXT("r.Lumen.Reflections.Allow")));
}

// Virtual Shadow Maps aan/uit. VSM is een dure render-/GPU-feature die de Potato-instelling NIET uitzette
// (los van de CSM-schaduw-instellingen). Uit = terug naar gewone shadow maps -> flink goedkoper.
void WeedShop_ApplyVSM(bool bOff)
{
	IConsoleManager& CM = IConsoleManager::Get();
	if (IConsoleVariable* CV = CM.FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable")))
	{
		CV->Set(bOff ? 0 : 1, ECVF_SetByConsole);
	}
	UE_LOG(LogWeedShop, Log, TEXT("Virtual Shadow Maps %s"), bOff ? TEXT("UIT") : TEXT("AAN"));
}

// Distance-field-AO + global distance field aan/uit. Op grote maps (de stad-beach) bouwen die runtime een
// enorme brick-atlas (DistanceFieldBrickTexture, tienduizenden bricks) zodra de shadow/AO-pass aangaat ->
// "Out of video memory" terwijl de GPU grotendeels leeg is (reserved-buffer-allocatie faalt). Geen GI-
// feature die we onder Epic nodig hebben. Console-prio zodat de scalability-slider 'm niet kan aanzetten.
void WeedShop_ApplyDistanceFieldGI(bool bOff)
{
	IConsoleManager& CM = IConsoleManager::Get();
	auto SetI = [&](const TCHAR* Name, int32 Val)
	{
		if (IConsoleVariable* CV = CM.FindConsoleVariable(Name)) { CV->Set(Val, ECVF_SetByConsole); }
	};
	SetI(TEXT("r.DistanceFieldAO"),        bOff ? 0 : 1);
	SetI(TEXT("r.AOGlobalDistanceField"),  bOff ? 0 : 1);
	// KRITISCH: de shadow-kwaliteit-slider zet op hoog/epic via scalability r.DistanceFieldShadowing AAN ->
	// distance-field-shadows bouwen dezelfde brick-atlas -> OOM. Console-prio uit zodat de slider 'm niet aanzet.
	SetI(TEXT("r.DistanceFieldShadowing"), bOff ? 0 : 1);
	UE_LOG(LogWeedShop, Log, TEXT("Distance-field GI %s"), bOff ? TEXT("UIT") : TEXT("AAN"));
}

// Open beach draait op gewone CSM-schaduwen (VSM staat uit ivm de VRAM-crash). Default-CSM op die map is
// zuinig: NPC's/kleine props worden weggeculld (RadiusThreshold) en de resolutie/cascades zijn laag ->
// slechte uitlijning + geen NPC-schaduw. Hier tillen we dat op. Plus de volumetrische wolken iets goedkoper
// (raysamples) want die zijn de grote lag-bron als je naar de zon kijkt. Alles console-prio zodat de
// scalability-slider deze niet platdrukt.
void WeedShop_ApplyBeachShadowQuality(bool bPotato)
{
	IConsoleManager& CM = IConsoleManager::Get();
	auto SetF = [&](const TCHAR* Name, float Val)
	{
		if (IConsoleVariable* CV = CM.FindConsoleVariable(Name)) { CV->Set(Val, ECVF_SetByConsole); }
	};
	SetF(TEXT("r.Shadow.RadiusThreshold"),         bPotato ? 0.03f : 0.01f);   // los HOOFD-mesh is klein -> op 0.08 werd z'n schaduw gecullt = koploze NPC-schaduw. 0.03 houdt koppen van NPC's dichtbij; echt verre/mini-casters vallen nog steeds af.
	// r.ShadowQuality is het ENIGE dat de shadow-slider nog verandert (medium=3, high/epic=5). Die 5 = maximale
	// PCF-filtering over de hele stad in beeld = de "super laggy richting de zon". Cap op 3 (speelbaar niveau)
	// zodat high/epic ~even soepel als medium worden; de schaduw-look verschilt nauwelijks.
	SetF(TEXT("r.ShadowQuality"),                        3.f);
	SetF(TEXT("r.Shadow.MaxCSMResolution"),              2048.f);  // scherpere zon-schaduw
	SetF(TEXT("r.Shadow.MaxResolution"),                 2048.f);
	SetF(TEXT("r.Shadow.CSM.MaxCascades"),               4.f);     // betere dekking/uitlijning over de open map
	SetF(TEXT("r.Shadow.CSM.TransitionScale"),           1.f);     // zachtere cascade-overgangen
	// High/Epic verlengen via scalability de schaduw-AFSTAND -> bij een lage zon cast de hele stad schaduw
	// (duizenden casters in beeld) -> zware lag richting de zon. Cap de afstand zodat high/epic ~even soepel
	// als medium presteren; nabije schaduwen (waar 't telt) blijven scherp op 2048/4-cascade.
	SetF(TEXT("r.Shadow.DistanceScale"),                 0.75f);
	SetF(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"),   384.f);// wolken iets goedkoper richting de zon
	SetF(TEXT("r.VolumetricCloud.ShadowViewRaySampleMaxCount"), 32.f);
	// === VSM SMOOTH MOVING-SUN (Fortnite-recept, geverifieerd tegen UE5.8-bron) ===  De zon beweegt nu VLOEIEND
	// (DriveUDS-throttle bijna weg). VSM gooit z'n directional-cache elk frame weg zodra de zon roteert - dat is de
	// BEDOELDE werking; Epic verwierp throttling expliciet ("te veel artefacten op page-grenzen" = ons getik). Je
	// maakt 't betaalbaar door de MOVING-sun-schaduw op LAGERE resolutie te zetten (de "halve res"-truc) + force-
	// invalidate (skip cache-bookkeeping voor de zon, die cachet toch nooit). De flikker zat in de 512-pool (nu 8192),
	// NIET in de bewegende zon - daarom kan de throttle weg zonder dat de flikker terugkomt. Console-prio, beach-only.
	SetF(TEXT("r.Shadow.Virtual.Cache"),                              1.f);   // caching aan (lokale lampen/statische wereld)
	SetF(TEXT("r.Shadow.Virtual.Cache.StaticSeparate"),              1.f);   // statische wereld los cachen van de bewegende zon -> gebouwen flikkeren niet mee
	SetF(TEXT("r.Shadow.Virtual.MaxPhysicalPages"),              8192.f);    // ruime page-pool = geen thrash/flikker (~512MB VRAM, niks op een 4080). DIT hield de flikker weg.
	SetF(TEXT("r.Shadow.Virtual.Cache.ForceInvalidateDirectional"),   1.f);  // AAN (terug na live-test): met 0 (cache op statische-zon-frames) gingen de BEWEGENDE NPC-schaduwen FLIKKEREN (dynamische casters niet elk frame geïnvalideerd). 1 = directional elk frame hertekenen = NPC-schaduwen smooth. Kost ~1ms; die winst moet ergens anders vandaan (decals/lamp-count/game-thread), NIET hier.
	// === SCHERPTE schaalt met de "Shadows"-setting (sg.ShadowQuality 0..3) === Die slider werkte op de beach niet:
	// hij zet CSM-cvars (niets voor VSM) en mijn config overschreef 'm. Nu stuurt 'ie de VSM-RESOLUTIE-bias. Flicker-
	// veilige basis per render-res: potato draait op 42% -> bias mag niet positief (page-grens-popping); volle res mag
	// +1 (goedkoop). De Shadows-setting maakt 't vanaf daar scherper: elke stap 0.5 lager = ~hogere VSM-resolutie.
	int32 ShadowQ = 2; // default ~High als er nog geen settings zijn
	if (UGameUserSettings* GU = GEngine ? GEngine->GetGameUserSettings() : nullptr) { ShadowQ = FMath::Clamp(GU->GetShadowQuality(), 0, 3); }
	const float BaseBias = bPotato ? -0.5f : 1.0f;  // potato-Low naar -0.5 (meer VSM-res): bij een LAGE/scherende zon vallen de dunne delen (kop) van de lange schaduw niet meer tussen de texels. Hogere res = FIJNERE pages = ook MINDER popping, geen nieuwe flikker.
	const float MoveBias = BaseBias - 0.5f * (float)ShadowQ;    // potato: -0.5/-1/-1.5/-2 ; volle res: +1/+0.5/0/-0.5 (Low..Epic)
	SetF(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving"), MoveBias);                  // smooth blijft (de 0.05-throttle bepaalt dat, niet de res)
	SetF(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),       FMath::Min(MoveBias, 0.f)); // stilstaande zon minstens zo scherp
	SetF(TEXT("r.Shadow.Scene.LightActiveFrameCount"),               12.f);                       // soepel terugblenden naar scherp zodra de zon vertraagt/stopt
	// SMRT-rays AAN, ÓÓK op potato-Low: met 0 rays = harde 1-tap schaduw -> op 42% render-res vallen dunne delen
	// (kop, heupen) tussen de texels = ontbrekende stukken + shimmer in de bewegende NPC-schaduw. SMRT = multi-tap =
	// zachte, COMPLETE schaduw. SMRT is GPU-kost en de GPU duimt (8ms) -> gratis t.o.v. de render-thread-bottleneck.
	const float Rays = bPotato ? (6.f + 2.f * (float)ShadowQ) : (4.f + 4.f * (float)ShadowQ);     // potato 6/8/10/12 ; volle 4/8/12/16
	SetF(TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"),          Rays);
	SetF(TEXT("r.Shadow.Virtual.SMRT.SamplesPerRayDirectional"),     4.f);                          // 4 samples (was 2 op potato) = gladdere penumbra -> minder shimmer/flikker + vult dunne delen (GPU-kost, GPU duimt)
	// CONTACT / PETER-PANNING: de schaduw moet AAN de voeten plakken, niet zweven. De engine-defaults (NormalBias 0.5,
	// RayLengthScaleDirectional 1.5) duwen de schaduw van het contactpunt weg -- erger bij een lage scherende zon, en onze
	// gehalveerde samples (4 i.p.v. 8) onderbemonsteren het contact-eind. Lager = schaduw plakt weer aan de voeten (player + alle NPCs).
	SetF(TEXT("r.Shadow.Virtual.NormalBias"),                      0.25f); // 0.5 default -> halveert het zweef-gat; niet onder ~0.15 (dan acne)
	SetF(TEXT("r.Shadow.Virtual.SMRT.RayLengthScaleDirectional"), 0.8f);  // 1.5 default -> strakkere penumbra bij het contactpunt met onze 4 samples
	UE_LOG(LogWeedShop, Log, TEXT("Beach-schaduw: smooth VSM (pool 8192, ShadowQ %d, moving-bias %.1f, rays %.0f, %s)"), ShadowQ, MoveBias, Rays, bPotato ? TEXT("potato/42%") : TEXT("full-res"));
}

// Beach-schaduw-GATE per tier. Gedeeld door BeginPlay + de Preset-rij + de losse settings, zodat ELKE tier-wissel
// de schaduwen opnieuw + deterministisch toepast (console-prio overschrijft scalability, dus de vorige stand moet
// expliciet teruggezet worden). Potato = schaduwen UIT (VSM uit + CSM-zon uit); Low en hoger = VSM smooth-sun.
void WeedShop_ApplyBeachShadows(bool bPotato)
{
	// Potato = schaduwen UIT (maximaal barebones: VSM uit, dus geen page-pool gealloceerd). Low en hoger = VSM smooth-sun.
	// LET OP: r.Shadow.Virtual.Enable kan niet betrouwbaar LIVE togglen (de pool alloceert 1x) -> over de Potato-grens OMHOOG
	// wisselen vergt een HERSTART voor de schaduwen (speler-keuze: max barebones boven live-wisselen). Naar Potato werkt wel live.
	if (bPotato)
	{
		WeedShop_ApplyVSM(true); // VSM uit (geen pool)
		if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"))) { CV->Set(0.f, ECVF_SetByConsole); } // CSM-zon ook uit = geen schaduw
	}
	else
	{
		WeedShop_ApplyVSM(false);                // VSM aan
		WeedShop_ApplyBeachShadowQuality(false); // zet r.ShadowQuality terug op 3
	}
}

// Ray-tracing-EFFECTEN aan/uit. r.RayTracing zelf is een opstart-CVar (niet live te schakelen), maar de
// effecten (schaduwen/reflecties/AO/GI via RT) wel - en die zijn de render-thread-kost. ForceRayTracingEffects=0
// zet ze allemaal uit; de losse schakelaars als extra zekerheid. -1 = terug naar de standaard (effecten aan).
void WeedShop_ApplyRayTracing(bool bOff)
{
	IConsoleManager& CM = IConsoleManager::Get();
	auto SetCV = [&](const TCHAR* Name, int32 Val)
	{
		if (IConsoleVariable* CV = CM.FindConsoleVariable(Name)) { CV->Set(Val, ECVF_SetByConsole); }
	};
	SetCV(TEXT("r.RayTracing.ForceRayTracingEffects"), bOff ? 0 : -1);
	SetCV(TEXT("r.RayTracing.Shadows"),               bOff ? 0 : 1);
	SetCV(TEXT("r.RayTracing.AmbientOcclusion"),      bOff ? 0 : 1);
	SetCV(TEXT("r.RayTracing.Reflections"),           bOff ? 0 : 1);
	SetCV(TEXT("r.RayTracing.GlobalIllumination"),    bOff ? 0 : 1);
	UE_LOG(LogWeedShop, Log, TEXT("Ray-tracing-effecten %s"), bOff ? TEXT("UIT") : TEXT("AAN"));
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
	SetF(TEXT("r.Streaming.MipBias"),    bPotato ? 3.0f : (Tier == 0 ? 1.5f : (Tier == 1 ? 0.5f : 0.f))); // ramp 3/1.5/0.5/0/0    // veel lagere-res textures runtime
	SetF(TEXT("r.Streaming.PoolSize"),   bPotato ? 250.f : (Tier == 0 ? 500.f : (Tier == 1 ? 800.f : (Tier == 2 ? 1200.f : 2000.f)))); // ramp; texture-pool-EFFECT pas na herstart // kleine texture-pool (VRAM)
	// View-distance GRADUAAL per tier: schaalt ALLE cull-afstanden mee (NPC's, gebouwen/props-HISMs, foliage,
	// algemene mesh-draw-distance). Lager = dichterbij cullen = meer FPS. Potato cullt agressief, Epic ziet ver.
	const float ViewDist = bPotato ? 0.35f : (Tier == 0 ? 0.6f : (Tier == 1 ? 0.85f : (Tier == 2 ? 1.0f : 1.3f)));
	SetF(TEXT("r.ViewDistanceScale"),    ViewDist);
	SetF(TEXT("r.LightMaxDrawDistanceScale"), ViewDist); // lampen-cull-afstand schaalt mee met de view distance (potato cullt lampen dichterbij = grote Lighting-winst, behoudt nabije vibe)
	SetF(TEXT("foliage.DensityScale"),   bPotato ? 0.2f : (Tier == 0 ? 0.5f : (Tier == 1 ? 0.75f : 1.f)));
	SetF(TEXT("grass.DensityScale"),     bPotato ? 0.2f : (Tier == 0 ? 0.5f : (Tier == 1 ? 0.75f : 1.f)));
	SetF(TEXT("r.MaxAnisotropy"),        bPotato ? 0.f : (Tier == 0 ? 2.f : (Tier == 1 ? 4.f : (Tier == 2 ? 8.f : 16.f)))); // ramp 0/2/4/8/16
	SetF(TEXT("r.Decal.FadeScreenSizeMult"), bPotato ? 0.25f : (Tier <= 1 ? 0.6f : 1.f)); // LAGER = MEER cullen! De mult schaalt de schijnbare scherm-grootte (CurrentScreenSize=(R/Dist)*Mult); hoog hield decals juist LANGER in beeld -> 4.0 was OMGEKEERD (856 decals bleven renderen). 0.25 op potato = verre/sub-pixel decals vallen uit de decal-pass = ~2,5-3,5ms Draw eraf (geverifieerd tegen UE5.8 CalculateDecalFadeAlpha)

	// === POST-STACK per tier (Potato = HARD UIT, dan oplopend). Bloom/AO/SSR/DoF/lensflare reden voorheen mee op
	// scalability-0 (= NIET uit); nu expliciet per tier -> Potato echt barebones + elke tier reset deterministisch.
	const int32 Q = bPotato ? 0 : (Tier + 1); // 0=Potato 1=Low 2=Med 3=High 4=Epic
	SetF(TEXT("r.BloomQuality"),               Q <= 0 ? 0.f : (Q == 1 ? 2.f : (Q == 2 ? 4.f : 5.f)));        // Potato: bloom UIT
	SetF(TEXT("r.AmbientOcclusionLevels"),     Q <= 1 ? 0.f : (Q == 2 ? 1.f : (Q == 3 ? 2.f : 3.f)));        // AO uit op Potato+Low
	SetF(TEXT("r.AmbientOcclusionMaxQuality"), Q <= 1 ? 0.f : 100.f);
	SetF(TEXT("r.SSR.Quality"),                Q <= 1 ? 0.f : (Q == 2 ? 1.f : (Q == 3 ? 2.f : 3.f)));        // screen-space reflections uit op Potato+Low
	SetF(TEXT("r.DepthOfFieldQuality"),        Q <= 1 ? 0.f : (Q == 2 ? 1.f : 2.f));                         // DoF uit op Potato+Low
	SetF(TEXT("r.SSGI.Quality"),               0.f);                                                          // screen-space GI ALTIJD uit (duur; Lumen doet GI op Epic)
	SetF(TEXT("r.LensFlareQuality"),           Q <= 1 ? 0.f : 2.f);                                          // lens flare uit op Potato+Low
	SetF(TEXT("r.SceneColorFringeQuality"),    Q <= 0 ? 0.f : 1.f);                                          // chromatic aberration uit op Potato
	SetF(TEXT("r.PostProcessAAQuality"),       Q <= 0 ? 0.f : (Q == 1 ? 2.f : (Q == 2 ? 4.f : (Q == 3 ? 5.f : 6.f)))); // TSR-kwaliteit; methode blijft TSR = nette upscale van de 42% Potato-render-res

	// --- LUMEN: de #1 GPU-kost. Alleen op EPIC. Potato/Low/Medium/High draaien zonder (de speler vindt
	//     de Lumen-uit-look prima), wat op High ~20-25 FPS scheelt. Epic blijft de mooie-maar-dure optie.
	WeedShop_ApplyLumen(Tier < 3);
	// --- RAY TRACING + VIRTUAL SHADOW MAPS: zware render-features die de Potato-tier vroeger NIET uitzette ---
	WeedShop_ApplyRayTracing(Tier < 3);  // RT-effecten alleen op Epic
	WeedShop_ApplyVSM(Tier <= 0);        // VSM uit op Potato + Low (gewone shadow maps = veel goedkoper)

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
	SetF(TEXT("r.Shadow.RadiusThreshold"),  bPotato ? 0.05f : (Tier == 0 ? 0.04f : (Tier == 1 ? 0.03f : (Tier == 2 ? 0.02f : 0.01f)))); // monotoon 0.05/0.04/0.03/0.02/0.01 (was niet-monotoon: High 0.03 cullde meer dan Med+Epic 0.01)

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
void WeedShop_ReadGfxFlags(bool& bLumenOff, bool& bPotato, bool& bMotionBlurOff, bool& bVSMOff, bool& bRTOff)
{
	FString T;
	FFileHelper::LoadFileToString(T, *(FPaths::ProjectSavedDir() / TEXT("GraphicsConfig.txt")));
	bLumenOff      = T.Contains(TEXT("LumenOff=1"));
	bPotato        = T.Contains(TEXT("Potato=1"));
	bMotionBlurOff = T.Contains(TEXT("MotionBlurOff=1"));
	bVSMOff        = T.Contains(TEXT("VSMOff=1"));
	bRTOff         = T.Contains(TEXT("RTOff=1"));
}

void WeedShop_WriteGfxFlags(bool bLumenOff, bool bPotato, bool bMotionBlurOff, bool bVSMOff, bool bRTOff)
{
	const FString Out = FString::Printf(TEXT("LumenOff=%d\nPotato=%d\nMotionBlurOff=%d\nVSMOff=%d\nRTOff=%d\n"),
		bLumenOff ? 1 : 0, bPotato ? 1 : 0, bMotionBlurOff ? 1 : 0, bVSMOff ? 1 : 0, bRTOff ? 1 : 0);
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
		static const FSlateColorBrush BarFillBrush(FLinearColor(0.4f, 0.85f, 0.45f)); // PRE-getint groen -> nooit een witte flash voor de tint pakt
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
						.FillColorAndOpacity(FLinearColor::White) // brush is al groen -> neutrale tint (geen dubbele tint)
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
		// De movie blijft staan tot de in-game COVER er is om naadloos over te nemen (geen gat waarin je de wereld
		// ziet inladen = de witte flash). Safety-cap (90s) zodat 'ie NOOIT blijft hangen als de cover niet verschijnt.
		if (WeedShop_IsCoverUp() || E > 90.f)
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
		// BOOT-SCHERM UITGESCHAKELD (2026-07-02): het movie-scherm op de allereerste map-load gaf een FLAKY
		// D3D12-crash (EXCEPTION_ACCESS_VIOLATION @0x260 op een PSO-worker-thread, ~2 van de 3 boots) — de
		// movie rendert met bWaitForManualStop dwars door de vroege PSO-precache-storm heen. Terug naar het
		// oude gedrag: GEEN scherm bij de boot (zwart venster is cosmetisch; een flaky crash is release-
		// blokkerend). Race-veilige variant hoort pas te starten NA OnFEngineLoopInitComplete — zie ROADMAP B.15.
		if (!GShowGameLoadingScreen) { return; }
		// In-game transitie (New Game/Load/Continue/Join): de bekende twee-schermen-flow hieronder.
		GShowGameLoadingScreen = false;

		// TWEE SCHERMEN, NAADLOOS IN ELKAAR OVERLOPEND:
		// 1) Dit MOVIE-scherm dekt de engine-map-load en VERDWIJNT automatisch zodra het level klaar is
		//    (bAutoCompleteWhenLoadingCompletes + GEEN manual-wait -> nooit een hang). Z'n progress-bar
		//    staat op dat moment NIET op 100% maar op de gedeelde laad-stand (E/12).
		// 2) Daaronder ligt dan al het in-game COVER-scherm (UBootCoverWidget, PhoneClientComponent::
		//    EnsureWidget): exact dezelfde look + DEZELFDE gedeelde timer/bar/tekst. Het loopt dus
		//    gewoon DOOR vanaf waar de movie zat (geen 'klaar -> opnieuw') tot je stil in de kamer staat.
		FLoadingScreenAttributes Attr;
		Attr.bAutoCompleteWhenLoadingCompletes = false; // NIET auto-weg op level-load -> de movie blijft tot de cover er is (geen gat/witte flash)
		Attr.bMoviesAreSkippable = false;
		Attr.bWaitForManualStop = true;                 // stopt pas via StopMovie hieronder (cover op beeld, of safety-cap) -> nooit een hang
		Attr.MinimumLoadingScreenDisplayTime = 1.0f;
		Attr.WidgetLoadingScreen = SNew(SWeedLoadingScreen);
		GetMoviePlayer()->SetupLoadingScreen(Attr);
	}

	bool bBootScreenShown = false; // de boot-variant maar 1x (elke volgende load beslist GShowGameLoadingScreen)
};

// Secundaire game-module (de primaire blijft 'ThePlugSIM' uit de template).
IMPLEMENT_MODULE(FWeedShopCoreModule, WeedShopCore);

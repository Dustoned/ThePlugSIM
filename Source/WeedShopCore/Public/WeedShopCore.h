// WeedShopCore — module-header. Declareert de gedeelde log-categorie voor alle game-logica.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Gebruik in code als:  UE_LOG(LogWeedShop, Log, TEXT("..."));
DECLARE_LOG_CATEGORY_EXTERN(LogWeedShop, Log, All);

// Geld: rond cents af op hele euro's (multiple van 100). De game toont/rekent alles in hele euro's.
inline int64 WeedRoundEuros(int64 Cents) { return ((Cents < 0 ? Cents - 50 : Cents + 50) / 100) * 100; }

// Roep dit aan vlak vóór een level-reload die IN-GAME gaat (New Game/Load/Continue): de eerstvolgende
// map-load toont dan het laadscherm. De boot naar het hoofdmenu zet dit NIET -> daar geen laadscherm.
WEEDSHOPCORE_API void WeedShop_RequestGameLoadingScreen();

// Stopt het in-game laadscherm handmatig (het blijft staan tot dit wordt aangeroepen). De HUD roept dit
// aan zodra de speler stil in de kamer staat (floor ingestreamd), met een eigen safety-cap.
WEEDSHOPCORE_API void WeedShop_StopGameLoadingScreen();

// Annuleert een lopende loading-cover flow (bv. Back to menu op de cover). Dit stopt de movie
// EN reset de gedeelde loading-flags/timer, zodat het hoofdmenu niet meteen weer een cover spawnt.
WEEDSHOPCORE_API void WeedShop_CancelGameLoadingScreen();

// "Kamer klaar"-vlag: DoorRetrofitter zet 'm zodra de penthouse-vloer onder de thuis-plek is ingestreamd.
// Het in-game cover-scherm (UBootCoverWidget) blijft over beeld tot dit waar is. Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetRoomReady(bool bReady);
WEEDSHOPCORE_API bool WeedShop_IsRoomReady();

// "Loading-cover is weg"-vlag (historische naam: CrowdSpawned). De BootCoverWidget zet 'm zodra de cover wegfadet.
// DoorRetrofitter leest 'm: cover nog op beeld -> crowd VERSNELD materialiseren (hitch verborgen); cover weg -> 1/keer
// (smooth gameplay). Reset bij een nieuwe load (verse cover die weer over beeld komt).
WEEDSHOPCORE_API void WeedShop_SetCrowdSpawned(bool bSpawned);
WEEDSHOPCORE_API bool WeedShop_IsCrowdSpawned();

// "Cover staat op beeld"-vlag: de BootCoverWidget (PhoneClientComponent) zet 'm zodra 'ie aangemaakt is. De
// MOVIE-loadingscreen blijft staan tot dit waar is -> de cover neemt naadloos over, geen gat waarin je de
// wereld ziet inladen (de witte flash). Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetCoverUp(bool bUp);
WEEDSHOPCORE_API bool WeedShop_IsCoverUp();

// "Stad omgebouwd"-vlag: DoorRetrofitter zet 'm zodra de gebakken kamers (BakedRooms-overlay) geladen+
// zichtbaar zijn en de ombouw-sweep een volle pass niks nieuws meer vond. De BootCoverWidget wacht hierop
// (op maps MET een DoorRetrofitter, zie WeedShop_IsCityRetroActive). Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetCityConverted(bool bConverted);
WEEDSHOPCORE_API bool WeedShop_IsCityConverted();

// "Crowd warm"-vlag: DoorRetrofitter zet 'm zodra alle spawnbare walkers binnen bereik (18000cm) een
// lichaam hebben of het lichaam-plafond staat. Host-side; co-op clients slaan deze cover-gate over
// (daar komen de zichtbare bodies via replicatie). Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetCrowdWarm(bool bWarm);
WEEDSHOPCORE_API bool WeedShop_IsCrowdWarm();

// "DoorRetrofitter actief"-vlag: gezet in ADoorRetrofitter::BeginPlay (alleen op de pack-map). Maps
// ZONDER retrofitter (geen stad-ombouw/crowd/thuis-vloer) slaan de bijbehorende cover-gates over,
// anders zou de cover daar altijd tot de harde cap blijven staan. Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetCityRetroActive(bool bActive);
WEEDSHOPCORE_API bool WeedShop_IsCityRetroActive();

// "Boot-laadscherm actief": de allereerste map-load (naar het hoofdmenu) toont nu ook het movie-scherm
// i.p.v. ~50s zwart venster. Zolang dit staat maakt EnsureWidget GEEN in-game cover aan (het menu heeft
// geen wereld-opbouw); PhoneClientComponent::ShowMainMenu stopt de movie en reset dit.
WEEDSHOPCORE_API void WeedShop_SetBootLoading(bool bBoot);
WEEDSHOPCORE_API bool WeedShop_IsBootLoading();

// "Lokale pawn staat op z'n eindpositie"-vlag: de speler-character zet 'm zodra 'ie na de load echt op de
// juiste (thuis/spawn) plek staat en bespeelbaar is. De co-op JOINER skipt de host-side CrowdWarm-gate; deze
// vlag is z'n vervanging: de cover blijft OOK staan tot dit waar is -> de cover verdwijnt pas als de speler
// echt geplaatst is (geen rauwe-beach-flits terwijl de pawn nog verplaatst wordt). Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetLocalPawnPlaced(bool b);
WEEDSHOPCORE_API bool WeedShop_IsLocalPawnPlaced();

// Verstreken seconden SINDS het einde van de map-load (PostLoadMapWithWorld). < 0 zolang de load nog loopt.
// De movie gebruikt dit voor een noodrem-cap die pas telt NA de load (E deelt de timer met JoinLan en stond
// bij een joiner al > 90 op het moment dat de load klaar was -> de oude 'E > 90'-cap doodde de movie te vroeg).
WEEDSHOPCORE_API double WeedShop_SecondsSinceLoadEnd();

// Gedeelde laad-timer + tekst: zowel de movie-loadingscreen als het in-game cover-scherm gebruiken deze,
// zodat ze er IDENTIEK uitzien en de progress/tekst naadloos doorlopen (geen zichtbare overgang).
WEEDSHOPCORE_API double WeedShop_LoadElapsedSeconds();
WEEDSHOPCORE_API FString WeedShop_LoadLine(int32 Step);

// Lumen (GI + reflecties) aan/uit via de render-cvars. Gedeeld door de settings-knop en de
// opstart-toepassing zodat de speler-keuze altijd dezelfde weg loopt.
WEEDSHOPCORE_API void WeedShop_ApplyLumen(bool bLumenOff);

// Grafische kwaliteit-tier toepassen. Tier: -1 = Potato (onder Low, voor zwakke pc's:
// scalability 0 + 50% render-resolutie, minimale textures/streaming, lage view-distance/
// foliage/schaduwen, Lumen uit), 0 = Low, 1 = Medium, 2 = High, 3 = Epic.
// bSkipFeatureGates: Lumen/RT/VSM-gates overslaan (boot-pad zet ze er direct na met de user-toggles
// -> voorkomt dure dubbele VSM-pool-toggles die de wereld-load seconden kostten).
WEEDSHOPCORE_API void WeedShop_ApplyGraphicsTier(int32 Tier, bool bSkipFeatureGates = false);

// Motion blur aan/uit + de cvar-gebaseerde grafische vlaggen (Lumen/Potato/MotionBlur) in
// Saved/GraphicsConfig.txt als geheel lezen/schrijven (zo wist het zetten van één vlag de andere niet).
WEEDSHOPCORE_API void WeedShop_ApplyMotionBlur(bool bOff);
WEEDSHOPCORE_API void WeedShop_ApplyVSM(bool bOff);          // Virtual Shadow Maps aan/uit
WEEDSHOPCORE_API void WeedShop_ApplyDistanceFieldGI(bool bOff); // distance-field-AO + global-DF aan/uit (VRAM)
WEEDSHOPCORE_API void WeedShop_ApplyBeachShadowQuality(bool bPotato = false);   // smooth moving-sun VSM (Fortnite-recept); potato = extra-zuinig (kwart-res moving, harde rays)
WEEDSHOPCORE_API void WeedShop_ApplyBeachShadows(bool bPotato);                 // beach-schaduw-GATE per tier (gedeeld: BeginPlay + Preset + sliders) -> Potato=uit, Low+=VSM. Deterministisch bij elke tier-wissel.
WEEDSHOPCORE_API void WeedShop_ApplyRayTracing(bool bOff);   // ray-tracing-effecten (RT-schaduwen+AO) aan/uit; alleen de "Ray tracing (experimental)"-toggle zet ze aan, presets nooit
WEEDSHOPCORE_API void WeedShop_ReadGfxFlags(bool& bLumenOff, bool& bPotato, bool& bMotionBlurOff, bool& bVSMOff, bool& bRTOff);
WEEDSHOPCORE_API int32 WeedShop_ReadTier();                  // opgeslagen kwaliteit-tier (-1=Potato, 0..3); key ontbreekt -> legacy-afleiding (Potato-vlag / GUS-overall-level)
WEEDSHOPCORE_API void WeedShop_WriteGfxFlags(bool bLumenOff, bool bPotato, bool bMotionBlurOff, bool bVSMOff, bool bRTOff, int32 Tier);

// Renderer-keuze DirectX 11 vs 12. De RHI wordt bij het OPSTARTEN gekozen (WindowsDynamicRHI); UE leest
// daarvoor zelf de key [D3DRHIPreference] PreferredRHI uit GameUserSettings.ini. Wij hoeven die dus alleen
// te schrijven - geldt na herstart. DX11=SM5 (geen Nanite/Lumen/VSM, lichter, omzeilt de D3D12 GPU-Scene
// reserved-buffer-VRAM-crash); DX12=SM6 (volledige features). Beide shader-formats worden al gecookt.
WEEDSHOPCORE_API int32 WeedShop_ReadPreferredRHI();          // 0 = DX12 (default), 1 = DX11
WEEDSHOPCORE_API void  WeedShop_WritePreferredRHI(bool bDX11);

// GEBAKKEN DATA: wereld-configuratie (map-border, kamer-jobs, deur-sloten, licht-instellingen)
// leeft tijdens het ontwikkelen in Saved/, maar Saved/ gaat NIET mee in een gepackagede build.
// Een snapshot staat daarom in Content/BakedData/ (packaged via DirectoriesToAlwaysStageAsUFS).
// Lezen gaat via deze resolver: ontbreekt het bestand in Saved/ (verse install), dan wordt de
// gebakken versie daarheen gekopieerd - alle bestaande lees/schrijf/append-code blijft op Saved/.
namespace WeedData
{
	inline FString File(const FString& Name)
	{
		const FString SavedPath = FPaths::ProjectSavedDir() / Name;
		if (!FPaths::FileExists(SavedPath))
		{
			const FString BakedPath = FPaths::ProjectContentDir() / TEXT("BakedData") / Name;
			if (FPaths::FileExists(BakedPath))
			{
			IFileManager::Get().Copy(*SavedPath, *BakedPath);
			}
		}
		return SavedPath;
	}

	// Map-variant (RoomTemplates): ontbrekende bestanden uit de gebakken snapshot bijkopieren.
	inline FString Dir(const FString& Name)
	{
		const FString SavedDir = FPaths::ProjectSavedDir() / Name;
		const FString BakedDir = FPaths::ProjectContentDir() / TEXT("BakedData") / Name;
		TArray<FString> BakedFiles;
		IFileManager::Get().FindFiles(BakedFiles, *(BakedDir / TEXT("*")), true, false);
		for (const FString& F : BakedFiles)
		{
			const FString Dst = SavedDir / F;
			if (!FPaths::FileExists(Dst)) { IFileManager::Get().Copy(*Dst, *(BakedDir / F)); }
		}
		return SavedDir;
	}

	// Verwijder een data-bestand permanent: uit Saved EN uit de gebakken snapshot (anders
	// staat het er volgende sessie gewoon weer).
	inline void DeleteFile(const FString& Name)
	{
		IFileManager::Get().Delete(*(FPaths::ProjectSavedDir() / Name), false, false, true);
		IFileManager::Get().Delete(*(FPaths::ProjectContentDir() / TEXT("BakedData") / Name), false, false, true);
	}

	// Bij opstart van een verse (packaged) install: kopieer ALLE gebakken data-bestanden naar Saved/,
	// zodat ook code die direct uit ProjectSavedDir() leest (en dus de File()-resolver omzeilt) z'n data
	// vindt. Kopieert alleen ontbrekende bestanden -> in de editor (waar Saved/ al gevuld is) een no-op,
	// en speler-wijzigingen in een bestaande install blijven staan. Eén niveau submappen mee (RoomTemplates).
	inline void RestoreAll()
	{
		IFileManager& FM = IFileManager::Get();
		const FString BakedRoot = FPaths::ProjectContentDir() / TEXT("BakedData");
		const FString SavedRoot = FPaths::ProjectSavedDir();
		if (!FPaths::DirectoryExists(BakedRoot)) { return; }

		TArray<FString> Files;
		FM.FindFiles(Files, *(BakedRoot / TEXT("*.*")), true, false);
		for (const FString& F : Files)
		{
			const FString Dst = SavedRoot / F;
			if (!FPaths::FileExists(Dst)) { FM.Copy(*Dst, *(BakedRoot / F)); }
		}

		TArray<FString> Dirs;
		FM.FindFiles(Dirs, *(BakedRoot / TEXT("*")), false, true);
		for (const FString& D : Dirs)
		{
			TArray<FString> SubFiles;
			FM.FindFiles(SubFiles, *(BakedRoot / D / TEXT("*.*")), true, false);
			for (const FString& F : SubFiles)
			{
				const FString Dst = SavedRoot / D / F;
				if (!FPaths::FileExists(Dst)) { FM.Copy(*Dst, *(BakedRoot / D / F)); }
			}
		}
	}
}

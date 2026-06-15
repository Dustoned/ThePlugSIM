// WeedShopCore — module-header. Declareert de gedeelde log-categorie voor alle game-logica.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Gebruik in code als:  UE_LOG(LogWeedShop, Log, TEXT("..."));
DECLARE_LOG_CATEGORY_EXTERN(LogWeedShop, Log, All);

// Roep dit aan vlak vóór een level-reload die IN-GAME gaat (New Game/Load/Continue): de eerstvolgende
// map-load toont dan het laadscherm. De boot naar het hoofdmenu zet dit NIET -> daar geen laadscherm.
WEEDSHOPCORE_API void WeedShop_RequestGameLoadingScreen();

// Stopt het in-game laadscherm handmatig (het blijft staan tot dit wordt aangeroepen). De HUD roept dit
// aan zodra de speler stil in de kamer staat (floor ingestreamd), met een eigen safety-cap.
WEEDSHOPCORE_API void WeedShop_StopGameLoadingScreen();

// "Kamer klaar"-vlag: DoorRetrofitter zet 'm zodra de penthouse-vloer onder de thuis-plek is ingestreamd.
// Het in-game cover-scherm (UBootCoverWidget) blijft over beeld tot dit waar is. Reset bij een nieuwe load.
WEEDSHOPCORE_API void WeedShop_SetRoomReady(bool bReady);
WEEDSHOPCORE_API bool WeedShop_IsRoomReady();

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
WEEDSHOPCORE_API void WeedShop_ApplyGraphicsTier(int32 Tier);

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

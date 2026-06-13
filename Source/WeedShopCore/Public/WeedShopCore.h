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

// Lumen (GI + reflecties) aan/uit via de render-cvars. Gedeeld door de settings-knop en de
// opstart-toepassing zodat de speler-keuze altijd dezelfde weg loopt.
WEEDSHOPCORE_API void WeedShop_ApplyLumen(bool bLumenOff);

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
}

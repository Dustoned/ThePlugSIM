#include "Game/WeedGameInstance.h"

#include "UI/WeedToast.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Containers/Ticker.h" // FTSTicker: pauze-immune delay (menu-wereld staat op pauze)
#include "UnrealClient.h" // FScreenshotRequest (-WeedShotEvery)

void UWeedGameInstance::Init()
{
	Super::Init();

	// Bind de engine-brede failure-delegates. Deze vuren bij een verbroken net-connectie (time-out, host
	// weg, kick) en bij een mislukte travel. Zonder handler laat UE de client stil hangen tot de time-out
	// en valt 'm dan zonder melding terug -> precies de D27-klacht. GameInstance leeft de hele sessie, dus
	// hier binden is veilig (geen dubbele bindings, geen unbind nodig).
	if (UEngine* E = GetEngine())
	{
		E->OnNetworkFailure().AddUObject(this, &UWeedGameInstance::HandleNetworkFailure);
		E->OnTravelFailure().AddUObject(this, &UWeedGameInstance::HandleTravelFailure);
	}

	// Dev-launch-flags: alleen binden als er uberhaupt een flag op de command-line staat (release-spelers
	// starten zonder flags -> nul overhead, nul gedrag).
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("WeedAutoContinue")) || FCString::Strifind(Cmd, TEXT("WeedGameHour=")) ||
		FCString::Strifind(Cmd, TEXT("WeedTPSpot=")) || FCString::Strifind(Cmd, TEXT("WeedExec=")) ||
		FCString::Strifind(Cmd, TEXT("WeedShotEvery=")))
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UWeedGameInstance::OnDevFlagsMapLoaded);
		UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] dev-flags gedetecteerd op de command-line; per map-load actief"));
	}
}

void UWeedGameInstance::OnDevFlagsMapLoaded(UWorld* World)
{
	if (!World) { return; }
	DevFlagRetries = 0;
	// Even wachten tot de spawn/menu-flow klaar is. CORE-ticker, GEEN world-timer: de hoofdmenu-wereld
	// staat op pauze waardoor world-timers daar NOOIT vuren (daar sneuvelden eerdere pogingen stil op).
	TWeakObjectPtr<UWeedGameInstance> WeakThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, World](float)
	{
		if (UWeedGameInstance* GI = WeakThis.Get()) { GI->RunDevFlags(World); }
		return false; // eenmalig
	}), 4.f);
}

void UWeedGameInstance::RunDevFlags(UWorld* World)
{
	if (!World || World != GetWorld()) { return; } // map inmiddels gewisseld -> de nieuwe load vuurt opnieuw
	const TCHAR* Cmd = FCommandLine::Get();
	const FString MapNm = World->GetMapName();
	APlayerController* PC = GetFirstLocalPlayerController(World);
	APawn* Pn = PC ? PC->GetPawn() : nullptr;
	UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] map=%s pc=%d pawn=%d"), *MapNm, PC ? 1 : 0, Pn ? 1 : 0);

	// HOOFDMENU: alleen auto-continue (RequestContinue = dezelfde route als de Continue-knop).
	if (MapNm.Contains(TEXT("MainMenu")))
	{
		if (FParse::Param(Cmd, TEXT("WeedAutoContinue")))
		{
			if (USaveGameSubsystem* Sv = GetSubsystem<USaveGameSubsystem>())
			{
				UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] auto-continue (menu -> laatste save)"));
				Sv->RequestContinue();
			}
		}
		return;
	}

	// WERELD: pawn nodig voor TP; nog niet possessed -> kort opnieuw proberen (max 5x).
	if (!Pn && DevFlagRetries++ < 5)
	{
		FTimerHandle Th;
		World->GetTimerManager().SetTimer(Th, FTimerDelegate::CreateUObject(this, &UWeedGameInstance::RunDevFlags, World), 2.f, false);
		return;
	}

	float DevHour = -1.f;
	if (FParse::Value(Cmd, TEXT("WeedGameHour="), DevHour) && DevHour >= 0.f)
	{
		if (AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>())
		{
			if (UDayCycleComponent* Day = GS->GetDayCycle())
			{
				Day->SetTimeOfDaySeconds(Day->TimeOfDayFromClockMinutes((int32)(DevHour * 60.f)));
				UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] klok gezet op %.2f uur"), DevHour);
			}
		}
	}

	int32 DevSpot = -1;
	if (FParse::Value(Cmd, TEXT("WeedTPSpot="), DevSpot) && DevSpot >= 0 && Pn)
	{
		// Regel-formaat: "F9 | map=... | pos=(-3163, 18539, 99) | yaw=90"
		FString F = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
		if (!FPaths::FileExists(F)) { F = FPaths::ProjectContentDir() / TEXT("BakedData/MarkedSpots.txt"); }
		TArray<FString> SpotLines;
		FFileHelper::LoadFileToStringArray(SpotLines, *F);
		if (SpotLines.IsValidIndex(DevSpot))
		{
			const FString& Ln = SpotLines[DevSpot];
			const int32 P0 = Ln.Find(TEXT("pos=("));
			int32 P1 = INDEX_NONE;
			FString T = (P0 != INDEX_NONE) ? Ln.Mid(P0 + 5) : FString();
			if (T.FindChar(TEXT(')'), P1))
			{
				TArray<FString> Parts;
				T.Left(P1).ParseIntoArray(Parts, TEXT(","), true);
				if (Parts.Num() >= 3)
				{
					const FVector Pos(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
					Pn->SetActorLocation(Pos + FVector(0.f, 0.f, 120.f));
					UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] teleport naar spot %d: %s"), DevSpot, *Pos.ToString());
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] spot %d niet gevonden in %s (%d regels)"), DevSpot, *F, SpotLines.Num());
		}
	}

	FString DevExec;
	if (FParse::Value(Cmd, TEXT("WeedExec="), DevExec) && !DevExec.IsEmpty() && PC)
	{
		TArray<FString> Cmds;
		DevExec.ParseIntoArray(Cmds, TEXT(";"), true);
		for (const FString& C : Cmds)
		{
			PC->ConsoleCommand(C.TrimStartAndEnd());
			UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] exec: %s"), *C);
		}
	}

	float DevShotEvery = -1.f;
	if (FParse::Value(Cmd, TEXT("WeedShotEvery="), DevShotEvery) && DevShotEvery > 0.f)
	{
		// Herhalende screenshots (auto-genummerd in Saved/Screenshots) voor visuele zelf-verificatie.
		FTimerHandle ShotTh;
		World->GetTimerManager().SetTimer(ShotTh, FTimerDelegate::CreateLambda([]()
		{
			FScreenshotRequest::RequestScreenshot(false);
		}), DevShotEvery, true, 1.f);
		UE_LOG(LogTemp, Display, TEXT("[DEVFLAG] screenshot elke %.1fs"), DevShotEvery);
	}
}

void UWeedGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* /*NetDriver*/, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// Netmode uit de doorgegeven World halen (betrouwbaarder dan GetWorld() tijdens een teardown).
	const ENetMode Mode = World ? World->GetNetMode() : NM_Standalone;

	UE_LOG(LogTemp, Warning, TEXT("[COOP] NetworkFailure (%d): %s [netmode=%d]"),
		(int32)FailureType, *ErrorString, (int32)Mode);

	if (Mode == NM_ListenServer || Mode == NM_DedicatedServer)
	{
		// HOST: een van de spelers is weggevallen. NIET travellen -> de host (en overige joiners) spelen
		// gewoon door. Alleen een korte melding op het host-scherm (lokaal; bij een harde disconnect is er
		// geen betrouwbare pawn om per-speler te routeren).
		UWeedToast::Notify(910, 4.f, FColor(230, 120, 60), TEXT("A player left the session"));
		return;
	}

	if (Mode == NM_Client)
	{
		// JOINER: de sessie is weg. Korte melding, dan een NETTE teardown terug naar het hoofdmenu
		// (zelfde bestemming als bij de boot). ReturnToMainMenu regelt de net-cleanup + travel.
		UWeedToast::Notify(910, 3.f, FColor(230, 120, 60), TEXT("Disconnected from host - returning to menu"));
		ReturnToMainMenu();
		return;
	}

	// Standalone e.d.: niks te doen (kan hier normaal niet komen).
}

void UWeedGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	const ENetMode Mode = World ? World->GetNetMode() : NM_Standalone;

	UE_LOG(LogTemp, Warning, TEXT("[COOP] TravelFailure (%d): %s [netmode=%d]"),
		(int32)FailureType, *ErrorString, (int32)Mode);

	// Een mislukte travel op een client -> laat 'm niet in het niets hangen: terug naar het menu.
	if (Mode == NM_Client)
	{
		UWeedToast::Notify(911, 3.f, FColor(230, 120, 60), TEXT("Connection lost - returning to menu"));
		ReturnToMainMenu();
	}
	// Op de host is een travel-fout zeldzaam en niet fataal voor de sessie -> alleen loggen.
}

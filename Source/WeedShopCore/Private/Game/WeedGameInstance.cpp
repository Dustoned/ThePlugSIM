#include "Game/WeedGameInstance.h"

#include "UI/WeedToast.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"

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

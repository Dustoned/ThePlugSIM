#include "Customer/CustomerSpawner.h"

#include "Customer/CustomerBase.h"
#include "World/CityGenerator.h"
#include "World/CityDoor.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACustomerSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Snel retry-interval zodat de bewoners verschijnen zodra de stad gebouwd is.
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ACustomerSpawner::TrySpawn, 2.0f, true, 1.0f);
	}
}

void ACustomerSpawner::TrySpawn()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }

	// Verlopen klanten opruimen.
	Spawned.RemoveAll([](const TObjectPtr<ACustomerBase>& C) { return !IsValid(C); });

	// De stad-bevolking = bewoners (dealbaar). Eén keer toewijzen zodra de stad gebouwd is.
	// (Geen test/random-modus meer: die ruimde alle klanten op behalve één.)
	if (!bResidentsSpawned) { SpawnResidents(); }
}

namespace
{
	// Stabiele NL-bewonersnaam afgeleid van het huisnummer (zodat elke woning een eigen bewoner heeft).
	FString ResidentNameForNumber(const FString& Num)
	{
		static const TCHAR* First[] = { TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Sanne"), TEXT("Emma"), TEXT("Daan"),
			TEXT("Lotte"), TEXT("Bram"), TEXT("Sven"), TEXT("Fleur"), TEXT("Tim"), TEXT("Noa"), TEXT("Rick"), TEXT("Iris"),
			TEXT("Joost"), TEXT("Mila"), TEXT("Gerrit"), TEXT("Truus"), TEXT("Henk"), TEXT("Willem"), TEXT("Bep"), TEXT("Cor"),
			TEXT("Sjaak"), TEXT("Ria"), TEXT("Dirk"), TEXT("Mieke"), TEXT("Bart"), TEXT("Loes"), TEXT("Ome Ton"), TEXT("Tante An") };
		// Goofy achternamen — grappig als een bewoner "dom" heet.
		static const TCHAR* Last[] = { TEXT("Pannenkoek"), TEXT("Stokvis"), TEXT("Bonk"), TEXT("Knol"), TEXT("Prummel"),
			TEXT("Druif"), TEXT("Kwast"), TEXT("Worst"), TEXT("Toeter"), TEXT("Boterham"), TEXT("Stamppot"), TEXT("Frikandel"),
			TEXT("Snoek"), TEXT("Klont"), TEXT("Hark"), TEXT("Sok"), TEXT("Krent"), TEXT("Pruim"), TEXT("Brok"), TEXT("Plof"),
			TEXT("Kwakkel"), TEXT("Tuthola"), TEXT("Schroef"), TEXT("Knapzak") };
		const uint32 NF = (uint32)UE_ARRAY_COUNT(First);
		const uint32 NL = (uint32)UE_ARRAY_COUNT(Last);
		const uint32 H = GetTypeHash(Num);
		return FString::Printf(TEXT("%s %s"), First[H % NF], Last[(H / NF) % NL]);
	}
}

void ACustomerSpawner::SpawnResidents()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }

	// Eén spawner regelt de bewoners: staan er al bewoners, dan niets doen (geen dubbele).
	for (TActorIterator<ACustomerBase> It(World); It; ++It)
	{
		if (IsValid(*It) && It->IsResident()) { bResidentsSpawned = true; return; }
	}

	// Vind de (lokaal gebouwde) stad met geregistreerde woningen.
	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(World); It; ++It) { City = *It; break; }
	if (!City) { return; }
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	if (Homes.Num() == 0) { return; } // stad nog niet klaar -> volgende tick

	bResidentsSpawned = true;

	// Ruim eerst alle vooraf-geplaatste/los rondslingerende klant-NPC's op (bv. test-NPC's die in de
	// level-map midden in de stad staan). Op dit punt bestaan er nog geen bewoners, dus dit raakt alleen
	// die ongewenste losse NPC's. Daarna spawnen we de echte bewoners op de woningen.
	for (TActorIterator<ACustomerBase> It(World); It; ++It)
	{
		if (IsValid(*It)) { It->Destroy(); }
	}

	AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	TSubclassOf<ACustomerBase> Cls = CustomerClass;
	if (!Cls) { Cls = ACustomerBase::StaticClass(); }

	// ELKE genummerde woning krijgt een bewoner (deur op slot met naam). Een gespreide subset loopt
	// ook ECHT rond (MaxResidents; 0 = allemaal). De rest "woont er" via de op-slot-deur met naam.
	const int32 Total = Homes.Num();
	const bool bAll = (MaxResidents <= 0);
	const int32 Want = bAll ? Total : FMath::Clamp(MaxResidents, 0, Total);
	const int32 Step = (Want > 0) ? FMath::Max(1, Total / Want) : (Total + 1);

	// De 3 koopbare panden (starter-flatje, rijtjeshuis, grote kamer) blijven ALTIJD vrij: geen bewoner,
	// deur als "TE KOOP" totdat de speler 'm koopt (dan ontgrendelt de PhoneClientComponent 'm lokaal).
	TArray<FCityPropertyOffer> Offers; City->GetPropertyOffers(Offers);
	TSet<int32> ForSale;
	for (const FCityPropertyOffer& O : Offers) { for (int32 Idx : O.Homes) { ForSale.Add(Idx); } }

	int32 Made = 0;
	for (int32 i = 0; i < Total; ++i)
	{
		const FApartmentHome& H = Homes[i];
		if (ForSale.Contains(i))
		{
			if (ACityDoor* Dr = H.Door.Get()) { Dr->SetForSale(); }
			continue; // geen NPC in een koopbaar pand
		}
		const bool bPhysical = (Made < Want) && (Step <= 0 ? true : (i % Step == 0));

		if (bPhysical)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (ACustomerBase* C = World->SpawnActor<ACustomerBase>(Cls, H.DoorPos, FRotator::ZeroRotator, SP))
			{
				if (Reg) { C->NpcId = Reg->AssignNpc(); } // registry voor stats/contact; deurnaam komt per-nummer
				C->SetupResident(H.DoorPos, H.InteriorPos, H.Number);
				if (Made == 0) // eerste bewoner = gegarandeerde koopklare test-klant
				{
					C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f;
					C->BecomeBuyerNow();
				}
				if (!C->GetController()) { C->SpawnDefaultController(); }
				Spawned.Add(C);
				++Made;
			}
		}
		// Deur op slot met een UNIEKE per-huisnummer naam (de registry round-robin kon namen herhalen,
		// waardoor meerdere huizen "dezelfde bewoner" leken te hebben). Elk huisnummer -> eigen naam.
		const FString DoorName = ResidentNameForNumber(H.Number);
		if (ACityDoor* Dr = H.Door.Get()) { Dr->SetResident(DoorName); }
	}
}

void ACustomerSpawner::SetupTestCustomer(ACustomerBase* C, const FVector& Park)
{
	if (!IsValid(C)) { return; }
	if (C->PatienceSeconds > 1.0e8f) { return; } // al ingesteld als test-klant

	C->Respect = 70.f; C->Loyalty = 40.f; C->Addiction = 80.f;
	C->bDespawnAfterServed = false;
	C->BecomeBuyerNow();
	C->PatienceSeconds = 1.0e9f; // vertrekt niet tijdens testen
	C->SetActorLocation(Park);
	C->SetHome(Park);
	C->SetSpot(Park);
	if (!C->GetController()) { C->SpawnDefaultController(); }
	C->WalkTo(Park);
}

#include "Phone/ContactsComponent.h"
#include "UI/WeedToast.h"
#include "UI/WeedUiStyle.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Customer/CustomerBase.h"
#include "Phone/PhoneClientComponent.h"
#include "Npc/NpcRegistryComponent.h"
#include "Progression/StoreComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "Progression/LevelComponent.h"
#include "Engine/Engine.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/StoreCounter.h"
#include "World/DoorRetrofitter.h" // IsInsideHomeRoom: meet-spots binnen een woon-kamer wegfilteren
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

UContactsComponent::UContactsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 5 Hz is ruim genoeg: de drempels zijn 60/150s (nudge/opgeven) en het afspraak-venster is ±2s;
	// MessageTimer telt met DeltaTime dus de uitkomst blijft identiek - alleen goedkoper.
	PrimaryComponentTick.TickInterval = 0.2f;
	SetIsReplicatedByDefault(true);

	static ConstructorHelpers::FObjectFinder<UDataTable> ProdFinder(
		TEXT("/Game/_Project/Data/DT_Products.DT_Products"));
	if (ProdFinder.Succeeded())
	{
		ProductTable = ProdFinder.Object;
	}
}

void UContactsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UContactsComponent, Contacts);
	DOREPLIFETIME(UContactsComponent, Messages);
}

void UContactsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	if (Contacts.Num() > 0)
	{
		MessageTimer += DeltaTime;
		if (MessageTimer >= MessageIntervalSeconds)
		{
			MessageTimer = 0.f;
			SendRandomAppointment();
		}
	}

	CheckAppointments();
}

void UContactsComponent::RegisterContact(FName ContactId, const FText& DisplayName, float Relationship)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone() || HasContact(ContactId))
	{
		return;
	}

	FPhoneContact C;
	C.ContactId = ContactId;
	C.DisplayName = DisplayName;
	C.Relationship = Relationship;
	Contacts.Add(C);

	UE_LOG(LogWeedShop, Log, TEXT("New contact: %s"), *DisplayName.ToString());
	if (GEngine)
	{
		UWeedToast::Notify(-1, 3.f, FColor::Cyan,
			FString::Printf(TEXT("New contact: %s"), *DisplayName.ToString()));
	}
}

bool UContactsComponent::HasContact(FName ContactId) const
{
	return Contacts.ContainsByPredicate([ContactId](const FPhoneContact& C) { return C.ContactId == ContactId; });
}

bool UContactsComponent::GetCycleTime(float& OutNow, float& OutLength) const
{
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UDayCycleComponent* Day = GS->GetDayCycle())
		{
			OutNow = Day->GetTimeOfDaySeconds();
			OutLength = FMath::Max(1.f, Day->DayLengthSeconds + Day->NightLengthSeconds);
			return true;
		}
	}
	return false;
}

void UContactsComponent::SendRandomAppointment()
{
	if (Contacts.Num() == 0)
	{
		return;
	}

	// Telefoon-orders schalen met progressie: alleen contacten (= NPC's die hun nummer al deelden),
	// niet op cooldown, en onder de dag-cap. De KANS + selectie weegt mee met hoe "warm" de relatie is
	// (verslaving/loyaliteit/respect): begin -> zelden een appje, naarmate ze warmer worden steeds vaker.
	UNpcRegistryComponent* Reg = nullptr;
	if (const AWeedShopGameState* GSc = Cast<AWeedShopGameState>(GetOwner())) { Reg = GSc->GetNpcRegistry(); }

	// 's Nachts bijna geen berichten: alleen echt verslaafde klanten appen 's nachts, en veel zeldzamer.
	bool bNight = false;
	if (const AWeedShopGameState* GSn = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UDayCycleComponent* D = GSn->GetDayCycle()) { bNight = D->IsNight(); }
	}
	constexpr float NightAddictMin = 55.f; // alleen flink verslaafden appen 's nachts

	struct FCand { int32 Idx; float Warmth; };
	TArray<FCand> Cands;
	float TopWarmth = 0.f;
	for (int32 i = 0; i < Contacts.Num(); ++i)
	{
		const FName Id = Contacts[i].ContactId;
		if (Reg && Reg->IsOnCooldown(Id)) { continue; }
		if (Reg && !Reg->CanAppointToday(Id)) { continue; }
		// Niet spammen: heeft deze klant al een open (onbeantwoord) verzoek, dan niet nóg een sturen.
		bool bHasOpen = false;
		for (const FPhoneMessage& M : Messages) { if (M.FromContactId == Id && !M.bFromMe && M.Status == 0 && M.WantQty > 0) { bHasOpen = true; break; } }
		if (bHasOpen) { continue; }
		float Warmth = 0.5f, Addict = 30.f;
		if (Reg)
		{
			float R = 0.f, L = 0.f, A = 0.f; FText N;
			Reg->GetStats(Id, R, L, A, N);
			Warmth = FMath::Clamp((A + L + R) / 300.f, 0.f, 1.f); // 0..1 gemiddelde relatie
			Addict = A;
		}
		if (bNight && Addict < NightAddictMin) { continue; } // 's nachts alleen verslaafden
		Cands.Add({ i, Warmth });
		TopWarmth = FMath::Max(TopWarmth, Warmth);
	}
	if (Cands.Num() == 0) { return; } // niemand beschikbaar (cooldown/dag-cap/nacht)

	// Globale kans dat er dit interval iemand appt, schaalt met de warmste relatie (begin laag).
	// 's Nachts veel lager zodat je tijd hebt om te kweken/andere dingen te doen.
	float SendChance = FMath::Clamp(0.12f + TopWarmth * 0.78f, 0.12f, 0.95f);
	if (bNight) { SendChance *= 0.15f; }
	if (FMath::FRand() > SendChance) { return; }

	// Gewogen keuze: warmere NPC's appen vaker (kleine basis zodat ook lauwe contacten soms appen).
	float Sum = 0.f; for (const FCand& Cd : Cands) { Sum += Cd.Warmth + 0.15f; }
	float Roll = FMath::FRandRange(0.f, Sum);
	int32 Pick = Cands[0].Idx;
	for (const FCand& Cd : Cands) { Roll -= (Cd.Warmth + 0.15f); if (Roll <= 0.f) { Pick = Cd.Idx; break; } }
	const FPhoneContact& C = Contacts[Pick];
	if (Reg) { Reg->NoteAppointment(C.ContactId); } // telt mee voor de 1-2/dag-cap

	float Now = 0.f, Length = 1800.f;
	GetCycleTime(Now, Length);

	// Afspraak 60-240 sec in de toekomst (binnen de cyclus).
	const float Offset = FMath::FRandRange(60.f, 240.f);
	const float ApptTime = FMath::Fmod(Now + Offset, Length);

	// Formatteer als HH:MM met DEZELFDE klok als de HUD (dag/nacht-split).
	const int32 TotalMin = ClockMinutesOf(ApptTime);
	const int32 HH = (TotalMin / 60) % 24;
	const int32 MM = TotalMin % 60;

	// Wat + hoeveel wil de klant: DEZELFDE keuze-logica als walk-ins (tier-weging + premium hasj/edibles +
	// soms wiet net boven je bereik). Geeft een volledig product-id (Bag_/Hash_/Edible_<strain>).
	int32 WantQty = 1;
	const FName WantProduct = ACustomerBase::PickDesiredProduct(Cast<AWeedShopGameState>(GetOwner()), ProductTable, C.ContactId, WantQty);
	FName WantStrain = NAME_None;
	{ FString L, R; if (WantProduct.ToString().Split(TEXT("_"), &L, &R)) { WantStrain = FName(*R); } }
	const FString WantStr = WantProduct.IsNone() ? TEXT("weed") : WeedUI::PrettyItemName(WantProduct);
	const FString WantClean = WantStr.Replace(TEXT(" bag"), TEXT(""), ESearchCase::IgnoreCase); // "Silver Haze bag" -> "Silver Haze" (gewoon de strain)

	FPhoneMessage Msg;
	Msg.FromContactId = C.ContactId;
	Msg.SenderName = C.DisplayName;
	Msg.AppointmentTimeOfDay = ApptTime;
	Msg.WantStrain = WantStrain;
	Msg.WantQty = WantQty;
	Msg.WantProduct = WantProduct; // volledig product (Bag_/Hash_/Edible_<strain>)
	Msg.SentRealTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f; // voor follow-up/opgeven + reactiesnelheid
	Msg.Kind = (FMath::RandBool()) ? EAppointmentKind::TheyComeToYou : EAppointmentKind::YouGoToThem;

	// --- DAG-ORDER: mid-game variatie. Vanaf level ~12 wordt een afspraak soms een premium VIP-order:
	// een specifieke wiet-strain met een min-THC-eis, een ruimere deadline en een bonus-uitbetaling.
	// Schaalt mee met level (vaker + groter + meer bonus). Beloont een goed gevulde, diverse voorraad.
	const AWeedShopGameState* GSo = Cast<AWeedShopGameState>(GetOwner());
	const int32 PlayerLvl = (GSo && GSo->GetLeveling()) ? GSo->GetLeveling()->GetLevel() : 1;
	{
		const bool bWeedProduct = WantProduct.ToString().StartsWith(TEXT("Bag_"));
		const float OrderChance = FMath::Clamp((PlayerLvl - 10) * 0.025f, 0.f, 0.45f);
		float StrainThc = 0.f;
		if (bWeedProduct && !WantStrain.IsNone() && GSo)
		{
			if (UStoreComponent* St = GSo->GetStore()) { float y = 0.f, g = 0.f; St->GetStrainStats(WantStrain, StrainThc, y, g); }
		}
		if (bWeedProduct && StrainThc > 0.f && !bNight && FMath::FRand() < OrderChance)
		{
			Msg.bOrder = true;
			// Min-THC: net onder de strain-basis, zodat een echte batch van DEZE strain (of sterker) slaagt,
			// maar een goedkopere lager-THC substituut niet. Vergevingsgezind voor kwaliteitsverlies.
			Msg.MinThc = FMath::Max(1.f, FMath::RoundToFloat(StrainThc) - 1.f);
			// Bonus +50% (level 12) oplopend tot +100% (level 50+).
			Msg.BonusMult = FMath::Clamp(1.5f + PlayerLvl / 100.f, 1.5f, 2.0f);
			// Premium = grotere bestelling.
			WantQty = FMath::Max(WantQty, FMath::RandRange(6, 14));
			Msg.WantQty = WantQty;
			// Ruimere deadline (3-7 min) zodat je voorraad kunt halen/aanvullen.
			const float OrderOffset = FMath::FRandRange(180.f, 420.f);
			Msg.AppointmentTimeOfDay = FMath::Fmod(Now + OrderOffset, Length);
			const int32 OTotalMin = ClockMinutesOf(Msg.AppointmentTimeOfDay);
			const int32 OHH = (OTotalMin / 60) % 24;
			const int32 OMM = OTotalMin % 60;
			Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
				? FText::FromString(FString::Printf(TEXT("VIP order\n%dg %s\nmin %.0f%% THC\nReady by %02d:%02d\nI'll come to you."), WantQty, *WantClean, Msg.MinThc, OHH, OMM))
				: FText::FromString(FString::Printf(TEXT("VIP order\n%dg %s\nmin %.0f%% THC\nReady by %02d:%02d\nBring it to my place."), WantQty, *WantClean, Msg.MinThc, OHH, OMM));
		}
	}

	// Adres opzoeken bij de bewoner met dit NpcId, zodat "kom bij mij langs" vertelt WAAR je heen moet.
	// PERF: klant-registry (O(NPC's)) i.p.v. TActorIterator over alle actors - zelfde set.
	// Per-proces registry -> op wereld filteren (PIE/co-op-in-1-proces).
	FString AddrStr;
	for (const TWeakObjectPtr<ACustomerBase>& WCb : ACustomerBase::GetAll())
	{
		ACustomerBase* Cb = WCb.Get();
		if (IsValid(Cb) && Cb->GetWorld() == GetWorld() && Cb->NpcId == C.ContactId && Cb->IsResident()) { AddrStr = Cb->GetHomeNumber(); break; }
	}

	if (!Msg.bOrder)
	{
		Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
			? FText::FromString(FString::Printf(TEXT("Hey, got any %s?\nI need %dg.\nI'll come by at %02d:%02d."), *WantClean, WantQty, HH, MM))
			: (AddrStr.IsEmpty()
				? FText::FromString(FString::Printf(TEXT("Hey, got any %s?\nI need %dg.\nCan you come to my place at %02d:%02d?"), *WantClean, WantQty, HH, MM))
				: FText::FromString(FString::Printf(TEXT("Hey, got any %s?\nI need %dg.\nCome to my place (no. %s) at %02d:%02d?"), *WantClean, WantQty, *AddrStr, HH, MM)));
	}
	else if (Msg.Kind == EAppointmentKind::YouGoToThem && !AddrStr.IsEmpty())
	{
		// Order met huisadres: voeg het adres toe zodat je weet waar je moet leveren.
		const int32 OTotalMin = ClockMinutesOf(Msg.AppointmentTimeOfDay);
		Msg.Body = FText::FromString(FString::Printf(TEXT("VIP order\n%dg %s\nmin %.0f%% THC\nReady by %02d:%02d\nBring it to my place (no. %s)."),
			WantQty, *WantClean, Msg.MinThc, (OTotalMin / 60) % 24, OTotalMin % 60, *AddrStr));
	}

	// COMPETITIVE: dit bericht is voor ÉÉN speler (eigen telefoon). Doel = de favoriete speler van dit contact
	// (TopOwner), anders een willekeurige verbonden speler. Co-op laat ForPlayerId leeg (iedereen ziet het).
	APawn* TargetPawn = nullptr;
	if (const AWeedShopGameState* GScm = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (GScm->IsCompetitive() && GetWorld())
		{
			TArray<APawn*> Pawns;
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr) { Pawns.Add(P); }
			}
			if (Pawns.Num() > 0)
			{
				FString Owner;
				if (UNpcRegistryComponent* Rg = GScm->GetNpcRegistry()) { Owner = Rg->GetTopOwner(C.ContactId); }
				for (APawn* P : Pawns) { if (USaveGameSubsystem::StablePlayerId(P) == Owner) { TargetPawn = P; break; } }
				if (!TargetPawn) { TargetPawn = Pawns[FMath::RandRange(0, Pawns.Num() - 1)]; }
				Msg.ForPlayerId = USaveGameSubsystem::StablePlayerId(TargetPawn);
			}
		}
	}

	StampAndInsert(Msg); // nieuwste bovenaan
	if (Messages.Num() > 40)
	{
		Messages.SetNum(40);
	}

	OnRep_Messages(); // server lokaal broadcasten
	if (GEngine)
	{
		const FString NoteTxt = FString::Printf(TEXT("Message from %s"), *C.DisplayName.ToString());
		if (TargetPawn) { UWeedToast::NotifyPawn(TargetPawn, -1, 4.f, FColor(120, 180, 255), NoteTxt); } // alleen de doelspeler
		else { UWeedToast::Notify(-1, 4.f, FColor(120, 180, 255), NoteTxt); }
	}
}

void UContactsComponent::CheckAppointments()
{
	float Now = 0.f, Length = 1800.f;
	if (!GetCycleTime(Now, Length))
	{
		return;
	}

	// --- Onbeantwoorde verzoeken: na een tijdje "you there?", daarna opgeven (met respect/loyaliteit-straf). ---
	{
		const float RealNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		constexpr float NudgeDelay = 60.f;     // herinnering na 1 min geen antwoord
		constexpr float GiveUpDelay = 150.f;   // geeft op na 2,5 min geen antwoord
		UNpcRegistryComponent* Reg = nullptr;
		if (const AWeedShopGameState* GSc = Cast<AWeedShopGameState>(GetOwner())) { Reg = GSc->GetNpcRegistry(); }

		struct FAct { FName Id; FText Name; int32 Type; }; // 1 = nudge, 2 = opgeven
		TArray<FAct> Acts;
		for (FPhoneMessage& Msg : Messages)
		{
			if (Msg.Status != 0 || Msg.bFromMe || Msg.WantQty <= 0 || Msg.SentRealTime < 0.f) { continue; }
			const float Elapsed = RealNow - Msg.SentRealTime;
			if (!Msg.bNudged && Elapsed >= NudgeDelay) { Msg.bNudged = true; Acts.Add({ Msg.FromContactId, Msg.SenderName, 1 }); }
			else if (Elapsed >= GiveUpDelay) { Msg.Status = 2; Acts.Add({ Msg.FromContactId, Msg.SenderName, 2 }); }
		}
		for (const FAct& Ac : Acts)
		{
			if (Ac.Type == 1)
			{
				PushInfoMessage(Ac.Id, Ac.Name, FText::FromString(TEXT("You there? Still need it, or should I look elsewhere?")));
			}
			else
			{
				PushInfoMessage(Ac.Id, Ac.Name, FText::FromString(TEXT("Nvm, took too long. I'll get it somewhere else.")));
				if (Reg)
				{
					float R = 0.f, L = 0.f, A = 0.f; FText N;
					if (Reg->GetStats(Ac.Id, R, L, A, N))
					{
						Reg->ApplyStats(Ac.Id, FMath::Max(0.f, R - 4.f), FMath::Max(0.f, L - 8.f), A); // wat respect, meer loyaliteit kwijt
					}
					Reg->SetApptCooldownMult(Ac.Id, 2.5f); // laat je daarna langer met rust
					Reg->NoteAppointment(Ac.Id);           // start de (langere) cooldown
				}
			}
		}
	}

	for (FPhoneMessage& Msg : Messages)
	{
		// Alleen geaccepteerde afspraken kondigen we aan.
		if (Msg.bAnnounced || Msg.Status != 1 || Msg.AppointmentTimeOfDay < 0.f)
		{
			continue;
		}
		// Binnen 2 sec van de afspraaktijd -> aankondigen.
		if (FMath::Abs(Now - Msg.AppointmentTimeOfDay) <= 2.f)
		{
			Msg.bAnnounced = true;
			UE_LOG(LogWeedShop, Log, TEXT("Appointment with %s is now."), *Msg.SenderName.ToString());
			if (GEngine)
			{
				const FString Where = (Msg.Kind == EAppointmentKind::TheyComeToYou)
					? FString::Printf(TEXT("%s is on the way!"), *Msg.SenderName.ToString())
					: FString::Printf(TEXT("%s is waiting at their place"), *Msg.SenderName.ToString());
				UWeedToast::Notify(-1, 5.f, FColor::Magenta, FString::Printf(TEXT("Appointment: %s"), *Where));
			}
			SpawnAppointmentCustomer(Msg);
		}
	}
}

// Een LOGISCHE wacht-plek voor een afspraak: een door de speler gemarkeerde meet-spot (MeetSpots.txt) voor
// deze map, anders vlakbij een winkel-toonbank. Geen dak, geen midden-op-de-weg. False = niks gevonden.
static bool PickLogicalMeetSpot(UWorld* W, FVector& Out)
{
	if (!W) { return false; }
	// KAMER-FILTER: kandidaten binnen een woon-kamer (starter, registry-units, competitive) vallen
	// af - een afspraak-NPC wacht in een steegje/hal/bij een toonbank, nooit zomaar binnen in een
	// kamer (stale markers uit oude dev-sessies zetten 'm daar anders neer).
	const ADoorRetrofitter* Retro = nullptr;
	for (TActorIterator<ADoorRetrofitter> RIt(W); RIt; ++RIt) { Retro = *RIt; break; }
	auto DropRoomCands = [Retro](TArray<FVector>& Arr)
	{
		if (Retro) { Arr.RemoveAll([Retro](const FVector& C) { return Retro->IsInsideHomeRoom(C); }); }
	};
	TArray<FVector> Cands;
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MeetSpots.txt"))))
	{
		const FString CurMap = W->GetOutermost()->GetName();
		for (const FString& Raw : Lines)
		{
			TArray<FString> P; Raw.TrimStartAndEnd().ParseIntoArray(P, TEXT("|"));
			if (P.Num() >= 4 && P[0] == CurMap)
			{
				Cands.Add(FVector(FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3])));
			}
		}
	}
	DropRoomCands(Cands);
	// Geen (bruikbare) gemarkeerde plekken -> val terug op de winkels (al gemarkeerd door de speler,
	// dus logisch + bereikbaar) - met dezelfde kamer-filter. PERF: balie-registry i.p.v. iterator
	// (per-proces registry -> op wereld filteren, PIE/co-op-in-1-proces).
	if (Cands.Num() == 0)
	{
		for (const TWeakObjectPtr<AStoreCounter>& WSc : AStoreCounter::GetAll()) { AStoreCounter* Sc = WSc.Get(); if (IsValid(Sc) && Sc->GetWorld() == W) { Cands.Add(Sc->GetActorLocation()); } }
		DropRoomCands(Cands);
	}
	if (Cands.Num() == 0) { return false; }
	Out = Cands[FMath::RandRange(0, Cands.Num() - 1)];
	return true;
}

void UContactsComponent::SpawnAppointmentCustomer(const FPhoneMessage& Msg)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Voorkeur: stuur de BESTAANDE bewoner met dit NpcId aan (geen dubbele NPC). YouGoToThem -> die
	// verschijnt in z'n eigen unit en wacht; TheyComeToYou -> die loopt naar de speler.
	// PERF: klant-registry (O(NPC's)) i.p.v. TActorIterator over alle actors - zelfde set.
	// Per-proces registry -> op wereld filteren (PIE/co-op-in-1-proces).
	for (const TWeakObjectPtr<ACustomerBase>& WCb : ACustomerBase::GetAll())
	{
		ACustomerBase* Cb = WCb.Get();
		if (IsValid(Cb) && Cb->GetWorld() == World && Cb->NpcId == Msg.FromContactId && Cb->IsResident())
		{
			Cb->SetApptWant(Msg.WantStrain, Msg.WantQty, Msg.WantProduct);
			if (Msg.bOrder) { Cb->SetApptOrder(Msg.MinThc, Msg.BonusMult); }
			Cb->BeginAppointment(Msg.Kind == EAppointmentKind::TheyComeToYou);
			return;
		}
	}

	// Fallback: het contact loopt niet fysiek rond (uitgeroteerd). Contacten blijven tóch bereikbaar:
	//  - "Kom bij mij" (YouGoToThem): spawn 'm op een gemarkeerde meet-spot (of anders bij de entree).
	//  - "Ik kom langs" (TheyComeToYou): spawn 'm beneden bij de BEZOEKERS-ENTREE van het gebouw van de
	//    speler (de bezorg-resolver), nooit binnen in de kamer.
	const bool bComeToYou = (Msg.Kind == EAppointmentKind::TheyComeToYou);
	FVector SpawnLoc(0.f, 0.f, 150.f);
	FRotator SpawnRot = FRotator::ZeroRotator;
	bool bPlacedAtHome = false;

	// Beach-map: contacten lopen niet meer bij een eigen procedurele woning rond. De afspraak-NPC
	// spawnt daarom bij de entree/meet-spot van de speler (zie hieronder), ook voor "kom bij mij langs".

	if (!bPlacedAtHome)
	{
		// Bij de JUISTE speler: competitive-afspraak hoort bij de speler in Msg.ForPlayerId (niet altijd de
		// host). Co-op (ForPlayerId leeg) -> host/eerste speler; het thuis is daar toch gedeeld.
		const APawn* Player = nullptr;
		if (!Msg.ForPlayerId.IsEmpty())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
				if (P && USaveGameSubsystem::StablePlayerId(P) == Msg.ForPlayerId) { Player = P; break; }
			}
		}
		if (!Player)
		{
			const APlayerController* PC0 = World->GetFirstPlayerController();
			Player = PC0 ? PC0->GetPawn() : nullptr;
		}
		{
			if (Player)
			{
				const UPhoneClientComponent* Ph = Player->FindComponentByClass<UPhoneClientComponent>();

				// "Kom bij MIJ" (YouGoToThem): wacht op een LOGISCHE plek - een door de speler gemarkeerde
				// meet-spot (Ctrl+F7: steegje/hotel-hal/...) of anders vlakbij een winkel. NOOIT op het dak
				// (de apartment-pos zit boven) of midden op de weg (random nav).
				if (!bComeToYou)
				{
					FVector MeetLoc;
					if (PickLogicalMeetSpot(World, MeetLoc)) { SpawnLoc = MeetLoc; bPlacedAtHome = true; }
				}

				// "Ik kom langs" (TheyComeToYou) + vangnet voor "kom bij mij" zonder meet-spot: spawn waar
				// bezoekers HOREN binnen te komen - de volledige bezorg-resolver van de telefoon (Shift+F7-
				// marker > echte voordeur-actor net BUITEN op de stoep > DeliveryPoint-tag > spawner-punt >
				// vóór de speler, incl. eigen vloer-trace). NOOIT GetActiveHomeLocation: DoorPos = het
				// kamer-MIDDEN op de pack-map, waardoor de afspraak-NPC soms midden in je appartement stond.
				if (!bPlacedAtHome && Ph)
				{
					SpawnLoc = Ph->FindDeliveryPoint();
					bPlacedAtHome = true;
				}
				if (!bPlacedAtHome)
				{
					SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 300.f;
					SpawnRot = (Player->GetActorLocation() - SpawnLoc).Rotation();
					SpawnRot.Pitch = 0.f;
					SpawnRot.Roll = 0.f;
				}

				// Navmesh-FIJNSLIJPEN, geen veto: projecteren als het lukt (netjes op beloopbare grond), maar
				// een marker-/deur-plek NIET terugkaatsen naar "vlak vóór de speler" als de projectie faalt -
				// op de pack-map is de navmesh plaatselijk dood en de speler staat bij een afspraak vaak
				// BINNEN, dus die fallback zette de NPC alsnog midden in de kamer. Markers zijn ground truth;
				// de vloer-trace hieronder zet 'm sowieso op de echte vloer.
				if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World))
				{
					FNavLocation Proj;
					if (Nav->ProjectPointToNavigation(SpawnLoc, Proj, FVector(220.f, 220.f, 400.f)))
					{
						SpawnLoc = Proj.Location;
					}
				}
			}
		}
	}

	// ALTIJD op de echte vloer zetten (nooit op een tafel/stoel/kast): meubels zijn WorldDynamic, dus een
	// ECC_WorldStatic-trace omlaag gaat er dwars doorheen en raakt de map-vloer. Geen treffer -> Z blijft staan.
	if (World)
	{
		FHitResult Floor;
		const FVector FS(SpawnLoc.X, SpawnLoc.Y, SpawnLoc.Z + 300.f);
		const FVector FE = FS - FVector(0.f, 0.f, 1500.f);
		FCollisionQueryParams FQ(FName(TEXT("ApptFloor")), false);
		if (World->LineTraceSingleByChannel(Floor, FS, FE, ECC_WorldStatic, FQ)) { SpawnLoc.Z = Floor.ImpactPoint.Z + 4.f; }
	}

	// Deferred spawn zodat we NpcId zetten vóór BeginPlay (klant laadt dan z'n eigen stats).
	const FTransform SpawnTM(SpawnRot, SpawnLoc);
	ACustomerBase* Cust = World->SpawnActorDeferred<ACustomerBase>(
		ACustomerBase::StaticClass(), SpawnTM, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Cust)
	{
		return;
	}

	Cust->NpcId = Msg.FromContactId; // dit is dezelfde persoon als het contact
	Cust->ProductTable = ProductTable;
	Cust->SetApptWant(Msg.WantStrain, Msg.WantQty, Msg.WantProduct);
	if (Msg.bOrder) { Cust->SetApptOrder(Msg.MinThc, Msg.BonusMult); }
	if (!Msg.WantProduct.IsNone())
	{
		Cust->DesiredProductId = Msg.WantProduct; // volledig product (kan hasj/edible zijn)
		Cust->DesiredQuantity = FMath::Max(1, Msg.WantQty);
	}
	else if (!Msg.WantStrain.IsNone())
	{
		Cust->DesiredProductId = FName(*FString::Printf(TEXT("Bag_%s"), *Msg.WantStrain.ToString()));
		Cust->DesiredQuantity = FMath::Max(1, Msg.WantQty);
	}
	else
	{
		Cust->DesiredProductId = FName(TEXT("Bag_NorthernLights"));
		Cust->DesiredQuantity = 2;
	}
	Cust->BudgetCentsPerUnit = 1500;
	Cust->bDespawnAfterServed = true; // afspraak-klant vertrekt na de deal
	Cust->bNeedsPlayer = true;        // afspraak: poppetje op de kompas (je moet bij deze zijn)

	// Blijf op je plek wachten (geen rondlopen) + ruim na de deal netjes op je eigen plek op.
	Cust->SetSpot(SpawnLoc);
	Cust->SetHome(SpawnLoc);

	Cust->FinishSpawning(SpawnTM);

	// Echte afspraak-state: bApptActive + een afgetelde wachttijd die schaalt met afstand + respect/loyaliteit
	// (ComputeApptWaitSeconds). Zo werken de chat-progressbar EN de no-show ook voor deze (niet-resident) NPC,
	// i.p.v. de oude 30s-patience waardoor 'ie te snel vertrok.
	Cust->BeginAppointment(bComeToYou);

	UE_LOG(LogWeedShop, Log, TEXT("Appointment customer spawned for %s."), *Msg.SenderName.ToString());
}

FString UContactsComponent::FormatApptClock(float TimeOfDay) const
{
	float Now = 0.f, Length = 1800.f;
	GetCycleTime(Now, Length);
	if (Length <= 0.f) { Length = 1800.f; }
	const int32 TotalMin = ClockMinutesOf(TimeOfDay);
	return FString::Printf(TEXT("%02d:%02d"), (TotalMin / 60) % 24, TotalMin % 60);
}

int32 UContactsComponent::ClockMinutesOf(float TimeOfDay) const
{
	// Gebruik DEZELFDE klok-mapping als de HUD (dag/nacht-split), zodat de getoonde tijd klopt met "07:51".
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UDayCycleComponent* Day = GS->GetDayCycle())
		{
			return ((FMath::RoundToInt(Day->ClockHourOf(FMath::Max(0.f, TimeOfDay)) * 60.f) % 1440) + 1440) % 1440;
		}
	}
	float Now = 0.f, Length = 1800.f; GetCycleTime(Now, Length); if (Length <= 0.f) { Length = 1800.f; }
	const float Frac = FMath::Fmod(FMath::Max(0.f, TimeOfDay), Length) / Length;
	return ((FMath::RoundToInt(Frac * 1440.f) % 1440) + 1440) % 1440;
}

void UContactsComponent::ApplyRelationshipDelta(FName ContactId, float Delta)
{
	// Relatie in de contactenlijst bijwerken.
	for (FPhoneContact& Contact : Contacts)
	{
		if (Contact.ContactId == ContactId)
		{
			Contact.Relationship = FMath::Clamp(Contact.Relationship + Delta, 0.f, 100.f);
			break;
		}
	}

	// Persistente loyaliteit van deze persoon bijwerken in het NPC-register.
	if (AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
		{
			float R = 0.f, L = 0.f, A = 0.f; FText N;
			if (Reg->GetStats(ContactId, R, L, A, N))
			{
				Reg->ApplyStats(ContactId, R, FMath::Clamp(L + Delta, 0.f, 100.f), A);
			}
		}
	}

	// Live klant met deze persoon ook meteen bijwerken (als die er is).
	// PERF: klant-registry (O(NPC's)) i.p.v. TActorIterator over alle actors - zelfde set.
	// Per-proces registry -> op wereld filteren (PIE/co-op-in-1-proces).
	for (const TWeakObjectPtr<ACustomerBase>& WCb : ACustomerBase::GetAll())
	{
		ACustomerBase* Cb = WCb.Get();
		if (IsValid(Cb) && Cb->GetWorld() == GetWorld() && Cb->NpcId == ContactId)
		{
			Cb->Loyalty = FMath::Clamp(Cb->Loyalty + Delta, 0.f, 100.f);
			break;
		}
	}
}

void UContactsComponent::RespondTopPending(bool bAccept)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	for (const FPhoneMessage& Msg : Messages)
	{
		if (Msg.Status == 0 && !Msg.bFromMe)
		{
			RespondToContact(Msg.FromContactId, bAccept);
			return;
		}
	}
}

void UContactsComponent::RespondToContact(FName ContactId, bool bAccept)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone())
	{
		return;
	}

	// Nieuwste open afspraak-bericht van dit contact (index 0 = nieuwste).
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Messages.Num(); ++i)
	{
		if (Messages[i].FromContactId == ContactId && Messages[i].Status == 0 && !Messages[i].bFromMe)
		{
			Found = i;
			break;
		}
	}
	if (Found == INDEX_NONE)
	{
		return;
	}

	const float ApptTime = Messages[Found].AppointmentTimeOfDay;
	const FText SenderName = Messages[Found].SenderName;
	Messages[Found].Status = bAccept ? 1 : 2;
	// Accepteren geeft GEEN gratis loyaliteit meer: de beloning komt pas bij de VOLTOOIDE verkoop
	// (ComputeAcceptedDeltas). Anders houd je +stats over aan een afspraak die je daarna alsnog laat
	// mislukken (afdingen/weglopen/no-show) -> een mislukte deal mag NOOIT netto omhoog brengen.
	const float Delta = bAccept ? 0.f : -12.f;
	ApplyRelationshipDelta(ContactId, Delta);

	// Reactiesnelheid bepaalt de volgende cooldown: snel antwoord = eerder weer een appje, traag = langer rust.
	if (AWeedShopGameState* GSr = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (UNpcRegistryComponent* Reg = GSr->GetNpcRegistry())
		{
			const float Sent = Messages[Found].SentRealTime;
			const float RealNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			const float ReplySec = (Sent >= 0.f) ? (RealNow - Sent) : 30.f;
			float Mult = 1.f;
			if (bAccept) { Mult = (ReplySec < 20.f) ? 0.7f : (ReplySec > 60.f ? 1.5f : 1.f); }
			else { Mult = 1.8f; }
			Reg->SetApptCooldownMult(ContactId, Mult);
		}
	}

	// Mijn antwoord als chat-regel (rechts) toevoegen.
	FPhoneMessage Reply;
	Reply.FromContactId = ContactId;
	Reply.SenderName = SenderName;
	Reply.bFromMe = true;
	Reply.Status = 3; // antwoord, geen openstaande afspraak
	Reply.AppointmentTimeOfDay = -1.f;
	Reply.Body = bAccept
		? FText::FromString(FString::Printf(TEXT("Sure, see you at %s."), *FormatApptClock(ApptTime)))
		: FText::FromString(TEXT("Sorry, can't make it."));
	StampAndInsert(Reply);
	if (Messages.Num() > 40) { Messages.SetNum(40); }

	if (GEngine)
	{
		UWeedToast::Notify(-1, 3.f, bAccept ? FColor::Green : FColor::Orange,
			FString::Printf(TEXT("%s: appointment %s"), *SenderName.ToString(),
				bAccept ? TEXT("accepted") : TEXT("cancelled")));
	}

	OnRep_Messages();
}

void UContactsComponent::OnRep_Messages()
{
	OnMessagesChanged.Broadcast();
}

void UContactsComponent::ProposeTimeToContact(FName ContactId, int32 MinutesOfDay)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone()) { return; }

	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Messages.Num(); ++i)
	{
		if (Messages[i].FromContactId == ContactId && Messages[i].Status == 0 && !Messages[i].bFromMe) { Found = i; break; }
	}
	if (Found == INDEX_NONE) { return; }

	// Kloktijd (minuten) -> TimeOfDay via DEZELFDE dag/nacht-mapping als de HUD.
	float Now = 0.f, Length = 1800.f; GetCycleTime(Now, Length); if (Length <= 0.f) { Length = 1800.f; }
	float NewTime;
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		const UDayCycleComponent* Day = GS->GetDayCycle();
		NewTime = Day ? Day->TimeOfDayFromClockMinutes(MinutesOfDay) : (FMath::Clamp((float)MinutesOfDay, 0.f, 1439.f) / 1440.f) * Length;
	}
	else { NewTime = (FMath::Clamp((float)MinutesOfDay, 0.f, 1439.f) / 1440.f) * Length; }

	const FText SenderName = Messages[Found].SenderName;
	Messages[Found].Status = 1;                      // geaccepteerd op JOUW tijd
	Messages[Found].AppointmentTimeOfDay = NewTime;
	ApplyRelationshipDelta(ContactId, 5.f);          // zelfde als gewoon accepteren; geen nadeel

	// Mijn voorstel (rechts).
	FPhoneMessage Mine;
	Mine.FromContactId = ContactId; Mine.SenderName = SenderName; Mine.bFromMe = true;
	Mine.Status = 3; Mine.AppointmentTimeOfDay = -1.f;
	Mine.Body = FText::FromString(FString::Printf(TEXT("Can we do %s instead?"), *FormatApptClock(NewTime)));
	StampAndInsert(Mine);
	// Hun antwoord: altijd akkoord (links).
	FPhoneMessage Theirs;
	Theirs.FromContactId = ContactId; Theirs.SenderName = SenderName; Theirs.bFromMe = false;
	Theirs.Status = 3; Theirs.AppointmentTimeOfDay = -1.f;
	Theirs.Body = FText::FromString(FString::Printf(TEXT("Works for me, see you at %s."), *FormatApptClock(NewTime)));
	StampAndInsert(Theirs);
	if (Messages.Num() > 40) { Messages.SetNum(40); }

	if (GEngine)
	{
		UWeedToast::Notify(-1, 3.f, FColor::Green, FString::Printf(TEXT("%s: agreed on %s"), *SenderName.ToString(), *FormatApptClock(NewTime)));
	}
	OnRep_Messages();
}

FName UContactsComponent::GetRequestedStrain(FName ContactId) const
{
	for (const FPhoneMessage& M : Messages)
	{
		if (M.FromContactId == ContactId && (M.Status == 0 || M.Status == 1) && !M.bFromMe && !M.WantStrain.IsNone())
		{
			return M.WantStrain;
		}
	}
	return NAME_None;
}

float UContactsComponent::SubstituteAcceptChance(FName ContactId, FName ReqStrain, FName NewStrain, float OfferedThc) const
{
	float R = 0.f, L = 0.f, A = 0.f; FText N;
	float ExpThc = 15.f;
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry()) { Reg->GetStats(ContactId, R, L, A, N); }
		if (UStoreComponent* St = GS->GetStore()) { float t = 0.f, y = 0.f, g = 0.f; if (St->GetStrainStats(ReqStrain, t, y, g) && t > 0.f) { ExpThc = t; } }
	}
	const float Off = (OfferedThc > 0.f) ? OfferedThc : ExpThc;
	const float ThcDelta = Off - ExpThc;
	float Chance = 0.45f + (L - 30.f) * 0.004f + (A - 30.f) * 0.005f + ThcDelta * 0.02f;
	if (NewStrain == ReqStrain) { Chance += 0.30f; } // dezelfde strain, alleen sterker -> bijna zeker
	return FMath::Clamp(Chance, 0.05f, 0.97f);
}

void UContactsComponent::ProposeAlternativeStrain(FName ContactId, FName NewStrain, float OfferedThc, float OfferedQualPct)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone() || NewStrain.IsNone()) { return; }
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Messages.Num(); ++i)
	{
		if (Messages[i].FromContactId == ContactId && (Messages[i].Status == 0 || Messages[i].Status == 1) && !Messages[i].bFromMe) { Found = i; break; }
	}
	if (Found == INDEX_NONE) { return; }

	const FName ReqStrain = Messages[Found].WantStrain;
	const FText SenderName = Messages[Found].SenderName;
	const float Chance = SubstituteAcceptChance(ContactId, ReqStrain, NewStrain, OfferedThc);
	const bool bAccept = FMath::FRand() <= Chance;

	// Mijn aanbod (rechts).
	FPhoneMessage Mine;
	Mine.FromContactId = ContactId; Mine.SenderName = SenderName; Mine.bFromMe = true; Mine.Status = 3; Mine.AppointmentTimeOfDay = -1.f;
	Mine.Body = FText::FromString(FString::Printf(TEXT("No %s right now - got %s at %.0f%% THC. Want that instead?"), *ReqStrain.ToString(), *NewStrain.ToString(), OfferedThc));
	StampAndInsert(Mine);

	FPhoneMessage Rep;
	Rep.FromContactId = ContactId; Rep.SenderName = SenderName; Rep.bFromMe = false; Rep.Status = 3; Rep.AppointmentTimeOfDay = -1.f;
	if (bAccept)
	{
		// De afspraak wil voortaan deze strain (her-vinden, want de index is verschoven door de insert).
		for (FPhoneMessage& M : Messages)
		{
			if (M.FromContactId == ContactId && (M.Status == 0 || M.Status == 1) && !M.bFromMe) { M.WantStrain = NewStrain; break; }
		}
		Rep.Body = FText::FromString(FString::Printf(TEXT("Yeah, %s works - bring that."), *NewStrain.ToString()));
		ApplyRelationshipDelta(ContactId, 1.f);
	}
	else
	{
		Rep.Body = FText::FromString(FString::Printf(TEXT("Nah, I really want the %s."), *ReqStrain.ToString()));
	}
	StampAndInsert(Rep);
	if (Messages.Num() > 40) { Messages.SetNum(40); }

	if (GEngine)
	{
		UWeedToast::Notify(-1, 3.f, bAccept ? FColor::Green : FColor::Orange,
			FString::Printf(TEXT("%s: %s"), *SenderName.ToString(), bAccept ? TEXT("took the alternative") : TEXT("declined the swap")));
	}
	OnRep_Messages();
}

void UContactsComponent::RestoreContacts(const TArray<FPhoneContact>& InContacts, const TArray<FPhoneMessage>& InMessages)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	Contacts = InContacts;
	Messages = InMessages;
	OnRep_Messages(); // UI bijwerken (server)
}

void UContactsComponent::StampAndInsert(FPhoneMessage& M)
{
	// In-game klok-uur stempelen voor de "HH:MM"-tijdstempel in de chat (als nog niet gezet).
	if (M.SentClockHour < 0.f)
	{
		if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
		{
			if (const UDayCycleComponent* DC = GS->GetDayCycle()) { M.SentClockHour = DC->GetClockHour(); }
		}
	}
	if (M.SentRealTime < 0.f && GetWorld()) { M.SentRealTime = GetWorld()->GetTimeSeconds(); }
	Messages.Insert(M, 0); // nieuwste bovenaan
}

void UContactsComponent::PushInfoMessage(FName ContactId, const FText& SenderName, const FText& Body)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone()) { return; }
	FPhoneMessage M;
	M.FromContactId = ContactId;
	M.SenderName = SenderName;
	M.Body = Body;
	M.AppointmentTimeOfDay = -1.f;
	M.Status = 3;        // los info-bericht, geen openstaande afspraak (geen ja/nee nodig)
	M.bFromMe = false;   // inkomend -> telt mee voor de ongelezen-bubble
	StampAndInsert(M);
	if (Messages.Num() > 40) { Messages.SetNum(40); }
	OnRep_Messages();    // server: meteen lokaal de UI bijwerken
}

void UContactsComponent::MarkThreadSeen(FName ContactId)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone()) { return; }
	bool bChanged = false;
	for (FPhoneMessage& M : Messages)
	{
		if (!M.bFromMe && M.FromContactId == ContactId && !M.bSeen) { M.bSeen = true; bChanged = true; }
	}
	if (bChanged) { OnRep_Messages(); } // server: meteen lokaal de badge bijwerken; repliceert naar clients
}

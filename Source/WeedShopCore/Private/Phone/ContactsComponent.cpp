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

void UContactsComponent::RegisterContact(FName ContactId, const FText& DisplayName, float Relationship, const FString& OwnerPlayerId)
{
	// ALTIJD op de BASIS-NpcId keyen (strip een eventuele "#spelerId"-suffix): matching + berichten draaien
	// op de basis-id, het per-speler eigenaarschap zit apart in OwnerPlayerId.
	ContactId = BaseNpcId(ContactId);
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone() || HasContact(ContactId))
	{
		return;
	}

	FPhoneContact C;
	C.ContactId = ContactId;
	C.DisplayName = DisplayName;
	C.Relationship = Relationship;
	C.OwnerPlayerId = OwnerPlayerId; // leeg = gedeeld/co-op; gevuld = de eigenaar-speler (competitive)
	Contacts.Add(C);

	UE_LOG(LogWeedShop, Log, TEXT("New contact: %s"), *DisplayName.ToString());
	// Per-speler: in competitive ziet alleen de eigenaar de "New contact"-toast; in co-op iedereen.
	NotifyOwnerPlayer(OwnerPlayerId, 3.f, FColor::Cyan,
		FString::Printf(TEXT("New contact: %s"), *DisplayName.ToString()));
}

bool UContactsComponent::HasContact(FName ContactId) const
{
	return Contacts.ContainsByPredicate([ContactId](const FPhoneContact& C) { return C.ContactId == ContactId; });
}

FName UContactsComponent::BaseNpcId(FName NpcId)
{
	// Strip een eventuele "#spelerId"-suffix: contacten/berichten/matching draaien op de BASIS-NpcId.
	const FString S = NpcId.ToString();
	int32 Hash = INDEX_NONE;
	if (S.FindChar(TEXT('#'), Hash))
	{
		return FName(*S.Left(Hash));
	}
	return NpcId;
}

APawn* UContactsComponent::ResolvePawnForPlayer(const FString& PlayerId) const
{
	if (PlayerId.IsEmpty() || !GetWorld()) { return nullptr; }
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (P && USaveGameSubsystem::StablePlayerId(P) == PlayerId) { return P; }
	}
	return nullptr;
}

void UContactsComponent::NotifyOwnerPlayer(const FString& PlayerId, float Seconds, const FColor& Color, const FString& Text) const
{
	if (!GEngine) { return; }
	// Co-op (leeg) -> iedereen; competitive -> alleen de eigenaar-pawn (nooit lekken naar de rivaal).
	// NotifyAllPawns route per speler (host lokaal + joiners via Client-RPC); een kaal Notify() bereikt op
	// een listen-server alleen het host-scherm, waardoor de joiner z'n afspraak/bericht-toast miste.
	if (PlayerId.IsEmpty())
	{
		UWeedToast::NotifyAllPawns(this, -1, Seconds, Color, Text, TEXT("ui_message"));
		return;
	}
	if (APawn* Owner = ResolvePawnForPlayer(PlayerId))
	{
		UWeedToast::NotifyPawn(Owner, -1, Seconds, Color, Text, TEXT("ui_message"));
	}
}

static bool PickLogicalMeetSpot(UWorld* W, FVector& Out, FName StableContactId = NAME_None);
static float ComputeYouGoToThemTravelExtra(UWorld* W, const APawn* OwnerPawn, const FVector& MeetSpot);

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

	// COMPETITIVE: resolve de DOEL-SPELER van een contact op EEN plek, zodat het cooldown-filter, de
	// product-keuze, het VIP-level en het send-target hieronder allemaal DEZELFDE eigenaar gebruiken.
	// Eerst het contact-eigenaarschap (OwnerPlayerId), anders de favoriete speler van dit contact
	// (GetTopOwner op de BASIS-NpcId). Co-op -> leeg = gedeelde base-waarden.
	const AWeedShopGameState* GSowner = Cast<AWeedShopGameState>(GetOwner());
	const bool bCompetitive = GSowner && GSowner->IsCompetitive();
	auto ResolveOwnerId = [&](const FPhoneContact& Ct) -> FString
	{
		if (!bCompetitive) { return FString(); }
		FString OwnerId = Ct.OwnerPlayerId;
		if (OwnerId.IsEmpty() && Reg) { OwnerId = Reg->GetTopOwner(BaseNpcId(Ct.ContactId)); }
		return OwnerId;
	};

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
		// Per-speler cooldown: check tegen de doel-speler van DIT contact (leeg = base/co-op).
		if (Reg && Reg->IsOnCooldown(Id, ResolveOwnerId(Contacts[i]))) { continue; }
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
	// GEHOISTE owner-resolve van het GEKOZEN contact: dezelfde doel-speler voor de cooldown-boekhouding,
	// de product-keuze, het VIP-level en het send-target hieronder (leeg + nullptr in co-op).
	const FString OwnerId = ResolveOwnerId(C);
	APawn* OwnerPawn = ResolvePawnForPlayer(OwnerId); // nullptr als de owner niet verbonden is (of co-op)
	if (Reg) { Reg->NoteAppointment(C.ContactId, OwnerId); } // telt mee voor de 1-2/dag-cap (per-speler)

	float Now = 0.f, Length = 1800.f;
	GetCycleTime(Now, Length);

	const EAppointmentKind ApptKind = (FMath::RandBool()) ? EAppointmentKind::TheyComeToYou : EAppointmentKind::YouGoToThem;
	FVector PlannedMeetSpot = FVector::ZeroVector;
	bool bHasPlannedMeetSpot = false;
	float TravelExtraSec = 0.f;
	if (ApptKind == EAppointmentKind::YouGoToThem)
	{
		bHasPlannedMeetSpot = PickLogicalMeetSpot(GetWorld(), PlannedMeetSpot, BaseNpcId(C.ContactId));
		if (bHasPlannedMeetSpot)
		{
			TravelExtraSec = ComputeYouGoToThemTravelExtra(GetWorld(), OwnerPawn, PlannedMeetSpot);
		}
	}

	// Afspraak in de toekomst (binnen de cyclus). Ondergrens = antwoord-venster + marge (ApptOffsetMinSec,
	// ~210s) zodat de gevraagde tijd ALTIJD na het opgeef-venster (GiveUpDelay 150s) valt. Als de speler
	// naar de klant moet, krijgt een verre buiten-wachtplek extra reistijd.
	const float Offset = FMath::Min(FMath::FRandRange(ApptOffsetMinSec, ApptOffsetMaxSec) + TravelExtraSec, ApptOffsetVisualMaxSec);
	const float ApptTime = FMath::Fmod(Now + Offset, Length);

	// Formatteer als HH:MM met DEZELFDE klok als de HUD (dag/nacht-split).
	const int32 TotalMin = ClockMinutesOf(ApptTime);
	const int32 HH = (TotalMin / 60) % 24;
	const int32 MM = TotalMin % 60;

	// Wat + hoeveel wil de klant: DEZELFDE keuze-logica als walk-ins (tier-weging + premium hasj/edibles +
	// soms wiet net boven je bereik). Geeft een volledig product-id (Bag_/Hash_/Edible_<strain>).
	int32 WantQty = 1;
	const FName WantProduct = ACustomerBase::PickDesiredProduct(Cast<AWeedShopGameState>(GetOwner()), ProductTable, C.ContactId, WantQty, OwnerId, OwnerPawn);
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
	Msg.Kind = ApptKind;
	Msg.bHasPlannedMeetSpot = bHasPlannedMeetSpot;
	Msg.PlannedMeetSpot = PlannedMeetSpot;

	// --- DAG-ORDER: mid-game variatie. Vanaf level ~12 wordt een afspraak soms een premium VIP-order:
	// een specifieke wiet-strain met een min-THC-eis, een ruimere deadline en een bonus-uitbetaling.
	// Schaalt mee met level (vaker + groter + meer bonus). Beloont een goed gevulde, diverse voorraad.
	const AWeedShopGameState* GSo = Cast<AWeedShopGameState>(GetOwner());
	// COMPETITIVE: de VIP-order-kans/bonus schaalt met het level van de speler VOOR wie deze afspraak is -
	// de GEHOISTE OwnerPawn hierboven (zelfde resolve als filter/product-keuze/send-target). Geen
	// speler-context (co-op, of niemand verbonden) -> nullptr = gedeelde crew-waarde.
	const int32 PlayerLvl = (GSo && GSo->GetLeveling()) ? GSo->GetLeveling()->GetLevelFor(OwnerPawn) : 1;
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
			// Ruimere deadline (3.5-7 min) zodat je voorraad kunt halen/aanvullen; YouGoToThem krijgt
			// dezelfde afstandsbuffer als normale afspraken.
			const float OrderOffset = FMath::Min(FMath::FRandRange(ApptOffsetMinSec, 420.f) + TravelExtraSec, 420.f + YouGoToThemTravelExtraMaxSec);
			Msg.AppointmentTimeOfDay = FMath::Fmod(Now + OrderOffset, Length);
			const int32 OTotalMin = ClockMinutesOf(Msg.AppointmentTimeOfDay);
			const int32 OHH = (OTotalMin / 60) % 24;
			const int32 OMM = OTotalMin % 60;
			Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
				? FText::FromString(FString::Printf(TEXT("VIP order\n%dg %s\nmin %.0f%% THC\nReady by %02d:%02d\nI'll come to you."), WantQty, *WantClean, Msg.MinThc, OHH, OMM))
				: FText::FromString(FString::Printf(TEXT("VIP order\n%dg %s\nmin %.0f%% THC\nReady by %02d:%02d\nMeet me outside."), WantQty, *WantClean, Msg.MinThc, OHH, OMM));
		}
	}

	if (!Msg.bOrder)
	{
		Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
			? FText::FromString(FString::Printf(TEXT("Hey, got any %s?\nI need %dg.\nI'll come by at %02d:%02d."), *WantClean, WantQty, HH, MM))
			: FText::FromString(FString::Printf(TEXT("Hey, got any %s?\nI need %dg.\nMeet me outside at %02d:%02d?"), *WantClean, WantQty, HH, MM));
	}

	// COMPETITIVE: dit bericht is voor EEN speler (eigen telefoon). Doel = de GEHOISTE eigenaar van dit
	// contact (OwnerPawn - zelfde resolve als het cooldown-filter en de product-keuze hierboven), anders
	// een willekeurige verbonden speler. Co-op laat ForPlayerId leeg (iedereen ziet het).
	APawn* TargetPawn = nullptr;
	if (bCompetitive && GetWorld())
	{
		TargetPawn = OwnerPawn;
		if (!TargetPawn)
		{
			TArray<APawn*> Pawns;
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr) { Pawns.Add(P); }
			}
			if (Pawns.Num() > 0) { TargetPawn = Pawns[FMath::RandRange(0, Pawns.Num() - 1)]; }
		}
		if (TargetPawn) { Msg.ForPlayerId = USaveGameSubsystem::StablePlayerId(TargetPawn); }
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
		if (TargetPawn) { UWeedToast::NotifyPawn(TargetPawn, -1, 4.f, FColor(120, 180, 255), NoteTxt); } // competitive: alleen de doelspeler
		else { UWeedToast::NotifyAllPawns(this, -1, 4.f, FColor(120, 180, 255), NoteTxt); } // co-op: iedereen (was host-only Notify)
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
		// Gedeelde constanten (D.13): NudgeDelay < GiveUpDelay < min-afspraak-offset (ApptOffsetMinSec),
		// zodat een net-op-tijd geaccepteerde afspraak nooit al verstreken is.
		constexpr float NudgeDelay = NudgeDelaySec;       // herinnering na 1 min geen antwoord
		constexpr float GiveUpDelay = ResponseWindowSec;  // geeft op na 2,5 min geen antwoord
		UNpcRegistryComponent* Reg = nullptr;
		if (const AWeedShopGameState* GSc = Cast<AWeedShopGameState>(GetOwner())) { Reg = GSc->GetNpcRegistry(); }

		struct FAct { FName Id; FText Name; int32 Type; FString ForPlayerId; }; // 1 = nudge, 2 = opgeven
		TArray<FAct> Acts;
		for (FPhoneMessage& Msg : Messages)
		{
			if (Msg.Status != 0 || Msg.bFromMe || Msg.WantQty <= 0 || Msg.SentRealTime < 0.f) { continue; }
			const float Elapsed = RealNow - Msg.SentRealTime;
			// AFGELEIDE berichten (nudge/opgeven) ERVEN ForPlayerId van het bron-bericht -> lekken niet naar de rivaal.
			if (!Msg.bNudged && Elapsed >= NudgeDelay) { Msg.bNudged = true; Acts.Add({ Msg.FromContactId, Msg.SenderName, 1, Msg.ForPlayerId }); }
			else if (Elapsed >= GiveUpDelay) { Msg.Status = 2; Acts.Add({ Msg.FromContactId, Msg.SenderName, 2, Msg.ForPlayerId }); }
		}
		for (const FAct& Ac : Acts)
		{
			if (Ac.Type == 1)
			{
				PushInfoMessage(Ac.Id, Ac.Name, FText::FromString(TEXT("You there? Still need it, or should I look elsewhere?")), Ac.ForPlayerId);
			}
			else
			{
				PushInfoMessage(Ac.Id, Ac.Name, FText::FromString(TEXT("Nvm, took too long. I'll get it somewhere else.")), Ac.ForPlayerId);
				if (Reg)
				{
					float R = 0.f, L = 0.f, A = 0.f; FText N;
					if (Reg->GetStats(Ac.Id, R, L, A, N))
					{
						Reg->ApplyStats(Ac.Id, FMath::Max(0.f, R - 4.f), FMath::Max(0.f, L - 8.f), A); // wat respect, meer loyaliteit kwijt
					}
					Reg->SetApptCooldownMult(Ac.Id, 2.5f, Ac.ForPlayerId); // laat je daarna langer met rust (per-speler)
					Reg->NoteAppointment(Ac.Id, Ac.ForPlayerId);           // start de (langere) cooldown (per-speler)
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
		// Aankondigen zodra de afspraaktijd is GEPASSEERD, binnen een ruimere marge (AnnounceWindowSec) -
		// wrap-veilig over de cyclus-grens. Een strak +-2s-venster miste een net-te-laat geaccepteerde
		// afspraak (spawnde dan pas een hele cyclus later); dit venster vangt 'm alsnog op (D.13).
		// Diff in [0, AnnounceWindowSec] = zojuist gepasseerd. Wrap: verschil kan bij een cyclus-grens
		// als bijna-Length lezen, dus terugbrengen naar het kortste teken (Length eraf als > Length/2).
		float PassedBy = Now - Msg.AppointmentTimeOfDay;
		if (PassedBy < -Length * 0.5f) { PassedBy += Length; }      // net na de grens gewrapt: alsnog zojuist gepasseerd
		else if (PassedBy > Length * 0.5f) { PassedBy -= Length; }  // net voor de grens: negatief (nog niet gepasseerd)
		if (PassedBy >= -2.f && PassedBy <= AnnounceWindowSec)
		{
			Msg.bAnnounced = true;
			UE_LOG(LogWeedShop, Log, TEXT("Appointment with %s is now."), *Msg.SenderName.ToString());
			{
				const FString Where = (Msg.Kind == EAppointmentKind::TheyComeToYou)
					? FString::Printf(TEXT("%s is on the way!"), *Msg.SenderName.ToString())
					: FString::Printf(TEXT("%s is waiting outside"), *Msg.SenderName.ToString());
				// Per-speler: alleen de eigenaar van deze afspraak (Msg.ForPlayerId) krijgt de aankondiging;
				// co-op (leeg) = alle spelers. Nooit Notify(-1) -> de rivaal ziet elkaars afspraak niet meer.
				NotifyOwnerPlayer(Msg.ForPlayerId, 5.f, FColor::Magenta, FString::Printf(TEXT("Appointment: %s"), *Where));
			}
			SpawnAppointmentCustomer(Msg);
		}
	}
}

static void RefineLogicalMeetSpot(UWorld* W, FVector& Spot)
{
	if (!W) { return; }
	if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(W))
	{
		FNavLocation Proj;
		if (Nav->ProjectPointToNavigation(Spot, Proj, FVector(220.f, 220.f, 400.f)))
		{
			Spot = Proj.Location;
		}
	}
	FHitResult Floor;
	const FVector FS(Spot.X, Spot.Y, Spot.Z + 300.f);
	const FVector FE = FS - FVector(0.f, 0.f, 1500.f);
	FCollisionQueryParams FQ(FName(TEXT("ApptMeetSpotFloor")), false);
	if (W->LineTraceSingleByChannel(Floor, FS, FE, ECC_WorldStatic, FQ))
	{
		Spot.Z = Floor.ImpactPoint.Z + 4.f;
	}
}

static void CollectLogicalMeetSpots(UWorld* W, TArray<FVector>& Out)
{
	Out.Reset();
	if (!W) { return; }
	// KAMER-FILTER: kandidaten binnen een woon-kamer (starter, registry-units, competitive) vallen
	// af - een afspraak-NPC wacht in een steegje/hal/bij een toonbank, nooit zomaar binnen in een
	// kamer (stale markers uit oude dev-sessies zetten 'm daar anders neer).
	const ADoorRetrofitter* Retro = nullptr;
	for (TActorIterator<ADoorRetrofitter> RIt(W); RIt; ++RIt) { Retro = *RIt; break; }
	auto DropRoomCands = [Retro](TArray<FVector>& Arr)
	{
		if (Retro) { Arr.RemoveAll([Retro](const FVector& C) { return Retro->IsInsideHomeRoom(C); }); }
	};
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MeetSpots.txt"))))
	{
		const FString CurMap = W->GetOutermost()->GetName();
		for (const FString& Raw : Lines)
		{
			TArray<FString> P; Raw.TrimStartAndEnd().ParseIntoArray(P, TEXT("|"));
			if (P.Num() >= 4 && P[0] == CurMap)
			{
				Out.Add(FVector(FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3])));
			}
		}
	}
	DropRoomCands(Out);
	// Geen gemarkeerde meet-spots -> automatische buiten-wachtplekken bij de commerciele deuren over de
	// hele map (winkel/garage/lobby/steeg). Zo krijgt een YouGoToThem-afspraak altijd een logische map-deur
	// i.p.v. alleen de eigen woning. Retro filtert woon-kamers zelf al weg; DropRoomCands als extra vangnet.
	if (Out.Num() == 0 && Retro)
	{
		TArray<FVector> WaitSpots;
		Retro->GetOutdoorWaitSpots(WaitSpots);
		Out.Append(WaitSpots);
		DropRoomCands(Out);
	}
	// Nog niks -> val terug op de winkels (al gemarkeerd door de speler, dus logisch + bereikbaar) - met
	// dezelfde kamer-filter. PERF: balie-registry i.p.v. iterator (per-proces registry -> op wereld filteren).
	if (Out.Num() == 0)
	{
		for (const TWeakObjectPtr<AStoreCounter>& WSc : AStoreCounter::GetAll()) { AStoreCounter* Sc = WSc.Get(); if (IsValid(Sc) && Sc->GetWorld() == W) { Out.Add(Sc->GetActorLocation()); } }
		DropRoomCands(Out);
	}
}

static uint32 StableContactSeed(FName ContactId)
{
	const FString S = ContactId.ToString();
	uint32 H = 2166136261u;
	for (TCHAR C : S)
	{
		H ^= (uint32)FChar::ToLower(C);
		H *= 16777619u;
	}
	return H ? H : 1u;
}

// Een LOGISCHE wacht-plek voor een afspraak: een door de speler gemarkeerde meet-spot (MeetSpots.txt) voor
// deze map, anders een automatische buitenplek bij deuren/garages/lobby's/steegjes. Met StableContactId
// krijgt dezelfde klant steeds dezelfde plek, zodat "waar die klant woont/afspreekt" herkenbaar blijft.
static bool PickLogicalMeetSpot(UWorld* W, FVector& Out, FName StableContactId)
{
	TArray<FVector> Cands;
	CollectLogicalMeetSpots(W, Cands);
	if (Cands.Num() == 0) { return false; }
	const int32 PickIdx = StableContactId.IsNone()
		? FMath::RandRange(0, Cands.Num() - 1)
		: (int32)(StableContactSeed(StableContactId) % (uint32)Cands.Num());
	Out = Cands[PickIdx];
	RefineLogicalMeetSpot(W, Out);
	return true;
}

static float ComputeYouGoToThemTravelExtra(UWorld* W, const APawn* OwnerPawn, const FVector& MeetSpot)
{
	if (!W) { return 0.f; }
	float BestDist = TNumericLimits<float>::Max();
	if (OwnerPawn && OwnerPawn->GetWorld() == W)
	{
		BestDist = FVector::Dist2D(OwnerPawn->GetActorLocation(), MeetSpot);
	}
	else
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr)
			{
				BestDist = FMath::Min(BestDist, (float)FVector::Dist2D(P->GetActorLocation(), MeetSpot));
			}
		}
	}
	if (BestDist >= TNumericLimits<float>::Max()) { return 0.f; }

	constexpr float FreeDistanceCm = 3500.f; // om-de-hoek afspraken blijven strak
	constexpr float CmPerExtraSecond = 120.f;
	return FMath::Clamp((BestDist - FreeDistanceCm) / CmPerExtraSecond, 0.f, UContactsComponent::YouGoToThemTravelExtraMaxSec);
}

void UContactsComponent::SpawnAppointmentCustomer(const FPhoneMessage& Msg)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Match ALTIJD op de BASIS-NpcId: het bericht draagt de basis-id, de rondlopende bewoner ook. Zonder
	// deze strip miste een "#spelerId"-bericht elke echte NPC en viel altijd terug op de fallback-spawn.
	const FName BaseId = BaseNpcId(Msg.FromContactId);

	// Voorkeur: stuur de BESTAANDE NPC met dit NpcId aan (geen dubbele NPC). YouGoToThem -> die
	// wacht buiten op een automatische wachtplek; TheyComeToYou -> die loopt naar de speler.
	// PERF: klant-registry (O(NPC's)) i.p.v. TActorIterator over alle actors - zelfde set.
	// Per-proces registry -> op wereld filteren (PIE/co-op-in-1-proces).
	//
	// DUBBELE MARKERS (fix): vroeger matchte dit ALLEEN residenten. Een eerder gespawnde NIET-residente
	// fallback-afspraak-NPC met dezelfde BaseId werd dan niet hergebruikt -> een nieuwe spawn = 2 groene
	// poppetjes (kompas + kaart). Nu matchen we elke levende NPC met NpcId==BaseId. Veilige variant:
	// hergebruik alleen een VRIJE NPC (geen actieve afspraak) - een half-vertrokken afspraak-NPC (bApptActive)
	// hijacken we NIET. Voorkeur voor een resident; anders de eerste vrije match. Overige vrije duplicaten
	// met dezelfde id ruimen we op zodat er nooit twee tegelijk zichtbaar zijn.
	ACustomerBase* Reuse = nullptr;
	for (const TWeakObjectPtr<ACustomerBase>& WCb : ACustomerBase::GetAll())
	{
		ACustomerBase* Cb = WCb.Get();
		if (!IsValid(Cb) || Cb->GetWorld() != World || Cb->NpcId != BaseId) { continue; }
		if (Cb->HasActiveAppointment()) { continue; } // half-vertrokken: met rust laten
		if (!Reuse) { Reuse = Cb; }
		else if (!Reuse->IsResident() && Cb->IsResident()) { Reuse = Cb; } // resident heeft voorrang
	}
	if (Reuse)
	{
		// Eventuele overige VRIJE duplicaten met dezelfde id opruimen (nooit 2 markers naast elkaar).
		for (const TWeakObjectPtr<ACustomerBase>& WCb : ACustomerBase::GetAll())
		{
			ACustomerBase* Cb = WCb.Get();
			if (IsValid(Cb) && Cb != Reuse && Cb->GetWorld() == World && Cb->NpcId == BaseId && !Cb->HasActiveAppointment())
			{
				Cb->SendHomeAndDespawn();
			}
		}
		Reuse->SetApptWant(Msg.WantStrain, Msg.WantQty, Msg.WantProduct);
		if (Msg.bOrder) { Reuse->SetApptOrder(Msg.MinThc, Msg.BonusMult); }
		Reuse->ApptForPlayerId = Msg.ForPlayerId; // competitive: bij welke speler deze afspraak-NPC hoort (compass/telefoon-filter)
		Reuse->BeginAppointment(Msg.Kind == EAppointmentKind::TheyComeToYou, Msg.Kind == EAppointmentKind::YouGoToThem && Msg.bHasPlannedMeetSpot, Msg.PlannedMeetSpot);
		return;
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
					if (Msg.bHasPlannedMeetSpot)
					{
						SpawnLoc = Msg.PlannedMeetSpot;
						bPlacedAtHome = true;
					}
					else
					{
						FVector MeetLoc;
						if (PickLogicalMeetSpot(World, MeetLoc, BaseId)) { SpawnLoc = MeetLoc; bPlacedAtHome = true; }
					}
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

	Cust->NpcId = BaseId; // dit is dezelfde persoon als het contact (BASIS-NpcId, geen "#spelerId"-suffix)
	Cust->ApptForPlayerId = Msg.ForPlayerId; // competitive: bij welke speler deze afspraak-NPC hoort (gerepliceerd, vóór FinishSpawning)
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
	Cust->BudgetCentsPerUnit = 8000; // was 1500 (EUR15/g = bug: afspraken haggleden bijna alles weg). Nu de normale ~EUR80/g-basis; de markt-relatieve budget-berekening in SubmitOfferProduct dekt duurdere producten.
	Cust->bDespawnAfterServed = true; // afspraak-klant vertrekt na de deal
	Cust->bNeedsPlayer = true;        // afspraak: poppetje op de kompas (je moet bij deze zijn)

	// Blijf op je plek wachten (geen rondlopen) + ruim na de deal netjes op je eigen plek op.
	Cust->SetSpot(SpawnLoc);
	Cust->SetHome(SpawnLoc);

	Cust->FinishSpawning(SpawnTM);

	// Echte afspraak-state: bApptActive + een afgetelde wachttijd die schaalt met afstand + respect/loyaliteit
	// (ComputeApptWaitSeconds). Zo werken de chat-progressbar EN de no-show ook voor deze (niet-resident) NPC,
	// i.p.v. de oude 30s-patience waardoor 'ie te snel vertrok.
	Cust->BeginAppointment(bComeToYou, !bComeToYou && Msg.bHasPlannedMeetSpot, Msg.PlannedMeetSpot);

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

void UContactsComponent::RespondTopPending(bool bAccept, const FString& CallerId)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	for (const FPhoneMessage& Msg : Messages)
	{
		// Competitive: alleen berichten die voor deze speler zijn (leeg CallerId of leeg ForPlayerId = co-op gedeeld).
		if (Msg.Status == 0 && !Msg.bFromMe && (Msg.ForPlayerId.IsEmpty() || CallerId.IsEmpty() || Msg.ForPlayerId == CallerId))
		{
			RespondToContact(Msg.FromContactId, bAccept, CallerId);
			return;
		}
	}
}

void UContactsComponent::RespondToContact(FName ContactId, bool bAccept, const FString& CallerId)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone())
	{
		return;
	}

	// Nieuwste open afspraak-bericht van dit contact (index 0 = nieuwste). Competitive: alleen berichten die
	// voor deze speler zijn (leeg ForPlayerId/CallerId = co-op gedeeld) -> de rivaal kan jouw afspraak niet accepteren.
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Messages.Num(); ++i)
	{
		if (Messages[i].FromContactId == ContactId && Messages[i].Status == 0 && !Messages[i].bFromMe
			&& (Messages[i].ForPlayerId.IsEmpty() || CallerId.IsEmpty() || Messages[i].ForPlayerId == CallerId))
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
	const FString MsgForPlayer = Messages[Found].ForPlayerId; // afgeleide reply erft dit
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
			// Per-speler: de cooldown hoort bij de speler VOOR wie dit bericht was (leeg = base/co-op).
			Reg->SetApptCooldownMult(ContactId, Mult, MsgForPlayer);
		}
	}

	// Mijn antwoord als chat-regel (rechts) toevoegen. AFGELEID bericht ERFT ForPlayerId van het bron-bericht.
	FPhoneMessage Reply;
	Reply.FromContactId = ContactId;
	Reply.SenderName = SenderName;
	Reply.bFromMe = true;
	Reply.Status = 3; // antwoord, geen openstaande afspraak
	Reply.AppointmentTimeOfDay = -1.f;
	Reply.ForPlayerId = MsgForPlayer; // erft van het bron-bericht -> blijft bij de juiste speler
	Reply.Body = bAccept
		? FText::FromString(FString::Printf(TEXT("Sure, see you at %s."), *FormatApptClock(ApptTime)))
		: FText::FromString(TEXT("Sorry, can't make it."));
	StampAndInsert(Reply);
	if (Messages.Num() > 40) { Messages.SetNum(40); }

	// Feedback alleen naar de antwoordende speler (competitive); co-op (leeg) = alle spelers.
	NotifyOwnerPlayer(MsgForPlayer, 3.f, bAccept ? FColor::Green : FColor::Orange,
		FString::Printf(TEXT("%s: appointment %s"), *SenderName.ToString(),
			bAccept ? TEXT("accepted") : TEXT("cancelled")));

	OnRep_Messages();
}

void UContactsComponent::OnRep_Messages()
{
	OnMessagesChanged.Broadcast();
}

void UContactsComponent::ProposeTimeToContact(FName ContactId, int32 MinutesOfDay, const FString& CallerId)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone()) { return; }

	// Competitive: alleen een bericht dat voor deze speler is (leeg = co-op gedeeld).
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Messages.Num(); ++i)
	{
		if (Messages[i].FromContactId == ContactId && Messages[i].Status == 0 && !Messages[i].bFromMe
			&& (Messages[i].ForPlayerId.IsEmpty() || CallerId.IsEmpty() || Messages[i].ForPlayerId == CallerId)) { Found = i; break; }
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
	const FString MsgForPlayer = Messages[Found].ForPlayerId; // afgeleide berichten erven dit
	Messages[Found].Status = 1;                      // geaccepteerd op JOUW tijd
	Messages[Found].AppointmentTimeOfDay = NewTime;
	ApplyRelationshipDelta(ContactId, 5.f);          // zelfde als gewoon accepteren; geen nadeel

	// Mijn voorstel (rechts). AFGELEID -> erft ForPlayerId van het bron-bericht.
	FPhoneMessage Mine;
	Mine.FromContactId = ContactId; Mine.SenderName = SenderName; Mine.bFromMe = true;
	Mine.Status = 3; Mine.AppointmentTimeOfDay = -1.f; Mine.ForPlayerId = MsgForPlayer;
	Mine.Body = FText::FromString(FString::Printf(TEXT("Can we do %s instead?"), *FormatApptClock(NewTime)));
	StampAndInsert(Mine);
	// Hun antwoord: altijd akkoord (links). AFGELEID -> erft ForPlayerId van het bron-bericht.
	FPhoneMessage Theirs;
	Theirs.FromContactId = ContactId; Theirs.SenderName = SenderName; Theirs.bFromMe = false;
	Theirs.Status = 3; Theirs.AppointmentTimeOfDay = -1.f; Theirs.ForPlayerId = MsgForPlayer;
	Theirs.Body = FText::FromString(FString::Printf(TEXT("Works for me, see you at %s."), *FormatApptClock(NewTime)));
	StampAndInsert(Theirs);
	if (Messages.Num() > 40) { Messages.SetNum(40); }

	// Feedback alleen naar de voorstellende speler (competitive); co-op (leeg) = alle spelers.
	NotifyOwnerPlayer(MsgForPlayer, 3.f, FColor::Green, FString::Printf(TEXT("%s: agreed on %s"), *SenderName.ToString(), *FormatApptClock(NewTime)));
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

void UContactsComponent::ProposeAlternativeStrain(FName ContactId, FName NewStrain, float OfferedThc, float OfferedQualPct, const FString& CallerId)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone() || NewStrain.IsNone()) { return; }
	// Competitive: alleen een bericht dat voor deze speler is (leeg = co-op gedeeld).
	auto MatchesCaller = [&CallerId](const FPhoneMessage& M) { return M.ForPlayerId.IsEmpty() || CallerId.IsEmpty() || M.ForPlayerId == CallerId; };
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Messages.Num(); ++i)
	{
		if (Messages[i].FromContactId == ContactId && (Messages[i].Status == 0 || Messages[i].Status == 1) && !Messages[i].bFromMe && MatchesCaller(Messages[i])) { Found = i; break; }
	}
	if (Found == INDEX_NONE) { return; }

	const FName ReqStrain = Messages[Found].WantStrain;
	const FText SenderName = Messages[Found].SenderName;
	const FString MsgForPlayer = Messages[Found].ForPlayerId; // afgeleide berichten erven dit
	const float Chance = SubstituteAcceptChance(ContactId, ReqStrain, NewStrain, OfferedThc);
	const bool bAccept = FMath::FRand() <= Chance;

	// Mijn aanbod (rechts). AFGELEID -> erft ForPlayerId.
	FPhoneMessage Mine;
	Mine.FromContactId = ContactId; Mine.SenderName = SenderName; Mine.bFromMe = true; Mine.Status = 3; Mine.AppointmentTimeOfDay = -1.f; Mine.ForPlayerId = MsgForPlayer;
	Mine.Body = FText::FromString(FString::Printf(TEXT("No %s right now - got %s at %.0f%% THC. Want that instead?"), *ReqStrain.ToString(), *NewStrain.ToString(), OfferedThc));
	StampAndInsert(Mine);

	FPhoneMessage Rep;
	Rep.FromContactId = ContactId; Rep.SenderName = SenderName; Rep.bFromMe = false; Rep.Status = 3; Rep.AppointmentTimeOfDay = -1.f; Rep.ForPlayerId = MsgForPlayer;
	if (bAccept)
	{
		// De afspraak wil voortaan deze strain (her-vinden, want de index is verschoven door de insert;
		// zelfde per-speler-filter zodat we het bericht van de JUISTE speler muteren).
		for (FPhoneMessage& M : Messages)
		{
			if (M.FromContactId == ContactId && (M.Status == 0 || M.Status == 1) && !M.bFromMe && MatchesCaller(M)) { M.WantStrain = NewStrain; break; }
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

	// Feedback alleen naar de aanbiedende speler (competitive); co-op (leeg) = alle spelers.
	NotifyOwnerPlayer(MsgForPlayer, 3.f, bAccept ? FColor::Green : FColor::Orange,
		FString::Printf(TEXT("%s: %s"), *SenderName.ToString(), bAccept ? TEXT("took the alternative") : TEXT("declined the swap")));
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

void UContactsComponent::PushInfoMessage(FName ContactId, const FText& SenderName, const FText& Body, const FString& ForPlayerId)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone()) { return; }
	FPhoneMessage M;
	M.FromContactId = ContactId;
	M.SenderName = SenderName;
	M.Body = Body;
	M.AppointmentTimeOfDay = -1.f;
	M.Status = 3;        // los info-bericht, geen openstaande afspraak (geen ja/nee nodig)
	M.bFromMe = false;   // inkomend -> telt mee voor de ongelezen-bubble
	M.ForPlayerId = ForPlayerId; // AFGELEID -> erft de eigenaar-speler van het bron-bericht (competitive)
	StampAndInsert(M);
	if (Messages.Num() > 40) { Messages.SetNum(40); }
	OnRep_Messages();    // server: meteen lokaal de UI bijwerken
}

void UContactsComponent::MarkThreadSeen(FName ContactId, const FString& CallerId)
{
	if (GetOwnerRole() != ROLE_Authority || ContactId.IsNone()) { return; }
	bool bChanged = false;
	for (FPhoneMessage& M : Messages)
	{
		// Competitive: alleen berichten die voor deze speler zijn als gelezen markeren (leeg = co-op gedeeld) -
		// zo raakt het openen van jouw thread de ongelezen-badge van de rivaal niet.
		if (!M.bFromMe && M.FromContactId == ContactId && !M.bSeen
			&& (M.ForPlayerId.IsEmpty() || CallerId.IsEmpty() || M.ForPlayerId == CallerId)) { M.bSeen = true; bChanged = true; }
	}
	if (bChanged) { OnRep_Messages(); } // server: meteen lokaal de badge bijwerken; repliceert naar clients
}

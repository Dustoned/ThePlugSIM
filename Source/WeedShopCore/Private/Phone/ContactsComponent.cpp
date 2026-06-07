#include "Phone/ContactsComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Customer/CustomerBase.h"
#include "Phone/PhoneClientComponent.h"
#include "World/CityGenerator.h"
#include "Npc/NpcRegistryComponent.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
#include "Engine/Engine.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

UContactsComponent::UContactsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
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

	UE_LOG(LogWeedShop, Log, TEXT("Nieuw contact: %s"), *DisplayName.ToString());
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

	struct FCand { int32 Idx; float Warmth; };
	TArray<FCand> Cands;
	float TopWarmth = 0.f;
	for (int32 i = 0; i < Contacts.Num(); ++i)
	{
		const FName Id = Contacts[i].ContactId;
		if (Reg && Reg->IsOnCooldown(Id)) { continue; }
		if (Reg && !Reg->CanAppointToday(Id)) { continue; }
		float Warmth = 0.5f;
		if (Reg)
		{
			float R = 0.f, L = 0.f, A = 0.f; FText N;
			Reg->GetStats(Id, R, L, A, N);
			Warmth = FMath::Clamp((A + L + R) / 300.f, 0.f, 1.f); // 0..1 gemiddelde relatie
		}
		Cands.Add({ i, Warmth });
		TopWarmth = FMath::Max(TopWarmth, Warmth);
	}
	if (Cands.Num() == 0) { return; } // niemand beschikbaar (cooldown/dag-cap)

	// Globale kans dat er dit interval iemand appt, schaalt met de warmste relatie (begin laag).
	const float SendChance = FMath::Clamp(0.12f + TopWarmth * 0.78f, 0.12f, 0.95f);
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

	// Kies alvast WAT (strain binnen ~je level) en HOEVEEL (grammen, per klant-tier) de klant wil.
	FName WantStrain; int32 WantQty = FMath::RandRange(1, 3);
	if (Reg) { int32 Mn = 1, Mx = 3; Reg->GetTierOrderGrams(C.ContactId, Mn, Mx); WantQty = FMath::RandRange(Mn, Mx); }
	{
		const AWeedShopGameState* GSp = Cast<AWeedShopGameState>(GetOwner());
		const int32 PlayerLvl = (GSp && GSp->GetLeveling()) ? GSp->GetLeveling()->GetLevel() : 1;
		UStoreComponent* Store = GSp ? GSp->GetStore() : nullptr;
		if (ProductTable)
		{
			TArray<FName> Eligible; FName Lowest; int32 LowestLvl = MAX_int32;
			for (const FName& Row : ProductTable->GetRowNames())
			{
				const FString RS = Row.ToString();
				if (!RS.StartsWith(TEXT("Bud_"))) { continue; }
				const FName Strain(*RS.RightChop(4));
				const int32 Lvl = Store ? Store->RequiredLevelFor(Strain) : 1;
				if (Lvl < LowestLvl) { LowestLvl = Lvl; Lowest = Strain; }
				if (Lvl <= PlayerLvl + 2) { Eligible.Add(Strain); }
			}
			WantStrain = (Eligible.Num() > 0) ? Eligible[FMath::RandRange(0, Eligible.Num() - 1)] : Lowest;
		}
	}
	const FString WantStr = WantStrain.IsNone() ? TEXT("weed") : WantStrain.ToString();

	FPhoneMessage Msg;
	Msg.FromContactId = C.ContactId;
	Msg.SenderName = C.DisplayName;
	Msg.AppointmentTimeOfDay = ApptTime;
	Msg.WantStrain = WantStrain;
	Msg.WantQty = WantQty;
	Msg.Kind = (FMath::RandBool()) ? EAppointmentKind::TheyComeToYou : EAppointmentKind::YouGoToThem;

	// Adres opzoeken bij de bewoner met dit NpcId, zodat "kom bij mij langs" vertelt WAAR je heen moet.
	FString AddrStr;
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId == C.ContactId && It->IsResident()) { AddrStr = It->GetHomeNumber(); break; }
	}

	Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
		? FText::FromString(FString::Printf(TEXT("Yo, got any %s? Need %dg. I'll come by at %02d:%02d."), *WantStr, WantQty, HH, MM))
		: (AddrStr.IsEmpty()
			? FText::FromString(FString::Printf(TEXT("Got any %s? Need %dg - can you come by mine at %02d:%02d?"), *WantStr, WantQty, HH, MM))
			: FText::FromString(FString::Printf(TEXT("Got any %s? Need %dg - come by my place (no. %s) at %02d:%02d?"), *WantStr, WantQty, *AddrStr, HH, MM)));

	Messages.Insert(Msg, 0); // nieuwste bovenaan
	if (Messages.Num() > 40)
	{
		Messages.SetNum(40);
	}

	OnRep_Messages(); // server lokaal broadcasten
	if (GEngine)
	{
		UWeedToast::Notify(-1, 4.f, FColor(120, 180, 255),
			FString::Printf(TEXT("Message from %s"), *C.DisplayName.ToString()));
	}
}

void UContactsComponent::CheckAppointments()
{
	float Now = 0.f, Length = 1800.f;
	if (!GetCycleTime(Now, Length))
	{
		return;
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

void UContactsComponent::SpawnAppointmentCustomer(const FPhoneMessage& Msg)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Voorkeur: stuur de BESTAANDE bewoner met dit NpcId aan (geen dubbele NPC). YouGoToThem -> die
	// verschijnt in z'n eigen unit en wacht; TheyComeToYou -> die loopt naar de speler.
	for (TActorIterator<ACustomerBase> It(World); It; ++It)
	{
		if (It->NpcId == Msg.FromContactId && It->IsResident())
		{
			It->SetApptWant(Msg.WantStrain, Msg.WantQty);
			It->BeginAppointment(Msg.Kind == EAppointmentKind::TheyComeToYou);
			return;
		}
	}

	// Fallback: het contact loopt niet fysiek rond (uitgeroteerd). Contacten blijven tóch bereikbaar:
	//  - "Kom bij mij" (YouGoToThem): spawn 'm bij z'n EIGEN woning (het adres staat in het bericht).
	//  - "Ik kom langs" (TheyComeToYou): spawn 'm vlak vóór de speler.
	const bool bComeToYou = (Msg.Kind == EAppointmentKind::TheyComeToYou);
	FVector SpawnLoc(0.f, 0.f, 150.f);
	FRotator SpawnRot = FRotator::ZeroRotator;
	bool bPlacedAtHome = false;

	if (!bComeToYou)
	{
		// Huis-index uit het NpcId "Resident_####" halen.
		const FString IdStr = Msg.FromContactId.ToString();
		if (IdStr.StartsWith(TEXT("Resident_")))
		{
			const int32 HomeIdx = FCString::Atoi(*IdStr.RightChop(9));
			ACityGenerator* City = nullptr;
			for (TActorIterator<ACityGenerator> It(World); It; ++It) { City = *It; break; }
			if (City)
			{
				const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
				if (Homes.IsValidIndex(HomeIdx))
				{
					const FApartmentHome& H = Homes[HomeIdx];
					// Bij de DEUR: appartement -> in de gang vóór de unitdeur (HallPos); rijtjeshuis -> vóór
					// de voordeur (DoorPos). Beide bereikbaar voor de speler.
					const FVector DoorSpot = !H.HallPos.IsNearlyZero() ? H.HallPos : H.DoorPos;
					SpawnLoc = DoorSpot + FVector(0.f, 0.f, 4.f);
					bPlacedAtHome = true;
				}
			}
		}
	}

	if (!bPlacedAtHome)
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Player = PC->GetPawn())
			{
				// "Ik kom langs": wacht BUITEN bij je hoofdingang (de plek vóór je voordeur, waar ook de
				// pakketjes komen) - nooit binnen in de kamer.
				FVector HomeDoor;
				const UPhoneClientComponent* Ph = Player->FindComponentByClass<UPhoneClientComponent>();
				if (bComeToYou && Ph && Ph->GetActiveHomeLocation(HomeDoor))
				{
					SpawnLoc = HomeDoor + FVector(0.f, 0.f, 4.f);
				}
				else
				{
					SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 300.f;
					SpawnRot = (Player->GetActorLocation() - SpawnLoc).Rotation();
					SpawnRot.Pitch = 0.f;
					SpawnRot.Roll = 0.f;
				}
			}
		}
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
	Cust->SetApptWant(Msg.WantStrain, Msg.WantQty);
	if (!Msg.WantStrain.IsNone())
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

	Cust->FinishSpawning(SpawnTM);

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
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId == ContactId)
		{
			It->Loyalty = FMath::Clamp(It->Loyalty + Delta, 0.f, 100.f);
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
	const float Delta = bAccept ? 5.f : -12.f;
	ApplyRelationshipDelta(ContactId, Delta);

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
	Messages.Insert(Reply, 0);
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
	Messages.Insert(Mine, 0);
	// Hun antwoord: altijd akkoord (links).
	FPhoneMessage Theirs;
	Theirs.FromContactId = ContactId; Theirs.SenderName = SenderName; Theirs.bFromMe = false;
	Theirs.Status = 3; Theirs.AppointmentTimeOfDay = -1.f;
	Theirs.Body = FText::FromString(FString::Printf(TEXT("Works for me, see you at %s."), *FormatApptClock(NewTime)));
	Messages.Insert(Theirs, 0);
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
	Messages.Insert(Mine, 0);

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
	Messages.Insert(Rep, 0);
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
	Messages.Insert(M, 0);
	if (Messages.Num() > 40) { Messages.SetNum(40); }
	OnRep_Messages();    // server: meteen lokaal de UI bijwerken
}

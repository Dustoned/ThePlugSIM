#include "Phone/ContactsComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Customer/CustomerBase.h"
#include "Phone/PhoneClientComponent.h"
#include "World/CityGenerator.h"
#include "Npc/NpcRegistryComponent.h"
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

	// Formatteer als HH:MM op een 24-uurs klok.
	const float Frac = ApptTime / Length;
	const int32 TotalMin = FMath::RoundToInt(Frac * 24.f * 60.f);
	const int32 HH = (TotalMin / 60) % 24;
	const int32 MM = TotalMin % 60;

	FPhoneMessage Msg;
	Msg.FromContactId = C.ContactId;
	Msg.SenderName = C.DisplayName;
	Msg.AppointmentTimeOfDay = ApptTime;
	Msg.Kind = (FMath::RandBool()) ? EAppointmentKind::TheyComeToYou : EAppointmentKind::YouGoToThem;

	// Adres opzoeken bij de bewoner met dit NpcId, zodat "kom bij mij langs" vertelt WAAR je heen moet.
	FString AddrStr;
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId == C.ContactId && It->IsResident()) { AddrStr = It->GetHomeNumber(); break; }
	}

	Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
		? FText::FromString(FString::Printf(TEXT("Yo, I'll come by at %02d:%02d."), HH, MM))
		: (AddrStr.IsEmpty()
			? FText::FromString(FString::Printf(TEXT("Can you come by mine at %02d:%02d?"), HH, MM))
			: FText::FromString(FString::Printf(TEXT("Come by my place (no. %s) at %02d:%02d?"), *AddrStr, HH, MM)));

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
			UE_LOG(LogWeedShop, Log, TEXT("Afspraak met %s is nu."), *Msg.SenderName.ToString());
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
	Cust->DesiredProductId = FName(TEXT("Bud_NorthernLights"));
	Cust->DesiredQuantity = 2;
	Cust->BudgetCentsPerUnit = 1500;
	Cust->bDespawnAfterServed = true; // afspraak-klant vertrekt na de deal
	Cust->bNeedsPlayer = true;        // afspraak: poppetje op de kompas (je moet bij deze zijn)

	Cust->FinishSpawning(SpawnTM);

	UE_LOG(LogWeedShop, Log, TEXT("Afspraak-klant gespawned voor %s."), *Msg.SenderName.ToString());
}

FString UContactsComponent::FormatApptClock(float TimeOfDay) const
{
	float Now = 0.f, Length = 1800.f;
	GetCycleTime(Now, Length);
	if (Length <= 0.f) { Length = 1800.f; }
	const float Frac = FMath::Fmod(FMath::Max(0.f, TimeOfDay), Length) / Length;
	const int32 TotalMin = FMath::RoundToInt(Frac * 24.f * 60.f);
	return FString::Printf(TEXT("%02d:%02d"), (TotalMin / 60) % 24, TotalMin % 60);
}

int32 UContactsComponent::ClockMinutesOf(float TimeOfDay) const
{
	float Now = 0.f, Length = 1800.f;
	GetCycleTime(Now, Length);
	if (Length <= 0.f) { Length = 1800.f; }
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

	float Now = 0.f, Length = 1800.f;
	GetCycleTime(Now, Length);
	if (Length <= 0.f) { Length = 1800.f; }
	const float Frac = FMath::Clamp(static_cast<float>(MinutesOfDay), 0.f, 1439.f) / 1440.f; // klok -> fractie van de dag
	const float NewTime = Frac * Length;

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

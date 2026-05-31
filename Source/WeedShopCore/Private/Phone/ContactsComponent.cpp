#include "Phone/ContactsComponent.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Customer/CustomerBase.h"
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
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
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

	const FPhoneContact& C = Contacts[FMath::RandRange(0, Contacts.Num() - 1)];

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
	Msg.Body = (Msg.Kind == EAppointmentKind::TheyComeToYou)
		? FText::FromString(FString::Printf(TEXT("Yo, I'll come by at %02d:%02d."), HH, MM))
		: FText::FromString(FString::Printf(TEXT("Can you come by mine at %02d:%02d?"), HH, MM));

	Messages.Insert(Msg, 0); // nieuwste bovenaan
	if (Messages.Num() > 40)
	{
		Messages.SetNum(40);
	}

	OnRep_Messages(); // server lokaal broadcasten
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor(120, 180, 255),
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
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Magenta,
					FString::Printf(TEXT("Appointment: %s is here!"), *Msg.SenderName.ToString()));
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

	// Spawn-plek: vlak vóór de eerste speler (zodat de klant 'bij je' arriveert).
	FVector SpawnLoc(0.f, 0.f, 150.f);
	FRotator SpawnRot = FRotator::ZeroRotator;
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Player = PC->GetPawn())
		{
			SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 300.f;
			SpawnRot = (Player->GetActorLocation() - SpawnLoc).Rotation();
			SpawnRot.Pitch = 0.f;
			SpawnRot.Roll = 0.f;
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
		GEngine->AddOnScreenDebugMessage(-1, 3.f, bAccept ? FColor::Green : FColor::Orange,
			FString::Printf(TEXT("%s: appointment %s"), *SenderName.ToString(),
				bAccept ? TEXT("accepted") : TEXT("cancelled")));
	}

	OnRep_Messages();
}

void UContactsComponent::OnRep_Messages()
{
	OnMessagesChanged.Broadcast();
}

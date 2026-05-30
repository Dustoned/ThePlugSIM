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
			FString::Printf(TEXT("Nieuw contact: %s"), *DisplayName.ToString()));
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
		? FText::FromString(FString::Printf(TEXT("Yo, ik kom om %02d:%02d langs."), HH, MM))
		: FText::FromString(FString::Printf(TEXT("Kun je om %02d:%02d bij mij langskomen?"), HH, MM));

	Messages.Insert(Msg, 0); // nieuwste bovenaan
	if (Messages.Num() > 12)
	{
		Messages.SetNum(12);
	}

	OnRep_Messages(); // server lokaal broadcasten
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor(120, 180, 255),
			FString::Printf(TEXT("Bericht van %s"), *C.DisplayName.ToString()));
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
					FString::Printf(TEXT("Afspraak: %s is er!"), *Msg.SenderName.ToString()));
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

void UContactsComponent::RespondTopPending(bool bAccept)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	for (FPhoneMessage& Msg : Messages)
	{
		if (Msg.Status != 0)
		{
			continue;
		}

		Msg.Status = bAccept ? 1 : 2;
		const float Delta = bAccept ? 5.f : -12.f;

		// Relatie in de contactenlijst bijwerken.
		for (FPhoneContact& Contact : Contacts)
		{
			if (Contact.ContactId == Msg.FromContactId)
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
				if (Reg->GetStats(Msg.FromContactId, R, L, A, N))
				{
					Reg->ApplyStats(Msg.FromContactId, R, FMath::Clamp(L + Delta, 0.f, 100.f), A);
				}
			}
		}

		// Live klant met deze persoon ook meteen bijwerken (als die er is).
		for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
		{
			if (It->NpcId == Msg.FromContactId)
			{
				It->Loyalty = FMath::Clamp(It->Loyalty + Delta, 0.f, 100.f);
				break;
			}
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, bAccept ? FColor::Green : FColor::Orange,
				FString::Printf(TEXT("%s: afspraak %s"), *Msg.SenderName.ToString(),
					bAccept ? TEXT("geaccepteerd") : TEXT("afgezegd")));
		}

		OnRep_Messages();
		return;
	}
}

void UContactsComponent::OnRep_Messages()
{
	OnMessagesChanged.Broadcast();
}

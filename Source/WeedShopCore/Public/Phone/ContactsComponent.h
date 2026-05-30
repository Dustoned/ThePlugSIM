// UContactsComponent — telefoon-contacten + berichten op de GameState (gedeeld, co-op).
// NPC-klanten waarmee je contact had worden contacten; ze sturen periodiek afspraak-berichten
// ("Ik kom om HH:MM langs" of "Kom om HH:MM bij mij"). Bij de afspraaktijd volgt een melding.
// Server-authoritative; contacten + berichten repliceren naar de clients.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ContactsComponent.generated.h"

UENUM(BlueprintType)
enum class EAppointmentKind : uint8
{
	TheyComeToYou	UMETA(DisplayName = "Klant komt langs"),
	YouGoToThem		UMETA(DisplayName = "Jij gaat langs")
};

USTRUCT(BlueprintType)
struct FPhoneContact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FName ContactId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FText DisplayName;

	// Relatie 0..100 (gemiddelde van respect/loyaliteit/verslaving op moment van toevoegen).
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	float Relationship = 0.f;
};

USTRUCT(BlueprintType)
struct FPhoneMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FName FromContactId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FText SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FText Body;

	// Tijdstip in de dag-cyclus (seconden) van de afspraak; < 0 = geen afspraak.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	float AppointmentTimeOfDay = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	EAppointmentKind Kind = EAppointmentKind::TheyComeToYou;

	// 0 = open (wacht op antwoord), 1 = geaccepteerd, 2 = geweigerd.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	uint8 Status = 0;

	// Is de afspraaktijd al aangekondigd?
	UPROPERTY()
	bool bAnnounced = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPhoneMessagesChanged);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UContactsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UContactsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Hoe vaak (seconden) een willekeurig contact een afspraak-bericht stuurt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Phone")
	float MessageIntervalSeconds = 25.f;

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Phone")
	FOnPhoneMessagesChanged OnMessagesChanged;

	// Server: voeg een contact toe (no-op als al bekend).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RegisterContact(FName ContactId, const FText& DisplayName, float Relationship);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool HasContact(FName ContactId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	const TArray<FPhoneContact>& GetContacts() const { return Contacts; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	const TArray<FPhoneMessage>& GetMessages() const { return Messages; }

	// Server: beantwoord het eerste open afspraak-bericht. Accept = loyaliteit +, weiger = loyaliteit -.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RespondTopPending(bool bAccept);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	TArray<FPhoneContact> Contacts;

	UPROPERTY(ReplicatedUsing = OnRep_Messages)
	TArray<FPhoneMessage> Messages;

	UFUNCTION()
	void OnRep_Messages();

	// Server: maak een afspraak-bericht van een willekeurig contact.
	void SendRandomAppointment();

	// Server: kondig afspraken aan waarvan de tijd is bereikt + laat de klant arriveren.
	void CheckAppointments();

	// Server: laat een klant verschijnen bij de speler voor deze (geaccepteerde) afspraak.
	void SpawnAppointmentCustomer(const FPhoneMessage& Msg);

	// Huidige tijd in de dag-cyclus (seconden) + cyclusduur, via de GameState.
	bool GetCycleTime(float& OutNow, float& OutLength) const;

	// Producten-tabel voor de arriverende klant (marktprijs-opzoek).
	UPROPERTY()
	TObjectPtr<class UDataTable> ProductTable;

	float MessageTimer = 0.f;
};

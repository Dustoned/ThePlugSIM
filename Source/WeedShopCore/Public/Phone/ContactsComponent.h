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

	// True = dit is mijn eigen antwoord in de chat (rechts uitgelijnd), niet van de klant.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	bool bFromMe = false;

	// Wat de klant wil (zodat je je kunt voorbereiden): strain + aantal zakjes. NAME_None/0 = onbekend.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FName WantStrain = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	int32 WantQty = 0;
	// Volledig product dat de klant wil (Bag_/Hash_/Edible_<strain>). Leeg = wiet (Bag_<WantStrain>).
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FName WantProduct = NAME_None;

	// COMPETITIVE: voor welke speler dit bericht is (stabiele speler-id). Leeg = voor iedereen (co-op).
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FString ForPlayerId;

	// Realtime-seconden toen dit verzoek binnenkwam (voor follow-up/opgeven + reactiesnelheid). < 0 = n.v.t.
	UPROPERTY()
	float SentRealTime = -1.f;
	// Heeft de klant al een "you there?"-herinnering gestuurd?
	UPROPERTY()
	bool bNudged = false;
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

	// Server: voeg een los info-bericht van een contact toe (afspraak-status: onderweg / ben er / te laat).
	// Verschijnt als ongelezen inkomend bericht (telt mee voor de notificatie-bubble), geen openstaande afspraak.
	void PushInfoMessage(FName ContactId, const FText& SenderName, const FText& Body);

	// Save/load: zet de hele contacten- + berichten-lijst terug.
	void RestoreContacts(const TArray<FPhoneContact>& InContacts, const TArray<FPhoneMessage>& InMessages);

	// Kloktijd (minuten van de dag, 0..1439) van een afspraak-TimeOfDay. Voor de tijd-kiezer in de chat.
	int32 ClockMinutesOf(float TimeOfDay) const;

	// Server: beantwoord het eerste open afspraak-bericht. Accept = loyaliteit +, weiger = loyaliteit -.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RespondTopPending(bool bAccept);

	// Server: beantwoord het nieuwste open afspraak-bericht van een specifiek contact (chat-thread).
	// Voegt ook mijn antwoord als chat-regel toe.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RespondToContact(FName ContactId, bool bAccept);

	// Server: stel je EIGEN kloktijd voor (MinutesOfDay 0..1439). Het contact gaat altijd akkoord, geen nadeel.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ProposeTimeToContact(FName ContactId, int32 MinutesOfDay);

	// Server: bied een ANDERE strain aan dan gevraagd (substituut) via de chat. Kans op akkoord o.b.v.
	// loyaliteit/verslaving + of de aangeboden THC de verwachte haalt/overtreft. Bij akkoord wil de afspraak
	// voortaan deze strain (de arriverende klant leest WantStrain).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ProposeAlternativeStrain(FName ContactId, FName NewStrain, float OfferedThc, float OfferedQualPct);

	// Kans (0..1) dat een contact een substituut-strain accepteert — voor de chat-preview.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	float SubstituteAcceptChance(FName ContactId, FName ReqStrain, FName NewStrain, float OfferedThc) const;

	// De strain die het open afspraak-bericht van dit contact vraagt (NAME_None als geen open afspraak).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	FName GetRequestedStrain(FName ContactId) const;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	TArray<FPhoneContact> Contacts;

	UPROPERTY(ReplicatedUsing = OnRep_Messages)
	TArray<FPhoneMessage> Messages;

	UFUNCTION()
	void OnRep_Messages();

	// Server: pas de relatie/loyaliteit van een persoon aan (contactenlijst + register + live klant).
	void ApplyRelationshipDelta(FName ContactId, float Delta);

	// Formatteer een tijdstip-in-cyclus (seconden) als "HH:MM" op een 24-uurs klok.
	FString FormatApptClock(float TimeOfDay) const;

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

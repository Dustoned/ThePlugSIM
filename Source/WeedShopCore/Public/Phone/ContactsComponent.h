// UContactsComponent — telefoon-contacten + berichten op de GameState (gedeeld, co-op).
// NPC-klanten waarmee je contact had worden contacten; ze sturen periodiek afspraak-berichten
// ("Ik kom om HH:MM langs" of "Meet me outside om HH:MM"). Bij de afspraaktijd volgt een melding.
// Server-authoritative; contacten + berichten repliceren naar de clients.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ContactsComponent.generated.h"

class APawn;

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

	// COMPETITIVE: welke speler dit contact BEZIT (stabiele speler-id). Leeg = gedeeld/co-op (iedereen ziet 't).
	// De ContactId zelf is ALTIJD de BASIS-NpcId (geen "#spelerId"-suffix); het eigenaarschap zit hier.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FString OwnerPlayerId;
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

	// Voor YouGoToThem: de exacte buiten-wachtplek die bij het bericht/tijdstip hoort.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	bool bHasPlannedMeetSpot = false;
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FVector PlannedMeetSpot = FVector::ZeroVector;

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

	// GELEZEN: gezet door de server zodra een speler de chat-thread opent. Repliceert mee (de hele Messages-
	// array repliceert), zodat de ongelezen-badge bij BEIDE co-op-spelers tegelijk verdwijnt.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	bool bSeen = false;

	// Realtime-seconden toen dit verzoek binnenkwam (voor follow-up/opgeven + reactiesnelheid). < 0 = n.v.t.
	UPROPERTY()
	float SentRealTime = -1.f;
	// In-game klok-uur (0..24) toen het bericht verstuurd werd, voor de "HH:MM"-tijdstempel in de chat. < 0 = onbekend.
	UPROPERTY()
	float SentClockHour = -1.f;
	// Heeft de klant al een "you there?"-herinnering gestuurd?
	UPROPERTY()
	bool bNudged = false;

	// --- DAG-ORDER (premium VIP-opdracht, schaalt met level) ---
	// True = premium order: specifieke strain + min-THC vóór de deadline = bonus-uitbetaling.
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	bool bOrder = false;
	// Minimaal vereiste THC% van het geleverde product (0 = geen eis).
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	float MinThc = 0.f;
	// Bonus-factor op de verkoopprijs als je op spec levert (1.0 = geen bonus, 1.6 = +60%).
	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	float BonusMult = 1.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPhoneMessagesChanged);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UContactsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UContactsComponent();

	// --- GEDEELDE AFSPRAAK-TIMING (D.13) ---
	// Een verzoek wacht ResponseWindowSec real-sec op antwoord voordat de klant opgeeft (GiveUpDelay).
	// De gevraagde afspraak-offset (in cyclus-seconden) moet ALTIJD na dat antwoord-venster vallen,
	// anders is de afspraaktijd al verstreken voordat je kon reageren. PhoneWidget's aftel-balk leest
	// deze constanten zodat balk en cancel niet uiteenlopen.
	static constexpr float ResponseWindowSec = 150.f; // real-sec dat een verzoek open blijft (== GiveUpDelay)
	static constexpr float NudgeDelaySec = 60.f;       // "you there?"-herinnering na deze real-sec zonder antwoord
	static constexpr float ApptOffsetMinSec = ResponseWindowSec + 60.f; // ondergrens gevraagde offset (cyclus-sec) = 210
	static constexpr float ApptOffsetMaxSec = 360.f;   // bovengrens gevraagde offset (cyclus-sec)
	static constexpr float YouGoToThemTravelExtraMaxSec = 180.f; // extra reistijd voor verre buiten-wachtplekken
	static constexpr float ApptOffsetVisualMaxSec = ApptOffsetMaxSec + YouGoToThemTravelExtraMaxSec;
	static constexpr float AnnounceWindowSec = 30.f;   // ruimere "gepasseerd binnen deze marge"-marge voor de afspraak-aankondiging

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Hoe vaak (seconden) een willekeurig contact een afspraak-bericht stuurt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Phone")
	float MessageIntervalSeconds = 25.f;

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Phone")
	FOnPhoneMessagesChanged OnMessagesChanged;

	// Server: voeg een contact toe (no-op als al bekend). ContactId = BASIS-NpcId (een eventuele "#spelerId"-
	// suffix wordt gestript). OwnerPlayerId = de eigenaar-speler in competitive (leeg = gedeeld/co-op).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RegisterContact(FName ContactId, const FText& DisplayName, float Relationship, const FString& OwnerPlayerId = FString());

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool HasContact(FName ContactId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	const TArray<FPhoneContact>& GetContacts() const { return Contacts; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	const TArray<FPhoneMessage>& GetMessages() const { return Messages; }

	// Server: voeg een los info-bericht van een contact toe (afspraak-status: onderweg / ben er / te laat).
	// Verschijnt als ongelezen inkomend bericht (telt mee voor de notificatie-bubble), geen openstaande afspraak.
	// ForPlayerId = de eigenaar-speler (competitive; leeg = gedeeld/co-op) -> lekt niet naar de rivaal.
	void PushInfoMessage(FName ContactId, const FText& SenderName, const FText& Body, const FString& ForPlayerId = FString());

	// Server: markeer alle inkomende berichten van deze contact als gelezen (bSeen). Repliceert mee, zodat
	// de ongelezen-badge bij BEIDE co-op-spelers tegelijk verdwijnt. CallerId = de speler die de thread opent
	// (competitive: alleen berichten die voor hem zijn; leeg/co-op = alle berichten van dit contact, ongewijzigd).
	void MarkThreadSeen(FName ContactId, const FString& CallerId = FString());

	// Save/load: zet de hele contacten- + berichten-lijst terug.
	void RestoreContacts(const TArray<FPhoneContact>& InContacts, const TArray<FPhoneMessage>& InMessages);

	// Kloktijd (minuten van de dag, 0..1439) van een afspraak-TimeOfDay. Voor de tijd-kiezer in de chat.
	int32 ClockMinutesOf(float TimeOfDay) const;

	// Server: beantwoord het eerste open afspraak-bericht. Accept = loyaliteit +, weiger = loyaliteit -.
	// CallerId = de antwoordende speler (competitive: alleen berichten die voor hem zijn; leeg/co-op = gedeeld).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RespondTopPending(bool bAccept, const FString& CallerId = FString());

	// Server: beantwoord het nieuwste open afspraak-bericht van een specifiek contact (chat-thread).
	// Voegt ook mijn antwoord als chat-regel toe. CallerId = de antwoordende speler (competitive-filter).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RespondToContact(FName ContactId, bool bAccept, const FString& CallerId = FString());

	// Server: stel je EIGEN kloktijd voor (MinutesOfDay 0..1439). Het contact gaat altijd akkoord, geen nadeel.
	// CallerId = de voorstellende speler (competitive-filter; leeg/co-op = gedeeld).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ProposeTimeToContact(FName ContactId, int32 MinutesOfDay, const FString& CallerId = FString());

	// Server: bied een ANDERE strain aan dan gevraagd (substituut) via de chat. Kans op akkoord o.b.v.
	// loyaliteit/verslaving + of de aangeboden THC de verwachte haalt/overtreft. Bij akkoord wil de afspraak
	// voortaan deze strain (de arriverende klant leest WantStrain). CallerId = de aanbiedende speler (comp-filter).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ProposeAlternativeStrain(FName ContactId, FName NewStrain, float OfferedThc, float OfferedQualPct, const FString& CallerId = FString());

	// Kans (0..1) dat een contact een substituut-strain accepteert — voor de chat-preview.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	float SubstituteAcceptChance(FName ContactId, FName ReqStrain, FName NewStrain, float OfferedThc) const;

	// De strain die het open afspraak-bericht van dit contact vraagt (NAME_None als geen open afspraak).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	FName GetRequestedStrain(FName ContactId) const;

	// Strip een eventuele "#spelerId"-suffix van een NpcId -> de BASIS-NpcId. Contacten/berichten/matching
	// draaien ALTIJD op de basis-id; het per-speler eigenaarschap zit in OwnerPlayerId/ForPlayerId apart.
	// Publiek zodat ACustomerBase (SubmitOfferProduct/WriteStatsToRegistry) dezelfde strip-regel deelt.
	static FName BaseNpcId(FName NpcId);

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

	// De pawn die hoort bij een stabiele speler-id (nullptr als niet verbonden). Voor per-speler meldingen.
	APawn* ResolvePawnForPlayer(const FString& PlayerId) const;

	// Per-speler melding: leeg PlayerId (co-op) -> alle spelers (Notify -1); anders alleen de eigenaar-pawn.
	// Voor afspraak-events (aankondiging/accept/decline/contact-unlock) die NOOIT bij de andere speler mogen lekken.
	void NotifyOwnerPlayer(const FString& PlayerId, float Seconds, const FColor& Color, const FString& Text) const;

	// Stempelt het bericht met de huidige tijd (in-game klok-uur + realtime) en zet 't bovenaan (nieuwste eerst).
	void StampAndInsert(FPhoneMessage& M);

	// Huidige tijd in de dag-cyclus (seconden) + cyclusduur, via de GameState.
	bool GetCycleTime(float& OutNow, float& OutLength) const;

	// Producten-tabel voor de arriverende klant (marktprijs-opzoek).
	UPROPERTY()
	TObjectPtr<class UDataTable> ProductTable;

	float MessageTimer = 0.f;
};

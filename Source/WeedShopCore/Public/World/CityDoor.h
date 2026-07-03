// ACityDoor — een scharnierende deur die automatisch opengaat als een speler dichtbij komt en weer
// dichtgaat als je weg loopt. Lokaal gespawnd door de CityGenerator bij de winkel-/gebouwopeningen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "CityDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class USphereComponent;
class UPrimitiveComponent;
class UWorldSyncComponent;

UCLASS()
class WEEDSHOPCORE_API ACityDoor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ACityDoor();

	// Registry van alle levende deuren (Add in BeginPlay, Remove in EndPlay): hot paths lopen
	// O(instanties) i.p.v. TActorIterator over ALLE actors. Weak-ptrs -> IsValid() checken.
	static const TArray<TWeakObjectPtr<ACityDoor>>& GetAll();

	// Stel de deur in (afmeting + kleur). Hinge zit aan de -X-kant; dicht = paneel langs +X.
	void Setup(float Width, float Height, const FLinearColor& Color);

	// Variant voor asset-pack deur-BLADEN (mesh met pivot op het scharnier): gebruik die mesh als paneel
	// (geen cube/schaal) en draai open naar OpenDeg. Voor de DoorRetrofitter in externe maps.
	void SetupLeaf(class UStaticMesh* LeafMesh, float OpenDeg, float TriggerRadius = 150.f);

	// SCHUIFDEUR-modus (balkon-puien): het blad schuift opzij langs z'n eigen breedte i.p.v. draaien.
	void SetSlideMode(float InSlideDist);
	// Dicht-stand-offset (lokaal): voor puien die in de map OPEN geparkeerd staan (blad gestapeld op
	// het vaste paneel). Dicht = blad verschoven naar de vrije opening; open = terug naar geparkeerd.
	void SetSlideClosedOffset(const FVector& LocalOff);

	// Extra mee-draaiend onderdeel (bv. het losse GLAS van een pack-deurblad): hangt aan het scharnier
	// op de gegeven wereld-transform, zodat het met de deur mee opent.
	void AddLeafExtra(class UStaticMesh* Mesh, const FTransform& WorldTM);

	// Deterministische bewoner-naam per huis-index (gelijk op server EN client, zodat de client zelf
	// de juiste "LOCKED - <naam>"-popup kan tonen zonder replicatie). bFemale kiest uit de vrouwen- of
	// mannen-voornamenlijst zodat de naam bij de (voorspelde) skin-gender past.
	static FString ResidentNameForIndex(int32 Index, bool bFemale = false);

	// Gender-correcte bewoner-naam voor een deur-bordje: bepaalt de gender DETERMINISTISCH uit de
	// voorspelde skin van "Resident_<NameIdx>" (zelfde afleiding als de rondlopende bewoner) zodat bordje
	// en bewoner NOOIT verschillen - werkt ook voordat de pawn bestaat. Prefereert de registry-DisplayName
	// als die er al staat (host schreef 'm gender-correct). W mag null zijn (val terug op mannen-lijst).
	static FString ResidentNameForDoor(class UWorld* W, int32 NameIdx);

	// Nette weergavenaam voor een NpcId, ook als de registry (nog) geen naam heeft: "Resident_0121"
	// -> de bijbehorende funny naam; lege id -> "Customer". Gebruikt door deal/map-popups als fallback.
	static FString FriendlyNpcName(FName NpcId);

	// Appartement-nummer (verdieping*100 + volgnummer, zoals een echt complex): bordje op het
	// deurblad aan beide kanten + het nummer in de interactie-prompts.
	void SetAptNumber(int32 Num);
	int32 GetAptNumber() const { return AptNumber; }

	// Maak dit een bewoner-deur: op slot voor de speler, met "LOCKED - <naam> lives here".
	// COMPETITIVE: als deze deur door een speler geclaimd is (bCompForced), negeer bewoner-toewijzing - anders
	// flikkert 'ie tussen "your home" en een NPC-naam doordat meerdere systemen (DoorRetrofitter + CustomerSpawner)
	// elke scan een bewoner toewijzen.
	void SetResident(const FString& Name) { if (bCompForced) { return; } bLocked = true; bPlayerHome = false; bForSale = false; ResidentName = Name; bOpen = false; }
	// COMPETITIVE co-op: claim deze deur als speler-huis (open) of als partner-deur (op slot, "Co-op partner")
	// en zet 'm STICKY -> verdere bewoner-toewijzing wordt genegeerd.
	void SetCompPlayerHome() { bLocked = false; bPlayerHome = true; bForSale = false; ResidentName.Empty(); bCompForced = true; }
	void SetCompPartner() { bLocked = true; bPlayerHome = false; bForSale = false; ResidentName = TEXT("Co-op partner"); bOpen = false; bCompForced = true; }
	void SetCompRelease() { bCompForced = false; } // niet langer een competitive-deur -> weer vrij voor bewoner-pass
	bool IsCompForced() const { return bCompForced; }
	bool IsLocked() const { return bLocked; }
	// Staat het blad NU in de dichte stand? (snap meet het blad-midden -> alleen kloppend als 'ie dicht is)
	bool IsPanelClosedNow() const { return FMath::Abs(CurAngle) < 2.f; }
	// Zet een scheef-geconverteerde deur netjes in het dichtstbijzijnde deurvak (blad gecentreerd op het kozijn).
	static void SnapToNearestFrame(class UWorld* W, ACityDoor* Door);
	UStaticMeshComponent* GetPanel() const { return Panel; }
	float GetOpenSwing() const { return OpenSwingDeg; }

	// HUUR: achterstallig -> deur op slot, F aan de deur = betalen (cash van de pawn).
	void SetRentOverdue(int64 Cents) { bLocked = true; bRentDue = true; RentCents = Cents; bForSale = false; bOpen = false; }
	void ClearRentOverdue() { bRentDue = false; bLocked = false; RentCents = 0; } // gedeelde overdue-vlag ging uit
	bool IsRentOverdue() const { return bRentDue; }
	bool ConsumeRentJustPaid() { const bool b = bRentJustPaid; bRentJustPaid = false; return b; }

	// Jouw eigen woning: open/dicht zoals normaal, prompt "Your home".
	// COMPETITIVE: een speler-geclaimde deur (bCompForced) niet stilletjes laten ontgrendelen door de
	// bewoner-pass o.i.d. - alleen SetCompPlayerHome/SetCompPartner mogen 'm nog wijzigen.
	void SetPlayerHome() { if (bCompForced) { return; } bLocked = false; bPlayerHome = true; bForSale = false; ResidentName.Empty(); }
	bool IsPlayerHome() const { return bPlayerHome; }
	// Koopbaar pand (nog niet van jou): op slot met "TE KOOP - koop via telefoon".
	void SetForSale() { bLocked = true; bPlayerHome = false; bForSale = true; bOpen = false; ResidentName.Empty(); }

	// Welke kant de deur opendraait (graden). Standaard -95. Voor de RECHTER helft van een dubbele deur
	// zet je +95 zodat beide bladen DEZELFDE kant op draaien i.p.v. tegengesteld.
	void SetOpenSwing(float Deg) { OpenSwingDeg = Deg; }

	// Interact (F) opent/sluit de deur.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	// CO-OP: gedeeld via stabiel positie-id (WorldSync) i.p.v. lokaal -> beide spelers zien de deur open/dicht.
	virtual uint32 GetWorldSyncDoorId() const override { return DoorSyncId; }
	uint32 DoorSyncId = 0; // berekend in BeginPlay uit positie+yaw (deterministisch gelijk op host/clients)

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // registry-remove
	virtual void Tick(float DeltaSeconds) override;

	// Cache van de gedeelde WorldSync (1x resolven; her-resolven zodra invalid): geen
	// GetGameState-lookup per deur per frame in Tick.
	TWeakObjectPtr<const UWorldSyncComponent> CachedWorldSync;

	// Auto-open: deur zwaait open als iemand ervoor staat (speler of NPC die naar buiten komt).
	UFUNCTION() void OnTriggerBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIdx, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION() void OnTriggerEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIdx);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LeafExtras; // glas-delen van het blad (collision volgt het paneel)
	UPROPERTY() TArray<TObjectPtr<class UTextRenderComponent>> AptTexts; // nummerbordjes (voor- en achterkant van het blad)
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger; // nabijheid-zone

	int32 NpcNear = 0;      // aantal NPC's in de zone
	int32 OtherNear = 0;    // aantal andere pawns (speler) in de zone
	float CurAngle = 0.f;   // huidige scharnierhoek
	float OpenSwingDeg = -95.f; // doelhoek bij open (per deur instelbaar; dubbele-deur rechter helft = +95)
	bool bSlideMode = false;    // true = schuiven (balkon-pui) i.p.v. draaien
	float SlideDist = 130.f;    // schuif-afstand (cm, langs lokale Y van het blad)
	FVector SlideBase = FVector::ZeroVector; // dicht-stand-offset van het blad (open-geparkeerde pui)
	bool bOpen = false;     // open/dicht (getoggled via interact)
	bool bLocked = false;   // bewoner-deur: kan niet door de speler geopend worden
	bool bPlayerHome = false; // jouw eigen woning (open/dicht, prompt "Your home")
	bool bForSale = false;    // koopbaar pand, nog niet van jou
	bool bCompForced = false; // COMPETITIVE: speler-geclaimde deur (speler-huis/partner) -> negeer bewoner-toewijzing
	FString ResidentName;   // naam voor de "LOCKED - ... lives here"-prompt
	int32 AptNumber = 0;    // 0 = geen nummer (winkeldeur e.d.)
	bool bRentDue = false;      // huur achterstallig: op slot tot betaald (F aan de deur)
	bool bRentJustPaid = false; // 1x-signaal naar de huur-administratie (DoorRetrofitter)
	int64 RentCents = 0;        // openstaand huurbedrag
};

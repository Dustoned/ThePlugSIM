// UBuildComponent — plaats-modus voor placeables (nu: de kweekpot). Zit op de speler-pawn.
//
// Flow: speler koopt een "Pot" (supplier) -> item in de inventory. Met de plaats-toets gaat hij
// in plaats-modus: een doorzichtige spook-pot volgt waar je naar de vloer kijkt. Links-klik
// plaatst (server-authoritative: spawnt een AGrowPlant en haalt 1 Pot uit de inventory).
//
// CO-OP: het richten/preview is puur lokaal (alleen voor de bestuurde speler). De daadwerkelijke
// spawn loopt via een Server-RPC zodat alle spelers de pot zien. De pot (AGrowPlant) repliceert.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Placement/PlaceableTypes.h"
#include "BuildComponent.generated.h"

class UStaticMeshComponent;
class UInventoryComponent;
class UMaterialInstanceDynamic;
class AGrowPlant;

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UBuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Hoe ver je kunt plaatsen (cm) vanaf het camerastandpunt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float PlaceDistance = 400.f;

	// Rastergrootte (cm) voor grid-plaatsing terwijl je Shift inhoudt (netjes op een rij).
	// Iets groter dan de pot-tussenruimte zodat gesnapte potten altijd geldig zijn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float GridSize = 60.f;

	// Start/stop de plaats-modus voor een pot (toggle). Doet niets als je geen pot hebt.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void TogglePotPlacement();

	// Stop de plaats-modus (zonder te plaatsen).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void CancelPlacing();

	// Plaats op de huidige (geldige) preview-positie.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void ConfirmPlacement();

	// Draai het te plaatsen object 90° (toets R tijdens plaats-modus).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void RotatePlacement();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	bool IsPlacing() const { return bPlacing; }

	// Hoe lang (sec) je de oppak-toets moet inhouden om een pot op te pakken.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float PickupHoldDuration = 0.5f;

	// Voortgang van het oppakken (0..1), voor de HUD. 0 als je nu niets oppakt.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	float GetPickupAlpha() const { return PickupHoldDuration > 0.f ? FMath::Clamp(PickupHoldAccum / PickupHoldDuration, 0.f, 1.f) : 0.f; }

	// Hoe lang (sec) je de weggooi-toets (X) moet inhouden om een geplante pot te legen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float DiscardHoldDuration = 1.0f;

	// Voortgang van het weggooien (0..1), voor de HUD. 0 als je nu niets weggooit.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	float GetDiscardAlpha() const { return DiscardHoldDuration > 0.f ? FMath::Clamp(DiscardHoldAccum / DiscardHoldDuration, 0.f, 1.f) : 0.f; }

	// Of de huidige preview-positie geldig is (kijkt naar een vlak/de vloer).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	bool IsPlacementValid() const { return bValidSpot; }
	// Concrete reden waarom je niet mag plaatsen (voor de popup-hint), i.p.v. altijd dezelfde tekst.
	FString GetPlacementHint() const { return PlacementHint.IsEmpty() ? FString(TEXT("Can't place here")) : PlacementHint; }

	// Alleen binnenshuis plaatsen toestaan (er moet een plafond/dak boven de plek zitten).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	bool bIndoorsOnly = true;

	// Hoe hoog (cm) we naar boven tracen om een plafond/dak te vinden = "binnen".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float CeilingTraceHeight = 800.f;

	// True als er binnen CeilingTraceHeight een plafond/dak boven dit vloerpunt zit (= binnenshuis).
	bool IsIndoors(const FVector& FloorPoint) const;

	// True als het punt BINNEN een woning valt die de speler heeft gekocht (niet in winkels/hal/ongekocht).
	bool IsInOwnedHome(const FVector& P) const;

	// Of dit aangekeken object opgepakt kan worden (pot/prop/rek/bench/schap/gootsteen). Voor de UI-hint.
	bool IsPickable(const AActor* A) const;

protected:
	virtual void BeginPlay() override;

	// Start plaats-modus met een specifiek item-id (moet in de inventory zitten).
	void StartPlacing(FName ItemId);

	// Server: plaats het placeable (haalt 1 uit de inventory, spawnt de actor).
	UFUNCTION(Server, Reliable)
	void ServerPlace(FName ItemId, FVector Location, FRotator Rotation);

	// De lokale speler stuurt z'n preview-staat naar de server (die repliceert 'm naar de andere
	// spelers, zodat zij de ghost ook zien). Unreliable + getemporiseerd.
	UFUNCTION(Server, Unreliable)
	void ServerUpdatePreview(bool bInPlacing, FVector Location, float Yaw, bool bValid, FName InItemId);

	// Betrouwbare sluiting van de preview. De gewone preview-stream is bewust unreliable, maar "uit"
	// moet als state-transition altijd aankomen.
	UFUNCTION(Server, Reliable)
	void ServerClearPreview();

	// Server: pak een (lege) pot weer op -> terug als item in de inventory.
	UFUNCTION(Server, Reliable)
	void ServerPickup(AActor* Target);

	// Server: gooi de inhoud van een GEPLANTE pot weg (X-hold): leegt alle plant-plekken + soil/fert.
	UFUNCTION(Server, Reliable)
	void ServerDiscardPlant(AGrowPlant* Plant);

	// Maakt de ghost-mesh + dynamisch materiaal aan indien nodig.
	void EnsureGhost();

	// Pool van rode no-go-vlakken (raster-cellen) die tijdens plaatsen op de vloer verschijnen waar je NIET mag plaatsen.
	void EnsureDoorMarks();
	// Boolean: valt de FOOTPRINT van je plaatsing in een deur-no-go-zone? (Voedt de ghost-validatie op regel 777.)
	bool UpdateDoorwayMarkers(bool bShow, const FVector& FootCenter, const FVector& FootHalf, float Yaw);
	bool DoorBlocksCell(const FVector& Cell, const FVector& BoxHalf, float Yaw) const; // valt deze footprint in een deur-zone (60cm rond een gecachete deur)?
	// Deur-posities in de buurt verzamelen via een overlap-sphere op mesh-naam "Door" (vangt apartment-deuren
	// of ze nu wel/niet tot ACityDoor zijn omgezet). Gecachet; ververst pas als je significant beweegt.
	void RefreshDoorCache(const FVector& Center);
	TArray<FVector> CachedDoorPositions;
	FVector LastDoorCachePos = FVector(1e9f);

	// No-go-raster: sampelt de ECHTE plaats-validatie (zelfde check als de ghost) over een raster rond je en
	// kleurt de ongeldige vloer-cellen rood. Zo zie je exact waar je niet mag plaatsen (deuren incl.).
	bool IsPlacementValidAt(const FVector& Loc, float Yaw, float& FloorZOut, bool& bHasFloorOut) const;
	void UpdateNoGoGrid(bool bShow, const FVector& Center, float Yaw);
	FVector LastGridPos = FVector(1e9f);
	float LastGridYaw = 0.f;
	float LastGridTime = -1.f;

	// Speler-gedefinieerde build-box (2 hoek-markers via Ctrl+F9 -> Saved/BuildArea.txt). Als gezet voor deze map:
	// ALLEEN binnen deze box mag je bouwen (jouw markers zijn leidend i.p.v. de onbetrouwbare home-heuristiek).
	void RefreshBuildArea();
	FBox BuildAreaBox = FBox(ForceInit);
	bool bHaveBuildArea = false;
	float BuildAreaTimer = 0.f;

	// NO-BUILD-zones: PAREN markers uit Saved/MarkedSpots.txt (F9) vormen elk een box waarbinnen plaatsen NIET mag
	// (bv. een deuropening). Speler-markers zijn leidend; periodiek herlezen. Returnt true als P in een no-build-box valt.
	void RefreshNoBuildZones();
	bool IsInNoBuildZone(const FVector& P) const;
	TArray<FBox> NoBuildZones;

	// COMPETITIVE co-op: gecachete retrofitter -> de eigen gespiegelde kamer (Apt 603/602) is bouwbaar +
	// levert de verschoven no-build-zones. Cache voorkomt een actor-scan per IsInOwnedHome-call. Leeg/geen
	// effect buiten competitive (en alleen op de pack-map geraadpleegd).
	mutable TWeakObjectPtr<class ADoorRetrofitter> CompRetroCache;
	class ADoorRetrofitter* GetCompRetro() const;

	// Preview = een echt (transient, cosmetisch) exemplaar van het te plaatsen model, in ghost-kleur,
	// zodat de preview er exact zo uitziet als wat je plaatst. Lokaal.
	void HidePlacementVisuals();
	void SpawnPreview(const struct FPlaceableDef& Def, FName ItemId);
	void DestroyPreview();

	// Tekent/positioneert de ghost van een NIET-lokale speler op basis van de gerepliceerde staat.
	void UpdateRemoteGhost();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Camerastandpunt van de bestuurde pawn (voor de plaats-trace).
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	// True als een placeable met deze footprint op deze positie/hoek een muur/object zou raken.
	// Voor potten geldt extra tussenruimte t.o.v. andere potten (plant-groei).
	bool IsSpotBlocked(const FVector& FloorPoint, const FVector& BoxHalf, float Yaw, bool bPotSpacing) const;

	UInventoryComponent* GetOwnerInventory() const;

	// Doorzichtige preview-mesh (lokaal; alleen voor de bestuurde speler).
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ghost;

	// Dynamisch materiaal van de ghost; kleur = blauw (ok) / rood (kan niet plaatsen).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMID;

	// Platte ring die het effect-bereik van een gear-upgrade toont tijdens plaatsen (alleen voor gear).
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RangeRing;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RangeRingMID;
	bool bPlacingGear = false; // huidige plaatsing is een gear-upgrade -> ring tonen
	// Upgrade die op een object MOET snappen: 0 geen, 1 pot (Gear_), 2 droogrek (DryUp_), 3 hasj-machine (ProcUp_).
	int32 CurUpgradeKind = 0;
	TWeakObjectPtr<AActor> SnapTarget; // het object waar de upgrade op snapt (voor de ring)
	// Dichtstbijzijnde geldige doel-actor (pot/rek/machine) bij Near, of nullptr.
	AActor* FindUpgradeTarget(int32 Kind, const FVector& Near) const;

	// Heldere ring op de pot die DEZE gear gaat krijgen (de dichtstbijzijnde pot in bereik).
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> TargetRing;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TargetRingMID;

	// Rode no-go-vlakken bij deur-openingen (tijdens plaatsen). Pool, hergebruikt per frame.
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> DoorMarks;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DoorMarkMIDs;

	// Echt model als preview (transient) + gedeeld ghost-materiaal voor al z'n onderdelen.
	UPROPERTY(Transient) TWeakObjectPtr<AActor> PreviewActor;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PreviewMID;
	FName RemotePreviewItem = NAME_None; // welk item de remote-ghost (co-op) nu toont (om respawn te beperken)

	// Of de huidige aim ergens op raakt (anders ghost verbergen).
	bool bAimHit = false;

	bool bPlacing = false;
	bool bValidSpot = false;
	FString PlacementHint; // reden waarom plaatsen nu niet mag (gevuld in de validatie-tak, getoond in de HUD-popup)
	FName PlacingItemId = NAME_None;
	FPlaceableDef CurrentDef;            // definitie van het item dat je nu plaatst
	FVector PreviewLocation = FVector::ZeroVector;
	FRotator PreviewRotation = FRotator::ZeroRotator;

	// Na ConfirmPlacement blijft de client soms nog een paar frames dezelfde hotbar-stack zien tot de
	// server-inventory-replicatie binnen is. Dan mag auto-preview niet direct opnieuw starten.
	FName SuppressAutoPreviewItem = NAME_None;
	float SuppressAutoPreviewTimer = 0.f;

	// Throttle voor het versturen van de preview naar de server.
	float PreviewSendAccum = 0.f;

	// Server-side demping na een betrouwbare clear: late unreliable "aan"-updates mogen de ghost niet heropenen.
	float PreviewClearIgnoreUntil = 0.f;

	// Handmatige draai-offset (graden) bovenop de kijkrichting; toets R stapt met 90°.
	float PlaceYawOffset = 0.f;

	// Opgebouwde tijd dat de oppak-toets ingedrukt is terwijl je een pot aankijkt.
	float PickupHoldAccum = 0.f;
	TWeakObjectPtr<AActor> PickupHoldTarget; // G-hold hoort aan een vast doel te hangen; focus-wissel reset.

	// Opgebouwde tijd dat de weggooi-toets (X) ingedrukt is terwijl je een geplante pot aankijkt.
	float DiscardHoldAccum = 0.f;
	// De pot waarop de X-hold nu loopt; wisselt de focus, dan reset de voortgang.
	TWeakObjectPtr<AGrowPlant> DiscardHoldPlant;

	// --- Gerepliceerde preview-staat (voor de ghost bij andere co-op spelers) ---
	UPROPERTY(Replicated)
	bool bRepPlacing = false;

	UPROPERTY(Replicated)
	bool bRepValid = false;

	UPROPERTY(Replicated)
	FVector RepLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float RepYaw = 0.f;

	UPROPERTY(Replicated)
	FName RepItemId = NAME_None;
};

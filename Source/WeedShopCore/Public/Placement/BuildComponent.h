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
#include "BuildComponent.generated.h"

class UStaticMeshComponent;
class UInventoryComponent;
class UMaterialInstanceDynamic;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float GridSize = 50.f;

	// Start/stop de plaats-modus voor een pot (toggle). Doet niets als je geen pot hebt.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void TogglePotPlacement();

	// Stop de plaats-modus (zonder te plaatsen).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void CancelPlacing();

	// Plaats op de huidige (geldige) preview-positie.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void ConfirmPlacement();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	bool IsPlacing() const { return bPlacing; }

	// Of de huidige preview-positie geldig is (kijkt naar een vlak/de vloer).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	bool IsPlacementValid() const { return bValidSpot; }

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
	void ServerUpdatePreview(bool bInPlacing, FVector Location, float Yaw, bool bValid);

	// Maakt de ghost-mesh + dynamisch materiaal aan indien nodig.
	void EnsureGhost();

	// Tekent/positioneert de ghost van een NIET-lokale speler op basis van de gerepliceerde staat.
	void UpdateRemoteGhost();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Camerastandpunt van de bestuurde pawn (voor de plaats-trace).
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	// True als een pot op deze vloer-positie een muur/andere pot zou raken (footprint-overlap).
	bool IsSpotBlocked(const FVector& FloorPoint) const;

	UInventoryComponent* GetOwnerInventory() const;

	// Doorzichtige preview-mesh (lokaal; alleen voor de bestuurde speler).
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ghost;

	// Dynamisch materiaal van de ghost; kleur = blauw (ok) / rood (kan niet plaatsen).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMID;

	// Of de huidige aim ergens op raakt (anders ghost verbergen).
	bool bAimHit = false;

	bool bPlacing = false;
	bool bValidSpot = false;
	FName PlacingItemId = NAME_None;
	FVector PreviewLocation = FVector::ZeroVector;
	FRotator PreviewRotation = FRotator::ZeroRotator;

	// Throttle voor het versturen van de preview naar de server.
	float PreviewSendAccum = 0.f;

	// --- Gerepliceerde preview-staat (voor de ghost bij andere co-op spelers) ---
	UPROPERTY(Replicated)
	bool bRepPlacing = false;

	UPROPERTY(Replicated)
	bool bRepValid = false;

	UPROPERTY(Replicated)
	FVector RepLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float RepYaw = 0.f;
};

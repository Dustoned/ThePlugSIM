// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Interaction/PlayerNpcActions.h"
#include "ThePlugSIMCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class ACustomerBase;
struct FInputActionValue;
struct FKey;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AThePlugSIMCharacter : public ACharacter, public IPlayerNpcActions
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** Genereert runtime-navmesh rond de speler (procedurele stad). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UNavigationInvokerComponent> NavInvoker;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Voorraad: gevuld door oogst, gebruikt bij verkoop (server-authoritative, replicated). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WeedShop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInventoryComponent> Inventory;

	/** Persoonlijke portemonnee (cash + bank) van deze speler — co-op: ieder z'n eigen geld. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WeedShop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UEconomyComponent> Economy;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* MouseLookAction;
	
public:
	AThePlugSIMCharacter();

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	// Bindt alle legacy BindKey-gameplaytoetsen (configureerbare lezen uit UControlSettings).
	void BindGameplayKeys(UInputComponent* Input);

	// Herbindt de gameplaytoetsen na een wijziging in UControlSettings (rebind in de telefoon).
	UFUNCTION()
	void RefreshKeyBindings();

	// Houdt het fysieke "Cash"-briefgeld in de inventory gelijk aan het cash-saldo (server).
	UFUNCTION()
	void OnCashChanged(int64 NewCashCents);

	/** Geeft de speler een startvoorraad (vloei, wat wiet, een zaadje). */
	virtual void BeginPlay() override;

	/** Telefoon-logica (openen, tabs, kopen, afspraken) — aangestuurd door input + HUD-klikken. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WeedShop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UPhoneClientComponent> Phone;

	/** Plaats-modus voor placeables (kweekpot): toets B = plaatsen, links-klik = bevestigen. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WeedShop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UBuildComponent> Build;

	/** Waterfles: hoeveel water je bij je hebt (vullen bij de gootsteen, gebruiken om te water geven). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WeedShop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UWaterCanComponent> WaterCan;

	/** Straat-werving: geef de aangekeken NPC een gratis sample (toets F). */
	void GiveSample();

	/** Server-RPC: voert het sample-geven authoritative uit op het aangekeken doel. */
	UFUNCTION(Server, Reliable)
	void ServerGiveSample(AActor* Target);

public:
	/** IPlayerNpcActions: geef een specifieke klant een joint (vanuit het praat-venster). */
	virtual void GiveJointToCustomer(ACustomerBase* Customer) override;

	/** Open/sluit het roll-paneel (toets R) — daar kies je het aantal gram. */
	void ToggleRollUI();

	/** Cijfertoets 1-8: kiest hotbar-slot als geen telefoon open is, anders telefoon-catalogus. */
	void HotbarOrPhoneKey(FKey Key);

	/** Scrollwiel: vorige/volgende hotbar-slot. */
	void HotbarPrev();
	void HotbarNext();

	/** Links-klik: bevestig plaatsen tijdens plaats-modus, anders interact met de pot / gebruik item. */
	void OnPrimaryClick();

	/** Interact-toets (E): zelfde als klikken op wat je aankijkt (pot/klant/ATM) + plaatsen bevestigen. */
	void OnInteractKey();

	/** ESC: open/sluit het pauze-/menu-scherm. */
	void OnPauseKey();

	/** Links-knop losgelaten: stopt de "joint overhandigen"-hold. */
	void OnPrimaryReleased();

	/** Laad-toets (F): laad de vloei in je hand met wiet (klaar om te rollen). */
	void OnLoadKey();

	/** Rechtermuisknop ingedrukt: papers -> roll-paneel; joint -> begin met "inhouden om te roken". */
	void OnSecondaryPressed();

	/** Rechtermuisknop losgelaten: stopt het rook-inhouden (annuleert als je te vroeg loslaat). */
	void OnSecondaryReleased();

	/** Gebruik het geselecteerde hotbar-item (bv. Pot -> plaats-modus, of een joint oproken). */
	void UseActiveItem();

	/** Open het pot-upgrade-paneel voor de aangekeken pot (toets U). */
	void OpenPotUpgradeUI();

	/** Rook de joint die je in de hand hebt: word stoned (buf) + XP-bonus op basis van hoe high. */
	void SmokeActiveJoint();

	UFUNCTION(Server, Reliable)
	void ServerSmokeJoint(FName JointId);

	/** Zet de stoned-buf op alle versies van deze pawn (zodat de owner 'm ziet). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyStoned(float Seconds, float Intensity, int32 XpBonus, float XpFrac);

	virtual void Tick(float DeltaSeconds) override;

	// Max duur van een volle high (sec). Een max-joint (backwoods vol topwiet) haalt dit ongeveer.
	static constexpr float StonedMaxSeconds = 150.f;

	float StonedSeconds = 0.f;   // resterende high-tijd
	float StonedIntensity = 0.f; // hoe high (0..1, kwaliteit x gram)
	float StonedXpFrac = 0.f;    // XP-bonus fractie terwijl high (op THC% gebaseerd)
	int32 LastSmokeXp = 0;       // XP-bonus van de laatste joint (voor de HUD)

	// Roken = rechtermuisknop inhouden (bewust, niet per ongeluk).
	bool bRmbDown = false;
	bool bSmokeFired = false;
	float SmokeHoldTime = 0.f;
	static constexpr float SmokeHoldRequired = 1.1f; // sec inhouden voor het oproken

	float PhoneCloseHold = 0.f; // LMB inhouden terwijl de telefoon open is -> sluiten

	// Anti-stuck: onthoud de laatste vaste grond en herstel als je te lang valt / onder de wereld zakt
	// (vangt het "ik vlieg opeens en kan niks"-geval op, ongeacht de oorzaak).
	FVector LastGroundLoc = FVector::ZeroVector;
	FVector InitialSpawnLoc = FVector::ZeroVector; // veilige terugval als er nergens grond gevonden wordt
	bool bHasGroundLoc = false;
	float FallTime = 0.f;
	float FloatTime = 0.f; // tijd dat je in de lucht hangt met bijna geen valsnelheid (= vast)
	void TickStuckRecovery(float DeltaSeconds);
	bool FindFloorAt(const FVector& Near, FVector& OutSafe) const; // omlaag-trace naar de echte vloer

	// Third-person lichaam (wat ANDERE spelers van je zien): speel walk/idle zelf af op snelheid
	// (single-node), want de meegeleverde locomotie-ABP geeft standalone geen loopcyclus -> spelers
	// "gleden" voor elkaar. Draait op alle machines (proxies), zodat iedereen elkaar ziet lopen.
	UPROPERTY() TObjectPtr<class UAnimSequence> TpIdleAnim;
	UPROPERTY() TObjectPtr<class UAnimSequence> TpWalkAnim;
	bool bTpMoving = false;
	bool bTpStarted = false;
	void UpdateBodyAnim();

	// Joint overhandigen: korte LMB-hold terwijl je een joint vasthoudt en een klant aankijkt.
	bool bLmbDown = false;
	bool bGiveFired = false;
	float GiveHoldTime = 0.f;
	static constexpr float GiveHoldRequired = 0.45f; // korte hold

	// Rollen: F laadt de vloei met wiet, daarna rechtermuis inhouden om te rollen. De lading blijft
	// staan tot je 'm rolt (dan kun je nieuwe wiet laden voor de volgende).
	bool bRollLoaded = false;
	int32 RollLoadGrams = 0;
	float RollLoadThc = 0.f;
	float RollLoadQuality = 0.f;
	bool bRollFired = false;
	float RollHoldTime = 0.f;
	static constexpr float RollHoldRequired = 0.6f; // ~zo lang als de pickup-hold

public:

	/** Hoe high je nu bent (0..1) voor de HUD. */
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	float GetStonedIntensity() const { return StonedSeconds > 0.f ? StonedIntensity : 0.f; }

	/** Resterende high als fractie van het maximum (0..1) voor een balkje. */
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	float GetStonedFraction() const { return FMath::Clamp(StonedSeconds / StonedMaxSeconds, 0.f, 1.f); }

	/** XP-bonus fractie terwijl je high bent (0..0.5), gebaseerd op de THC% van de wiet. */
	UFUNCTION(BlueprintPure, Category = "WeedShop")
	float GetStonedXpFrac() const { return StonedSeconds > 0.f ? StonedXpFrac : 0.f; }

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Returns the inventory component **/
	class UInventoryComponent* GetInventory() const { return Inventory; }

	/** Returns this player's personal wallet. */
	class UEconomyComponent* GetEconomy() const { return Economy; }

};


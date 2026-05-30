// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ThePlugSIMCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;
struct FKey;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AThePlugSIMCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Voorraad: gevuld door oogst, gebruikt bij verkoop (server-authoritative, replicated). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WeedShop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInventoryComponent> Inventory;

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

	/** Telefoon openen/sluiten (Tab) — schakelt ook de muis-cursor/input-mode. */
	void TogglePhone();

	/** Handler voor de nummertoetsen 1-6: mapt de toets naar een catalogus-index. */
	void BuyPhoneKey(FKey Key);

	/** Koopt het N-de catalogus-item van de telefoon. */
	void BuyPhoneIndex(int32 Index);

	/** Wisselt de telefoon-tab (Upgrades <-> Suppliers). */
	void CyclePhoneTab();

	/** Server-RPC: koopt een upgrade via de gedeelde GameState. */
	UFUNCTION(Server, Reliable)
	void ServerBuyUpgrade(FName UpgradeId);

	/** Server-RPC: koopt een zaadje bij de supplier. */
	UFUNCTION(Server, Reliable)
	void ServerBuySeed(FName StrainId);

	/** Of de telefoon nu open is (lokaal). */
	bool bPhoneOpen = false;

	/** Actieve telefoon-tab: 0 = Upgrades, 1 = Suppliers. */
	int32 PhoneTab = 0;


public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Returns the inventory component **/
	class UInventoryComponent* GetInventory() const { return Inventory; }

};


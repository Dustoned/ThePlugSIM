// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThePlugSIMCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "UI/WeedShopHUD.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"
#include "ThePlugSIM.h"

AThePlugSIMCharacter::AThePlugSIMCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;

	// Voorraad-component (oogst in, verkoop uit).
	Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
}

void AThePlugSIMCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AThePlugSIMCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AThePlugSIMCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AThePlugSIMCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AThePlugSIMCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AThePlugSIMCharacter::LookInput);
	}

	// Telefoon: directe key-bindings (geen aparte Input Action-assets nodig).
	PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AThePlugSIMCharacter::TogglePhone);
	PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AThePlugSIMCharacter::BuyPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AThePlugSIMCharacter::BuyPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AThePlugSIMCharacter::BuyPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AThePlugSIMCharacter::BuyPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Five,  IE_Pressed, this, &AThePlugSIMCharacter::BuyPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Six,   IE_Pressed, this, &AThePlugSIMCharacter::BuyPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Q,     IE_Pressed, this, &AThePlugSIMCharacter::CyclePhoneTab);

	if (!Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		UE_LOG(LogThePlugSIM, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void AThePlugSIMCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AThePlugSIMCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AThePlugSIMCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AThePlugSIMCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AThePlugSIMCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AThePlugSIMCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

void AThePlugSIMCharacter::TogglePhone()
{
	bPhoneOpen = !bPhoneOpen;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	if (AWeedShopHUD* HUD = Cast<AWeedShopHUD>(PC->GetHUD()))
	{
		HUD->SetPhoneOpen(bPhoneOpen);
		HUD->SetPhoneTab(PhoneTab);
	}

	PC->SetShowMouseCursor(bPhoneOpen);
	if (bPhoneOpen)
	{
		PC->SetInputMode(FInputModeGameAndUI());
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void AThePlugSIMCharacter::BuyPhoneKey(FKey Key)
{
	int32 Index = -1;
	if (Key == EKeys::One)        Index = 0;
	else if (Key == EKeys::Two)   Index = 1;
	else if (Key == EKeys::Three) Index = 2;
	else if (Key == EKeys::Four)  Index = 3;
	else if (Key == EKeys::Five)  Index = 4;
	else if (Key == EKeys::Six)   Index = 5;
	if (Index >= 0)
	{
		BuyPhoneIndex(Index);
	}
}

void AThePlugSIMCharacter::CyclePhoneTab()
{
	if (!bPhoneOpen)
	{
		return;
	}
	PhoneTab = (PhoneTab + 1) % 4;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AWeedShopHUD* HUD = Cast<AWeedShopHUD>(PC->GetHUD()))
		{
			HUD->SetPhoneTab(PhoneTab);
		}
	}
}

void AThePlugSIMCharacter::BuyPhoneIndex(int32 Index)
{
	if (!bPhoneOpen)
	{
		return;
	}
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	if (PhoneTab == 1)
	{
		// Suppliers: koop een zaadje.
		if (GS->GetStore())
		{
			const TArray<FName> Seeds = GS->GetStore()->GetSeedCatalog();
			if (Seeds.IsValidIndex(Index))
			{
				ServerBuySeed(Seeds[Index]);
			}
		}
		return;
	}

	if (PhoneTab == 3)
	{
		// Berichten: 1 = accepteren, 2 = weigeren (eerste open afspraak).
		if (Index == 0)      { ServerRespondAppointment(true); }
		else if (Index == 1) { ServerRespondAppointment(false); }
		return;
	}

	if (PhoneTab == 2)
	{
		// Contacten: alleen weergave.
		return;
	}

	// Upgrades.
	if (GS->GetUpgrades())
	{
		const TArray<FName> Ids = GS->GetUpgrades()->GetAllUpgradeIds();
		if (Ids.IsValidIndex(Index))
		{
			ServerBuyUpgrade(Ids[Index]);
		}
	}
}

void AThePlugSIMCharacter::ServerBuyUpgrade_Implementation(FName UpgradeId)
{
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GS->GetUpgrades())
		{
			GS->GetUpgrades()->BuyUpgrade(UpgradeId);
		}
	}
}

void AThePlugSIMCharacter::ServerBuySeed_Implementation(FName StrainId)
{
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GS->GetStore())
		{
			GS->GetStore()->BuySeed(StrainId, Inventory);
		}
	}
}

void AThePlugSIMCharacter::ServerRespondAppointment_Implementation(bool bAccept)
{
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GS->GetContacts())
		{
			GS->GetContacts()->RespondTopPending(bAccept);
		}
	}
}

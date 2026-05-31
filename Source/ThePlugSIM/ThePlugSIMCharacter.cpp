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
#include "Phone/PhoneClientComponent.h"
#include "Placement/BuildComponent.h"
#include "Cultivation/WaterCanComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Customer/CustomerBase.h"
#include "Npc/NpcRegistryComponent.h"
#include "World/HeatComponent.h"
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

	// Telefoon-logica (openen, tabs, kopen, afspraken).
	Phone = CreateDefaultSubobject<UPhoneClientComponent>(TEXT("Phone"));

	// Plaats-modus voor placeables (kweekpot).
	Build = CreateDefaultSubobject<UBuildComponent>(TEXT("Build"));

	// Waterfles-staat (vullen bij de gootsteen).
	WaterCan = CreateDefaultSubobject<UWaterCanComponent>(TEXT("WaterCan"));
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

	// Telefoon: Tab = open/sluit, Q = wissel tab. Cijfers 1-8 kiezen een hotbar-slot (in de hand);
	// staat de telefoon open dan werken ze als catalogus-keuze (afgehandeld in HotbarOrPhoneKey).
	if (UPhoneClientComponent* Ph = Phone.Get())
	{
		PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, Ph, &UPhoneClientComponent::Toggle);
		PlayerInputComponent->BindKey(EKeys::Q,   IE_Pressed, Ph, &UPhoneClientComponent::CycleTab);
		PlayerInputComponent->BindKey(EKeys::I,   IE_Pressed, Ph, &UPhoneClientComponent::ToggleInventory);
	}
	PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Five,  IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Six,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	PlayerInputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);

	// Scrollwiel: vorige/volgende hotbar-slot.
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarPrev);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AThePlugSIMCharacter::HotbarNext);

	// Straat-werving: F geeft de aangekeken NPC een gratis sample; V opent het joint-roll-paneel.
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AThePlugSIMCharacter::GiveSample);
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AThePlugSIMCharacter::ToggleRollUI);

	// R draait het te plaatsen meubel 90° tijdens de plaats-modus (anders niets).
	if (UBuildComponent* B = Build.Get())
	{
		PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, B, &UBuildComponent::RotatePlacement);
	}

	// Plaats-modus is automatisch: een plaatsbaar item in de hand toont meteen de preview
	// (zie UBuildComponent). Links-klik plaatst; schakel naar een ander hotbar-slot om te stoppen.
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AThePlugSIMCharacter::OnPrimaryClick);

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
	// Geen camera-kijken terwijl er een UI open is (telefoon/roll/deal), anders draait de
	// camera mee terwijl je de muis/slider gebruikt.
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()))
	{
		return;
	}

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

// Telefoon-logica zit nu in UPhoneClientComponent (zie Phone-component op deze pawn).

void AThePlugSIMCharacter::GiveSample()
{
	// Bepaal lokaal het aangekeken doel en stuur het naar de server.
	if (const UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AActor* Focus = IC->GetFocusedActor())
		{
			ServerGiveSample(Focus);
		}
	}
}

void AThePlugSIMCharacter::ServerGiveSample_Implementation(AActor* Target)
{
	ACustomerBase* Customer = Cast<ACustomerBase>(Target);
	if (!Customer)
	{
		return;
	}

	// Reikwijdte-check.
	if (FVector::DistSquared(GetActorLocation(), Customer->GetActorLocation()) > FMath::Square(400.f))
	{
		return;
	}

	// Een sample is een gedraaide joint. Pak de beste joint (hoogste gram) die je hebt.
	if (!Inventory)
	{
		return;
	}
	FName BestJoint = NAME_None;
	int32 BestGrams = 0;
	for (const FInventoryStack& Stack : Inventory->GetStacks())
	{
		const FString Id = Stack.ItemId.ToString();
		if (Id.StartsWith(TEXT("Joint_")) && Id.EndsWith(TEXT("g")))
		{
			const int32 G = FCString::Atoi(*Id.Mid(6)); // "Joint_3g" -> 3
			if (G > BestGrams)
			{
				BestGrams = G;
				BestJoint = Stack.ItemId;
			}
		}
	}
	if (BestJoint.IsNone() || !Inventory->RemoveItem(BestJoint, 1))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No joint to give — roll one first (R)."));
		}
		return;
	}

	// Kwaliteit 0..1 op basis van gram. Effect schaalt met kwaliteit.
	const float Quality = FMath::Clamp(BestGrams / 5.f, 0.f, 1.f);
	float LoyGain = 4.f + Quality * 12.f;   // 1g ~5.6, 5g ~16
	float AddGain = 3.f + Quality * 9.f;
	float RespGain = 1.f + Quality * 4.f;

	// Kieskeurigheid: weinig-verslaafde mensen (locals/kenners) lusten geen slappe joint.
	const bool bPicky = Customer->Addiction < 20.f;
	if (bPicky && Quality < 0.5f)
	{
		LoyGain = -3.f; RespGain = -2.f; AddGain = 1.f;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange,
				FString::Printf(TEXT("%s found the joint too weak."), *Customer->NpcId.ToString()));
		}
	}

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetNpcRegistry() && !Customer->NpcId.IsNone())
	{
		float R = 0.f, L = 0.f, A = 0.f; FText N;
		if (GS->GetNpcRegistry()->GetStats(Customer->NpcId, R, L, A, N))
		{
			GS->GetNpcRegistry()->ApplyStats(Customer->NpcId, R + RespGain, L + LoyGain, A + AddGain);
		}
	}
	Customer->Respect = FMath::Clamp(Customer->Respect + RespGain, 0.f, 100.f);
	Customer->Loyalty = FMath::Clamp(Customer->Loyalty + LoyGain, 0.f, 100.f);
	Customer->Addiction = FMath::Clamp(Customer->Addiction + AddGain, 0.f, 100.f);

	// Straat-dealen is riskant -> heat omhoog.
	if (GS && GS->GetHeat())
	{
		GS->GetHeat()->AddHeat(5.f);
	}

	if (GEngine && !(bPicky && Quality < 0.5f))
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Sample given (%dg joint, relationship +)."), BestGrams));
	}
}

void AThePlugSIMCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Startvoorraad (concept): wat vloei, een paar gram wiet en een zaadje.
	if (HasAuthority() && Inventory)
	{
		Inventory->AddItem(FName(TEXT("Papers_Small")), 10);
		Inventory->AddItem(FName(TEXT("Bud_SilverHaze")), 5);
		Inventory->AddItem(FName(TEXT("Seed_SilverHaze")), 1);
		Inventory->AddItem(FName(TEXT("Soil_Basic")), 2);
		Inventory->AddItem(FName(TEXT("WaterBottle_Plastic")), 1);
		Inventory->AddItem(FName(TEXT("Pot_Broken")), 1);
	}
}

void AThePlugSIMCharacter::ToggleRollUI()
{
	if (Phone)
	{
		Phone->ToggleRollUI();
	}
}

void AThePlugSIMCharacter::HotbarOrPhoneKey(FKey Key)
{
	// Telefoon open -> catalogus-keuze; anders hotbar-slot selecteren.
	if (Phone && Phone->IsOpen())
	{
		Phone->HandleNumberKey(Key);
		return;
	}
	int32 Index = -1;
	if (Key == EKeys::One)        Index = 0;
	else if (Key == EKeys::Two)   Index = 1;
	else if (Key == EKeys::Three) Index = 2;
	else if (Key == EKeys::Four)  Index = 3;
	else if (Key == EKeys::Five)  Index = 4;
	else if (Key == EKeys::Six)   Index = 5;
	else if (Key == EKeys::Seven) Index = 6;
	else if (Key == EKeys::Eight) Index = 7;
	if (Index >= 0 && Inventory)
	{
		Inventory->SetActiveSlot(Index);
	}
}

void AThePlugSIMCharacter::HotbarPrev()
{
	if (Inventory && Phone && !Phone->IsOpen())
	{
		Inventory->CycleActiveSlot(-1);
	}
}

void AThePlugSIMCharacter::HotbarNext()
{
	if (Inventory && Phone && !Phone->IsOpen())
	{
		Inventory->CycleActiveSlot(+1);
	}
}

void AThePlugSIMCharacter::OnPrimaryClick()
{
	// Klik gaat naar de UI als die open is (HUD hit-boxes regelen dat zelf).
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()))
	{
		return;
	}
	// In plaats-modus: bevestig de plaatsing. Anders: gebruik het item in de hand.
	if (Build && Build->IsPlacing())
	{
		Build->ConfirmPlacement();
		return;
	}
	UseActiveItem();
}

void AThePlugSIMCharacter::UseActiveItem()
{
	if (!Inventory || !Build)
	{
		return;
	}
	// UI open? Niet gebruiken (klik is voor de UI).
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()))
	{
		return;
	}
	const FName Item = Inventory->GetActiveItemId();
	if (Item == FName(TEXT("Pot")))
	{
		Build->TogglePotPlacement();
	}
}

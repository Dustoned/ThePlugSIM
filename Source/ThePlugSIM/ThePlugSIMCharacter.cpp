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
#include "Economy/EconomyComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Placement/BuildComponent.h"
#include "Placement/PlaceableProp.h"
#include "Cultivation/WaterCanComponent.h"
#include "EngineUtils.h"
#include "Cultivation/GrowPlant.h"
#include "Interaction/InteractionComponent.h"
#include "Customer/CustomerBase.h"
#include "Customer/CustomerSpawner.h"
#include "Npc/NpcRegistryComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Input/ControlSettings.h"
#include "World/Atm.h"
#include "World/PackBench.h"
#include "SmokePuff.h"
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

	// Persoonlijke portemonnee (cash + bank) — co-op: ieder z'n eigen geld.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));

	// Telefoon-logica (openen, tabs, kopen, afspraken).
	Phone = CreateDefaultSubobject<UPhoneClientComponent>(TEXT("Phone"));

	// Plaats-modus voor placeables (kweekpot).
	Build = CreateDefaultSubobject<UBuildComponent>(TEXT("Build"));

	// Waterfles-staat (vullen bij de gootsteen).
	WaterCan = CreateDefaultSubobject<UWaterCanComponent>(TEXT("WaterCan"));

	// Tick aan voor de stoned-buf-timer.
	PrimaryActorTick.bCanEverTick = true;
}

void AThePlugSIMCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (StonedSeconds > 0.f)
	{
		StonedSeconds = FMath::Max(0.f, StonedSeconds - DeltaSeconds);
		if (StonedSeconds <= 0.f) { StonedIntensity = 0.f; StonedXpFrac = 0.f; }
	}

	// Roken = rechtermuisknop inhouden met een joint in de hand. Duidelijke voortgangsbalk via de HUD,
	// zodat je niet per ongeluk je eigen joint oprookt.
	{
		const bool bUiOpen = Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen()
			|| Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen());
		const bool bJointInHand = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Joint_"));
		if (bRmbDown && !bUiOpen && bJointInHand)
		{
			SmokeHoldTime += DeltaSeconds;
			if (!bSmokeFired && SmokeHoldTime >= SmokeHoldRequired)
			{
				bSmokeFired = true;
				SmokeActiveJoint();
			}
		}
		else
		{
			SmokeHoldTime = 0.f;
		}
		if (Phone)
		{
			Phone->SetSmokeHoldFrac(bSmokeFired ? 0.f : FMath::Clamp(SmokeHoldTime / SmokeHoldRequired, 0.f, 1.f));
			Phone->SetStonedHud(GetStonedFraction(), StonedSeconds, GetStonedIntensity(), GetStonedXpFrac());
		}

		// Rollen: geladen vloei in de hand + rechtermuis inhouden -> rol de joint. (Laden gebeurt via
		// de "Load"-knop in het rol-menu; de lading blijft staan tot je 'm rolt.)
		const bool bPapersInHand = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Papers_"));
		if (bRmbDown && !bUiOpen && bPapersInHand && Phone && Phone->IsRollLoadedUI())
		{
			RollHoldTime += DeltaSeconds;
			if (!bRollFired && RollHoldTime >= RollHoldRequired)
			{
				bRollFired = true;
				Phone->SetRollGrams(Phone->GetRollLoadGramsUI());
				Phone->ConfirmRoll();
				Phone->SetRollLoadedUI(false, 0); // gerold -> open het menu weer (F) om opnieuw te laden
			}
		}
		else
		{
			RollHoldTime = 0.f;
		}
		if (Phone) { Phone->SetRollHoldFrac(bRollFired ? 0.f : FMath::Clamp(RollHoldTime / RollHoldRequired, 0.f, 1.f)); }
		// Stoned = XP-bonus op basis van de THC% van je wiet (niet hoe high je bent), zodat je niet te
		// snel levelt. Een 17%-joint -> +17% XP, max +50%. Server zet de multiplier.
		if (HasAuthority())
		{
			if (AWeedShopGameState* GSx = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
			{
				if (ULevelComponent* Lvx = GSx->GetLeveling())
				{
					Lvx->SetXpMultiplier(1.f + GetStonedXpFrac());
				}
			}
		}
	}

	// Joint overhandigen: korte LMB-hold terwijl je een joint vasthoudt en een klant aankijkt.
	{
		const bool bUiOpenG = Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen()
			|| Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen());
		const bool bJointG = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Joint_"));
		ACustomerBase* FocusCust = nullptr;
		if (const UInteractionComponent* ICx = FindComponentByClass<UInteractionComponent>())
		{
			FocusCust = Cast<ACustomerBase>(ICx->GetFocusedActor());
		}
		if (bLmbDown && !bUiOpenG && bJointG && FocusCust)
		{
			GiveHoldTime += DeltaSeconds;
			if (!bGiveFired && GiveHoldTime >= GiveHoldRequired)
			{
				bGiveFired = true;
				GiveSample();
			}
		}
		else
		{
			GiveHoldTime = 0.f;
		}
		if (Phone) { Phone->SetGiveHoldFrac(bGiveFired ? 0.f : FMath::Clamp(GiveHoldTime / GiveHoldRequired, 0.f, 1.f)); }
	}

	// Stoned = wat extra motion blur + lichte vaagheid, zodat rondkijken "high" aanvoelt.
	if (FirstPersonCameraComponent)
	{
		FPostProcessSettings& PP = FirstPersonCameraComponent->PostProcessSettings;
		const float I = GetStonedIntensity();
		if (I > 0.f)
		{
			PP.bOverride_MotionBlurAmount = true;   PP.MotionBlurAmount = 0.35f + I * 0.65f;
			PP.bOverride_MotionBlurMax = true;      PP.MotionBlurMax = 8.f + I * 30.f;
			PP.bOverride_VignetteIntensity = true;  PP.VignetteIntensity = 0.35f + I * 0.55f;
			PP.bOverride_SceneFringeIntensity = true; PP.SceneFringeIntensity = I * 3.5f;
		}
		else
		{
			PP.bOverride_MotionBlurAmount = false;
			PP.bOverride_MotionBlurMax = false;
			PP.bOverride_VignetteIntensity = false;
			PP.bOverride_SceneFringeIntensity = false;
		}
	}
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

	// Alle losse gameplaytoetsen (incl. de herbindbare) in één plek, zodat we ze kunnen herbinden.
	BindGameplayKeys(PlayerInputComponent);

	if (!Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		UE_LOG(LogThePlugSIM, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AThePlugSIMCharacter::BindGameplayKeys(UInputComponent* Input)
{
	if (!Input) { return; }
	UControlSettings* CS = UControlSettings::Get();

	// Herbindbare acties: bind zowel de MAIN- als de ALT-toets (als die geldig is).
	UPhoneClientComponent* Ph = Phone.Get();
	UBuildComponent* B = Build.Get();
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const bool bAlt = (Slot == 1);
		if (Ph)
		{
			const FKey KPhone = CS->GetKey(TEXT("Phone"), bAlt);
			if (KPhone.IsValid()) { Input->BindKey(KPhone, IE_Pressed, Ph, &UPhoneClientComponent::Toggle); }
			const FKey KTab = CS->GetKey(TEXT("PhoneTab"), bAlt);
			if (KTab.IsValid()) { Input->BindKey(KTab, IE_Pressed, Ph, &UPhoneClientComponent::CycleTab); }
			const FKey KInv = CS->GetKey(TEXT("Inventory"), bAlt);
			if (KInv.IsValid()) { Input->BindKey(KInv, IE_Pressed, Ph, &UPhoneClientComponent::ToggleInventory); }
		}
		const FKey KInteract = CS->GetKey(TEXT("Interact"), bAlt);
		if (KInteract.IsValid()) { Input->BindKey(KInteract, IE_Pressed, this, &AThePlugSIMCharacter::OnInteractKey); }
		const FKey KLoad = CS->GetKey(TEXT("RollLoad"), bAlt);
		if (KLoad.IsValid()) { Input->BindKey(KLoad, IE_Pressed, this, &AThePlugSIMCharacter::OnLoadKey); }
		if (B)
		{
			const FKey KRot = CS->GetKey(TEXT("Rotate"), bAlt);
			if (KRot.IsValid()) { Input->BindKey(KRot, IE_Pressed, B, &UBuildComponent::RotatePlacement); }
		}
		const FKey KUpg = CS->GetKey(TEXT("PotUpgrade"), bAlt);
		if (KUpg.IsValid()) { Input->BindKey(KUpg, IE_Pressed, this, &AThePlugSIMCharacter::OpenPotUpgradeUI); }
	}

	// Vaste toetsen (niet herbindbaar): cijfers, scroll, muisknoppen.
	Input->BindKey(EKeys::One,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Two,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Three, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Four,  IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Five,  IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Six,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Seven, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Eight, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarPrev);
	Input->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AThePlugSIMCharacter::HotbarNext);
	Input->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AThePlugSIMCharacter::OnSecondaryPressed);
	Input->BindKey(EKeys::RightMouseButton, IE_Released, this, &AThePlugSIMCharacter::OnSecondaryReleased);
	Input->BindKey(EKeys::LeftMouseButton,  IE_Pressed,  this, &AThePlugSIMCharacter::OnPrimaryClick);
	Input->BindKey(EKeys::LeftMouseButton,  IE_Released, this, &AThePlugSIMCharacter::OnPrimaryReleased);
}

void AThePlugSIMCharacter::RefreshKeyBindings()
{
	if (InputComponent)
	{
		InputComponent->KeyBindings.Empty(); // alleen de losse BindKey's; Enhanced-acties blijven staan
		BindGameplayKeys(InputComponent);
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
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen()))
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
	// Wiet-kwaliteit van de joint (0..1) vóór we 'm weghalen — slechte wiet verslaaft/bindt minder.
	const float WeedQ = FMath::Clamp(Inventory->GetItemQualityPct(BestJoint) / 100.f, 0.f, 1.f);

	if (BestJoint.IsNone() || !Inventory->RemoveItem(BestJoint, 1))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, TEXT("No joint to give — roll one first (R)."));
		}
		return;
	}

	// Effectieve kwaliteit = wiet-kwaliteit geschaald met het aantal gram (zelfde formule als de
	// joint-sterkte): een dun jointje voelt zwakker en bindt/verslaaft daardoor minder.
	const float Quality = UPhoneClientComponent::JointIntensity(BestGrams, 0.f, WeedQ * 100.f);
	float LoyGain = 4.f + Quality * 12.f;   // top-joint ~16, brak ~4
	float AddGain = 3.f + Quality * 9.f;    // slechte wiet verslaaft nauwelijks
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

	// Prospect genoeg opgewarmd? Dan wordt het nu een echte klant ("vond het lekker, heb je meer?").
	const bool bConverted = Customer->RefreshProspect();

	// XP voor het werven: klein per sample, bonus als je iemand omzet naar koper.
	if (GS && GS->GetLeveling())
	{
		GS->GetLeveling()->AddXP(bConverted ? 25 : 3);
	}

	if (GEngine && !(bPicky && Quality < 0.5f))
	{
		if (bConverted)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
				TEXT("\"Damn, that's good! Got any more to sell?\" - they'll buy now."));
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
				FString::Printf(TEXT("Sample given (%dg joint, relationship +)."), BestGrams));
		}
	}
}

void AThePlugSIMCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Herbind de gameplaytoetsen zodra de speler ze in de telefoon (Settings -> Controls) wijzigt.
	if (IsLocallyControlled())
	{
		UControlSettings::Get()->OnBindingsChanged.AddUObject(this, &AThePlugSIMCharacter::RefreshKeyBindings);
	}

	// Startvoorraad (concept): wat vloei, een paar gram wiet en een zaadje.
	if (HasAuthority() && Inventory)
	{
		Inventory->AddItem(FName(TEXT("Papers_Small")), 10);
		// Startwiet heeft een nette THC% + Kwaliteit% (geen 0%-wiet).
		Inventory->AddItem(FName(TEXT("Bud_SilverHaze")), 5, /*THC%*/ 17.f, /*Quality%*/ 70.f);
		Inventory->AddItem(FName(TEXT("Seed_SilverHaze")), 1);
		Inventory->AddItem(FName(TEXT("Soil_Basic")), 2);
		Inventory->AddItem(FName(TEXT("WaterBottle_Plastic")), 1);
		Inventory->AddItem(FName(TEXT("Pot_Broken")), 1);
		Inventory->AddItem(FName(TEXT("Atm")), 1); // plaatsbare geldautomaat (voor nu); zet 'm waar je wilt

		// Alle meubels die in de starter-home staan gaan in de inventory (niet de hotbar): we vegen
		// de geplaatste props op en geven het bijbehorende item. Zo begin je met een leeg huis en
		// kun je zelf bepalen waar alles komt. (Solo/host: bij level-start bestaan alleen de
		// map-meubels; speler-geplaatste props komen pas later.)
		for (TActorIterator<APlaceableProp> It(GetWorld()); It; ++It)
		{
			APlaceableProp* Prop = *It;
			if (!IsValid(Prop) || Prop->ItemId.IsNone()) { continue; }
			Inventory->AddItem(Prop->ItemId, 1);
			Prop->Destroy();
		}
	}

	// (De ATM is nu een plaatsbaar item in je inventory: zet 'm zelf neer waar je wilt, binnen of buiten.)

	// Cash = fysiek briefgeld in de inventory: houd het gelijk aan MIJN cash-saldo (server).
	if (HasAuthority() && Inventory && Economy)
	{
		Economy->OnBalanceChanged.AddDynamic(this, &AThePlugSIMCharacter::OnCashChanged);
		Inventory->SetCashDisplayEuros((int64)Economy->GetBalanceEuros()); // begin-sync

		// Mijn inkomsten tellen mee voor de gedeelde milestones/fase.
		if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UMilestoneComponent* Ms = GS->GetMilestones())
			{
				Economy->OnMoneyEarned.AddDynamic(Ms, &UMilestoneComponent::HandleMoneyEarned);
			}
		}
	}
}

void AThePlugSIMCharacter::OnCashChanged(int64 NewCashCents)
{
	if (HasAuthority() && Inventory)
	{
		Inventory->SetCashDisplayEuros(NewCashCents / 100);
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
	bLmbDown = true; GiveHoldTime = 0.f; bGiveFired = false;

	// Klik gaat naar de UI als die open is (HUD hit-boxes regelen dat zelf). Ook als een UI-knop
	// deze klik net heeft verwerkt (bv. een paneel sloot) negeren we 'm hier, zodat dezelfde klik
	// niet alsnog de wereld-interactie (en daarmee bv. de deal opnieuw) opent.
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()
		|| Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen() || Phone->DidUiConsumeClickRecently()))
	{
		return;
	}
	// In plaats-modus: bevestig de plaatsing.
	if (Build && Build->IsPlacing())
	{
		Build->ConfirmPlacement();
		return;
	}
	// Joint in de hand + je kijkt een klant aan -> dat is een "joint overhandigen" (korte LMB-hold,
	// afgehandeld in Tick). Niet meteen interacten (anders opent de deal).
	const bool bJointInHand = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Joint_"));
	if (UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AActor* Focus = IC->GetFocusedActor())
		{
			if (bJointInHand && Cast<ACustomerBase>(Focus))
			{
				return; // hold-to-give regelt dit
			}
			// ATM -> open lokaal het ATM-scherm (bankieren) i.p.v. een server-interactie.
			if (Cast<AAtm>(Focus))
			{
				if (Phone) { Phone->OpenAtm(); }
				return;
			}
			// Verpak-tafel -> open lokaal het verpak-menu (met de batch-grootte van deze tafel).
			if (APackBench* Bench = Cast<APackBench>(Focus))
			{
				if (Phone) { Phone->OpenPack(Bench->GetPackPerAction()); }
				return;
			}
			IC->TryInteract();
			return;
		}
	}
	UseActiveItem();
}

void AThePlugSIMCharacter::OnPrimaryReleased()
{
	bLmbDown = false;
	GiveHoldTime = 0.f;
	bGiveFired = false;
	if (Phone) { Phone->SetGiveHoldFrac(0.f); }
}

void AThePlugSIMCharacter::OnInteractKey()
{
	// E doet hetzelfde als links-klikken op wat je aankijkt (pot/klant/ATM) + plaatsen bevestigen,
	// maar gebruikt nooit het actieve hand-item.
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()
		|| Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen()))
	{
		return;
	}
	if (Build && Build->IsPlacing())
	{
		Build->ConfirmPlacement();
		return;
	}
	if (UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AActor* Focus = IC->GetFocusedActor())
		{
			if (Cast<AAtm>(Focus))
			{
				if (Phone) { Phone->OpenAtm(); }
				return;
			}
			IC->TryInteract();
		}
	}
}

void AThePlugSIMCharacter::OnSecondaryPressed()
{
	if (!Phone || !Inventory)
	{
		return;
	}
	bRmbDown = true;
	SmokeHoldTime = 0.f;
	bSmokeFired = false;
	RollHoldTime = 0.f;
	bRollFired = false;
	// Rechtermuis = INHOUDEN: papers (geladen) -> rollen, joint -> roken. Beide in Tick afgehandeld.
}

void AThePlugSIMCharacter::OnSecondaryReleased()
{
	bRmbDown = false;
	SmokeHoldTime = 0.f;
	bSmokeFired = false;
	RollHoldTime = 0.f;
	bRollFired = false;
	if (Phone) { Phone->SetSmokeHoldFrac(0.f); Phone->SetRollHoldFrac(0.f); }
}

void AThePlugSIMCharacter::OnLoadKey()
{
	if (!Phone || !Inventory) { return; }
	if (Phone->IsOpen() || Phone->IsInventoryOpen() || Phone->IsDealOpen() || Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen()) { return; }

	// F opent (of sluit) het rol-menu zolang je vloei vasthoudt. In het menu kies je het aantal gram
	// en klik je op "Load"; daarna rechtermuis inhouden om te rollen.
	if (Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Papers_")) || Phone->IsRollOpen())
	{
		Phone->ToggleRollUI();
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("Hold rolling papers (hotbar) to open the roll menu."));
	}
}

void AThePlugSIMCharacter::UseActiveItem()
{
	if (!Inventory || !Build)
	{
		return;
	}
	// UI open? Niet gebruiken (klik is voor de UI).
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen()))
	{
		return;
	}
	const FName Item = Inventory->GetActiveItemId();
	if (Item == FName(TEXT("Pot")))
	{
		Build->TogglePotPlacement();
	}
	// Joints rook je NIET met links-klik (dat zou per ongeluk je joint opbranden); dat gaat via
	// rechtermuisknop INHOUDEN (zie OnSecondaryPressed/Released + Tick), met een duidelijke balk.
}

void AThePlugSIMCharacter::SmokeActiveJoint()
{
	if (!Inventory) { return; }
	const FName Joint = Inventory->GetActiveItemId();
	if (!Joint.ToString().StartsWith(TEXT("Joint_"))) { return; }
	ServerSmokeJoint(Joint);
}

void AThePlugSIMCharacter::ServerSmokeJoint_Implementation(FName JointId)
{
	if (!Inventory || !JointId.ToString().StartsWith(TEXT("Joint_"))) { return; }

	// Hoe high: meer gram + hogere THC% + betere kwaliteit = sterker. Backwoods (10g) vol topwiet
	// (~36% THC, 100% kwaliteit) tikt tegen het maximum aan.
	const int32 Grams = FCString::Atoi(*JointId.ToString().Mid(6)); // "Joint_5g" -> 5
	const float Thc = Inventory->GetItemQuality(JointId);            // ~0..36
	const float Q = Inventory->GetItemQualityPct(JointId) / 100.f;   // 0..1

	if (!Inventory->RemoveItem(JointId, 1))
	{
		return;
	}

	const float Intensity = UPhoneClientComponent::JointIntensity(Grams, Thc, Q * 100.f);

	// Roken zelf geeft GEEN XP (alleen oogsten + klanten helpen leveren XP). Wel een XP-BOOST terwijl
	// je high bent, gebaseerd op de THC% van de wiet: 17%-joint -> +17% XP, gemaximeerd op +50%.
	const float XpFrac = FMath::Clamp(Thc / 100.f, 0.f, 0.5f);

	// Stoned-buf: duur schaalt met intensiteit (cap op het maximum).
	const float AddSeconds = Intensity * StonedMaxSeconds;
	const float NewSeconds = FMath::Min(StonedMaxSeconds, StonedSeconds + AddSeconds);
	const float NewIntensity = FMath::Max(StonedIntensity, Intensity);
	const float NewXpFrac = FMath::Max(StonedXpFrac, XpFrac);
	MulticastApplyStoned(NewSeconds, NewIntensity, 0, NewXpFrac);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor(120, 220, 160),
			FString::Printf(TEXT("You smoke the %dg joint... high %.0f%%  (XP boost active)"), Grams, Intensity * 100.f));
	}
}

void AThePlugSIMCharacter::MulticastApplyStoned_Implementation(float Seconds, float Intensity, int32 XpBonus, float XpFrac)
{
	StonedSeconds = Seconds;
	StonedIntensity = Intensity;
	StonedXpFrac = XpFrac;
	LastSmokeXp = XpBonus;

	// Klein rookwolkje uit het hoofd (cosmetisch, op elke client).
	if (UWorld* World = GetWorld())
	{
		const FVector Head = FirstPersonCameraComponent
			? FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * 22.f
			: GetActorLocation() + FVector(0.f, 0.f, 70.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const int32 Puffs = 1 + FMath::RoundToInt(Intensity * 2.f); // sterker = wat meer wolk
		for (int32 i = 0; i < Puffs; ++i)
		{
			const FVector Off(FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(0.f, 8.f));
			World->SpawnActor<ASmokePuff>(ASmokePuff::StaticClass(), Head + Off, FRotator::ZeroRotator, Params);
		}
	}
}

void AThePlugSIMCharacter::OpenPotUpgradeUI()
{
	if (!Phone)
	{
		return;
	}
	if (const UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AGrowPlant* Pot = Cast<AGrowPlant>(IC->GetFocusedActor()))
		{
			Phone->OpenPotUpgrade(Pot);
		}
	}
}

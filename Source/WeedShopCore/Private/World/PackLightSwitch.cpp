#include "World/PackLightSwitch.h"

#include "World/DayNightController.h"
#include "Phone/PhoneClientComponent.h"
#include "Input/ControlSettings.h"
#include "WeedShopCore.h" // WeedData::File

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/FileHelper.h"
#include "EngineUtils.h"

APackLightSwitch::APackLightSwitch()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // licht is lokaal/cosmetisch

	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	SetRootComponent(Plate);
	Plate->SetMobility(EComponentMobility::Movable);
	// Klein muurplaatje. Cube = 100cm -> dun in de kijk-richting (X), smal (Y), hoger (Z).
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Plate->SetStaticMesh(M);
	}
	Plate->SetWorldScale3D(FVector(0.03f, 0.085f, 0.13f));
	// Aanklikbaar voor de interactie-trace (ECC_Visibility), maar geen fysieke botsing.
	Plate->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Plate->SetCollisionResponseToAllChannels(ECR_Ignore);
	Plate->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Plate->SetCanEverAffectNavigation(false);
	// Zacht wit oplichtend plaatje (zelfde unlit-glow als de elevator-cijfers) -> in het donker te vinden.
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitGlow.M_DigitGlow")))
	{
		Plate->SetMaterial(0, Glow);
	}
}

void APackLightSwitch::Setup(const FString& InKey, float InRadius)
{
	PersistKey = InKey;
	if (InRadius > 0.f) { ControlRadius = InRadius; }
	Load();
}

void APackLightSwitch::BeginPlay()
{
	Super::BeginPlay();
	ClaimTimer = 1.f; // direct een eerste claim-poging in de eerste Tick
}

static float LightSwitch_Mult(float B01)
{
	// 0..1 -> 0.3..1.7, midden (0.5) ~ normale geauthorde sterkte.
	return 0.3f + 1.4f * FMath::Clamp(B01, 0.f, 1.f);
}

static APackLightSwitch* LightSwitch_Nearest(UWorld* W, const FVector& P)
{
	APackLightSwitch* Best = nullptr; float BestD = TNumericLimits<float>::Max();
	for (TActorIterator<APackLightSwitch> It(W); It; ++It)
	{
		const float D = FVector::DistSquared(It->GetActorLocation(), P);
		if (D < BestD) { BestD = D; Best = *It; }
	}
	return Best;
}

void APackLightSwitch::ClaimLamps()
{
	UWorld* W = GetWorld();
	ADayNightController* DNC = W ? ADayNightController::GetLocal(W) : nullptr;
	if (!DNC) { return; }
	const FVector Loc = GetActorLocation();

	TArray<UPointLightComponent*> NearLights;
	DNC->CollectCeilingLightsNear(Loc, ControlRadius, NearLights);
	for (UPointLightComponent* PL : NearLights)
	{
		if (!PL) { continue; }
		// Alleen claimen als WIJ de dichtstbijzijnde schakelaar zijn -> grote kamer vs badkamer splitst vanzelf.
		if (LightSwitch_Nearest(W, PL->GetComponentLocation()) != this) { continue; }
		bool bAlready = false;
		for (const TWeakObjectPtr<UPointLightComponent>& E : Lights) { if (E.Get() == PL) { bAlready = true; break; } }
		if (bAlready) { continue; }
		Lights.Add(PL);
		DNC->SetSwitchControlledLight(PL, true);
	}

	TArray<UMaterialInstanceDynamic*> NearMids; TArray<float> NearBright; TArray<FVector> NearPos;
	DNC->CollectCeilingEmisNear(Loc, ControlRadius, NearMids, NearBright, NearPos);
	for (int32 i = 0; i < NearMids.Num(); ++i)
	{
		UMaterialInstanceDynamic* Mid = NearMids[i];
		if (!Mid) { continue; }
		if (LightSwitch_Nearest(W, NearPos[i]) != this) { continue; }
		bool bAlready = false;
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& E : Emis) { if (E.Get() == Mid) { bAlready = true; break; } }
		if (bAlready) { continue; }
		Emis.Add(Mid);
		EmisBase.Add(NearBright[i]);
		DNC->SetSwitchControlledEmis(Mid, true);
	}

	ApplyToLamps();
}

void APackLightSwitch::ApplyToLamps()
{
	UWorld* W = GetWorld();
	ADayNightController* DNC = W ? ADayNightController::GetLocal(W) : nullptr;
	const float Mult = LightSwitch_Mult(Brightness01);
	for (const TWeakObjectPtr<UPointLightComponent>& WP : Lights)
	{
		UPointLightComponent* PL = WP.Get();
		if (!PL) { continue; }
		const float OnI = DNC ? DNC->CeilingOnIntensity(PL) : (PL->ComponentHasTag(TEXT("CeilGlow")) ? 4.6f : 46.f);
		PL->SetIntensity(bOn ? OnI * Mult : 0.f);
	}
	for (int32 i = 0; i < Emis.Num(); ++i)
	{
		UMaterialInstanceDynamic* Mid = Emis[i].Get();
		if (!Mid) { continue; }
		const float Base = EmisBase.IsValidIndex(i) ? EmisBase[i] : 1.f;
		Mid->SetScalarParameterValue(TEXT("Brightness"), bOn ? Base * Mult : 0.f);
	}
}

void APackLightSwitch::ToggleOnOff()
{
	bOn = !bOn;
	ApplyToLamps();
	Save();
}

void APackLightSwitch::SetOn(bool bNewOn)
{
	if (bOn == bNewOn) { return; }
	bOn = bNewOn;
	ApplyToLamps();
	Save();
}

void APackLightSwitch::SetBrightness01(float V)
{
	Brightness01 = FMath::Clamp(V, 0.f, 1.f);
	if (!bOn) { bOn = true; } // aan de dimmer draaien = licht aan
	ApplyToLamps();
	Save();
}

void APackLightSwitch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Periodiek (her)claimen: lampen kunnen later in-streamen (gestreamde appartement-levels).
	ClaimTimer += DeltaSeconds;
	if (ClaimTimer >= 1.f) { ClaimTimer = 0.f; ClaimLamps(); }

	// Tap/hold oplossen op het echte keystate van de speler.
	if (bPressArmed)
	{
		APlayerController* PC = PressPC.Get();
		bool bDown = false;
		if (PC)
		{
			UControlSettings* CS = UControlSettings::Get();
			const FKey K1 = CS ? CS->GetKey(TEXT("Interact"), false) : EKeys::Invalid;
			const FKey K2 = CS ? CS->GetKey(TEXT("Interact"), true) : EKeys::Invalid;
			bDown = (K1.IsValid() && PC->IsInputKeyDown(K1))
				|| (K2.IsValid() && PC->IsInputKeyDown(K2))
				|| PC->IsInputKeyDown(EKeys::LeftMouseButton);
		}
		if (bDown)
		{
			HoldTimer += DeltaSeconds;
			if (!bHoldFired && HoldTimer >= HoldRequired)
			{
				bHoldFired = true;
				bPressArmed = false;
				if (APawn* Pw = PressPawn.Get())
				{
					if (UPhoneClientComponent* Ph = Pw->FindComponentByClass<UPhoneClientComponent>())
					{
						Ph->OpenLightDimmer(this);
					}
				}
			}
		}
		else
		{
			// Losgelaten vóór de hold-drempel = TAP -> aan/uit.
			if (!bHoldFired) { ToggleOnOff(); }
			bPressArmed = false;
		}
	}
}

void APackLightSwitch::Interact_Implementation(APawn* InstigatorPawn)
{
	// Staat de dimmer al open voor deze schakelaar? Dan stuurt de slider-popup; negeer de interact.
	if (InstigatorPawn)
	{
		if (UPhoneClientComponent* Ph = InstigatorPawn->FindComponentByClass<UPhoneClientComponent>())
		{
			if (Ph->IsLightDimmerOpen() && Ph->GetDimmerSwitch() == this) { return; }
		}
	}
	// 'Arm' de druk; de Tick beslist tap vs hold aan de hand van het keystate.
	PressPawn = InstigatorPawn;
	PressPC = InstigatorPawn ? Cast<APlayerController>(InstigatorPawn->GetController()) : nullptr;
	bPressArmed = true;
	bHoldFired = false;
	HoldTimer = 0.f;
}

FText APackLightSwitch::GetInteractionPrompt_Implementation() const
{
	return FText::FromString(FString::Printf(TEXT("Light: %s   (hold to dim)"), bOn ? TEXT("on") : TEXT("off")));
}

void APackLightSwitch::Load()
{
	if (PersistKey.IsEmpty()) { return; }
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(TEXT("LightSwitchState.txt")));
	for (const FString& L : Lines)
	{
		FString K, V;
		if (L.Split(TEXT("="), &K, &V) && K.TrimStartAndEnd() == PersistKey)
		{
			FString A, B;
			if (V.Split(TEXT(","), &A, &B))
			{
				bOn = (A.TrimStartAndEnd() == TEXT("1"));
				Brightness01 = FMath::Clamp(FCString::Atof(*B), 0.f, 1.f);
			}
			return;
		}
	}
}

void APackLightSwitch::Save() const
{
	if (PersistKey.IsEmpty()) { return; }
	const FString Path = WeedData::File(TEXT("LightSwitchState.txt"));
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *Path);
	Lines.RemoveAll([this](const FString& L)
	{
		FString K, V;
		return L.Split(TEXT("="), &K, &V) && K.TrimStartAndEnd() == PersistKey;
	});
	Lines.Add(FString::Printf(TEXT("%s=%d,%.3f"), *PersistKey, bOn ? 1 : 0, Brightness01));
	FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

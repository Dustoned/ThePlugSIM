#include "World/PackLightSwitch.h"

#include "World/DayNightController.h"
#include "Phone/PhoneClientComponent.h"
#include "Input/ControlSettings.h"
#include "WeedShopCore.h" // WeedData::File
#include "World/WorldSyncComponent.h"
#include "Game/WeedShopGameState.h"
#include "Interaction/InteractionComponent.h"

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

	// Root (onschaalbaar) zodat plaat + tuimelknop elk hun eigen schaal/positie houden.
	USceneComponent* RootC = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootC);

	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	Plate->SetupAttachment(RootC);
	Plate->SetMobility(EComponentMobility::Movable);
	// Muurplaatje. Cube = 100cm -> dun in de kijk-richting (X), smal (Y), hoger (Z).
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Plate->SetStaticMesh(M);
	}
	Plate->SetRelativeScale3D(FVector(0.03f, 0.085f, 0.13f));
	// Aanklikbaar voor de interactie-trace (ECC_Visibility), maar geen fysieke botsing.
	Plate->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Plate->SetCollisionResponseToAllChannels(ECR_Ignore);
	Plate->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Plate->SetCanEverAffectNavigation(false);
	// Belicht cremE-plaatje (NIET het felle witte glow-kubusje van eerst) -> ziet eruit als een echte
	// schakelaar-plaat i.p.v. een wit blok.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		PlateMID = UMaterialInstanceDynamic::Create(Base, this);
		if (PlateMID)
		{
			PlateMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.85f, 0.84f, 0.79f));
			Plate->SetMaterial(0, PlateMID);
		}
	}

	// Tuimelknop (rocker): klein kubusje dat uit de plaat-voorkant steekt, met een zachte glow zodat ALLEEN
	// de knop in het donker oplicht (niet de hele plaat) -> leest als een schakelaar met indicatie.
	Toggle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Toggle"));
	Toggle->SetupAttachment(RootC);
	Toggle->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Toggle->SetStaticMesh(M);
	}
	Toggle->SetRelativeScale3D(FVector(0.022f, 0.045f, 0.06f)); // 2.2cm diep x 4.5 breed x 6 hoog
	Toggle->SetRelativeLocation(FVector(1.6f, 0.f, 0.f));       // steekt uit de plaat naar de kamer (+X)
	Toggle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Toggle->SetCanEverAffectNavigation(false);
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitGlow.M_DigitGlow")))
	{
		Toggle->SetMaterial(0, Glow);
	}
}

void APackLightSwitch::Setup(const FString& InKey, float InRadius)
{
	PersistKey = InKey;
	if (InRadius > 0.f) { ControlRadius = InRadius; }
	Load();
	LoadLinks();
	LampSyncId = UWorldSyncComponent::MakeId(GetActorLocation(), GetActorRotation().Yaw); // gedeelde co-op lamp-id
}

void APackLightSwitch::BeginPlay()
{
	Super::BeginPlay();
	ClaimTimer = 1.f; // direct een eerste claim-poging in de eerste Tick
	// CO-OP: de server publiceert de geladen lamp-staat naar WorldSync zodat clients + late joiners 'm oppikken.
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Client) { PublishState(); }
}

void APackLightSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Bij verwijderen (oppakken/clear): de geclaimde lampen + emissives teruggeven aan de dag/nacht-klok,
	// anders blijven ze 'bevroren' op hun laatste stand hangen (niemand stuurt ze meer aan).
	if (UWorld* W = GetWorld())
	{
		if (ADayNightController* DNC = ADayNightController::GetLocal(W))
		{
			for (const TWeakObjectPtr<UPointLightComponent>& WP : Lights)
			{
				if (UPointLightComponent* PL = WP.Get()) { DNC->SetSwitchControlledLight(PL, false); }
			}
			for (const TWeakObjectPtr<UMaterialInstanceDynamic>& WP : Emis)
			{
				if (UMaterialInstanceDynamic* M = WP.Get()) { DNC->SetSwitchControlledEmis(M, false); }
			}
		}
	}
	Super::EndPlay(EndPlayReason);
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

// True als een ANDERE schakelaar deze lamp expliciet gelinkt heeft (dan mag een auto-switch 'm niet pakken).
static bool LinkSwitch_ClaimedByOther(UWorld* W, const FString& LampKey, const APackLightSwitch* Self)
{
	for (TActorIterator<APackLightSwitch> It(W); It; ++It)
	{
		if (*It == Self) { continue; }
		if (It->HasManualLinks() && It->IsLampLinked(LampKey)) { return true; }
	}
	return false;
}

FString APackLightSwitch::MakeLampKey(const FVector& P)
{
	const int32 X = FMath::RoundToInt(P.X / 50.f) * 50;
	const int32 Y = FMath::RoundToInt(P.Y / 50.f) * 50;
	const int32 Z = FMath::RoundToInt(P.Z / 200.f) * 200; // grof op Z: de point-light (~45cm onder de fixture) en
	                                                       // de emissive van DEZELFDE lamp vallen zo in 1 cel;
	                                                       // verschillende verdiepingen (~320cm) blijven gescheiden.
	return FString::Printf(TEXT("%d_%d_%d"), X, Y, Z);
}

bool APackLightSwitch::OwnsByLinkOrProximity(UWorld* W, const FVector& Pos) const
{
	const FString Key = MakeLampKey(Pos);
	if (HasManualLinks()) { return LinkedLampKeys.Contains(Key); } // handmatig => exact deze set, niks anders
	// Geen manual links => oud auto-gedrag, maar een lamp die een ANDERE switch gelinkt heeft niet afpakken.
	if (LinkSwitch_ClaimedByOther(W, Key, this)) { return false; }
	return LightSwitch_Nearest(W, Pos) == this;
}

void APackLightSwitch::ToggleLampLink(const FString& LampKey)
{
	if (LinkedLampKeys.Contains(LampKey)) { LinkedLampKeys.Remove(LampKey); }
	else { LinkedLampKeys.Add(LampKey); }
	SaveLinks();
	// In de link-modus alleen herkleuren (de preview heeft de lampen al overgenomen); anders meteen echt herclaimen.
	if (bLinkPreview) { ApplyLinkPreview(); } else { ClaimLamps(); }
}

void APackLightSwitch::LoadLinks()
{
	if (PersistKey.IsEmpty()) { return; }
	LinkedLampKeys.Reset();
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(TEXT("LightSwitchLinks.txt")));
	for (const FString& L : Lines)
	{
		FString K, V;
		if (L.Split(TEXT("="), &K, &V) && K.TrimStartAndEnd() == PersistKey)
		{
			TArray<FString> Keys;
			V.ParseIntoArray(Keys, TEXT(","), true);
			for (FString& One : Keys) { LinkedLampKeys.Add(One.TrimStartAndEnd()); }
			return;
		}
	}
}

void APackLightSwitch::SaveLinks() const
{
	if (PersistKey.IsEmpty()) { return; }
	const FString Path = WeedData::File(TEXT("LightSwitchLinks.txt"));
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *Path);
	Lines.RemoveAll([this](const FString& L)
	{
		FString K, V;
		return L.Split(TEXT("="), &K, &V) && K.TrimStartAndEnd() == PersistKey;
	});
	if (LinkedLampKeys.Num() > 0)
	{
		TArray<FString> Keys = LinkedLampKeys.Array();
		Lines.Add(FString::Printf(TEXT("%s=%s"), *PersistKey, *FString::Join(Keys, TEXT(","))));
	}
	FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void APackLightSwitch::SetLinkPreview(bool bEnable)
{
	if (bEnable == bLinkPreview) { return; }
	UWorld* W = GetWorld();
	ADayNightController* DNC = W ? ADayNightController::GetLocal(W) : nullptr;
	if (!DNC) { return; }
	bLinkPreview = bEnable;

	if (bEnable)
	{
		// Diffusers (lightboxes) in de buurt overnemen. Gelinkte DIMMEN we sterk zodat de blauwe marker-glow
		// erover heen domineert (= blauwe lightbox); niet-gelinkte blijven helder wit aan, zodat je ze ziet+klikt.
		PreviewEmis.Reset();
		PreviewEmisPos.Reset();
		PreviewEmisBright.Reset();
		TArray<UMaterialInstanceDynamic*> Mids; TArray<float> Bright; TArray<FVector> Pos;
		DNC->CollectCeilingEmisNear(GetActorLocation(), ControlRadius, Mids, Bright, Pos);
		for (int32 i = 0; i < Mids.Num(); ++i)
		{
			UMaterialInstanceDynamic* M = Mids[i];
			if (!M) { continue; }
			PreviewEmis.Add(M);
			PreviewEmisPos.Add(Pos.IsValidIndex(i) ? Pos[i] : FVector::ZeroVector);
			PreviewEmisBright.Add(Bright.IsValidIndex(i) ? Bright[i] : 1.f);
			DNC->SetSwitchControlledEmis(M, true); // de klok laat 'm los; wij sturen de brightness tijdens preview
		}
		ApplyLinkPreview();
	}
	else
	{
		// Terug naar normaal: originele brightness herstellen; niet-gelinkte diffusers weer aan de klok geven.
		for (int32 i = 0; i < PreviewEmis.Num(); ++i)
		{
			UMaterialInstanceDynamic* M = PreviewEmis[i].Get();
			if (!M) { continue; }
			if (PreviewEmisBright.IsValidIndex(i)) { M->SetScalarParameterValue(TEXT("Brightness"), PreviewEmisBright[i]); }
			const FString Key = MakeLampKey(PreviewEmisPos.IsValidIndex(i) ? PreviewEmisPos[i] : FVector::ZeroVector);
			if (!LinkedLampKeys.Contains(Key)) { DNC->SetSwitchControlledEmis(M, false); } // niet gelinkt -> auto
		}
		PreviewEmis.Reset();
		PreviewEmisPos.Reset();
		PreviewEmisBright.Reset();
		ClaimLamps(); // echte eigendom + aan/uit/dim herstellen op basis van de definitieve links
	}
}

void APackLightSwitch::ApplyLinkPreview()
{
	for (int32 i = 0; i < PreviewEmis.Num(); ++i)
	{
		UMaterialInstanceDynamic* M = PreviewEmis[i].Get();
		if (!M) { continue; }
		const FString Key = MakeLampKey(PreviewEmisPos.IsValidIndex(i) ? PreviewEmisPos[i] : FVector::ZeroVector);
		const bool bLinked = LinkedLampKeys.Contains(Key);
		const float Orig = PreviewEmisBright.IsValidIndex(i) ? PreviewEmisBright[i] : 1.f;
		// Gelinkt: wit-glow flink dimmen (blauwe marker domineert). Niet-gelinkt: helder wit aan.
		M->SetScalarParameterValue(TEXT("Brightness"), bLinked ? Orig * 0.10f : Orig);
	}
}

void APackLightSwitch::ClaimLamps()
{
	UWorld* W = GetWorld();
	ADayNightController* DNC = W ? ADayNightController::GetLocal(W) : nullptr;
	if (!DNC) { return; }
	const FVector Loc = GetActorLocation();

	// LOSLATEN: lampen/emissives waarvoor wij niet (meer) de dichtstbijzijnde schakelaar zijn -> vrijgeven,
	// zodat de ECHTE dichtstbijzijnde schakelaar ze exclusief aanstuurt. Zonder dit blijft een eerder geplaatste
	// schakelaar een lamp vasthouden die nu bij een dichtere hoort -> beide sturen 'm om-en-om aan/uit = knipperen.
	Lights.RemoveAll([&](const TWeakObjectPtr<UPointLightComponent>& WP)
	{
		UPointLightComponent* PL = WP.Get();
		return !PL || !OwnsByLinkOrProximity(W, PL->GetComponentLocation());
	});
	for (int32 i = Emis.Num() - 1; i >= 0; --i)
	{
		const bool bDrop = !Emis[i].IsValid() || (EmisPos.IsValidIndex(i) && !OwnsByLinkOrProximity(W, EmisPos[i]));
		if (bDrop)
		{
			Emis.RemoveAt(i);
			if (EmisBase.IsValidIndex(i)) { EmisBase.RemoveAt(i); }
			if (EmisPos.IsValidIndex(i)) { EmisPos.RemoveAt(i); }
		}
	}

	TArray<UPointLightComponent*> NearLights;
	DNC->CollectCeilingLightsNear(Loc, ControlRadius, NearLights);
	for (UPointLightComponent* PL : NearLights)
	{
		if (!PL) { continue; }
		// Alleen claimen als WIJ de dichtstbijzijnde schakelaar zijn -> grote kamer vs badkamer splitst vanzelf.
		if (!OwnsByLinkOrProximity(W, PL->GetComponentLocation())) { continue; }
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
		if (!OwnsByLinkOrProximity(W, NearPos[i])) { continue; }
		bool bAlready = false;
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& E : Emis) { if (E.Get() == Mid) { bAlready = true; break; } }
		if (bAlready) { continue; }
		Emis.Add(Mid);
		EmisBase.Add(NearBright[i]);
		EmisPos.Add(NearPos[i]);
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
	PublishState();
}

void APackLightSwitch::SetOn(bool bNewOn)
{
	if (bOn == bNewOn) { return; }
	bOn = bNewOn;
	ApplyToLamps();
	Save();
	PublishState();
}

void APackLightSwitch::SetBrightness01(float V)
{
	Brightness01 = FMath::Clamp(V, 0.f, 1.f);
	if (!bOn) { bOn = true; } // aan de dimmer draaien = licht aan
	ApplyToLamps();
	Save();
	PublishState();
}

void APackLightSwitch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Periodiek (her)claimen: lampen kunnen later in-streamen (gestreamde appartement-levels).
	ClaimTimer += DeltaSeconds;
	if (ClaimTimer >= 1.f) { ClaimTimer = 0.f; ClaimLamps(); }

	// CO-OP: de gedeelde lamp-staat lezen (server schrijft, iedereen leest) -> host/joiner + late joiner gelijk.
	// Na een EIGEN toggle/dim even niet lezen (SyncSuppressUntil) zodat de lamp niet 1 frame terugflitst tot de RPC rond is.
	if (LampSyncId != 0 && GetWorld() && GetWorld()->GetTimeSeconds() >= SyncSuppressUntil)
	{
		if (AWeedShopGameState* GSl = GetWorld()->GetGameState<AWeedShopGameState>())
		{
			if (UWorldSyncComponent* WS = GSl->GetWorldSync())
			{
				bool SOn = bOn; float SBright = Brightness01;
				if (WS->GetLampState(LampSyncId, SOn, SBright) && (SOn != bOn || !FMath::IsNearlyEqual(SBright, Brightness01, 0.001f)))
				{
					bOn = SOn; Brightness01 = SBright; ApplyToLamps(); // GEEN PublishState -> geen loop
				}
			}
		}
	}

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

void APackLightSwitch::PublishState()
{
	UWorld* W = GetWorld();
	if (!W || LampSyncId == 0) { return; }
	if (W->GetNetMode() == NM_Client)
	{
		// Client: de schakelaar is bReplicates=false -> relay via de LOKALE pawn z'n InteractionComponent naar de server.
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (APawn* Pw = PC->GetPawn())
			{
				if (UInteractionComponent* IC = Pw->FindComponentByClass<UInteractionComponent>())
				{
					IC->RelayLampState(LampSyncId, bOn, Brightness01);
				}
			}
		}
	}
	else if (AWeedShopGameState* GS = W->GetGameState<AWeedShopGameState>())
	{
		// Host/standalone heeft authority -> direct naar WorldSync.
		if (UWorldSyncComponent* WS = GS->GetWorldSync()) { WS->SetLampState(LampSyncId, bOn, Brightness01); }
	}
	SyncSuppressUntil = W->GetTimeSeconds() + 0.6f; // eigen wijziging even niet terug-lezen tot de RPC rond is
}

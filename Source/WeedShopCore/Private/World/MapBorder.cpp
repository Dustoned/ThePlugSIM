#include "World/MapBorder.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/WeedToast.h"

AMapBorder::AMapBorder()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // afstand-check hoeft niet per frame
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMapBorder::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();
}

void AMapBorder::Rebuild()
{
	for (UStaticMeshComponent* C : Segments)
	{
		if (C) { C->DestroyComponent(); }
	}
	Segments.Reset();
	Points.Reset();

	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MapBorder.txt")));
	for (const FString& Line : Lines)
	{
		TArray<FString> P;
		Line.ParseIntoArray(P, TEXT(","));
		if (P.Num() >= 3) { Points.Add(FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]))); }
	}
	if (Points.Num() < 2) { return; }

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/CityBeachStrip/Materials/Glass/MI_Window_TwoSided.MI_Window_TwoSided"));
	if (!Cube) { return; }

	// Open keten in marker-volgorde; wil je hem sluiten, zet de laatste marker bij de eerste.
	for (int32 i = 0; i + 1 < Points.Num(); ++i)
	{
		const FVector A = Points[i];
		const FVector B = Points[i + 1];
		const float Len = FVector::Dist2D(A, B);
		if (Len < 50.f) { continue; }
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
		const FVector Mid((A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f, FMath::Min(A.Z, B.Z) + 500.f);

		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Cube);
		if (Glass) { C->SetMaterial(0, Glass); }
		C->SetWorldLocationAndRotation(Mid, FRotator(0.f, Yaw, 0.f));
		// Hoge dunne glaswand: 14m hoog (2m onder marker tot 12m erboven), 12cm dik.
		C->SetWorldScale3D(FVector(Len / 100.f, 0.12f, 14.f));
		C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		C->SetCollisionResponseToAllChannels(ECR_Block);
		C->SetCanEverAffectNavigation(false);
		C->SetVisibility(false); // verschijnt pas als je dichtbij komt (Tick)
		Segments.Add(C);
	}
	UE_LOG(LogTemp, Warning, TEXT("MapBorder: %d wand-segmenten gebouwd uit %d markers"), Segments.Num(), Points.Num());
}

void AMapBorder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Segments.Num() == 0) { return; }
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!P) { return; }
	const FVector L = P->GetActorLocation();

	ToastCooldown -= 0.25f;
	float Nearest = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Segments.Num() && i + 1 < Points.Num() + 1; ++i)
	{
		UStaticMeshComponent* C = Segments[i];
		if (!C) { continue; }
		const FVector A = Points[i];
		const FVector B = Points[i + 1];
		const float D = FMath::PointDistToSegment(FVector(L.X, L.Y, 0.f),
			FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f));
		Nearest = FMath::Min(Nearest, D);
		// Wand fade-in: alleen zichtbaar binnen 35m van dit segment.
		C->SetVisibility(D < 3500.f);
	}
	if (Nearest < 350.f && ToastCooldown <= 0.f)
	{
		ToastCooldown = 4.f;
		UWeedToast::NotifyPawn(P, -1, 3.f, FColor::Orange, TEXT("Map border - you cannot go further"));
	}
}

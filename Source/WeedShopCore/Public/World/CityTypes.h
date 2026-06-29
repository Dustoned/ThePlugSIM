// CityTypes.h - gedeelde wereld-structs die door de LEVENDE beach-map gebruikt worden
// (DoorRetrofitter woning-registry, CustomerBase, Phone, MapWidget). Apart gehouden van de
// (verwijderde) CityGenerator-klasse zodat de beach-code deze types houdt zonder de legacy-stad.
#pragma once

#include "CoreMinimal.h"
#include "CityTypes.generated.h"

class ACityDoor;

// Eén appartement-woning: de deur, een binnen-plek, een plek net buiten de deur, en het huisnummer.
USTRUCT()
struct FApartmentHome
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<ACityDoor> Door;
	UPROPERTY() FVector InteriorPos = FVector::ZeroVector;
	UPROPERTY() FVector HallPos = FVector::ZeroVector;
	UPROPERTY() FVector DoorPos = FVector::ZeroVector;
	UPROPERTY() FString Number;
	UPROPERTY() bool bApartment = false; // true = flat-unit, false = rijtjeshuis
	UPROPERTY() int32 Floor = 0;         // verdieping (0 = begane grond / rijtjeshuis)
	// Kamer-grenzen rond InteriorPos: X/Y = horizontale halve-afmeting, Z = kamerhoogte omhoog.
	// Gebruikt om te checken of een plaatsing BINNEN deze (gekochte) woning valt.
	UPROPERTY() FVector RoomHalf = FVector(200.f, 200.f, 320.f);
};

// Eén koopbaar pand voor de speler (3 stuks: starter-flatje, rijtjeshuis, grote flat-kamer).
USTRUCT()
struct FCityPropertyOffer
{
	GENERATED_BODY()
	UPROPERTY() int32 HomeIndex = -1;   // primaire woning (spawn/active/marker)
	UPROPERTY() TArray<int32> Homes;    // ALLE woningen die dit pand omvat (1, of 3 bij het rijtjesblok)
	UPROPERTY() FString Title;          // "Klein flatje (bovenin)" enz.
	UPROPERTY() FString Sub;            // "Nr 102-7  -  3e verdieping"
	UPROPERTY() int64 PriceCents = 0;   // koopprijs (bank); 0 = starter (gratis, al van jou)
	UPROPERTY() bool bStarter = false;  // het flatje waarin je begint
};

// Eén blok op de kaart: wereld-XY-midden + kleur + label (winkelnaam of huisnummer-reeks).
USTRUCT()
struct FCityMapBlock
{
	GENERATED_BODY()
	UPROPERTY() FVector2D Center = FVector2D::ZeroVector;
	UPROPERTY() FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);
	UPROPERTY() FString Label;
	UPROPERTY() bool bShop = false;
};

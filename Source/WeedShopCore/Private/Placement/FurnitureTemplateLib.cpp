#include "Placement/FurnitureTemplateLib.h"

#include "Placement/PlaceableProp.h"
#include "World/WaterSink.h"
#include "Cultivation/DryingRack.h"
#include "World/PackBench.h"
#include "World/StorageShelf.h"
#include "World/CeilingLamp.h"
#include "World/PackLightSwitch.h"
#include "World/Atm.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString FurnitureTemplates::FilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("FurnitureTemplates.txt");
}

FString FurnitureTemplates::TypeKey(bool bApartment, const FVector& RoomHalf)
{
	// Maten gesorteerd (klein x groot) zodat dezelfde kamer in beide oriëntaties dezelfde sleutel krijgt
	// -> je hoeft maar één kamer per maat in te richten; de oriëntatie draait bij het plaatsen mee.
	const int32 A = FMath::RoundToInt(RoomHalf.X / 50.f);
	const int32 B = FMath::RoundToInt(RoomHalf.Y / 50.f);
	return FString::Printf(TEXT("%s_%dx%d"), bApartment ? TEXT("Apt") : TEXT("Row"), FMath::Min(A, B), FMath::Max(A, B));
}

bool FurnitureTemplates::LoadTemplates(TMap<FString, TArray<FFurnitureEntry>>& Out)
{
	Out.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath()))
	{
		// INGEBAKKEN default-layout: werkt altijd, ook zonder Saved-bestand. (Een Saved-bestand van F8
		// overschrijft dit volledig.)
		Text = FString(
			TEXT("Row_6x7|Fridge|297.4|-242.0|-8.0|-0.5\n")
			TEXT("Row_6x7|Table|62.9|-231.8|-8.0|-0.3\n")
			TEXT("Row_6x7|Mattress|282.6|5.5|328.0|90.2\n")
			TEXT("Row_6x7|Chest|283.4|137.2|355.0|0.3\n")
			TEXT("Row_6x7|Shelf|37.8|-253.8|413.0|179.8\n")
			TEXT("Row_6x7|Sink|223.7|-240.3|37.0|-0.3\n")
			TEXT("Row_4x7|Fridge|303.1|174.2|-8.0|-179.0\n")
			TEXT("Row_4x7|Table|28.2|161.4|-8.0|-178.7\n")
			TEXT("Row_4x7|Mattress|280.3|-6.1|328.0|90.3\n")
			TEXT("Row_4x7|Chest|280.2|123.4|355.0|0.7\n")
			TEXT("Row_4x7|Sink|227.2|169.4|37.0|-178.7\n")
			TEXT("Apt_7x11|Fridge|488.2|313.5|-8.0|90.2\n")
			TEXT("Apt_7x11|Table|482.7|11.0|-8.0|89.1\n")
			TEXT("Apt_7x11|Mattress|-427.6|-303.5|-8.0|1.4\n")
			TEXT("Apt_7x11|Shelf|-363.8|321.0|77.0|0.8\n")
			TEXT("Apt_7x11|Chest|-296.1|-302.8|19.0|-90.1\n")
			TEXT("Apt_7x11|Sink|487.1|237.5|37.0|89.8\n")
			TEXT("Apt_5x7|Fridge|315.1|-227.8|-2.0|-180.8\n")
			TEXT("Apt_5x7|Table|5.5|-217.7|-2.0|-180.4\n")
			TEXT("Apt_5x7|Mattress|-252.7|214.7|-2.0|-0.5\n")
			TEXT("Apt_5x7|Sink|235.5|-225.9|43.0|-180.8\n"));
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> C;
		Line.ParseIntoArray(C, TEXT("|"));
		if (C.Num() < 6) { continue; }
		FFurnitureEntry E;
		E.ItemId = FName(*C[1]);
		E.Local = FVector(FCString::Atof(*C[2]), FCString::Atof(*C[3]), FCString::Atof(*C[4]));
		E.Yaw = FCString::Atof(*C[5]);
		Out.FindOrAdd(C[0]).Add(E);
	}
	return Out.Num() > 0;
}

AActor* FurnitureTemplates::SpawnEntry(UWorld* W, const FFurnitureEntry& E, const FVector& HomeInterior, const FVector& RoomHalf, bool bCosmetic)
{
	if (!W || E.ItemId.IsNone()) { return nullptr; }

	// Canoniek (lang/kort) -> wereld, gedraaid naar de oriëntatie van DIT huis. E.Local.X = langs de
	// lange muur, E.Local.Y = langs de korte. Als de lange as van dit huis langs wereld-Y ligt, draaien
	// we de layout 90 graden mee.
	const bool bLongX = RoomHalf.X >= RoomHalf.Y;
	const float WX = bLongX ? E.Local.X : E.Local.Y;
	const float WY = bLongX ? E.Local.Y : E.Local.X;
	const float WYaw = E.Yaw + (bLongX ? 0.f : 90.f);

	const float MX = FMath::Max(0.f, RoomHalf.X - 35.f);
	const float MY = FMath::Max(0.f, RoomHalf.Y - 35.f);
	FVector L = HomeInterior + FVector(FMath::Clamp(WX, -MX, MX), FMath::Clamp(WY, -MY, MY), E.Local.Z);
	const FTransform TM(FRotator(0.f, WYaw, 0.f), L);

	const FString S = E.ItemId.ToString();
	AActor* Spawned = nullptr;
	if (S == TEXT("Sink"))
	{
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawned = W->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), TM, SP);
	}
	else if (S.StartsWith(TEXT("DryRack")))
	{
		ADryingRack* R = W->SpawnActorDeferred<ADryingRack>(ADryingRack::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (R) { R->RackTier = E.ItemId; R->FinishSpawning(TM); }
		Spawned = R;
	}
	else if (S.StartsWith(TEXT("Bench")))
	{
		APackBench* B = W->SpawnActorDeferred<APackBench>(APackBench::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (B) { B->BenchTier = E.ItemId; B->FinishSpawning(TM); }
		Spawned = B;
	}
	else if (S == TEXT("Shelf") || S == TEXT("Chest"))
	{
		AStorageShelf* Sh = W->SpawnActorDeferred<AStorageShelf>(AStorageShelf::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Sh) { Sh->ShelfTier = E.ItemId; Sh->FinishSpawning(TM); }
		Spawned = Sh;
	}
	else if (S == TEXT("Lamp_Ceiling"))
	{
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawned = W->SpawnActor<ACeilingLamp>(ACeilingLamp::StaticClass(), TM, SP);
	}
	else if (S == TEXT("Atm"))
	{
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawned = W->SpawnActor<AAtm>(AAtm::StaticClass(), TM, SP);
	}
	else if (S == TEXT("LightSwitch"))
	{
		APackLightSwitch* Sw = W->SpawnActorDeferred<APackLightSwitch>(APackLightSwitch::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Sw)
		{
			Sw->Setup(FString::Printf(TEXT("sw_%d_%d_%d"),
				FMath::RoundToInt(L.X / 10.f), FMath::RoundToInt(L.Y / 10.f), FMath::RoundToInt(L.Z / 10.f)), 800.f);
			Sw->FinishSpawning(TM);
		}
		Spawned = Sw;
	}
	else
	{
		// Standaard meubel (Table/Fridge/Mattress/...): APlaceableProp met ItemId.
		APlaceableProp* P = W->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (P) { P->ItemId = E.ItemId; P->FinishSpawning(TM); }
		Spawned = P;
	}

	// Cosmetisch (niet oppakbaar) als: NPC-woning, OF het is een gootsteen. Sinks zijn ALTIJD vaste
	// fixtures -> ook in je eigen woning kun je ze niet oppakken.
	const bool bSinkLocked = (S == TEXT("Sink"));
	if (Spawned && (bCosmetic || bSinkLocked)) { Spawned->Tags.Add(FName(TEXT("Cosmetic"))); }
	return Spawned;
}

int32 FurnitureTemplates::ClearPlaced(UWorld* W)
{
	if (!W) { return 0; }
	int32 N = 0;
	for (TActorIterator<APlaceableProp> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<AWaterSink> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<ADryingRack> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<APackBench> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<AStorageShelf> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<ACeilingLamp> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<AAtm> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	for (TActorIterator<APackLightSwitch> It(W); It; ++It) { if (IsValid(*It)) { It->Destroy(); ++N; } }
	return N;
}

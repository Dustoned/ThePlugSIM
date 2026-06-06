#include "Placement/FurnitureTemplateLib.h"

#include "World/CityGenerator.h"
#include "Placement/PlaceableProp.h"
#include "World/WaterSink.h"
#include "Cultivation/DryingRack.h"
#include "World/PackBench.h"
#include "World/StorageShelf.h"
#include "World/CeilingLamp.h"
#include "World/Atm.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// Welke woning hoort bij dit punt? Royale tolerantie zodat meubels TEGEN de muur (sinks, wandrekken)
	// nog meegenomen worden; valt anders terug op de dichtstbijzijnde woning binnen redelijk bereik.
	int32 FindHomeForPoint(const TArray<FApartmentHome>& Homes, const FVector& L)
	{
		int32 Best = INDEX_NONE;
		float BestD = TNumericLimits<float>::Max();
		// 1) Woning wiens kamer-AABB het punt (ruim) omvat -> dichtstbijzijnde centrum.
		for (int32 i = 0; i < Homes.Num(); ++i)
		{
			const FApartmentHome& H = Homes[i];
			const FVector D = L - H.InteriorPos;
			const float Tol = 160.f; // tegen de muur mag ruim buiten de binnenmaat vallen
			if (FMath::Abs(D.X) <= H.RoomHalf.X + Tol && FMath::Abs(D.Y) <= H.RoomHalf.Y + Tol &&
				D.Z > -110.f && D.Z < H.RoomHalf.Z + 140.f)
			{
				// 3D-afstand: bij GESTAPELDE appartementen (zelfde XY, andere verdieping) wint zo de
				// JUISTE verdieping i.p.v. een willekeurige (anders kreeg je een hele-etage Z-offset +
				// soms de verkeerde kamergrootte/sleutel).
				const float DD = D.SizeSquared();
				if (DD < BestD) { BestD = DD; Best = i; }
			}
		}
		if (Best != INDEX_NONE) { return Best; }

		// 2) Fallback: dichtstbijzijnde woning-centrum in XY, mits binnen kamergrootte + marge.
		float BestF = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Homes.Num(); ++i)
		{
			const FApartmentHome& H = Homes[i];
			const float DD = FVector::DistSquared2D(L, H.InteriorPos);
			const float Cap = FMath::Max(H.RoomHalf.X, H.RoomHalf.Y) + 300.f;
			if (DD < Cap * Cap && DD < BestF) { BestF = DD; Best = i; }
		}
		return Best;
	}

	struct FPlaced { FName ItemId; FVector Loc; float Yaw; };

	// Verzamel alle geplaatste meubel-actors met hun item-id.
	// Auto-geplaatste fixtures (default-layout) worden bij capture GENEGEERD; alleen wat de speler zelf
	// neerzette telt mee.
	bool IsAuto(const AActor* A) { return A && A->ActorHasTag(FName(TEXT("AutoFixture"))); }

	void GatherPlaced(UWorld* W, TArray<FPlaced>& Out)
	{
		for (TActorIterator<APlaceableProp> It(W); It; ++It)
		{
			if (IsValid(*It) && !It->ItemId.IsNone() && !IsAuto(*It)) { Out.Add({ It->ItemId, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<AWaterSink> It(W); It; ++It)
		{
			if (IsValid(*It) && !IsAuto(*It)) { Out.Add({ FName(TEXT("Sink")), It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<ADryingRack> It(W); It; ++It)
		{
			if (IsValid(*It) && !IsAuto(*It)) { const FName Id = It->RackTier.IsNone() ? FName(TEXT("DryRack_Std")) : It->RackTier; Out.Add({ Id, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<APackBench> It(W); It; ++It)
		{
			if (IsValid(*It) && !IsAuto(*It)) { const FName Id = It->BenchTier.IsNone() ? FName(TEXT("Bench_Pack")) : It->BenchTier; Out.Add({ Id, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<AStorageShelf> It(W); It; ++It)
		{
			if (IsValid(*It) && !IsAuto(*It)) { const FName Id = It->ShelfTier.IsNone() ? FName(TEXT("Shelf")) : It->ShelfTier; Out.Add({ Id, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<ACeilingLamp> It(W); It; ++It)
		{
			if (IsValid(*It) && !IsAuto(*It)) { Out.Add({ FName(TEXT("Lamp_Ceiling")), It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<AAtm> It(W); It; ++It)
		{
			if (IsValid(*It) && !IsAuto(*It)) { Out.Add({ FName(TEXT("Atm")), It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
	}
}

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

void FurnitureTemplates::CountHomeTypes(ACityGenerator* City, TMap<FString, int32>& Out)
{
	Out.Reset();
	if (!City) { return; }
	// Koopbare panden niet meetellen (die vullen we sowieso). De rest = de woon-typen.
	TArray<FCityPropertyOffer> Offers; City->GetPropertyOffers(Offers);
	TSet<int32> ForSale;
	for (const FCityPropertyOffer& O : Offers) { for (int32 Idx : O.Homes) { ForSale.Add(Idx); } }
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	for (int32 i = 0; i < Homes.Num(); ++i)
	{
		if (ForSale.Contains(i)) { continue; }
		Out.FindOrAdd(TypeKey(Homes[i].bApartment, Homes[i].RoomHalf))++;
	}
}

int32 FurnitureTemplates::SaveFromWorld(UWorld* W, ACityGenerator* City)
{
	if (!W || !City) { return 0; }
	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	if (Homes.Num() == 0) { return 0; }

	TArray<FPlaced> Placed;
	GatherPlaced(W, Placed);
	if (Placed.Num() == 0) { return 0; }

	// Groepeer geplaatste meubels per woning-index.
	TMap<int32, TArray<FPlaced>> PerHome;
	for (const FPlaced& P : Placed)
	{
		const int32 Hi = FindHomeForPoint(Homes, P.Loc);
		if (Hi != INDEX_NONE) { PerHome.FindOrAdd(Hi).Add(P); }
	}
	if (PerHome.Num() == 0) { return 0; }

	// Per type: kies de woning met de MEESTE meubels als sjabloon.
	TMap<FString, int32> BestHomePerType;   // type -> home-index
	TMap<FString, int32> BestCountPerType;
	for (const TPair<int32, TArray<FPlaced>>& KV : PerHome)
	{
		const FString Type = TypeKey(Homes[KV.Key].bApartment, Homes[KV.Key].RoomHalf);
		const int32 Count = KV.Value.Num();
		if (Count > BestCountPerType.FindRef(Type)) { BestCountPerType.Add(Type, Count); BestHomePerType.Add(Type, KV.Key); }
	}

	// MERGE: begin vanaf de bestaande templates (file of ingebakken default) en overschrijf ALLEEN de
	// types die je deze keer opnieuw hebt ingericht. Zo blijven types die je niet aanraakt behouden
	// (bv. je goede starter), terwijl de rest netjes vervangen wordt.
	TMap<FString, TArray<FFurnitureEntry>> Merged;
	LoadTemplates(Merged);

	int32 Types = 0;
	for (const TPair<FString, int32>& KV : BestHomePerType)
	{
		const FString& Type = KV.Key;
		const int32 Hi = KV.Value;
		const FApartmentHome& H = Homes[Hi];
		// Canoniek frame: lokale X = de LANGE muur-as van de kamer, lokale Y = de korte. Zo is de layout
		// oriëntatie-onafhankelijk en draait 'ie bij het plaatsen mee naar elk huis.
		const bool bLongX = H.RoomHalf.X >= H.RoomHalf.Y;
		TArray<FFurnitureEntry> Entries;
		for (const FPlaced& P : PerHome[Hi])
		{
			const FVector D = P.Loc - H.InteriorPos;
			FFurnitureEntry E;
			E.ItemId = P.ItemId;
			E.Local = FVector(bLongX ? D.X : D.Y, bLongX ? D.Y : D.X, D.Z);
			E.Yaw = P.Yaw - (bLongX ? 0.f : 90.f);
			Entries.Add(E);
		}
		// Heb je in sandbox zelf een sink geplaatst? Dan gebruik die. Zo niet, behoud de bestaande sink
		// van dit type (sinks zijn in een normaal spel vast/niet plaatsbaar), zodat 'ie niet verdwijnt.
		const bool bUserSink = Entries.ContainsByPredicate([](const FFurnitureEntry& E) { return E.ItemId == FName(TEXT("Sink")); });
		if (!bUserSink)
		{
			if (const TArray<FFurnitureEntry>* Old = Merged.Find(Type))
			{
				for (const FFurnitureEntry& OE : *Old) { if (OE.ItemId == FName(TEXT("Sink"))) { Entries.Add(OE); } }
			}
		}
		Merged.Add(Type, Entries); // overschrijf dit type (met behoud van de sink)
		++Types;
	}

	// Serialiseer de volledige merged-map.
	FString Text;
	for (const TPair<FString, TArray<FFurnitureEntry>>& KV : Merged)
	{
		for (const FFurnitureEntry& E : KV.Value)
		{
			Text += FString::Printf(TEXT("%s|%s|%.1f|%.1f|%.1f|%.1f\n"),
				*KV.Key, *E.ItemId.ToString(), E.Local.X, E.Local.Y, E.Local.Z, E.Yaw);
		}
	}
	FFileHelper::SaveStringToFile(Text, *FilePath());
	return Types;
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
	return N;
}

#include "Placement/FurnitureTemplateLib.h"

#include "World/CityGenerator.h"
#include "Placement/PlaceableProp.h"
#include "World/WaterSink.h"
#include "Cultivation/DryingRack.h"
#include "World/PackBench.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// Welke woning bevat dit punt? Kies de dichtstbijzijnde wiens kamer-AABB het punt omvat.
	int32 FindHomeForPoint(const TArray<FApartmentHome>& Homes, const FVector& L)
	{
		int32 Best = INDEX_NONE;
		float BestD = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Homes.Num(); ++i)
		{
			const FApartmentHome& H = Homes[i];
			const FVector D = L - H.InteriorPos;
			const float Tol = 70.f;
			if (FMath::Abs(D.X) <= H.RoomHalf.X + Tol && FMath::Abs(D.Y) <= H.RoomHalf.Y + Tol &&
				D.Z > -70.f && D.Z < H.RoomHalf.Z + 90.f)
			{
				const float DD = D.SizeSquared();
				if (DD < BestD) { BestD = DD; Best = i; }
			}
		}
		return Best;
	}

	struct FPlaced { FName ItemId; FVector Loc; float Yaw; };

	// Verzamel alle geplaatste meubel-actors met hun item-id.
	void GatherPlaced(UWorld* W, TArray<FPlaced>& Out)
	{
		for (TActorIterator<APlaceableProp> It(W); It; ++It)
		{
			if (IsValid(*It) && !It->ItemId.IsNone()) { Out.Add({ It->ItemId, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<AWaterSink> It(W); It; ++It)
		{
			if (IsValid(*It)) { Out.Add({ FName(TEXT("Sink")), It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<ADryingRack> It(W); It; ++It)
		{
			if (IsValid(*It)) { const FName Id = It->RackTier.IsNone() ? FName(TEXT("DryRack_Std")) : It->RackTier; Out.Add({ Id, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
		}
		for (TActorIterator<APackBench> It(W); It; ++It)
		{
			if (IsValid(*It)) { const FName Id = It->BenchTier.IsNone() ? FName(TEXT("Bench_Pack")) : It->BenchTier; Out.Add({ Id, It->GetActorLocation(), (float)It->GetActorRotation().Yaw }); }
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

	// Schrijf: per type de entries als lokale offset t.o.v. de sjabloon-woning.
	FString Text;
	int32 Types = 0;
	for (const TPair<FString, int32>& KV : BestHomePerType)
	{
		const FString& Type = KV.Key;
		const int32 Hi = KV.Value;
		const FApartmentHome& H = Homes[Hi];
		// Canoniek frame: lokale X = de LANGE muur-as van de kamer, lokale Y = de korte. Zo is de layout
		// oriëntatie-onafhankelijk en draait 'ie bij het plaatsen mee naar elk huis.
		const bool bLongX = H.RoomHalf.X >= H.RoomHalf.Y;
		for (const FPlaced& P : PerHome[Hi])
		{
			const FVector D = P.Loc - H.InteriorPos;
			const float LLong = bLongX ? D.X : D.Y;
			const float LShort = bLongX ? D.Y : D.X;
			const float LYaw = P.Yaw - (bLongX ? 0.f : 90.f);
			Text += FString::Printf(TEXT("%s|%s|%.1f|%.1f|%.1f|%.1f\n"),
				*Type, *P.ItemId.ToString(), LLong, LShort, D.Z, LYaw);
		}
		++Types;
	}

	FFileHelper::SaveStringToFile(Text, *FilePath());
	return Types;
}

bool FurnitureTemplates::LoadTemplates(TMap<FString, TArray<FFurnitureEntry>>& Out)
{
	Out.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath())) { return false; }

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
		Spawned = W->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), TM);
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
	else
	{
		// Standaard meubel (Table/Fridge/Mattress/...): APlaceableProp met ItemId.
		APlaceableProp* P = W->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (P) { P->ItemId = E.ItemId; P->FinishSpawning(TM); }
		Spawned = P;
	}

	// NPC-woning -> markeer als cosmetisch (niet oppakbaar). Jouw eigen woning-meubels blijven onbetagd.
	if (Spawned && bCosmetic) { Spawned->Tags.Add(FName(TEXT("Cosmetic"))); }
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
	return N;
}

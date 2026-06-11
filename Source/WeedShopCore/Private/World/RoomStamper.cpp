#include "World/RoomStamper.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "UI/WeedToast.h"

namespace
{
	FString TemplatesDir() { return FPaths::ProjectSavedDir() / TEXT("RoomTemplates"); }

	// Marker-parsing (zelfde formaat als MarkedSpots.txt).
	TArray<FVector> ReadMarks(UWorld* W)
	{
		TArray<FVector> Marks;
		const FString MapPath = W ? W->GetOutermost()->GetName() : FString();
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
		for (const FString& Line : Lines)
		{
			if (!Line.Contains(MapPath)) { continue; }
			const int32 PIdx = Line.Find(TEXT("pos=("));
			if (PIdx == INDEX_NONE) { continue; }
			FString PosStr = Line.Mid(PIdx + 5);
			int32 Close = INDEX_NONE;
			if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
			TArray<FString> Parts;
			PosStr.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() >= 3) { Marks.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]))); }
		}
		return Marks;
	}
}

ARoomStamper::ARoomStamper()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

TArray<FString> ARoomStamper::ListTemplates()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(TemplatesDir() / TEXT("*.txt")), true, false);
	for (FString& F : Files) { F = FPaths::GetBaseFilename(F); }
	Files.Sort();
	return Files;
}

bool ARoomStamper::SaveTemplateFromMarkers(UWorld* W, const FString& TemplateName, FString& OutError)
{
	const TArray<FVector> Marks = ReadMarks(W);
	if (Marks.Num() < 2) { OutError = TEXT("Set 2 markers around the room first"); return false; }

	FBox2D Rect(ForceInit);
	Rect += FVector2D(Marks[0].X, Marks[0].Y);
	Rect += FVector2D(Marks[1].X, Marks[1].Y);
	Rect = Rect.ExpandBy(130.f);
	const float Feet = Marks[0].Z - 98.f;
	const float SrcZ = 480.f + 350.f * FMath::RoundToFloat((Feet - 480.f) / 350.f);

	// Anker zoeken: de VOORDEUR = het deurframe met een SM_Door_Apartment02-blad ernaast
	// (badkamer/binnendeuren hebben een ander blad en tellen niet mee). Fallback als er geen
	// appartement-deur in de rechthoek hangt: het frame dichtst bij de rechthoek-rand.
	TArray<FVector> AptLeaves;
	for (TActorIterator<AActor> LeafIt(W); LeafIt; ++LeafIt)
	{
		if (!IsValid(*LeafIt)) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> LeafComps(*LeafIt);
		for (UStaticMeshComponent* Comp : LeafComps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			if (!Comp->GetStaticMesh()->GetName().Contains(TEXT("Door_Apartment02"))) { continue; }
			AptLeaves.Add(Comp->GetComponentLocation());
		}
	}
	FTransform AnchorTM = FTransform::Identity;
	FTransform FallbackTM = FTransform::Identity;
	bool bAnchor = false, bFallback = false;
	float BestD = TNumericLimits<float>::Max();
	float BestEdge = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		if (!IsValid(*It)) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			if (!Comp->GetStaticMesh()->GetName().Contains(TEXT("DoorFrame"))) { continue; }
			const FVector L = Comp->GetComponentLocation();
			if (FMath::Abs(L.Z - SrcZ) > 60.f) { continue; }
			if (L.X < Rect.Min.X || L.X > Rect.Max.X || L.Y < Rect.Min.Y || L.Y > Rect.Max.Y) { continue; }
			float LeafD = TNumericLimits<float>::Max();
			for (const FVector& LV : AptLeaves)
			{
				if (FMath::Abs(LV.Z - L.Z) > 200.f) { continue; }
				LeafD = FMath::Min(LeafD, FVector::Dist2D(LV, L));
			}
			if (LeafD < 200.f && LeafD < BestD) { BestD = LeafD; AnchorTM = Comp->GetComponentTransform(); bAnchor = true; }
			const float EdgeDist = FMath::Min(
				FMath::Min(L.X - Rect.Min.X, Rect.Max.X - L.X),
				FMath::Min(L.Y - Rect.Min.Y, Rect.Max.Y - L.Y));
			if (EdgeDist < BestEdge) { BestEdge = EdgeDist; FallbackTM = Comp->GetComponentTransform(); bFallback = true; }
		}
	}
	if (!bAnchor && bFallback) { AnchorTM = FallbackTM; bAnchor = true; }
	if (!bAnchor) { OutError = TEXT("No door frame found inside the marked rect"); return false; }
	// Anker plat op de verdieping (geen pitch/roll, Z op vloer-niveau) zodat snappen schoon blijft.
	AnchorTM.SetRotation(FRotator(0.f, AnchorTM.GetRotation().Rotator().Yaw, 0.f).Quaternion());
	AnchorTM.SetLocation(FVector(AnchorTM.GetLocation().X, AnchorTM.GetLocation().Y, SrcZ));
	const FTransform AnchorInv = AnchorTM.Inverse();

	// Stukken verzamelen (zelfde uitsluitingen als de verticale vuller).
	FString Out;
	int32 Count = 0;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		const bool bHiddenActor = A->IsHidden();
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			const FString MeshName = Comp->GetStaticMesh()->GetName();
			if (bHiddenActor && !MeshName.Contains(TEXT("Door"))) { continue; }
			if (MeshName.Contains(TEXT("Camera")) || MeshName.Contains(TEXT("SecurityCam")) || MeshName.Contains(TEXT("MatineeCam"))
				|| MeshName.Contains(TEXT("DomeCam")) || MeshName.Contains(TEXT("SecurityLight"))
				|| MeshName.Contains(TEXT("Railing")) || MeshName.Contains(TEXT("Handrail")) || MeshName.Contains(TEXT("Balustrade"))
				|| MeshName.StartsWith(TEXT("SM_Top_"))
				|| MeshName.Contains(TEXT("Umbrella")) || MeshName.Contains(TEXT("Parasol")) || MeshName.Contains(TEXT("Lounger"))
				|| MeshName.Contains(TEXT("SunBed")) || MeshName.Contains(TEXT("Sunbed")) || MeshName.Contains(TEXT("Chair"))
				|| MeshName.Contains(TEXT("Table")) || MeshName.Contains(TEXT("Awning")) || MeshName.Contains(TEXT("Pool"))) { continue; }
			const FVector L = Comp->Bounds.Origin;
			if (L.X < Rect.Min.X || L.X > Rect.Max.X || L.Y < Rect.Min.Y || L.Y > Rect.Max.Y) { continue; }
			if (L.Z < SrcZ - 20.f || L.Z > SrcZ + 335.f) { continue; }

			const FTransform Rel = Comp->GetComponentTransform() * AnchorInv;
			const FVector RL = Rel.GetLocation();
			const FRotator RR = Rel.GetRotation().Rotator();
			const FVector RS = Rel.GetScale3D();
			FString MatList;
			for (int32 Mi = 0; Mi < Comp->GetNumMaterials(); ++Mi)
			{
				if (Mi > 0) { MatList += TEXT(";"); }
				UMaterialInterface* M = Comp->GetMaterial(Mi);
				MatList += M ? M->GetPathName() : TEXT("-");
			}
			Out += FString::Printf(TEXT("PIECE|%s|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%s"),
				*Comp->GetStaticMesh()->GetPathName(), RL.X, RL.Y, RL.Z, RR.Pitch, RR.Yaw, RR.Roll, RS.X, RS.Y, RS.Z, *MatList);
			Out += LINE_TERMINATOR;
			++Count;
		}
	}
	if (Count < 8) { OutError = FString::Printf(TEXT("Only %d pieces found - room not streamed in?"), Count); return false; }

	IFileManager::Get().MakeDirectory(*TemplatesDir(), true);
	FFileHelper::SaveStringToFile(Out, *(TemplatesDir() / (TemplateName + TEXT(".txt"))));
	UE_LOG(LogWeedShop, Warning, TEXT("RoomStamper: template '%s' opgeslagen (%d stukken)"), *TemplateName, Count);
	return true;
}

bool ARoomStamper::LoadTemplate(const FString& TemplateName, TArray<FStampPiece>& OutPieces)
{
	OutPieces.Reset();
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(TemplatesDir() / (TemplateName + TEXT(".txt"))))) { return false; }
	for (const FString& Line : Lines)
	{
		if (!Line.StartsWith(TEXT("PIECE|"))) { continue; }
		TArray<FString> P;
		Line.ParseIntoArray(P, TEXT("|"));
		if (P.Num() < 5) { continue; }
		FStampPiece Piece;
		Piece.Mesh = LoadObject<UStaticMesh>(nullptr, *P[1]);
		if (!Piece.Mesh) { continue; }
		TArray<FString> L, R, S;
		P[2].ParseIntoArray(L, TEXT(",")); P[3].ParseIntoArray(R, TEXT(",")); P[4].ParseIntoArray(S, TEXT(","));
		if (L.Num() < 3 || R.Num() < 3 || S.Num() < 3) { continue; }
		Piece.RelTM = FTransform(
			FRotator(FCString::Atof(*R[0]), FCString::Atof(*R[1]), FCString::Atof(*R[2])),
			FVector(FCString::Atof(*L[0]), FCString::Atof(*L[1]), FCString::Atof(*L[2])),
			FVector(FCString::Atof(*S[0]), FCString::Atof(*S[1]), FCString::Atof(*S[2])));
		if (P.Num() >= 6)
		{
			TArray<FString> Ms;
			P[5].ParseIntoArray(Ms, TEXT(";"));
			for (const FString& Mp : Ms)
			{
				Piece.Mats.Add((Mp != TEXT("-")) ? LoadObject<UMaterialInterface>(nullptr, *Mp) : nullptr);
			}
		}
		OutPieces.Add(Piece);
	}
	return OutPieces.Num() > 0;
}

bool ARoomStamper::BeginStamp(const FString& TemplateName)
{
	CancelStamp();
	if (!LoadTemplate(TemplateName, Pieces)) { return false; }

	// Preview-componenten (geen collision; volgen het anker live).
	for (const FStampPiece& Piece : Pieces)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetStaticMesh(Piece.Mesh);
		C->SetRelativeTransform(Piece.RelTM);
		for (int32 Mi = 0; Mi < Piece.Mats.Num(); ++Mi)
		{
			if (Piece.Mats[Mi]) { C->SetMaterial(Mi, Piece.Mats[Mi]); }
		}
		PreviewComps.Add(C);
	}
	// Kamer-zwaartepunt (relatief aan het anker): bepaalt de van-je-af-orientatie bij plaatsen.
	CentroidRel = FVector::ZeroVector;
	for (const FStampPiece& Piece : Pieces) { CentroidRel += Piece.RelTM.GetLocation(); }
	CentroidRel /= FMath::Max(1, Pieces.Num());
	CentroidRel.Z = 0.f;

	ActiveTemplate = TemplateName;
	UserYaw = 0.f;
	bStamping = true;
	UWeedToast::NotifyPawn(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr,
		-1, 4.f, FColor::Cyan, TEXT("STAMP: aim (door snaps to door!) - R rotate, LMB place, RMB cancel"));
	return true;
}

void ARoomStamper::CancelStamp()
{
	for (UStaticMeshComponent* C : PreviewComps) { if (C) { C->DestroyComponent(); } }
	PreviewComps.Reset();
	bStamping = false;
}

FTransform ARoomStamper::ComputeAnchor() const
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!PC || !P) { return FTransform::Identity; }

	FVector ViewLoc; FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	FHitResult Hit;
	FCollisionQueryParams QP(SCENE_QUERY_STAT(RoomStampTrace), false);
	QP.AddIgnoredActor(P);
	QP.AddIgnoredActor(const_cast<ARoomStamper*>(this));
	const bool bHit = W->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + ViewRot.Vector() * 3000.f, ECC_Visibility, QP);

	// DEUR-OP-DEUR: mik je op (de buurt van) een deurframe, dan snapt het anker er exact op.
	if (bHit && Hit.GetComponent())
	{
		UStaticMeshComponent* HitSMC = Cast<UStaticMeshComponent>(Hit.GetComponent());
		// ALLEEN op VOORDEUREN snappen: een frame telt alleen mee als er een SM_Door_Apartment02-blad
		// naast hangt (badkamer/binnendeuren dus niet). Frames binnen 220 van het raakpunt.
		TArray<FTransform> CandFrames; TArray<float> CandDist; TArray<FVector> NearLeaves;
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			if (!IsValid(*It) || *It == this) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				const FString MN = Comp->GetStaticMesh()->GetName();
				const FVector CL = Comp->GetComponentLocation();
				if (MN.Contains(TEXT("DoorFrame")))
				{
					const float D = FVector::Dist(CL, Hit.ImpactPoint);
					if (D < 220.f) { CandFrames.Add(Comp->GetComponentTransform()); CandDist.Add(D); }
				}
				else if (MN.Contains(TEXT("Door_Apartment02")) && FVector::Dist2D(CL, Hit.ImpactPoint) < 600.f)
				{
					NearLeaves.Add(CL);
				}
			}
		}
		FTransform BestFrame; float BestD = 220.f; bool bFrame = false;
		for (int32 Ci = 0; Ci < CandFrames.Num(); ++Ci)
		{
			const FVector FL = CandFrames[Ci].GetLocation();
			bool bApt = false;
			for (const FVector& LV : NearLeaves)
			{
				if (FMath::Abs(LV.Z - FL.Z) < 200.f && FVector::Dist2D(LV, FL) < 200.f) { bApt = true; break; }
			}
			if (bApt && CandDist[Ci] < BestD) { BestD = CandDist[Ci]; BestFrame = CandFrames[Ci]; bFrame = true; }
		}
		if (bFrame)
		{
			FVector L = BestFrame.GetLocation();
			L.Z = 480.f + 350.f * FMath::RoundToFloat((L.Z - 480.f) / 350.f); // verdieping-grid
			// Standaard de kant VAN DE SPELER AF: kies frameYaw of frameYaw+180 zodat het kamer-
			// zwaartepunt van je af wijst; R flipt alsnog 180 als je 'm toch andersom wil.
			const float FY = BestFrame.GetRotation().Rotator().Yaw;
			const FVector ToPawn = (P->GetActorLocation() - L).GetSafeNormal2D();
			float BaseYaw = FY;
			{
				const FVector C0 = FRotator(0.f, FY, 0.f).RotateVector(CentroidRel).GetSafeNormal2D();
				if (FVector::DotProduct(C0, ToPawn) > 0.f) { BaseYaw = FY + 180.f; }
			}
			const_cast<ARoomStamper*>(this)->bSnappedToDoor = true;
			// Altijd GELIJK met de deur: alleen 0/180 toegestaan - 90-graden resten uit grid-modus vervallen.
			const float Flip = FMath::Fmod(FMath::GridSnap(UserYaw, 180.f), 360.f);
			return FTransform(FRotator(0.f, BaseYaw + Flip, 0.f), L);
		}
	}
	const_cast<ARoomStamper*>(this)->bSnappedToDoor = false;

	// GRID-modus: raakpunt (of 12m voor je) gesnapt op 50cm + verdieping van de speler.
	FVector Base = bHit ? Hit.ImpactPoint : (ViewLoc + ViewRot.Vector() * 1200.f);
	Base.X = FMath::GridSnap(Base.X, 50.f);
	Base.Y = FMath::GridSnap(Base.Y, 50.f);
	const float Feet = P->GetActorLocation().Z - 88.f;
	Base.Z = (FMath::Abs(Feet - 50.f) < 215.f) ? 50.f : 480.f + 350.f * FMath::RoundToFloat((Feet - 480.f) / 350.f);
	// VAN JE AF: draai het anker zo dat het kamer-zwaartepunt in je kijkrichting ligt (de kamer
	// strekt zich voor je uit, niet om/achter je heen). R draait alsnog in 90-stappen.
	const float CentroidYaw = FMath::RadiansToDegrees(FMath::Atan2(CentroidRel.Y, CentroidRel.X));
	const float BaseYaw = FMath::GridSnap(ViewRot.Yaw - CentroidYaw, 90.f);
	return FTransform(FRotator(0.f, BaseYaw + UserYaw, 0.f), Base);
}

void ARoomStamper::UpdatePreview()
{
	SetActorTransform(CurrentAnchor);
}

void ARoomStamper::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bStamping) { return; }
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) { return; }

	CurrentAnchor = ComputeAnchor();
	UpdatePreview();

	const bool bRot = PC->IsInputKeyDown(EKeys::R);
	if (bRot && !bRotKeyWas) { UserYaw = FMath::Fmod(UserYaw + (bSnappedToDoor ? 180.f : 90.f), 360.f); }
	bRotKeyWas = bRot;

	const bool bPlace = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (bPlace && !bPlaceKeyWas) { PlaceStamp(); }
	bPlaceKeyWas = bPlace;

	const bool bCancel = PC->IsInputKeyDown(EKeys::RightMouseButton);
	if (bCancel && !bCancelKeyWas)
	{
		CancelStamp();
		UWeedToast::NotifyPawn(PC->GetPawn(), -1, 2.f, FColor::Orange, TEXT("Stamp cancelled"));
	}
	bCancelKeyWas = bCancel;
}

void ARoomStamper::PlaceStamp()
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector AL = CurrentAnchor.GetLocation();
	const float AY = CurrentAnchor.GetRotation().Rotator().Yaw;
	const FString StampId = FString::Printf(TEXT("STAMP_%d_%d_%d"), FMath::RoundToInt(AL.X), FMath::RoundToInt(AL.Y), FMath::RoundToInt(AY));
	FString BakeOut;
	int32 Placed = 0;
	for (const FStampPiece& Piece : Pieces)
	{
		const FTransform NewTM = Piece.RelTM * CurrentAnchor;
		AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
		if (!SMA) { continue; }
		SMA->Tags.Add(FName(*StampId)); // voor undo/verwijderen
		if (UStaticMeshComponent* C = SMA->GetStaticMeshComponent())
		{
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(Piece.Mesh);
			C->SetCanEverAffectNavigation(false);
			for (int32 Mi = 0; Mi < Piece.Mats.Num(); ++Mi)
			{
				if (Piece.Mats[Mi]) { C->SetMaterial(Mi, Piece.Mats[Mi]); }
			}
		}
		const FVector SL = NewTM.GetLocation();
		const FRotator SR = NewTM.GetRotation().Rotator();
		const FVector SS = NewTM.GetScale3D();
		FString MatList;
		for (int32 Mi = 0; Mi < Piece.Mats.Num(); ++Mi)
		{
			if (Mi > 0) { MatList += TEXT(";"); }
			MatList += Piece.Mats[Mi] ? Piece.Mats[Mi]->GetPathName() : TEXT("-");
		}
		BakeOut += FString::Printf(TEXT("SPAWN|%s|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%s"),
			*Piece.Mesh->GetPathName(), SL.X, SL.Y, SL.Z, SR.Pitch, SR.Yaw, SR.Roll, SS.X, SS.Y, SS.Z, *MatList);
		BakeOut += LINE_TERMINATOR;
		++Placed;
	}

	// Persistentie: stempel-regel (herbouw per sessie tot de bake) + bake-export.
	const FString StampLine = FString::Printf(TEXT("%s|%.1f,%.1f,%.1f|%.1f"), *ActiveTemplate, AL.X, AL.Y, AL.Z, AY) + LINE_TERMINATOR;
	FFileHelper::SaveStringToFile(StampLine, *(FPaths::ProjectSavedDir() / TEXT("RoomStamps.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("JOB|%s"), *StampId) + LINE_TERMINATOR + BakeOut,
		*(FPaths::ProjectSavedDir() / TEXT("RoomBake.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);

	UE_LOG(LogWeedShop, Warning, TEXT("RoomStamper: '%s' geplaatst op (%.0f, %.0f, %.0f) yaw %.0f - %d stukken"), *ActiveTemplate, AL.X, AL.Y, AL.Z, AY, Placed);
	UWeedToast::NotifyPawn(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr,
		-1, 2.5f, FColor::Green, FString::Printf(TEXT("Room stamped! (%d pieces) - LMB again to stamp more, RMB to stop"), Placed));
}

FString ARoomStamper::StampIdFromLine(const FString& Line)
{
	TArray<FString> P;
	Line.ParseIntoArray(P, TEXT("|"));
	if (P.Num() < 3) { return FString(); }
	TArray<FString> Lp;
	P[1].ParseIntoArray(Lp, TEXT(","));
	if (Lp.Num() < 3) { return FString(); }
	return FString::Printf(TEXT("STAMP_%d_%d_%d"),
		FMath::RoundToInt(FCString::Atof(*Lp[0])),
		FMath::RoundToInt(FCString::Atof(*Lp[1])),
		FMath::RoundToInt(FCString::Atof(*P[2])));
}

TArray<FString> ARoomStamper::ListPlacedStamps(UWorld* W)
{
	TSet<FString> Baked;
	{
		TArray<FString> BL;
		FFileHelper::LoadFileToStringArray(BL, *(FPaths::ProjectSavedDir() / TEXT("BakedJobs.txt")));
		for (const FString& B : BL) { Baked.Add(B.TrimStartAndEnd()); }
	}
	TArray<FString> Lines, Out;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("RoomStamps.txt")));
	for (const FString& L : Lines)
	{
		const FString T = L.TrimStartAndEnd();
		if (T.IsEmpty()) { continue; }
		const FString Id = StampIdFromLine(T);
		if (Id.IsEmpty() || Baked.Contains(Id)) { continue; }
		Out.Add(T);
	}
	return Out;
}

bool ARoomStamper::RemoveStamp(UWorld* W, const FString& StampLine)
{
	const FString Id = StampIdFromLine(StampLine);
	if (Id.IsEmpty() || !W) { return false; }

	// 1) Alle actors met deze stamp-tag weghalen (geplaatst of sessie-herbouwd).
	int32 Killed = 0;
	const FName Tag(*Id);
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		if (IsValid(*It) && It->ActorHasTag(Tag)) { It->Destroy(); ++Killed; }
	}

	// 2) Regel uit RoomStamps.txt (anders komt hij volgende sessie terug).
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("RoomStamps.txt");
		TArray<FString> Lines, Keep;
		FFileHelper::LoadFileToStringArray(Lines, *Path);
		for (const FString& L : Lines)
		{
			const FString T = L.TrimStartAndEnd();
			if (!T.IsEmpty() && StampIdFromLine(T) == Id) { continue; }
			Keep.Add(L);
		}
		FFileHelper::SaveStringToFile(FString::Join(Keep, LINE_TERMINATOR) + LINE_TERMINATOR, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// 3) Bake-blok uit RoomBake.txt (JOB|id + de SPAWN-regels erna, tot het volgende JOB-blok).
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("RoomBake.txt");
		TArray<FString> Lines, Keep;
		FFileHelper::LoadFileToStringArray(Lines, *Path);
		bool bSkipping = false;
		for (const FString& L : Lines)
		{
			const FString T = L.TrimStartAndEnd();
			if (T == FString::Printf(TEXT("JOB|%s"), *Id)) { bSkipping = true; continue; }
			if (bSkipping && T.StartsWith(TEXT("JOB|"))) { bSkipping = false; }
			if (bSkipping && T.StartsWith(TEXT("SPAWN|"))) { continue; }
			Keep.Add(L);
		}
		FFileHelper::SaveStringToFile(FString::Join(Keep, LINE_TERMINATOR) + LINE_TERMINATOR, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	UE_LOG(LogWeedShop, Warning, TEXT("RoomStamper: stempel %s verwijderd (%d actors)"), *Id, Killed);
	return true;
}

bool ARoomStamper::UndoLastStamp(UWorld* W, FString& OutInfo)
{
	const TArray<FString> Placed = ListPlacedStamps(W);
	if (Placed.Num() == 0) { OutInfo = TEXT("No stamps to undo"); return false; }
	const FString Last = Placed.Last();
	TArray<FString> P;
	Last.ParseIntoArray(P, TEXT("|"));
	OutInfo = P.Num() > 0 ? P[0] : Last;
	return RemoveStamp(W, Last);
}

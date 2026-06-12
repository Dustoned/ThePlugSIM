#include "World/RoomStamper.h"
#include "World/CityDoor.h"
#include "Components/InstancedStaticMeshComponent.h"

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
	TArray<TPair<FVector, FString>> LeafLines; // deurbladen apart: max 1 per deuropening
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		// Runtime-deuren overslaan: hun blad is de conversie van het (verborgen) geparkeerde
		// origineel dat hieronder al meegenomen wordt - anders stapelen de deurbladen zich op.
		if (A->IsA(ACityDoor::StaticClass())) { continue; }
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
			// Parallax interior-kaartjes van ramen (SM_..._Window_..._Interior) niet meenemen: ze zijn
			// eenzijdig naar buiten gericht en blokkeren het echte doorkijkje de kamer in.
			if (MeshName.Contains(TEXT("Window")) && MeshName.EndsWith(TEXT("_Interior"))) { continue; }
			// Het voordeur-BLAD niet meenemen (het doelframe heeft al een werkende deur) - maar
			// muurdelen met een deuropening in de naam (SM_InteriorWall_3m_Door01 e.d.) horen er WEL bij.
			if (MeshName.Contains(TEXT("Door")) && !MeshName.Contains(TEXT("DoorFrame"))
				&& !MeshName.Contains(TEXT("Wall"))
				&& FVector::Dist2D(L, AnchorTM.GetLocation()) < 170.f) { continue; }

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
			FString PieceLine = FString::Printf(TEXT("PIECE|%s|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%s"),
				*Comp->GetStaticMesh()->GetPathName(), RL.X, RL.Y, RL.Z, RR.Pitch, RR.Yaw, RR.Roll, RS.X, RS.Y, RS.Z, *MatList);
			PieceLine += LINE_TERMINATOR;
			const bool bLeaf = MeshName.Contains(TEXT("Door")) && !MeshName.Contains(TEXT("DoorFrame")) && !MeshName.Contains(TEXT("Wall"));
			if (bLeaf) { LeafLines.Add(TPair<FVector, FString>(L, PieceLine)); }
			else { Out += PieceLine; ++Count; }
		}
	}
	// Maximaal EEN deurblad per deuropening: kopieen (top-ups, oude vullingen, geparkeerde dubbelen)
	// op dezelfde plek vallen weg - dit was de bron van de gestapelde badkamerdeuren.
	TArray<FVector> LeafKept;
	for (const TPair<FVector, FString>& Lf : LeafLines)
	{
		bool bDup = false;
		for (const FVector& K : LeafKept)
		{
			if (FVector::Dist2D(K, Lf.Key) < 150.f && FMath::Abs(K.Z - Lf.Key.Z) < 200.f) { bDup = true; break; }
		}
		if (bDup) { continue; }
		LeafKept.Add(Lf.Key);
		Out += Lf.Value;
		++Count;
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
		// Parallax interior-kaartjes ook uit oude templates filteren.
		if (P[1].Contains(TEXT("Window")) && P[1].EndsWith(TEXT("_Interior"))) { continue; }
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
	bMirrored = false;
	bStamping = true;
	UWeedToast::NotifyPawn(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr,
		-1, 4.f, FColor::Cyan, TEXT("STAMP: aim (door snaps to door!) - R rotate, T mirror, LMB place, RMB cancel"));
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

	// Bij spiegelen klapt het kamer-lichaam naar de andere kant van het anker (Y-negatie).
	const FVector EffCentroid = bMirrored ? FVector(CentroidRel.X, -CentroidRel.Y, CentroidRel.Z) : CentroidRel;

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
				const FVector C0 = FRotator(0.f, FY, 0.f).RotateVector(EffCentroid).GetSafeNormal2D();
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
	const float CentroidYaw = FMath::RadiansToDegrees(FMath::Atan2(EffCentroid.Y, EffCentroid.X));
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

	const bool bMir = PC->IsInputKeyDown(EKeys::T);
	if (bMir && !bMirrorKeyWas)
	{
		bMirrored = !bMirrored;
		for (int32 Pi = 0; Pi < PreviewComps.Num() && Pi < Pieces.Num(); ++Pi)
		{
			if (PreviewComps[Pi]) { PreviewComps[Pi]->SetRelativeTransform(bMirrored ? MirrorPieceTM(Pieces[Pi]) : Pieces[Pi].RelTM); }
		}
		UWeedToast::NotifyPawn(PC->GetPawn(), -1, 1.5f, FColor::Cyan, bMirrored ? TEXT("Mirrored (T to undo)") : TEXT("Normal"));
	}
	bMirrorKeyWas = bMir;

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
	// Niet twee keer op precies dezelfde plek stempelen (dubbelklik) - dat stapelt twee kamers op elkaar.
	{
		TArray<FString> Existing;
		FFileHelper::LoadFileToStringArray(Existing, *(FPaths::ProjectSavedDir() / TEXT("RoomStamps.txt")));
		for (const FString& Ex : Existing)
		{
			if (StampIdFromLine(Ex.TrimStartAndEnd()) == StampId)
			{
				UWeedToast::NotifyPawn(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr,
					-1, 2.5f, FColor::Orange, TEXT("Already stamped here - move or rotate first"));
				return;
			}
		}
	}
	FString BakeOut;
	int32 Placed = 0;
	for (const FStampPiece& Piece : Pieces)
	{
		const FTransform NewTM = (bMirrored ? MirrorPieceTM(Piece) : Piece.RelTM) * CurrentAnchor;
		AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
		if (!SMA) { continue; }
		SMA->Tags.Add(FName(*StampId)); // voor undo/verwijderen
		if (bMirrored)
		{
			const FString MNm = Piece.Mesh->GetName();
			if (MNm.Contains(TEXT("Door")) && !MNm.Contains(TEXT("DoorFrame")) && !MNm.Contains(TEXT("Wall")))
			{
				SMA->Tags.Add(TEXT("MirroredDoor")); // converter: standaard-zwaai omklappen
			}
		}
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
	const FString StampLine = FString::Printf(TEXT("%s|%.1f,%.1f,%.1f|%.1f%s"), *ActiveTemplate, AL.X, AL.Y, AL.Z, AY,
		bMirrored ? TEXT("|M") : TEXT("")) + LINE_TERMINATOR;
	FFileHelper::SaveStringToFile(StampLine, *(FPaths::ProjectSavedDir() / TEXT("RoomStamps.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("JOB|%s"), *StampId) + LINE_TERMINATOR + BakeOut,
		*(FPaths::ProjectSavedDir() / TEXT("RoomBake.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);

	ApplyWindowFix(W, ActiveTemplate, CurrentAnchor, bMirrored);

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

	// 1b) Werkende deuren (ACityDoor) die uit stempel-bladen geconverteerd zijn maar de tag missen
	// (oudere stempels): alles binnen het kamer-volume opruimen, behalve de eigen voordeur van het
	// gebouw (vlak bij het anker).
	{
		TArray<FString> P;
		StampLine.ParseIntoArray(P, TEXT("|"));
		TArray<FStampPiece> TplPieces;
		TArray<FString> Lp;
		if (P.Num() >= 3) { P[1].ParseIntoArray(Lp, TEXT(",")); }
		if (Lp.Num() >= 3 && LoadTemplate(P[0], TplPieces))
		{
			const FVector AL(FCString::Atof(*Lp[0]), FCString::Atof(*Lp[1]), FCString::Atof(*Lp[2]));
			const float AY = FCString::Atof(*P[2]);
			const bool bMirror = P.Num() >= 4 && P[3].TrimStartAndEnd() == TEXT("M");
			const FTransform Anchor(FRotator(0.f, AY, 0.f), AL);
			FBox RoomBox(ForceInit);
			for (const FStampPiece& Piece : TplPieces)
			{
				RoomBox += ((bMirror ? MirrorPieceTM(Piece) : Piece.RelTM) * Anchor).GetLocation();
			}
			RoomBox = RoomBox.ExpandBy(FVector(60.f, 60.f, 60.f));
			for (TActorIterator<ACityDoor> It(W); It; ++It)
			{
				if (!IsValid(*It)) { continue; }
				const FVector DL = It->GetActorLocation();
				if (!RoomBox.IsInside(DL)) { continue; }
				if (FVector::Dist2D(DL, AL) < 170.f) { continue; } // voordeur van het gebouw zelf
				It->Destroy(); ++Killed;
			}
		}
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

FTransform ARoomStamper::MirrorRelTM(const FTransform& In)
{
	const FVector L = In.GetLocation();
	const FRotator R = In.GetRotation().Rotator();
	const FVector S = In.GetScale3D();
	return FTransform(FRotator(R.Pitch, -R.Yaw, -R.Roll), FVector(L.X, -L.Y, L.Z), FVector(S.X, -S.Y, S.Z));
}

FTransform ARoomStamper::MirrorPieceTM(const FStampPiece& Piece)
{
	const FString MN = Piece.Mesh ? Piece.Mesh->GetName() : FString();
	// Alleen deurBLADEN niet met negatieve scale spiegelen: de deur-converter rekent met normale
	// scale. Een plat blad gespiegeld over het ankervlak is exact hetzelfde als dat blad met yaw
	// 180-theta op de gespiegelde plek met gewone scale. KOZIJNEN spiegelen wel echt mee: hun
	// aanslag/sponning zit aan een kant - geroteerd ipv gespiegeld zit de deur te diep in het gat.
	const bool bLeaf = MN.Contains(TEXT("Door")) && !MN.Contains(TEXT("DoorFrame")) && !MN.Contains(TEXT("Wall"));
	if (!bLeaf) { return MirrorRelTM(Piece.RelTM); }
	const FVector L = Piece.RelTM.GetLocation();
	const FRotator R = Piece.RelTM.GetRotation().Rotator();
	return FTransform(FRotator(R.Pitch, 180.f - R.Yaw, R.Roll), FVector(L.X, -L.Y, L.Z), Piece.RelTM.GetScale3D());
}

void ARoomStamper::ApplyWindowFix(UWorld* W, const FString& TemplateName, const FTransform& Anchor, bool bMirror)
{
	if (!W) { return; }
	TArray<FStampPiece> Tpl;
	if (!LoadTemplate(TemplateName, Tpl)) { return; }

	// Template-ramen: wereldpositie + yaw (vlak-richting).
	struct FTplWin { FVector Pos; float Yaw; };
	FBox Foot(ForceInit);
	TArray<FTplWin> TplWindows;
	for (const FStampPiece& Piece : Tpl)
	{
		const FTransform TM = (bMirror ? MirrorPieceTM(Piece) : Piece.RelTM) * Anchor;
		Foot += TM.GetLocation();
		const FString MN = Piece.Mesh ? Piece.Mesh->GetName() : FString();
		if (MN.Contains(TEXT("Window")) || MN.Contains(TEXT("Glass")) || MN.Contains(TEXT("BalconyDoor")))
		{
			TplWindows.Add({ TM.GetLocation(), (float)TM.GetRotation().Rotator().Yaw });
		}
	}
	const FBox FootTight = Foot.ExpandBy(FVector(60.f, 60.f, 0.f));
	const FBox FootWide  = Foot.ExpandBy(FVector(380.f, 380.f, 0.f));
	const float ZMin = Anchor.GetLocation().Z - 30.f;
	const float ZMax = Anchor.GetLocation().Z + 360.f;
	const FVector Center = Foot.GetCenter();

	auto MatchesTplWindow = [&TplWindows](const FVector& L) -> int32
	{
		for (int32 Ti = 0; Ti < TplWindows.Num(); ++Ti)
		{
			const FTplWin& TW = TplWindows[Ti];
			if (FMath::Abs(TW.Pos.Z - L.Z) > 300.f) { continue; }
			const FVector D = L - TW.Pos;
			const float A = FMath::Abs(FVector::DotProduct(D, FRotator(0.f, TW.Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f))));
			const float B = FMath::Abs(FVector::DotProduct(D, FRotator(0.f, TW.Yaw, 0.f).RotateVector(FVector(0.f, 1.f, 0.f))));
			if ((A < 130.f && B < 380.f) || (B < 130.f && A < 380.f)) { return Ti; }
		}
		return INDEX_NONE;
	};

	// PASS 1: gevel-kandidaten verzamelen (nog niets verbergen - eerst de diepte-offset meten).
	struct FFacadeHit { UStaticMeshComponent* Comp; UInstancedStaticMeshComponent* ISM; int32 Inst; int32 TWi; FVector Pos; };
	TArray<FFacadeHit> Matches;
	TArray<UStaticMeshComponent*> Unmatched; // strak binnen de stempel, geen match -> donor-herstel
	int32 Cands = 0;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A) || A->IsA(ACityDoor::StaticClass())) { continue; }
		bool bStampActor = false;
		for (const FName& Tg : A->Tags) { if (Tg.ToString().StartsWith(TEXT("STAMP_"))) { bStampActor = true; break; } }
		if (bStampActor) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh() || !Comp->IsVisible()) { continue; }
			const FString MN = Comp->GetStaticMesh()->GetName();
			if (!(MN.Contains(TEXT("Window")) || MN.Contains(TEXT("Glass")))) { continue; }

			if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Comp))
			{
				for (int32 Ii = 0; Ii < ISM->GetInstanceCount(); ++Ii)
				{
					FTransform ITM;
					if (!ISM->GetInstanceTransform(Ii, ITM, true)) { continue; }
					const FVector IL = ITM.GetLocation();
					if (IL.Z < ZMin || IL.Z > ZMax || !FootWide.IsInsideXY(IL)) { continue; }
					++Cands;
					const int32 TWi = MatchesTplWindow(IL);
					if (TWi != INDEX_NONE) { Matches.Add({ nullptr, ISM, Ii, TWi, IL }); }
				}
				continue;
			}

			const FVector L = Comp->Bounds.Origin;
			if (L.Z < ZMin || L.Z > ZMax || !FootWide.IsInsideXY(L)) { continue; }
			++Cands;
			const int32 TWi = MatchesTplWindow(L);
			if (TWi != INDEX_NONE) { Matches.Add({ Comp, nullptr, INDEX_NONE, TWi, L }); }
			else if (FootTight.IsInsideXY(L)) { Unmatched.Add(Comp); }
		}
	}

	// PASS 2: HELE STEMPEL diepte-corrigeren. De kamer-muur kan een paar cm voor of achter de echte
	// gevel liggen (uitstekende muurvin / kozijnen door de dichte muur). Meet via de eerste match de
	// offset langs de raam-normaal en schuif alle stempel-actors zo dat de kamermuur 6cm ACHTER de
	// gevel ligt. Idempotent per sessie: respawn is vers, daarna een keer schuiven.
	const FString StampId = FString::Printf(TEXT("STAMP_%d_%d_%d"),
		FMath::RoundToInt(Anchor.GetLocation().X), FMath::RoundToInt(Anchor.GetLocation().Y),
		FMath::RoundToInt(Anchor.GetRotation().Rotator().Yaw));
	const FName StampTag(*StampId);

	FVector StampShift = FVector::ZeroVector;
	if (Matches.Num() > 0)
	{
		const FFacadeHit& M0 = Matches[0];
		const FTplWin& TW = TplWindows[M0.TWi];
		FVector N = FRotator(0.f, TW.Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
		FVector Out = (FVector::DotProduct(N, FVector(TW.Pos.X - Center.X, TW.Pos.Y - Center.Y, 0.f)) >= 0.f) ? N : -N;
		// Meet tegen de ECHTE huidige positie van ons raamsegment (niet de template-positie):
		// dan is de shift idempotent en kan de fix veilig opnieuw draaien na late streaming.
		FVector WallRef = TW.Pos;
		for (TActorIterator<AActor> AIt(W); AIt; ++AIt)
		{
			if (!IsValid(*AIt) || !AIt->ActorHasTag(StampTag)) { continue; }
			AStaticMeshActor* WSMA = Cast<AStaticMeshActor>(*AIt);
			UStaticMeshComponent* WC = WSMA ? WSMA->GetStaticMeshComponent() : nullptr;
			if (!WC || !WC->GetStaticMesh() || !WC->GetStaticMesh()->GetName().Contains(TEXT("Window"))) { continue; }
			const FVector WL = WSMA->GetActorLocation();
			if (FVector::Dist2D(WL, TW.Pos) < 200.f && FMath::Abs(WL.Z - TW.Pos.Z) < 300.f) { WallRef = WL; break; }
		}
		const float DdOut = FVector::DotProduct(M0.Pos - WallRef, Out); // gevel t.o.v. kamer-muur (buitenwaarts +)
		const float Shift = FMath::Clamp(DdOut - 6.f, -60.f, 60.f);
		if (FMath::Abs(Shift) > 1.f)
		{
			StampShift = Out * Shift;
			// ALLEEN de buitenmuur-laag verschuiven (muurdelen + ramen op het raam-vlak), NIET de
			// hele kamer: de entree is op de gang-deur gesnapt en moet daar exact blijven.
			for (TActorIterator<AActor> It(W); It; ++It)
			{
				if (!IsValid(*It) || !It->ActorHasTag(StampTag)) { continue; }
				const float Dd = FVector::DotProduct(It->GetActorLocation() - WallRef, Out);
				if (Dd < -40.f) { continue; } // binnenin de kamer: laten staan
				It->AddActorWorldOffset(StampShift);
			}
		}
	}

	// Helder glas-materiaal van het template-raam (voor het echt maken van gevel-ramen).
	TArray<TObjectPtr<UMaterialInterface>> ClearGlass;
	for (const FStampPiece& Piece : Tpl)
	{
		const FString PMN = Piece.Mesh ? Piece.Mesh->GetName() : FString();
		if (PMN.Contains(TEXT("Glass")) && Piece.Mats.Num() > 0) { ClearGlass = Piece.Mats; break; }
	}

	// PASS 3: gematchte gevel-ramen VOLLEDIG verbergen. Ons eigen raamsegment (een compleet
	// muurstuk met het raam erin) zit er vlak achter en neemt de opening over - gevel-kozijn en
	// eigen kozijn samen zichtbaar (net verschoven) was een rommel van dubbele ramen.
	// (GEEN eigen stukken verbergen: dat slaat een gat in de buitenmuur van de kamer.)
	int32 Hidden = 0;
	for (const FFacadeHit& M : Matches)
	{
		if (M.ISM)
		{
			FTransform ITM;
			if (M.ISM->GetInstanceTransform(M.Inst, ITM, true))
			{
				ITM.SetScale3D(FVector(0.001f));
				M.ISM->UpdateInstanceTransform(M.Inst, ITM, true, true, true);
				++Hidden;
			}
			continue;
		}
		if (M.Comp) { M.Comp->SetVisibility(false, true); ++Hidden; }
	}

	// PASS 4: niet-gematchte gevel-ramen binnen de stempel ECHT maken in plaats van fake: het
	// gevel-raam staat op de goede plek (hoort bij dit gebouw), alleen de fake-3D view moet weg.
	// Parallax interior-kaartjes verbergen + het heldere glas van het template-raam overnemen;
	// de kamer-muur erachter is van binnenuit gewoon dicht maar van buiten onzichtbaar (single-
	// sided), dus je kijkt er echt de kamer mee in.
	int32 Synced = 0;
	for (UStaticMeshComponent* Comp : Unmatched)
	{
		if (!IsValid(Comp) || !Comp->GetStaticMesh()) { continue; }
		const FString CMN = Comp->GetStaticMesh()->GetName();
		if (CMN.EndsWith(TEXT("_Interior")))
		{
			Comp->SetVisibility(false, true); // fake-3D kaartje weg
			++Synced;
			continue;
		}
		if (CMN.Contains(TEXT("Glass")) && ClearGlass.Num() > 0)
		{
			for (int32 Mi = 0; Mi < ClearGlass.Num() && Mi < Comp->GetNumMaterials(); ++Mi)
			{
				if (ClearGlass[Mi]) { Comp->SetMaterial(Mi, ClearGlass[Mi]); }
			}
			++Synced;
		}
	}

	UE_LOG(LogWeedShop, Warning, TEXT("RoomStamper: window-fix '%s' - %d kandidaten, %d matches, shift (%.0f, %.0f), %d gevel-ramen verborgen, %d echt gemaakt (%d tpl-ramen)"),
		*TemplateName, Cands, Matches.Num(), StampShift.X, StampShift.Y, Hidden, Synced, TplWindows.Num());
}

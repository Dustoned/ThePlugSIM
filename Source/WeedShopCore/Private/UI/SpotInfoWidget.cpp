#include "UI/SpotInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

TSharedRef<SWidget> USpotInfoWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		B->SetBrush(WeedUI::Rounded(WeedUI::ColPanel(0.9f), 10.f));
		B->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
		Card = B;

		InfoText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColGood(), false, true);
		B->SetContent(InfoText);

		UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(B);
		CS->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f)); // boven-midden
		CS->SetAlignment(FVector2D(0.5f, 0.f));
		CS->SetPosition(FVector2D(0.f, 14.f));
		CS->SetAutoSize(true);
		B->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Super::RebuildWidget();
}

void USpotInfoWidget::SetInfoVisibleSilent(bool bVisible)
{
	bInfoVisible = bVisible;
	if (Card) { Card->SetVisibility(bInfoVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
}

void USpotInfoWidget::ToggleInfo()
{
	bInfoVisible = !bInfoVisible;
	if (Card) { Card->SetVisibility(bInfoVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	// Bij AANzetten: plek ook vastleggen in Saved/MarkedSpots.txt (label F9).
	if (bInfoVisible)
	{
		if (const APawn* P = GetOwningPlayerPawn())
		{
			const FVector L = P->GetActorLocation();
			const float Yaw = GetOwningPlayer() ? GetOwningPlayer()->GetControlRotation().Yaw : 0.f;
			const FString MapPath = GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("?");
			const FString Line = FString::Printf(TEXT("F9 | map=%s | pos=(%.0f, %.0f, %.0f) | yaw=%.0f"), *MapPath, L.X, L.Y, L.Z, Yaw) + TEXT("\n");
			const FString File = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
			FFileHelper::SaveStringToFile(Line, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), FILEWRITE_Append);
		}
	}
}

void USpotInfoWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	if (!bInfoVisible || !InfoText) { return; }
	UpdateAccum += DeltaTime;
	if (UpdateAccum < 0.15f) { return; } // ~7x per seconde is zat
	UpdateAccum = 0.f;

	APawn* P = GetOwningPlayerPawn();
	APlayerController* PC = GetOwningPlayer();
	if (!P || !PC) { return; }

	const FVector L = P->GetActorLocation();
	const float Yaw = PC->GetControlRotation().Yaw;
	FString MapPath = GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("?");
	int32 SlashIdx = INDEX_NONE;
	if (MapPath.FindLastChar(TEXT('/'), SlashIdx)) { MapPath = MapPath.RightChop(SlashIdx + 1); }

	// Waar kijk je naar? (mesh-naam + afstand - precies wat nodig is om iets aan te wijzen voor fixes)
	FString LookAt = TEXT("-");
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	FHitResult Hit;
	FCollisionQueryParams QP(SCENE_QUERY_STAT(SpotInfoTrace), true);
	QP.AddIgnoredActor(P);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, CamLoc + CamRot.Vector() * 2500.f, ECC_Visibility, QP))
	{
		const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Hit.GetComponent());
		const FString MeshName = (SMC && SMC->GetStaticMesh()) ? SMC->GetStaticMesh()->GetName()
			: (Hit.GetActor() ? Hit.GetActor()->GetClass()->GetName() : TEXT("?"));
		LookAt = FString::Printf(TEXT("%s  (%.1fm)"), *MeshName, Hit.Distance / 100.f);
	}
	// ONZICHTBARE BLOCKERS opsporen: BOL-SWEEP (25cm, zo dik als je lijf) op het Pawn-kanaal langs
	// je blik, plus een tweede sweep op heuphoogte recht vooruit - vangt ook smalle blockers die
	// een dunne kijklijn missen. Dichterbij dan wat je ziet = er staat iets onzichtbaars.
	const float VisDist = Hit.bBlockingHit ? Hit.Distance : 2500.f;
	auto TryBlockSweep = [&](const FVector& From, const FVector& Dir) -> bool
	{
		FHitResult BlockHit;
		if (!GetWorld() || !GetWorld()->SweepSingleByChannel(BlockHit, From, From + Dir * 600.f, FQuat::Identity,
			ECC_Pawn, FCollisionShape::MakeSphere(25.f), QP)) { return false; }
		if (BlockHit.Distance >= VisDist - 40.f) { return false; }
		const UStaticMeshComponent* BMC = Cast<UStaticMeshComponent>(BlockHit.GetComponent());
		const FString BName = (BMC && BMC->GetStaticMesh()) ? BMC->GetStaticMesh()->GetName()
			: (BlockHit.GetActor() ? BlockHit.GetActor()->GetClass()->GetName() : TEXT("?"));
		LookAt += FString::Printf(TEXT("\nBLOCK %s  (%.1fm)%s"), *BName, BlockHit.Distance / 100.f,
			(BMC && !BMC->IsVisible()) ? TEXT(" [invisible]") : TEXT(""));
		return true;
	};
	if (!TryBlockSweep(CamLoc, CamRot.Vector()))
	{
		// Exact wat je lijf doet: CAPSULE-sweep op OBJECT-types (sommige blockers reageren wel
		// op object-collisie maar negeren trace-kanalen, dan vond de gewone sweep niks).
		FVector Flat = CamRot.Vector(); Flat.Z = 0.f; Flat.Normalize();
		FHitResult CapHit;
		FCollisionObjectQueryParams ObjQ;
		ObjQ.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjQ.AddObjectTypesToQuery(ECC_WorldDynamic);
		const FVector Start = P->GetActorLocation();
		// Niet-blokkerende hits (trigger-bollen e.d.) overslaan en doorzoeken: alleen iets dat
		// pawns ECHT blokkeert telt - anders maskeert een onschuldige overlap het echte blok.
		FCollisionQueryParams QP2 = QP;
		bool bGotBlock = false;
		for (int32 Tries = 0; Tries < 6 && !bGotBlock; ++Tries)
		{
			if (!GetWorld() || !GetWorld()->SweepSingleByObjectType(CapHit, Start, Start + Flat * 220.f, FQuat::Identity, ObjQ,
				FCollisionShape::MakeCapsule(30.f, 80.f), QP2) || !CapHit.GetComponent()) { break; }
			if (CapHit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
				|| CapHit.GetComponent()->GetCollisionEnabled() == ECollisionEnabled::QueryOnly)
			{
				QP2.AddIgnoredComponent(CapHit.GetComponent());
				continue;
			}
			bGotBlock = true;
		}
		if (bGotBlock)
		{
			const UStaticMeshComponent* BMC = Cast<UStaticMeshComponent>(CapHit.GetComponent());
			FString BName;
			if (BMC && BMC->GetStaticMesh()) { BName = BMC->GetStaticMesh()->GetName(); }
			else
			{
				BName = FString::Printf(TEXT("%s/%s"), *CapHit.GetComponent()->GetClass()->GetName(),
					CapHit.GetActor() ? *CapHit.GetActor()->GetName() : TEXT("?"));
			}
			LookAt += FString::Printf(TEXT("\nBLOCK %s  (%.1fm)%s"), *BName, CapHit.Distance / 100.f,
				CapHit.GetComponent()->IsVisible() ? TEXT("") : TEXT(" [invisible]"));
		}
	}

	InfoText->SetText(FText::FromString(FString::Printf(
		TEXT("MAP %s\nPOS %.0f, %.0f, %.0f   YAW %.0f\nLOOK %s\n(F9 logged this spot to MarkedSpots.txt)"),
		*MapPath, L.X, L.Y, L.Z, Yaw, *LookAt)));
}

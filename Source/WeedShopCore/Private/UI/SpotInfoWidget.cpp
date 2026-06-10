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
		B->SetBrush(WeedUI::Rounded(FLinearColor(0.02f, 0.03f, 0.05f, 0.88f), 10.f));
		B->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
		Card = B;

		InfoText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.6f, 1.f, 0.8f), false, true);
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
			const FString MapPath = GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("?");
			const FString Line = FString::Printf(TEXT("F9 | map=%s | pos=(%.0f, %.0f, %.0f)"), *MapPath, L.X, L.Y, L.Z) + TEXT("\n");
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

	InfoText->SetText(FText::FromString(FString::Printf(
		TEXT("MAP %s\nPOS %.0f, %.0f, %.0f   YAW %.0f\nLOOK %s\n(F9 logged this spot to MarkedSpots.txt)"),
		*MapPath, L.X, L.Y, L.Z, Yaw, *LookAt)));
}

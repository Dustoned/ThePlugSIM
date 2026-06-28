#include "UI/WardrobeWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/PlayerNpcActions.h"
#include "Customization/OutfitCatalog.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"

void UWardrobeWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* WrdBtn(UWidgetTree* Tree, const FLinearColor& Col, float Radius, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, Radius);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, Radius);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, Radius);
		S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(S);
		return B;
	}

	// Body-mesh per skin (zelfde mapping als AThePlugSIMCharacter::ApplySkinMesh).
	const TCHAR* BodyMeshPath(uint8 Skin)
	{
		switch (Skin)
		{
		case 1:  return TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
		case 2:  return WeedOutfit::FullBodyPaths[0];
		case 3:  return TEXT("/Game/Gamer_Girl/Mesh/SK_GamerGirl_01.SK_GamerGirl_01");                       // Gamer_Girl
		case 4:  return TEXT("/Game/SchoolGirl/Mesh/SK_SchoolGirl_CasualOutfit.SK_SchoolGirl_CasualOutfit"); // School_Girl
		case 5:  return WeedOutfit::MaleBodyPath;        // male (Tony basis-body + kleren)
		case 6:  return WeedOutfit::CitizenManBodyPath;  // Citizen_man (headless basis-body)
		default: return TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
		}
	}
}

TSharedRef<SWidget> UWardrobeWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UWardrobeWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WardrobeCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.04f, 0.06f, 0.99f), 20.f));
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(1000.f, 730.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop-balk.
	UBorder* Head = WidgetTree->ConstructWidget<UBorder>();
	Head->SetBrush(WeedUI::Rounded(FLinearColor(0.13f, 0.10f, 0.16f, 1.f), 10.f));
	Head->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Head->SetContent(HeadRow);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("WARDROBE"), 18, FLinearColor(0.85f, 0.6f, 1.f), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* CloseB = WrdBtn(WidgetTree, FLinearColor(0.4f, 0.2f, 0.2f), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseWardrobe(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 12, FLinearColor::White, true));
	HeadRow->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UBorder* ScreenB = WidgetTree->ConstructWidget<UBorder>();
	ScreenB->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.06f, 0.10f, 1.f), 12.f));
	ScreenB->SetPadding(FMargin(14.f));
	UVerticalBoxSlot* ScS = Outer->AddChildToVerticalBox(ScreenB);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Links: live studio-preview (slepen = draaien, scrollen = zoomen). Rechts: de outfit-keuzes.
	UHorizontalBox* Split = WidgetTree->ConstructWidget<UHorizontalBox>();
	ScreenB->SetContent(Split);

	UBorder* PrevB = WidgetTree->ConstructWidget<UBorder>();
	PrevB->SetBrush(WeedUI::Rounded(FLinearColor(0.03f, 0.025f, 0.05f, 1.f), 10.f));
	PrevB->SetPadding(FMargin(4.f));
	PreviewImage = WidgetTree->ConstructWidget<UImage>();
	PreviewImage->SetVisibility(ESlateVisibility::Visible); // muis-events (rotate/zoom) moeten 'm raken
	PrevB->SetContent(PreviewImage);
	USizeBox* PrevSz = WidgetTree->ConstructWidget<USizeBox>();
	PrevSz->SetWidthOverride(400.f);
	PrevSz->SetContent(PrevB);
	UHorizontalBoxSlot* PS = Split->AddChildToHorizontalBox(PrevSz);
	PS->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));

	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* BSl = Split->AddChildToHorizontalBox(Body);
	BSl->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UWardrobeWidget::EnsurePreview()
{
	APawn* Pawn = GetOwningPlayerPawn();
	UWorld* W = GetWorld();
	if (!Pawn || !W) { return; }

	if (!PreviewRT)
	{
		PreviewRT = NewObject<UTextureRenderTarget2D>(this);
		PreviewRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		PreviewRT->InitAutoFormat(480, 700);
		PreviewRT->UpdateResourceImmediate(true);
		if (PreviewImage)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(PreviewRT);
			Brush.ImageSize = FVector2D(392.f, 572.f);
			PreviewImage->SetBrush(Brush);
		}
	}

	// Kloon herbouwen zodra skin/outfit wijzigt (waarden komen gerepliceerd binnen).
	FString Sig;
	if (const IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(Pawn))
	{
		Sig = FString::Printf(TEXT("%d"), Pl->GetPlayerSkinIndex());
		for (int32 SlotIdx = 0; SlotIdx < WeedOutfit::SlotCount(); ++SlotIdx) { Sig += FString::Printf(TEXT("|%d"), Pl->GetOutfitPart(SlotIdx)); }
	}
	if (Sig != PreviewOutfitSig || !PreviewActor.IsValid())
	{
		PreviewOutfitSig = Sig;
		RebuildPreviewActor();
	}

	if (!PreviewCapture.IsValid())
	{
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASceneCapture2D* Cap = W->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FTransform::Identity, SP);
		if (!Cap) { return; }
		if (USceneCaptureComponent2D* C = Cap->GetCaptureComponent2D())
		{
			C->TextureTarget = PreviewRT;
			C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			if (PreviewActor.IsValid()) { C->ShowOnlyActors.Add(PreviewActor.Get()); }
			C->FOVAngle = 30.f;
			C->bCaptureEveryFrame = true; // live: animatie + outfit-wissels direct zichtbaar
			C->bAlwaysPersistRenderingState = true; // exposure/TAA-history vasthouden -> de gepinde exposure drift niet meer weg na een paar seconden
			// Vaste belichting, dag en nacht: pin de auto-exposure vast (min == max) zodat de preview
			// niet meedimt met de zon; de eigen studio-lampen doen het werk.
			C->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
			C->PostProcessSettings.AutoExposureMinBrightness = 1.0f;
			C->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
			C->PostProcessSettings.AutoExposureMaxBrightness = 1.0f;
			// VASTE belichting via AEM_Manual -> eye-adaptation HELEMAAL uit, geen drift meer (de min==max-histogram
			// hield 'm niet vast: na een paar seconden dimde de preview alsnog). De EERDERE Manual-poging werd ZWART
			// omdat 'ie de physical-camera-exposure (donkere EV) pakte -> die hier UIT + een vaste bias; lampen doen de rest.
			C->PostProcessSettings.bOverride_AutoExposureMethod = true;
			C->PostProcessSettings.AutoExposureMethod = AEM_Manual;
			C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = 0; // GEEN physical-camera-EV (dat maakte 'm zwart)
			C->PostProcessSettings.bOverride_AutoExposureBias = true;
			C->PostProcessSettings.AutoExposureBias = 1.0f; // vaste belichting (tunebaar); geen adaptatie meer
				C->ShowFlags.SetEyeAdaptation(false); // eye-adaptation-PAS uit -> geen overbelichte puls op de eerste frames bij het openen
		}
		// Lampen aan de CAMERA: de voorkant is altijd verlicht, hoe je ook draait.
		if (UPointLightComponent* Key = NewObject<UPointLightComponent>(Cap))
		{
			Key->SetupAttachment(Cap->GetRootComponent());
			Key->RegisterComponent();
			Key->SetRelativeLocation(FVector(-40.f, -60.f, 60.f));
			Key->SetIntensity(9500.f); Key->SetAttenuationRadius(900.f);
			Key->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f)); Key->SetCastShadows(false);
		}
		if (UPointLightComponent* Fill = NewObject<UPointLightComponent>(Cap))
		{
			Fill->SetupAttachment(Cap->GetRootComponent());
			Fill->RegisterComponent();
			Fill->SetRelativeLocation(FVector(-30.f, 80.f, -20.f));
			Fill->SetIntensity(4000.f); Fill->SetAttenuationRadius(800.f);
			Fill->SetLightColor(FLinearColor(0.85f, 0.9f, 1.f)); Fill->SetCastShadows(false);
		}
		// Vaste "player-view"-lamp aan de camera: sinds de UDS-integratie dimt de wereld-SkyLight bij nacht naar ~0.06,
		// waardoor de preview donker werd. Deze lamp hangt aan de camera en verlicht de kloon altijd vol, onafhankelijk
		// van dag/nacht/weer (alleen in de capture, niet in de wereld).
		if (UPointLightComponent* ViewLamp = NewObject<UPointLightComponent>(Cap))
		{
			ViewLamp->SetupAttachment(Cap->GetRootComponent());
			ViewLamp->RegisterComponent();
			ViewLamp->SetRelativeLocation(FVector(0.f, 0.f, 25.f));
			ViewLamp->SetIntensity(7000.f); ViewLamp->SetAttenuationRadius(1800.f);
			ViewLamp->SetLightColor(FLinearColor(1.f, 0.97f, 0.92f)); ViewLamp->SetCastShadows(false);
		}
		PreviewCapture = Cap;
	}
	else if (PreviewActor.IsValid())
	{
		// Show-only up-to-date houden na een kloon-herbouw.
		if (USceneCaptureComponent2D* C = PreviewCapture->GetCaptureComponent2D())
		{
			if (!C->ShowOnlyActors.Contains(PreviewActor.Get()))
			{
				C->ShowOnlyActors.Reset();
				C->ShowOnlyActors.Add(PreviewActor.Get());
			}
		}
	}

	UpdatePreviewCamera();
}

void UWardrobeWidget::RebuildPreviewActor()
{
	UWorld* W = GetWorld();
	APawn* Pawn = GetOwningPlayerPawn();
	if (!W || !Pawn) { return; }
	if (PreviewActor.IsValid()) { PreviewActor->Destroy(); PreviewActor = nullptr; }

	IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(Pawn);
	const uint8 Skin = Pl ? Pl->GetPlayerSkinIndex() : 0;

	// Studio diep onder de map: niets anders in beeld (show-only) en stoort niemand.
	const FVector StageLoc = Pawn->GetActorLocation() + FVector(0.f, 0.f, -2600.f);
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASkeletalMeshActor* Actor = W->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, StageLoc), SP);
	if (!Actor) { return; }
	USkeletalMeshComponent* BodyComp = Actor->GetSkeletalMeshComponent();
	if (!BodyComp) { Actor->Destroy(); return; }
	BodyComp->SetMobility(EComponentMobility::Movable);
	BodyComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Male (5): de body volgt de gekozen complete Tony-look; anders de vaste skin-mapping.
	const TCHAR* BodyPath = (Skin == 5 && Pl) ? WeedOutfit::PartAt(0, Pl->GetOutfitPart(0), true).Path
		: ((Skin == 3 || Skin == 4) && Pl) ? WeedOutfit::GirlVariantPath(Skin, Pl->GetOutfitPart(0)) // Gamer/School: gekozen variant-mesh
		: BodyMeshPath(Skin);
	if (USkeletalMesh* BodyMesh = LoadObject<USkeletalMesh>(nullptr, BodyPath))
	{
		BodyComp->SetSkeletalMeshAsset(BodyMesh);
	}
	// Zelfde AnimBP als je echte poppetje -> nette idle (geen T-pose).
	if (const ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (Char->GetMesh() && Char->GetMesh()->GetAnimClass())
		{
			BodyComp->SetAnimInstanceClass(Char->GetMesh()->GetAnimClass());
		}
	}
	// Haar/cloth STIL in de preview: cloth-sim + AnimDynamics + body-physics uit (alleen hier, niet in-game).
	BodyComp->bDisableClothSimulation = true;
	BodyComp->SetAllBodiesSimulatePhysics(false);
	BodyComp->SetAllowAnimDynamicsAnimNode(false);

	// Outfit-parts (leader-pose): Casual-girls (2-4, female) of de male (5, citizens Tony - eigen kleren).
	if (Pl && (Skin == 2 || Skin == 5 || Skin == 6)) // Casual-girl (2), Tony (5), of Citizen_man (6) -> losse parts
	{
		const bool bMaleSkin = (Skin == 5 || Skin == 6);
		const bool bCitMan = (Skin == 6);
		auto Attach = [&](const TCHAR* MeshPath)
		{
			if (!MeshPath) { return; } // "None"-keuze
			USkeletalMesh* PartMesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
			if (!PartMesh) { return; }
			USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(Actor);
			C->SetupAttachment(BodyComp);
			C->RegisterComponent();
			C->SetSkeletalMeshAsset(PartMesh);
			C->SetLeaderPoseComponent(BodyComp);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->bDisableClothSimulation = true; // haar-part ook stil (eigen component, niet door de body-disable gedekt)
			C->SetAllBodiesSimulatePhysics(false);
			C->SetAllowAnimDynamicsAnimNode(false);
		};
		if (!bMaleSkin) { Attach(WeedOutfit::UnderwearPath); }
		for (int32 SlotIdx = 0; SlotIdx < WeedOutfit::SlotCount(); ++SlotIdx)
		{
			const WeedOutfit::FPart& P = bCitMan
				? WeedOutfit::PartAtM(SlotIdx, Pl->GetOutfitPart(SlotIdx), WeedOutfit::EMaleKind::CitizenMan)
				: WeedOutfit::PartAt(SlotIdx, Pl->GetOutfitPart(SlotIdx), bMaleSkin);
			Attach(P.Path);
		}
	}

	PreviewActor = Actor;
}

void UWardrobeWidget::UpdatePreviewCamera()
{
	if (!PreviewCapture.IsValid() || !PreviewActor.IsValid()) { return; }
	const FVector Stage = PreviewActor->GetActorLocation();
	const FVector Focus = Stage + FVector(0.f, 0.f, FMath::Clamp(PreviewFocusZ, 5.f, 150.f)); // verticaal slepen schuift dit
	const float Rad = FMath::DegreesToRadians(PreviewYaw);
	// Start recht voor het poppetje (mannequin kijkt naar +X), orbit met PreviewYaw.
	const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
	const FVector CamLoc = Focus + Dir * FMath::Clamp(PreviewDist, 70.f, 330.f);
	PreviewCapture->SetActorLocationAndRotation(CamLoc, (Focus - CamLoc).Rotation());
}

void UWardrobeWidget::ReleasePreview()
{
	if (PreviewCapture.IsValid()) { PreviewCapture->Destroy(); }
	if (PreviewActor.IsValid()) { PreviewActor->Destroy(); }
	PreviewCapture = nullptr;
	PreviewActor = nullptr;
	PreviewOutfitSig.Reset();
	bPreviewDrag = false;
}

bool UWardrobeWidget::IsOverPreview(const FPointerEvent& Ev) const
{
	return PreviewImage && PreviewImage->GetCachedGeometry().IsUnderLocation(Ev.GetScreenSpacePosition());
}

FReply UWardrobeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// GEEN mouse-capture: die kon blijven hangen waardoor de muis 'locked' en je nergens meer op kon
	// klikken. Drag werkt prima zonder: we volgen de cursor zolang de linker knop ingedrukt is.
	if (PhoneComp.IsValid() && PhoneComp->IsWardrobeOpen() && IsOverPreview(InMouseEvent)
		&& InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPreviewDrag = true;
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UWardrobeWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bPreviewDrag)
	{
		bPreviewDrag = false;
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UWardrobeWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bPreviewDrag)
	{
		// Veiligheid: knop al los (up gemist)? Stop de drag.
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			bPreviewDrag = false;
			return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
		}
		const FVector2D Delta = InMouseEvent.GetCursorDelta();
		PreviewYaw = FMath::Fmod(PreviewYaw + Delta.X * 0.7f, 360.f);              // horizontaal = draaien
		PreviewFocusZ = FMath::Clamp(PreviewFocusZ + Delta.Y * 0.15f, 5.f, 150.f); // verticaal = hoofd <-> schoenen (rustig + begrensd)
		UpdatePreviewCamera();
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UWardrobeWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PhoneComp.IsValid() && PhoneComp->IsWardrobeOpen() && IsOverPreview(InMouseEvent))
	{
		PreviewDist = FMath::Clamp(PreviewDist - InMouseEvent.GetWheelDelta() * 20.f, 70.f, 330.f);
		UpdatePreviewCamera();
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void UWardrobeWidget::FillBody()
{
	if (!Body) { return; }
	Body->ClearChildren();
	SlotNameTexts.Reset();
	ModelButtons.Reset(); ModelButtonSkins.Reset();

	IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(GetOwningPlayerPawn());
	if (!Pl) { Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("No character."), 14, FLinearColor::Gray)); return; }
	const uint8 Skin = Pl->GetPlayerSkinIndex();

	auto Row = [this](UWidget* W, const FMargin& Pad) { Body->AddChildToVerticalBox(W)->SetPadding(Pad); };

	// --- Model-keuze (binnen het gekozen geslacht). Man/Vrouw schakel je in Phone -> Settings -> Game. ---
	const bool bFemaleSkin = (Skin >= 1 && Skin <= 4); // 1 = Quinn (legacy), 2-4 = Casual girls
	if (bFemaleSkin)
	{
		Row(WeedUI::Text(WidgetTree, TEXT("Model"), 14, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0, 0, 0, 4));
		UHorizontalBox* BodyRowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
		struct FBodyChoice { uint8 Idx; const TCHAR* Name; };
		static const FBodyChoice Choices[] = { { 2, TEXT("Casual") }, { 3, TEXT("Gamer Girl") }, { 4, TEXT("School Girl") } };
		bool bFirstBody = true;
		for (const FBodyChoice& Ch : Choices)
		{
			const uint8 bi = Ch.Idx;
			UWeedActionButton* BB = WrdBtn(WidgetTree,
				(Skin == bi) ? FLinearColor(0.55f, 0.30f, 0.80f) : FLinearColor(0.15f, 0.14f, 0.20f), 8.f,
				[this, bi]()
				{
					if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { P->SetPlayerSkinIndex(bi); }
					// In-place: actieve knop oplichten + alle slot-teksten verversen (geen full rebuild -> geen flikker).
					RecolorModelButtons(bi);
					for (const TPair<int32, TWeakObjectPtr<UTextBlock>>& KV : SlotNameTexts) { UpdateSlotText(KV.Key); }
				});
			BB->SetContent(WeedUI::Text(WidgetTree, Ch.Name, 12, FLinearColor::White, true));
			ModelButtons.Add(BB); ModelButtonSkins.Add(bi);
			UHorizontalBoxSlot* BS = BodyRowBox->AddChildToHorizontalBox(BB);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BS->SetPadding(FMargin(bFirstBody ? 0.f : 4.f, 0.f, 0.f, 0.f));
			bFirstBody = false;
		}
		Row(BodyRowBox, FMargin(0, 0, 0, 6));
	}
	// --- MALE model-keuze: Tony (complete looks, 1 slot) of Citizen (volledig modulair, 7 slots). ---
	if (Skin == 5 || Skin == 6)
	{
		Row(WeedUI::Text(WidgetTree, TEXT("Model"), 14, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0, 0, 0, 4));
		UHorizontalBox* MBodyRowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
		struct FMaleChoice { uint8 Idx; const TCHAR* Name; };
		static const FMaleChoice MaleChoices[] = { { 5, TEXT("Tony") }, { 6, TEXT("Citizen") } };
		bool bFirstMBody = true;
		for (const FMaleChoice& Ch : MaleChoices)
		{
			const uint8 bi = Ch.Idx;
			UWeedActionButton* BB = WrdBtn(WidgetTree,
				(Skin == bi) ? FLinearColor(0.55f, 0.30f, 0.80f) : FLinearColor(0.15f, 0.14f, 0.20f), 8.f,
				[this, bi]()
				{
					if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { P->SetPlayerSkinIndex(bi); }
					RecolorModelButtons(bi);
					// Tony<->Citizen wisselt de slot-STRUCTUUR (1 vs 7) -> categorie wijzigt -> NativeTick rebuildt FillBody (veilig, geen reentrante rebuild hier).
				});
			BB->SetContent(WeedUI::Text(WidgetTree, Ch.Name, 12, FLinearColor::White, true));
			ModelButtons.Add(BB); ModelButtonSkins.Add(bi);
			UHorizontalBoxSlot* BS = MBodyRowBox->AddChildToHorizontalBox(BB);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BS->SetPadding(FMargin(bFirstMBody ? 0.f : 4.f, 0.f, 0.f, 0.f));
			bFirstMBody = false;
		}
		Row(MBodyRowBox, FMargin(0, 0, 0, 6));
	}
	Row(WeedUI::Text(WidgetTree, TEXT("Switch Male / Female in Phone - Settings - Game."), 11, FLinearColor(0.6f, 0.62f, 0.72f)), FMargin(0, 0, 0, 10));

	// Legacy Manny/Quinn (0/1) hebben geen losse outfit-parts -> kies hierboven een model.
	if (Skin < 2)
	{
		Row(WeedUI::Text(WidgetTree, TEXT("Pick a model above for outfits."), 13, FLinearColor(0.65f, 0.68f, 0.78f)), FMargin(0, 6, 0, 0));
		return;
	}

	// Gamer Girl (3) / School Girl (4) zijn COMPLETE meshes: geen Casual-kledingslots, maar een variant-switcher.
	if (Skin == 3 || Skin == 4)
	{
		const int32 VarCount = FMath::Max(1, WeedOutfit::GirlVariantCount(Skin));
		const uint8 CurVar = (uint8)FMath::Clamp((int32)Pl->GetOutfitPart(0), 0, VarCount - 1);
		Row(WeedUI::Text(WidgetTree, TEXT("Outfit"), 13, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0, 4, 0, 2));
		UHorizontalBox* VR = WidgetTree->ConstructWidget<UHorizontalBox>();

		UWeedActionButton* PrevV = WrdBtn(WidgetTree, FLinearColor(0.2f, 0.25f, 0.4f), 8.f,
			[this, VarCount]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					const uint8 C = P->GetOutfitPart(0);
					P->SetOutfitPart(0, (uint8)((C + VarCount - 1) % VarCount));
				}
				UpdateSlotText(0);
			});
		PrevV->SetContent(WeedUI::Text(WidgetTree, TEXT("<"), 14, FLinearColor::White, true));
		VR->AddChildToHorizontalBox(PrevV);

		UTextBlock* VNameT = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s  (%d/%d)"), WeedOutfit::GirlVariantName(Skin, CurVar), CurVar + 1, VarCount), 14, FLinearColor(0.95f, 0.95f, 1.f), false, true);
		SlotNameTexts.Add(0, VNameT);
		UHorizontalBoxSlot* VNS = VR->AddChildToHorizontalBox(VNameT);
		VNS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VNS->SetHorizontalAlignment(HAlign_Center); VNS->SetVerticalAlignment(VAlign_Center);

		UWeedActionButton* NextV = WrdBtn(WidgetTree, FLinearColor(0.2f, 0.25f, 0.4f), 8.f,
			[this, VarCount]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					const uint8 C = P->GetOutfitPart(0);
					P->SetOutfitPart(0, (uint8)((C + 1) % VarCount));
				}
				UpdateSlotText(0);
			});
		NextV->SetContent(WeedUI::Text(WidgetTree, TEXT(">"), 14, FLinearColor::White, true));
		VR->AddChildToHorizontalBox(NextV);

		Row(VR, FMargin(0, 0, 0, 4));
		Row(WeedUI::Text(WidgetTree, TEXT("Drag the preview to rotate - scroll to zoom. Press B in-game for third person."), 11, FLinearColor(0.6f, 0.62f, 0.72f)), FMargin(0, 12, 0, 0));
		return; // geen Casual-slots voor Gamer/School
	}

	// --- Outfit-slots: < naam > per categorie ---
	// Volgorde van boven naar onder: Headwear, Hair, Necklace, Top, Pants, Socks, Shoes.
	const bool bMaleSkin = (Skin == 5 || Skin == 6);
	const bool bCitMan = (Skin == 6);
	static const int32 DisplayOrder[] = { 4, 3, 5, 0, 1, 6, 2 };
	static const int32 CitManOrder[] = { 6, 0, 1, 2, 3, 4, 5 }; // Citizen: Face (kop) eerst, dan kleren
	const int32* Order = bCitMan ? CitManOrder : DisplayOrder;
	const int32 OrderNum = bCitMan ? (int32)UE_ARRAY_COUNT(CitManOrder) : (int32)UE_ARRAY_COUNT(DisplayOrder);
	for (int32 oi = 0; oi < OrderNum; ++oi)
	{
		const int32 SlotIdx = Order[oi];
		if (bMaleSkin && !bCitMan && SlotIdx != 0) { continue; } // Tony: alleen de "Look"-keuze; Citizen: alle 7 slots
		const int32 Count = bCitMan ? WeedOutfit::PartCountM(SlotIdx, WeedOutfit::EMaleKind::CitizenMan) : WeedOutfit::PartCount(SlotIdx, bMaleSkin);
		const uint8 Cur = Pl->GetOutfitPart(SlotIdx);
		const WeedOutfit::FPart& Part = bCitMan ? WeedOutfit::PartAtM(SlotIdx, Cur, WeedOutfit::EMaleKind::CitizenMan) : WeedOutfit::PartAt(SlotIdx, Cur, bMaleSkin);

		const TCHAR* SlotLbl = bCitMan ? WeedOutfit::SlotNameM(SlotIdx, WeedOutfit::EMaleKind::CitizenMan) : (bMaleSkin ? TEXT("Look") : WeedOutfit::SlotName(SlotIdx));
		Row(WeedUI::Text(WidgetTree, SlotLbl, 13, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0, 4, 0, 2));
		UHorizontalBox* R = WidgetTree->ConstructWidget<UHorizontalBox>();

		UWeedActionButton* PrevB = WrdBtn(WidgetTree, FLinearColor(0.2f, 0.25f, 0.4f), 8.f,
			[this, SlotIdx, Count]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					const uint8 Cur = P->GetOutfitPart(SlotIdx); // VERS lezen (niet de capture van bij het bouwen)
					P->SetOutfitPart(SlotIdx, (uint8)((Cur + Count - 1) % Count));
				}
				UpdateSlotText(SlotIdx); // in-place, geen rebuild
			});
		PrevB->SetContent(WeedUI::Text(WidgetTree, TEXT("<"), 14, FLinearColor::White, true));
		R->AddChildToHorizontalBox(PrevB);

		UTextBlock* NameT = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s  (%d/%d)"), Part.Name, Cur + 1, Count), 14, FLinearColor(0.95f, 0.95f, 1.f), false, true);
		SlotNameTexts.Add(SlotIdx, NameT); // bewaren -> in-place updaten bij </>
		UHorizontalBoxSlot* NS = R->AddChildToHorizontalBox(NameT);
		NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NS->SetHorizontalAlignment(HAlign_Center); NS->SetVerticalAlignment(VAlign_Center);

		UWeedActionButton* NextB = WrdBtn(WidgetTree, FLinearColor(0.2f, 0.25f, 0.4f), 8.f,
			[this, SlotIdx, Count]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					const uint8 Cur = P->GetOutfitPart(SlotIdx);
					P->SetOutfitPart(SlotIdx, (uint8)((Cur + 1) % Count));
				}
				UpdateSlotText(SlotIdx);
			});
		NextB->SetContent(WeedUI::Text(WidgetTree, TEXT(">"), 14, FLinearColor::White, true));
		R->AddChildToHorizontalBox(NextB);

		Row(R, FMargin(0, 0, 0, 4));
	}

	Row(WeedUI::Text(WidgetTree, TEXT("Drag the preview to rotate - scroll to zoom. Press B in-game for third person."), 11, FLinearColor(0.6f, 0.62f, 0.72f)), FMargin(0, 12, 0, 0));
}

void UWardrobeWidget::UpdateSlotText(int32 SlotIdx)
{
	TWeakObjectPtr<UTextBlock>* Found = SlotNameTexts.Find(SlotIdx);
	if (!Found || !Found->IsValid()) { return; }
	const IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(GetOwningPlayerPawn());
	if (!Pl) { return; }
	const uint8 Sk = Pl->GetPlayerSkinIndex();
	if ((Sk == 3 || Sk == 4) && SlotIdx == 0) // Gamer/School: variant-switcher (slot 0 = variant-index)
	{
		const int32 VC = FMath::Max(1, WeedOutfit::GirlVariantCount(Sk));
		const uint8 CV = (uint8)FMath::Clamp((int32)Pl->GetOutfitPart(0), 0, VC - 1);
		Found->Get()->SetText(FText::FromString(FString::Printf(TEXT("%s  (%d/%d)"), WeedOutfit::GirlVariantName(Sk, CV), CV + 1, VC)));
		return;
	}
	const bool bMaleSkin = (Sk == 5 || Sk == 6);
	const bool bCitMan = (Sk == 6);
	const int32 Count = bCitMan ? WeedOutfit::PartCountM(SlotIdx, WeedOutfit::EMaleKind::CitizenMan) : WeedOutfit::PartCount(SlotIdx, bMaleSkin);
	const uint8 Cur = Pl->GetOutfitPart(SlotIdx);
	const WeedOutfit::FPart& Part = bCitMan ? WeedOutfit::PartAtM(SlotIdx, Cur, WeedOutfit::EMaleKind::CitizenMan) : WeedOutfit::PartAt(SlotIdx, Cur, bMaleSkin);
	Found->Get()->SetText(FText::FromString(FString::Printf(TEXT("%s  (%d/%d)"), Part.Name, Cur + 1, Count)));
}

void UWardrobeWidget::RecolorModelButtons(uint8 ActiveSkin)
{
	for (int32 i = 0; i < ModelButtons.Num(); ++i)
	{
		UWeedActionButton* B = ModelButtons[i].Get();
		if (!B) { continue; }
		const FLinearColor Col = (ModelButtonSkins.IsValidIndex(i) && ModelButtonSkins[i] == ActiveSkin)
			? FLinearColor(0.55f, 0.30f, 0.80f) : FLinearColor(0.15f, 0.14f, 0.20f);
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 8.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(S);
	}
}

void UWardrobeWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::Visible); // hit-testbaar voor rotate/zoom op de preview

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsWardrobeOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen)
	{
		LastSig.Reset();
		ReleasePreview();
		SetVisibility(ESlateVisibility::SelfHitTestInvisible); // dicht: geen input-vangst
		return;
	}
	EnsurePreview();

	// Body alleen herbouwen bij een STRUCTURELE wijziging (legacy <2 / female 2-4 / male 5). Een outfit-keuze
	// of model-wissel binnen dezelfde categorie updaten we IN-PLACE (geen flikker, geen rebuild).
	int32 Cat = 0;
	if (const IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
	{
		const uint8 Sk = Pl->GetPlayerSkinIndex();
		Cat = (Sk == 2) ? 1 : (Sk == 3 ? 4 : (Sk == 4 ? 5 : (Sk == 5 ? 2 : (Sk == 6 ? 3 : 0)))); // skin 3/4 eigen categorie -> rebuild bij Casual<->Gamer<->School (UI verschilt)
	}
	const FString Sig = FString::FromInt(Cat);
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}

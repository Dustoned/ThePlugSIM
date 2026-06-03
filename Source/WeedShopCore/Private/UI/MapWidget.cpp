#include "UI/MapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "World/CityGenerator.h"
#include "Customer/CustomerBase.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"

namespace
{
	constexpr float GMapDS = 760.f;  // ontwerp-grootte van het kaartvlak (px)
	constexpr float GMapPad = 44.f;  // marge
}

FVector2D UMapWidget::WorldToCanvas(float Wx, float Wy) const
{
	const float rx = Wx - CenterXY.X;
	const float ry = Wy - CenterXY.Y;
	// scherm-rechts = wereld +Y, scherm-omhoog = wereld +X (noorden boven)
	return FVector2D(GMapDS * 0.5f + ry * Scale, GMapDS * 0.5f - rx * Scale);
}

UTextBlock* UMapWidget::AddCanvasText(const FString& T, FVector2D Pos, float W, int32 Size, const FLinearColor& Col, int32 ZOrder)
{
	UTextBlock* Tb = WidgetTree->ConstructWidget<UTextBlock>();
	Tb->SetText(FText::FromString(T));
	Tb->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", Size));
	Tb->SetColorAndOpacity(FSlateColor(Col));
	Tb->SetJustification(ETextJustify::Center);
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(Tb);
	Cs->SetAutoSize(false);
	Cs->SetSize(FVector2D(W, Size + 6.f));
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetPosition(Pos);
	Cs->SetZOrder(ZOrder);
	return Tb;
}

UBorder* UMapWidget::AddDot(const FLinearColor& Col, float Sz, int32 ZOrder)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrushColor(Col);
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(B);
	Cs->SetAutoSize(false);
	Cs->SetSize(FVector2D(Sz, Sz));
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetZOrder(ZOrder);
	return B;
}

TSharedRef<SWidget> UMapWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MapRoot"));
		WidgetTree->RootWidget = Root;

		// Donkere achtergrond (dimt de game bij fullscreen).
		UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
		Bg->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.07f, bFullscreen ? 0.92f : 1.f));
		Root->AddChildToOverlay(Bg);

		// Vaste-grootte kaartvlak, geschaald naar de beschikbare ruimte.
		UScaleBox* SB = WidgetTree->ConstructWidget<UScaleBox>();
		SB->SetStretch(EStretch::ScaleToFit);
		if (UOverlaySlot* OS = Root->AddChildToOverlay(SB))
		{
			OS->SetHorizontalAlignment(HAlign_Center);
			OS->SetVerticalAlignment(VAlign_Center);
		}
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(GMapDS);
		Box->SetHeightOverride(GMapDS);
		SB->AddChild(Box);

		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MapCanvas"));
		Box->SetContent(Canvas);

		// Titel + kompas.
		AddCanvasText(TEXT("STAD - KAART"), FVector2D(GMapDS * 0.5f, 20.f), GMapDS, 20, FLinearColor(0.7f, 0.85f, 1.f), 50);
		AddCanvasText(TEXT("N"), FVector2D(GMapDS * 0.5f, 44.f), 40.f, 14, FLinearColor(0.8f, 0.8f, 0.9f), 50);
	}
	return Super::RebuildWidget();
}

void UMapWidget::BuildBlocks()
{
	if (!Canvas || !City.IsValid()) { return; }

	const int32 R = City->GetGridRadiusClamped();
	const float Pitch = City->GetPitch();
	const FVector C = City->GetCityCenter();
	CenterXY = FVector2D(C.X, C.Y);
	const float EH = (R + 0.7f) * Pitch;             // halve wereld-omvang
	Scale = (GMapDS - 2.f * GMapPad) / (2.f * EH);

	TArray<FCityMapBlock> Blocks;
	City->GetMapBlocks(Blocks);
	const float BlkPx = City->GetMapBlockSize() * Scale;

	for (const FCityMapBlock& Bk : Blocks)
	{
		const FVector2D P = WorldToCanvas(Bk.Center.X, Bk.Center.Y);
		UBorder* Tile = WidgetTree->ConstructWidget<UBorder>();
		Tile->SetBrushColor(Bk.Color);
		UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(Tile);
		Cs->SetAutoSize(false);
		Cs->SetSize(FVector2D(BlkPx, BlkPx));
		Cs->SetAlignment(FVector2D(0.5f, 0.5f));
		Cs->SetPosition(P);
		Cs->SetZOrder(1);
		// Label (winkelnaam of huisnummer-reeks), zwart op de gekleurde tegel.
		AddCanvasText(Bk.Label, P, BlkPx, Bk.bShop ? 13 : 11, FLinearColor(0.05f, 0.05f, 0.06f), 2);
	}

	// Speler-marker (boven alles).
	PlayerDot = AddDot(FLinearColor(0.2f, 0.9f, 1.f), 16.f, 20);
	AddCanvasText(TEXT("Legenda: groen=grow  paars=meubels  blauw=supplies  rood=gas"),
		FVector2D(GMapDS * 0.5f, GMapDS - 18.f), GMapDS, 11, FLinearColor(0.7f, 0.72f, 0.8f), 50);

	bBuiltBlocks = true;
}

void UMapWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	if (!City.IsValid())
	{
		for (TActorIterator<ACityGenerator> It(GetWorld()); It; ++It) { City = *It; break; }
	}
	if (!bBuiltBlocks && City.IsValid()) { BuildBlocks(); }
	if (!bBuiltBlocks || !Canvas) { return; }

	// Speler.
	if (PlayerDot)
	{
		if (APawn* P = GetOwningPlayerPawn())
		{
			const FVector L = P->GetActorLocation();
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(PlayerDot->Slot)) { Cs->SetPosition(WorldToCanvas(L.X, L.Y)); }
			PlayerDot->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	// Klanten/NPC's verzamelen.
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	int32 N = 0;
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		ACustomerBase* C = *It;
		if (!IsValid(C)) { continue; }

		while (NpcDots.Num() <= N) { NpcDots.Add(AddDot(FLinearColor(1.f, 0.6f, 0.15f), 14.f, 18)); }
		while (NpcLabels.Num() <= N) { NpcLabels.Add(AddCanvasText(TEXT(""), FVector2D::ZeroVector, 120.f, 11, FLinearColor(1.f, 0.85f, 0.5f), 19)); }

		const FVector L = C->GetActorLocation();
		const FVector2D Pos = WorldToCanvas(L.X, L.Y);
		if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(NpcDots[N]->Slot)) { Cs->SetPosition(Pos); }
		NpcDots[N]->SetVisibility(ESlateVisibility::HitTestInvisible);

		FString Name = C->NpcId.IsNone() ? FString(TEXT("Klant")) : C->NpcId.ToString();
		if (Reg && !C->NpcId.IsNone()) { float r, l, a; FText Nm; if (Reg->GetStats(C->NpcId, r, l, a, Nm)) { Name = Nm.ToString(); } }
		const bool bWants = (C->State == ECustomerState::WantsToOrder || C->State == ECustomerState::Negotiating);
		if (NpcLabels[N])
		{
			NpcLabels[N]->SetText(FText::FromString(bWants ? (Name + TEXT(" !")) : Name));
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(NpcLabels[N]->Slot)) { Cs->SetPosition(Pos + FVector2D(0.f, -14.f)); }
			NpcLabels[N]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		++N;
	}
	// Overtollige markers verbergen.
	for (int32 k = N; k < NpcDots.Num(); ++k) { if (NpcDots[k]) { NpcDots[k]->SetVisibility(ESlateVisibility::Collapsed); } }
	for (int32 k = N; k < NpcLabels.Num(); ++k) { if (NpcLabels[k]) { NpcLabels[k]->SetVisibility(ESlateVisibility::Collapsed); } }
}

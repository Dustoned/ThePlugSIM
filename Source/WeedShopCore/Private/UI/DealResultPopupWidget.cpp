#include "UI/DealResultPopupWidget.h"

#include "UI/WeedUiStyle.h"
#include "Customer/CustomerBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

// --- Timing van de fade (seconden) ---
static constexpr float GPopFadeIn = 0.25f;  // infaden
static constexpr float GPopHold = 3.4f;     // volledig zichtbaar
static constexpr float GPopFadeOut = 0.75f; // uitfaden
static constexpr float GPopLife = GPopFadeIn + GPopHold + GPopFadeOut;

TSharedRef<SWidget> UDealResultPopupWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		RootCanvas = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UDealResultPopupWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDealResultPopupWidget::ClearChips()
{
	for (UBorder* Chip : Chips)
	{
		if (Chip) { Chip->RemoveFromParent(); }
	}
	Chips.Reset();
	ChipOffsets.Reset();
	ChipDelays.Reset();
}

void UDealResultPopupWidget::AddChip(const FString& InText, const FLinearColor& Color, const FVector2D& Offset, bool bBig, const FString& IconName, bool bKitIcon)
{
	if (!RootCanvas || !WidgetTree) { return; }

	UBorder* Chip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), NAME_None);
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColBg(0.72f), 7.f);
		Br.OutlineSettings.Width = 1.f;
		Br.OutlineSettings.Color = FSlateColor(FLinearColor(Color.R, Color.G, Color.B, 0.48f));
		Chip->SetBrush(Br);
	}
	Chip->SetPadding(FMargin(bBig ? 10.f : 8.f, bBig ? 4.f : 3.f));
	Chip->SetVisibility(ESlateVisibility::Collapsed);
	Chip->SetRenderOpacity(0.f);
	Chip->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (!IconName.IsEmpty())
	{
		const float IconSize = bBig ? 18.f : 14.f;
		USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
		IconBox->SetWidthOverride(IconSize);
		IconBox->SetHeightOverride(IconSize);
		if (bKitIcon)
		{
			IconBox->SetContent(WeedUI::KitIcon(WidgetTree, IconName, IconSize, Color));
		}
		else
		{
			WeedUI::EIcon Fallback = WeedUI::EIcon::Coin;
			if (IconName == TEXT("ui_level")) { Fallback = WeedUI::EIcon::Level; }
			else if (IconName == TEXT("ui_person")) { Fallback = WeedUI::EIcon::Person; }
			else if (IconName == TEXT("ui_flame")) { Fallback = WeedUI::EIcon::Flame; }
			IconBox->SetContent(WeedUI::UiGlyph(WidgetTree, IconName, IconSize, Color, Fallback));
		}
		UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(IconBox);
		IS->SetVerticalAlignment(VAlign_Center);
		IS->SetPadding(FMargin(0.f, 0.f, bBig ? 6.f : 5.f, 0.f));
	}

	UTextBlock* T = WeedUI::Text(WidgetTree, InText, bBig ? 19 : 15, Color, true, true);
	T->SetJustification(ETextJustify::Center);
	T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	T->SetShadowOffset(FVector2D(1.f, 1.f));
	UHorizontalBoxSlot* TS = Row->AddChildToHorizontalBox(T);
	TS->SetVerticalAlignment(VAlign_Center);
	Chip->SetContent(Row);

	UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(Chip);
	CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);

	Chips.Add(Chip);
	ChipOffsets.Add(Offset);
	ChipDelays.Add(0.04f * float(Chips.Num() - 1));
}

void UDealResultPopupWidget::ShowResult(ACustomerBase* Customer, const FVector& AnchorWorld, int32 Cents, int32 XP, int32 dR, int32 dL, int32 dA)
{
	if (!RootCanvas) { return; }

	AnchorCustomer = Customer;
	FallbackWorld = AnchorWorld;

	ClearChips();
	const bool bNegativeOnly = Cents <= 0 && XP <= 0 && (dR < 0 || dL < 0 || dA < 0) && dR <= 0 && dL <= 0 && dA <= 0;
	auto AddDeltaChip = [this](int32 Delta, const TCHAR* Label, const FVector2D& Offset, const FString& Icon)
	{
		if (Delta == 0) { return; }
		const FLinearColor ChipColor = (Delta > 0) ? WeedUI::ColGood() : WeedUI::ColWarn();
		AddChip(FString::Printf(TEXT("%+d %s"), Delta, Label), ChipColor, Offset, false, Icon, true);
	};
	if (bNegativeOnly)
	{
		AddChip(TEXT("Refused"), WeedUI::ColWarn(), FVector2D(0.f, -34.f), /*bBig*/ true);
		AddDeltaChip(dR, TEXT("respect"), FVector2D(0.f, 8.f), TEXT("t_medal_128"));
		AddDeltaChip(dL, TEXT("loyalty"), FVector2D(-70.f, 8.f), TEXT("t_heart_red_128"));
		AddDeltaChip(dA, TEXT("hooked"), FVector2D(70.f, 8.f), TEXT("t_flame_128"));
		Age = 0.f;
		LifeTime = GPopLife;
		bActive = true;
		return;
	}
	if (Cents > 0)
	{
		// Centen -> hele euro's (afgerond naar boven zodat een deal nooit "+EUR 0" toont).
		const int32 Euros = FMath::Max(1, (Cents + 99) / 100);
		AddChip(FString::Printf(TEXT("+EUR %d"), Euros), WeedUI::ColGood(), FVector2D(0.f, -54.f), /*bBig*/ true, TEXT("ui_coin"));
	}
	if (XP > 0) { AddChip(FString::Printf(TEXT("+%d XP"), XP), WeedUI::ColAccent(), FVector2D(72.f, -18.f), false, TEXT("ui_level")); }
	AddDeltaChip(dR, TEXT("respect"), FVector2D(-78.f, -16.f), TEXT("t_medal_128"));
	AddDeltaChip(dL, TEXT("loyalty"), FVector2D(-68.f, 24.f), TEXT("t_heart_red_128"));
	AddDeltaChip(dA, TEXT("hooked"), FVector2D(72.f, 24.f), TEXT("t_flame_128"));

	// Niks zinnigs om te tonen? Dan toch een korte bevestiging zodat de popup nooit leeg is.
	if (Chips.Num() == 0)
	{
		AddChip(TEXT("Sold!"), WeedUI::ColGood(), FVector2D(0.f, -34.f), true, TEXT("ui_coin"));
	}

	Age = 0.f;
	LifeTime = GPopLife;
	bActive = true;
}

void UDealResultPopupWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!RootCanvas) { return; }

	if (!bActive)
	{
		for (UBorder* Chip : Chips)
		{
			if (Chip && Chip->GetVisibility() != ESlateVisibility::Collapsed) { Chip->SetVisibility(ESlateVisibility::Collapsed); }
		}
		return;
	}

	Age += DeltaTime;
	if (Age >= LifeTime)
	{
		// Klaar: verberg + verwijder de widget helemaal (spawner maakt/hergebruikt hem bij de volgende deal).
		bActive = false;
		ClearChips();
		RemoveFromParent();
		return;
	}

	// --- Ankerpunt bepalen: boven het hoofd van de klant, anders de meegegeven fallback-locatie. ---
	FVector Anchor = FallbackWorld;
	if (ACustomerBase* C = AnchorCustomer.Get())
	{
		Anchor = C->GetActorLocation();
		// Rond hoofd/schouders: halve capsule-hoogte + kleine marge; de chip-offsets verdelen de cijfers.
		float Half = 90.f;
		if (const UCapsuleComponent* Cap = C->GetCapsuleComponent()) { Half = Cap->GetScaledCapsuleHalfHeight(); }
		Anchor.Z += Half + 20.f;
	}

	// --- Projecteer naar het scherm. Achter de camera / niet-projecteerbaar -> even verbergen. ---
	APlayerController* PC = GetOwningPlayer();
	FVector2D Screen = FVector2D::ZeroVector;
	const bool bOnScreen = PC && PC->ProjectWorldLocationToScreen(Anchor, Screen, /*bPlayerViewportRelative*/ true);
	if (!bOnScreen)
	{
		for (UBorder* Chip : Chips)
		{
			if (Chip)
			{
				Chip->SetRenderOpacity(0.f);
				if (Chip->GetVisibility() != ESlateVisibility::Collapsed) { Chip->SetVisibility(ESlateVisibility::Collapsed); }
			}
		}
		return;
	}

	// Scherm-coordinaten (pixels, viewport-relatief) -> canvas-coordinaten (DPI-schaal terugrekenen,
	// zelfde idioom als de rest van de UI: UWidgetLayoutLibrary::GetViewportScale).
	float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
	if (DPI <= 0.f) { DPI = 1.f; }
	FVector2D CanvasPos = Screen / DPI;
	// KRITISCH: ProjectWorldLocationToScreen geeft ook true als het punt VOOR de camera zit maar BUITEN de
	// viewport valt (coords negatief / voorbij de rand). Vlak voor een NPC zit het kop-anker (~130cm boven de
	// origin) vrijwel altijd BOVEN de bovenrand -> het kaartje hing daardoor onzichtbaar buiten beeld. Daarom
	// klemmen we de positie binnen het scherm (met marge), zodat de popup ALTIJD leesbaar in beeld staat.
	const FVector2D ViewSize = UWidgetLayoutLibrary::GetViewportSize(this) / DPI;
	CanvasPos.X = FMath::Clamp(CanvasPos.X, 120.f, FMath::Max(120.f, ViewSize.X - 120.f));
	CanvasPos.Y = FMath::Clamp(CanvasPos.Y, 110.f, FMath::Max(110.f, ViewSize.Y - 80.f));

	for (int32 i = 0; i < Chips.Num(); ++i)
	{
		UBorder* Chip = Chips[i];
		if (!Chip) { continue; }

		const float LocalAge = Age - (ChipDelays.IsValidIndex(i) ? ChipDelays[i] : 0.f);
		if (LocalAge <= 0.f)
		{
			Chip->SetRenderOpacity(0.f);
			Chip->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		float Op = 1.f;
		if (LocalAge < GPopFadeIn) { Op = LocalAge / GPopFadeIn; }
		else if (Age > GPopFadeIn + GPopHold) { Op = FMath::Clamp((LifeTime - Age) / GPopFadeOut, 0.f, 1.f); }

		const FVector2D Offset = ChipOffsets.IsValidIndex(i) ? ChipOffsets[i] : FVector2D::ZeroVector;
		const float Drift = FMath::Clamp(LocalAge * 16.f, 0.f, 28.f);
		FVector2D Pos = CanvasPos + Offset + FVector2D(0.f, -Drift);
		Pos.X = FMath::Clamp(Pos.X, 70.f, FMath::Max(70.f, ViewSize.X - 70.f));
		Pos.Y = FMath::Clamp(Pos.Y, 65.f, FMath::Max(65.f, ViewSize.Y - 45.f));

		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Chip->Slot))
		{
			CS->SetPosition(Pos);
		}

		Chip->SetRenderOpacity(Op);
		if (Chip->GetVisibility() != ESlateVisibility::HitTestInvisible) { Chip->SetVisibility(ESlateVisibility::HitTestInvisible); }
	}
}

#include "UI/DealResultPopupWidget.h"

#include "UI/WeedUiStyle.h"
#include "Customer/CustomerBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

// --- Timing van de fade (seconden) ---
static constexpr float GPopFadeIn = 0.2f;   // infaden
static constexpr float GPopHold = 2.0f;     // volledig zichtbaar
static constexpr float GPopFadeOut = 0.5f;  // uitfaden
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

	// Het zwevende kaartje: nette afgeronde box in het palet + dunne rand. Positie zetten we per
	// frame via de canvas-slot (anker linksboven, uitlijning midden-onder zodat het BOVEN het punt zit).
	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DealPopCard"));
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.95f), 12.f);
		Br.OutlineSettings.Width = 1.f;
		Br.OutlineSettings.Color = FSlateColor(WeedUI::ColGood(0.55f)); // groene rand = geslaagde deal
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(14.f, 9.f, 14.f, 9.f));
	CardB->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f)); // absoluut positioneren (linksboven-anker)
	CS->SetAlignment(FVector2D(0.5f, 1.f));       // midden-onder = het kaartje zit BOVEN het scherm-punt
	CS->SetAutoSize(true);

	Rows = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Rows);

	// Onzichtbaar tot ShowResult 'm aanzet.
	Card->SetRenderOpacity(0.f);
	Card->SetVisibility(ESlateVisibility::Collapsed);
}

void UDealResultPopupWidget::AddLine(const FString& InText, const FLinearColor& Color, bool bBig)
{
	if (!Rows) { return; }
	UTextBlock* T = WeedUI::Text(WidgetTree, InText, bBig ? 18 : 14, Color, true, true);
	T->SetJustification(ETextJustify::Center);
	Rows->AddChildToVerticalBox(T)->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f));
}

void UDealResultPopupWidget::ShowResult(ACustomerBase* Customer, const FVector& AnchorWorld, int32 Cents, int32 XP, int32 dR, int32 dL, int32 dA)
{
	if (!Card || !Rows) { return; }

	AnchorCustomer = Customer;
	FallbackWorld = AnchorWorld;

	// Regels opnieuw opbouwen (de popup wordt hergebruikt per deal). Geld altijd bovenaan + groot;
	// daaronder alleen de stats die echt omhoog gingen (delta > 0), elk met eigen palet-kleur.
	Rows->ClearChildren();
	if (Cents > 0)
	{
		// Centen -> hele euro's (afgerond naar boven zodat een deal nooit "+EUR 0" toont).
		const int32 Euros = FMath::Max(1, (Cents + 99) / 100);
		AddLine(FString::Printf(TEXT("+EUR %d"), Euros), WeedUI::ColGood(), /*bBig*/ true);
	}
	if (XP > 0) { AddLine(FString::Printf(TEXT("+%d XP"), XP), WeedUI::ColAccent()); }
	if (dR > 0) { AddLine(FString::Printf(TEXT("+%d respect"), dR), WeedUI::ColText()); }
	if (dL > 0) { AddLine(FString::Printf(TEXT("+%d loyalty"), dL), WeedUI::ColHighlight()); }
	if (dA > 0) { AddLine(FString::Printf(TEXT("+%d hooked"), dA), WeedUI::ColWarn()); }

	// Niks zinnigs om te tonen? Dan toch een korte bevestiging zodat de popup nooit leeg is.
	if (Rows->GetChildrenCount() == 0)
	{
		AddLine(TEXT("Sold!"), WeedUI::ColGood(), true);
	}

	Age = 0.f;
	LifeTime = GPopLife;
	bActive = true;
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card->SetRenderOpacity(0.f);
}

void UDealResultPopupWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!Card) { return; }

	if (!bActive)
	{
		if (Card->GetVisibility() != ESlateVisibility::Collapsed) { Card->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	Age += DeltaTime;
	if (Age >= LifeTime)
	{
		// Klaar: verberg + verwijder de widget helemaal (spawner maakt een nieuwe bij de volgende deal).
		bActive = false;
		Card->SetRenderOpacity(0.f);
		Card->SetVisibility(ESlateVisibility::Collapsed);
		RemoveFromParent();
		return;
	}

	// --- Ankerpunt bepalen: boven het hoofd van de klant, anders de meegegeven fallback-locatie. ---
	FVector Anchor = FallbackWorld;
	if (ACustomerBase* C = AnchorCustomer.Get())
	{
		Anchor = C->GetActorLocation();
		// Boven het hoofd: halve capsule-hoogte + marge (fallback als er geen capsule is).
		float Half = 90.f;
		if (const UCapsuleComponent* Cap = C->GetCapsuleComponent()) { Half = Cap->GetScaledCapsuleHalfHeight(); }
		Anchor.Z += Half + 40.f;
	}

	// --- Projecteer naar het scherm. Achter de camera / niet-projecteerbaar -> even verbergen. ---
	APlayerController* PC = GetOwningPlayer();
	FVector2D Screen = FVector2D::ZeroVector;
	const bool bOnScreen = PC && PC->ProjectWorldLocationToScreen(Anchor, Screen, /*bPlayerViewportRelative*/ true);
	if (!bOnScreen)
	{
		Card->SetRenderOpacity(0.f);
		if (Card->GetVisibility() != ESlateVisibility::Collapsed) { Card->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	// Scherm-coordinaten (pixels, viewport-relatief) -> canvas-coordinaten (DPI-schaal terugrekenen,
	// zelfde idioom als de rest van de UI: UWidgetLayoutLibrary::GetViewportScale).
	float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
	if (DPI <= 0.f) { DPI = 1.f; }
	const FVector2D CanvasPos = Screen / DPI;
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Card->Slot))
	{
		CS->SetPosition(CanvasPos);
	}

	// --- Fade: in (GPopFadeIn) -> hold -> uit (GPopFadeOut). ---
	float Op = 1.f;
	if (Age < GPopFadeIn) { Op = Age / GPopFadeIn; }
	else if (Age > GPopFadeIn + GPopHold) { Op = FMath::Clamp((LifeTime - Age) / GPopFadeOut, 0.f, 1.f); }
	Card->SetRenderOpacity(Op);
	if (Card->GetVisibility() != ESlateVisibility::HitTestInvisible) { Card->SetVisibility(ESlateVisibility::HitTestInvisible); }
}

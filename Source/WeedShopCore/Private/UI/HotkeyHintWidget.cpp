#include "UI/HotkeyHintWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Placement/BuildComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"
#include "Inventory/InventoryComponent.h"
#include "Customer/CustomerBase.h"
#include "Cultivation/GrowPlant.h"
#include "Input/ControlSettings.h"

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
#include "GameFramework/Pawn.h"
#include "Misc/ConfigCacheIni.h"

bool UHotkeyHintWidget::AreHintsEnabled()
{
	bool bOn = true;
	if (GConfig) { GConfig->GetBool(TEXT("ThePlugSIM.UI"), TEXT("ShowControlHints"), bOn, GGameUserSettingsIni); }
	return bOn;
}

void UHotkeyHintWidget::SetHintsEnabled(bool bEnabled)
{
	if (GConfig)
	{
		GConfig->SetBool(TEXT("ThePlugSIM.UI"), TEXT("ShowControlHints"), bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

TSharedRef<SWidget> UHotkeyHintWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UHotkeyHintWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HintCard"));
	CardB->SetBrush(WeedUI::Rounded(WeedUI::ColPanel(0.8f), 10.f));
	CardB->SetPadding(FMargin(9.f, 6.f, 10.f, 7.f));
	CardB->SetVisibility(ESlateVisibility::HitTestInvisible);
	// Clip binnen het paneel: als de lijst bij een scroll/hint-wissel 1 frame groter is dan het AutoSize-paneel
	// (re-measure-lag), wordt de overtollige rij AFGESNEDEN i.p.v. buiten het paneel getoond (speler-klacht: tekst
	// die er even buiten valt). De rij verschijnt gewoon een frame later netjes binnen het paneel.
	CardB->SetClipping(EWidgetClipping::ClipToBounds);
	Card = CardB;

	// Rechtsonder verankerd (alle controls staan hier, op één compacte plek).
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
	CS->SetAlignment(FVector2D(1.f, 1.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(-14.f, -14.f));

	// Vaste breedte zodat de tekst links netjes uitlijnt en de toets-tags rechts uitlijnen.
	USizeBox* ListSz = WidgetTree->ConstructWidget<USizeBox>();
	ListSz->SetWidthOverride(190.f);
	List = WidgetTree->ConstructWidget<UVerticalBox>();
	ListSz->SetContent(List);
	CardB->SetContent(ListSz);
	BuildRowPool(12); // POOL: rijen 1x bouwen; de tick vult ze in-place (geen ClearChildren/teardown = geen flits)

	// Gecentreerde interactie-popup (net onder het midden / crosshair): toont wat je aankijkt
	// ("LOCKED - X lives here", "Open door", ...) als nette popup i.p.v. in de hoek.
	CenterPromptCard = WidgetTree->ConstructWidget<UBorder>();
	CenterPromptCard->SetBrush(WeedUI::Rounded(WeedUI::ColPanel(0.85f), 8.f));
	CenterPromptCard->SetPadding(FMargin(14.f, 7.f));
	CenterPromptCard->SetVisibility(ESlateVisibility::Collapsed);
	CenterPromptText = WeedUI::Text(WidgetTree, TEXT(""), 15, FLinearColor(0.95f, 0.97f, 1.f), true, true);
	CenterPromptCard->SetContent(CenterPromptText);
	UCanvasPanelSlot* PS = Root->AddChildToCanvas(CenterPromptCard);
	PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PS->SetAlignment(FVector2D(0.5f, 0.f));
	PS->SetAutoSize(true);
	PS->SetPosition(FVector2D(0.f, 70.f)); // net onder het crosshair (bij het object - dat is z'n functie)
}

void UHotkeyHintWidget::BuildRowPool(int32 Count)
{
	if (!List) { return; }
	for (int32 i = 0; i < Count; ++i)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

		// Omschrijving LINKS (vult de rij, dus alle tekst begint op één verticale lijn). 1x gebouwd; tekst in-place.
		UTextBlock* Label = WeedUI::Text(WidgetTree, TEXT(""), 11, FLinearColor(0.86f, 0.89f, 0.95f));
		UHorizontalBoxSlot* LSlot = Row->AddChildToHorizontalBox(Label);
		LSlot->SetVerticalAlignment(VAlign_Center);
		LSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Toets-"chip" RECHTS (auto-breedte, tegen de rechterrand uitgelijnd). Brush 1x gezet, niet per rebuild.
		UBorder* Chip = WidgetTree->ConstructWidget<UBorder>();
		Chip->SetBrush(WeedUI::Rounded(WeedUI::ColSlot(0.95f), 5.f));
		Chip->SetPadding(FMargin(6.f, 1.f, 6.f, 1.f));
		UTextBlock* KeyT = WeedUI::Text(WidgetTree, TEXT(""), 10, WeedUI::ColAccent(), true, true);
		Chip->SetContent(KeyT);
		UHorizontalBoxSlot* CSlot = Row->AddChildToHorizontalBox(Chip);
		CSlot->SetVerticalAlignment(VAlign_Center);
		CSlot->SetHorizontalAlignment(HAlign_Right);
		CSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));

		List->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 1.5f, 0.f, 1.5f));
		Row->SetVisibility(ESlateVisibility::Collapsed); // ongebruikt tot de tick 'm vult
		RowPool.Add(Row); RowLabels.Add(Label); RowKeys.Add(KeyT);
	}
}

void UHotkeyHintWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!List) { return; }

	// In de instellingen uit te zetten.
	const bool bEnabled = AreHintsEnabled();
	if (Card) { Card->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bEnabled) { if (CenterPromptCard) { CenterPromptCard->SetVisibility(ESlateVisibility::Collapsed); LastFocusPrompt.Empty(); } return; }

	APawn* P = GetOwningPlayerPawn();
	if (!P) { return; }

	FString FocusPrompt; // wat je aankijkt -> gecentreerde popup (niet in de hoek-kaart)

	// Component-cache (weak + pawn-check): niet 4x FindComponentByClass per tick.
	if (CachedCompPawn.Get() != P || !CachedPhone.IsValid() || !CachedBuild.IsValid() || !CachedInteract.IsValid() || !CachedInv.IsValid())
	{
		CachedCompPawn = P;
		CachedPhone = P->FindComponentByClass<UPhoneClientComponent>();
		CachedBuild = P->FindComponentByClass<UBuildComponent>();
		CachedInteract = P->FindComponentByClass<UInteractionComponent>();
		CachedInv = P->FindComponentByClass<UInventoryComponent>();
	}
	UPhoneClientComponent* Phone = CachedPhone.Get();
	UBuildComponent* Build = CachedBuild.Get();
	UInteractionComponent* Interact = CachedInteract.Get();
	UInventoryComponent* Inv = CachedInv.Get();

	// Focus-HOLD: houd de laatste geldige focus ~0.2s vast. Een 1-frame trace-miss (een crowd-body die langs de
	// crosshair drijft) gooit zo de hint-set NIET om -> de sig blijft stabiel -> geen constante rebuild = geen flits.
	AActor* RawFocus = Interact ? Interact->GetFocusedActor() : nullptr;
	if (RawFocus) { HeldFocus = RawFocus; FocusHoldTimer = 0.f; }
	else if (HeldFocus.IsValid() && FocusHoldTimer < 0.2f) { FocusHoldTimer += DeltaTime; }
	else { HeldFocus = nullptr; }
	AActor* Focus = HeldFocus.Get();

	// Bouw de lijst (key,label) op basis van de context. De herbindbare toetsen komen uit de instellingen.
	UControlSettings* CS = UControlSettings::Get();
	auto K = [CS](const TCHAR* Action) { return CS->GetKey(FName(Action), false).GetDisplayName().ToString(); };
	TArray<TPair<FString, FString>> Hints;

	const bool bPlacing = Build && Build->IsPlacing();
	const bool bPhone = Phone && Phone->IsOpen();
	const bool bInv = Phone && Phone->IsInventoryOpen();
	const bool bRoll = Phone && Phone->IsRollOpen();
	const bool bDeal = Phone && Phone->IsDealOpen();
	const bool bPot = Phone && Phone->IsPotUpgradeOpen();

	if (bPlacing)
	{
		Hints.Emplace(TEXT("LMB"), TEXT("Place"));
		Hints.Emplace(K(TEXT("Rotate")), TEXT("Rotate"));
		Hints.Emplace(TEXT("Shift"), TEXT("Snap to grid"));
		Hints.Emplace(TEXT("Scroll"), TEXT("Put away"));
		// Kijk je tijdens het plaatsen een NIET-plaatsbaar wereld-object aan (deur/lift), dan kun je er nog
		// steeds mee interacten (F) i.p.v. plaatsen -> toon de prompt + de Interact-toets.
		// (Focus = de vastgehouden focus, hierboven bepaald.)
		if (Focus && Build && !Build->IsPickable(Focus))
		{
			FString Prompt = TEXT("Interact");
			const FText T = IInteractable::Execute_GetInteractionPrompt(Focus);
			if (!T.IsEmpty()) { Prompt = T.ToString(); }
			FocusPrompt = Prompt;
			Hints.Emplace(K(TEXT("Interact")), TEXT("Interact"));
		}
	}
	else if (bPhone)
	{
		Hints.Emplace(TEXT("LMB"), TEXT("Use"));
		Hints.Emplace(K(TEXT("PhoneTab")), TEXT("Switch app"));
		Hints.Emplace(K(TEXT("Phone")), TEXT("Close phone"));
	}
	else if (bInv)
	{
		Hints.Emplace(TEXT("Drag"), TEXT("Move items"));
		Hints.Emplace(K(TEXT("Inventory")), TEXT("Close inventory"));
	}
	else if (bRoll)
	{
		Hints.Emplace(TEXT("LMB"), TEXT("Pick grams / roll"));
		Hints.Emplace(TEXT("RMB"), TEXT("Close"));
	}
	else if (bDeal)
	{
		Hints.Emplace(TEXT("LMB"), TEXT("Adjust / confirm"));
	}
	else if (bPot)
	{
		Hints.Emplace(TEXT("LMB"), TEXT("Buy upgrade"));
		Hints.Emplace(K(TEXT("PotUpgrade")), TEXT("Close"));
	}
	else
	{
		// Vrij rondlopen: context van wat je aankijkt (vastgehouden focus) + wat je vasthoudt.
		if (Focus)
		{
			FString Prompt = TEXT("Interact");
			if (Focus->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
			{
				const FText T = IInteractable::Execute_GetInteractionPrompt(Focus);
				if (!T.IsEmpty()) { Prompt = T.ToString(); }
			}
			// De prompt-tekst (LOCKED / Open door / ...) komt als GECENTREERDE popup, niet in de hoek.
			FocusPrompt = Prompt;
			const FName ActiveF = Inv ? Inv->GetActiveItemId() : NAME_None;
			const bool bJointHand = ActiveF.ToString().StartsWith(TEXT("Joint_"));
			// Klant + joint in de hand -> korte hold om 'm een hit te geven.
			if (Cast<ACustomerBase>(Focus) && bJointHand) { Hints.Emplace(TEXT("Hold LMB"), TEXT("Give them a hit")); }
			const AGrowPlant* FocusPot = Cast<AGrowPlant>(Focus);
			if (FocusPot) { Hints.Emplace(K(TEXT("PotUpgrade")), TEXT("Upgrade pot")); }
			// Geplante pot: X inhouden gooit de plant weg (voortgangsbalk op de HUD).
			if (FocusPot && FocusPot->IsPlanted()) { Hints.Emplace(TEXT("Hold X"), TEXT("Discard plant")); }
			// Plaatsbare objecten kun je oppakken (G inhouden).
			if (Build && Build->IsPickable(Focus)) { Hints.Emplace(TEXT("Hold G"), TEXT("Pick up")); }
		}

		const FName Active = Inv ? Inv->GetActiveItemId() : NAME_None;
		const FString AS = Active.ToString();
		if (AS.StartsWith(TEXT("Papers_")))
		{
			if (Phone->IsRollLoadedUI())
			{
				// De geladen-omschrijving staat nu bij de hand-preview (UHandInfoWidget), niet meer hier.
				Hints.Emplace(TEXT("Hold RMB"), TEXT("Roll the joint"));
			}
			else { Hints.Emplace(K(TEXT("RollLoad")), TEXT("Load")); }
		}
		else if (AS.StartsWith(TEXT("Joint_"))) { Hints.Emplace(TEXT("Hold RMB"), TEXT("Smoke joint")); }

		if (!AS.IsEmpty()) { Hints.Emplace(TEXT("Hold Q"), TEXT("Drop item")); }

		Hints.Emplace(K(TEXT("Phone")), TEXT("Phone"));
		Hints.Emplace(K(TEXT("Inventory")), TEXT("Inventory"));
		Hints.Emplace(TEXT("1-8"), TEXT("Hotbar slot"));
		Hints.Emplace(TEXT("Scroll"), TEXT("Switch slot"));
		Hints.Emplace(TEXT("Esc"), TEXT("Pause / menu"));
	}

	// Gecentreerde interactie-popup bijwerken (onafhankelijk van de hoek-kaart).
	// Changed-check: SetText/visibility alleen als de prompt echt wijzigde.
	if (CenterPromptCard && CenterPromptText && FocusPrompt != LastFocusPrompt)
	{
		LastFocusPrompt = FocusPrompt;
		if (!FocusPrompt.IsEmpty())
		{
			CenterPromptText->SetText(FText::FromString(FocusPrompt));
			CenterPromptCard->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else { CenterPromptCard->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// Signature -> alleen herbouwen bij wijziging (geen flicker).
	FString Sig;
	for (const TPair<FString, FString>& H : Hints) { Sig += H.Key; Sig += TEXT("="); Sig += H.Value; Sig += TEXT("|"); }
	if (Sig == LastSig) { return; }
	LastSig = Sig;

	// POOL-update (geen ClearChildren/rebuild = geen teardown-flits): vul de eerste N rijen in-place, verberg de rest.
	for (int32 i = 0; i < RowPool.Num(); ++i)
	{
		if (i < Hints.Num())
		{
			if (RowLabels[i]) { RowLabels[i]->SetText(FText::FromString(Hints[i].Value)); }
			if (RowKeys[i])   { RowKeys[i]->SetText(FText::FromString(Hints[i].Key)); }
			if (RowPool[i])   { RowPool[i]->SetVisibility(ESlateVisibility::HitTestInvisible); }
		}
		else if (RowPool[i]) { RowPool[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

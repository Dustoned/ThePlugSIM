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

namespace
{
	void NormalizePromptWhitespace(FString& S)
	{
		S.TrimStartAndEndInline();
		while (S.Contains(TEXT("  "))) { S.ReplaceInline(TEXT("  "), TEXT(" ")); }
	}

	// ND7.12: label voor de controls-kaart rechtsonder als de center-popup uitstaat - de beschrijvende
	// prompt-tekst zonder de pickup-staart (daar is de aparte "Hold G / Pick up"-rij al voor).
	FString CornerPromptLabel(const FString& RawPrompt)
	{
		FString S = RawPrompt;
		NormalizePromptWhitespace(S);
		S.ReplaceInline(TEXT(" - hold G to pick up"), TEXT(""));
		S.ReplaceInline(TEXT(" - Hold G to pick up"), TEXT(""));
		NormalizePromptWhitespace(S);
		return S;
	}

	void BuildCenterPromptParts(const FString& RawPrompt, const FString& InteractKey, bool bPickable,
		FString& OutTitle, FString& OutKey, FString& OutAction)
	{
		OutTitle = RawPrompt;
		OutKey = InteractKey;
		OutAction = TEXT("Interact");
		NormalizePromptWhitespace(OutTitle);

		FString PickTitle = OutTitle;
		const bool bHasPickupTail = PickTitle.ReplaceInline(TEXT(" - hold G to pick up"), TEXT("")) > 0
			|| PickTitle.ReplaceInline(TEXT(" - Hold G to pick up"), TEXT("")) > 0;
		if (bHasPickupTail)
		{
			NormalizePromptWhitespace(PickTitle);
			OutTitle = PickTitle;
			OutKey = TEXT("Hold G");
			OutAction = TEXT("Pick up");
		}

		FString Left, Right;
		if (OutTitle.Split(TEXT(" - "), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			NormalizePromptWhitespace(Left);
			NormalizePromptWhitespace(Right);
			if (!Left.IsEmpty()) { OutTitle = Left; }
			if (!Right.IsEmpty())
			{
				OutKey = InteractKey;
				OutAction = Right;
				if (!Right.IsEmpty())
				{
					OutAction = Right.Left(1).ToUpper() + Right.RightChop(1);
				}
			}
		}

		if (OutTitle.StartsWith(TEXT("Pick up ")))
		{
			OutAction = TEXT("Pick up");
			OutTitle = OutTitle.RightChop(8);
			NormalizePromptWhitespace(OutTitle);
		}
		else if (bHasPickupTail && !bPickable)
		{
			OutKey = InteractKey;
			OutAction = TEXT("Interact");
		}
		else if (bPickable && OutAction == TEXT("Interact"))
		{
			OutKey = TEXT("Hold G");
			OutAction = TEXT("Pick up");
		}
		if (OutTitle.IsEmpty()) { OutTitle = RawPrompt; NormalizePromptWhitespace(OutTitle); }
	}

	bool IsAmbientWalkHint(const FString& Label)
	{
		return Label == TEXT("Phone")
			|| Label == TEXT("Inventory")
			|| Label == TEXT("Hotbar slot")
			|| Label == TEXT("Switch slot")
			|| Label == TEXT("Pause / menu");
	}

	FSlateBrush HintKeyBrush(bool bAmbient, bool bHold)
	{
		const FLinearColor Fill = bAmbient
			? WeedUI::ColInner(0.56f)
			: (bHold ? WeedUI::ColAccentDim(0.92f) : WeedUI::ColSlot(0.96f));
		FSlateBrush B = WeedUI::Rounded(Fill, 5.f);
		B.OutlineSettings.Width = 1.f;
		B.OutlineSettings.Color = FSlateColor(bAmbient ? WeedUI::ColStroke(0.18f) : WeedUI::ColAccentDim(0.38f));
		return B;
	}
}

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
	FSlateBrush PromptBrush = WeedUI::Rounded(WeedUI::ColPanel(0.64f), 7.f);
	PromptBrush.OutlineSettings.Width = 1.f;
	PromptBrush.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.28f));
	CenterPromptCard->SetBrush(PromptBrush);
	CenterPromptCard->SetPadding(FMargin(10.f, 6.f, 10.f, 7.f));
	CenterPromptCard->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBox* PromptCol = WidgetTree->ConstructWidget<UVerticalBox>();
	CenterPromptTitle = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.95f, 0.97f, 1.f), true, true);
	CenterPromptTitle->SetMinDesiredWidth(96.f);
	CenterPromptTitle->SetAutoWrapText(true);
	CenterPromptTitle->SetWrapTextAt(260.f);
	PromptCol->AddChildToVerticalBox(CenterPromptTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
	UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	CenterPromptKey = WeedUI::Text(WidgetTree, TEXT(""), 9, FLinearColor::White, false, true);
	CenterPromptKeyPill = WidgetTree->ConstructWidget<UBorder>();
	CenterPromptKeyPill->SetBrush(WeedUI::Rounded(WeedUI::ColAccentDim(0.96f), 5.f));
	CenterPromptKeyPill->SetPadding(FMargin(6.f, 0.f, 6.f, 2.f));
	CenterPromptKeyPill->SetContent(CenterPromptKey);
	UHorizontalBoxSlot* KeyS = ActionRow->AddChildToHorizontalBox(CenterPromptKeyPill);
	KeyS->SetVerticalAlignment(VAlign_Center);
	KeyS->SetPadding(FMargin(0.f, 0.f, 7.f, 0.f));
	CenterPromptAction = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, true);
	ActionRow->AddChildToHorizontalBox(CenterPromptAction)->SetVerticalAlignment(VAlign_Center);
	PromptCol->AddChildToVerticalBox(ActionRow)->SetHorizontalAlignment(HAlign_Center);
	CenterPromptProgressTrack = WidgetTree->ConstructWidget<UBorder>();
	CenterPromptProgressTrack->SetBrush(WeedUI::Rounded(WeedUI::ColWell(0.62f), 2.f));
	CenterPromptProgressTrack->SetPadding(FMargin(0.f));
	CenterPromptProgressTrack->SetHorizontalAlignment(HAlign_Left);
	CenterPromptProgressTrack->SetVisibility(ESlateVisibility::Collapsed);
	CenterPromptProgressFillSize = WidgetTree->ConstructWidget<USizeBox>();
	CenterPromptProgressFillSize->SetWidthOverride(0.f);
	CenterPromptProgressFillSize->SetHeightOverride(3.f);
	CenterPromptProgressFill = WidgetTree->ConstructWidget<UBorder>();
	CenterPromptProgressFill->SetBrush(WeedUI::Rounded(WeedUI::ColAccentDim(0.95f), 2.f));
	CenterPromptProgressFillSize->SetContent(CenterPromptProgressFill);
	CenterPromptProgressTrack->SetContent(CenterPromptProgressFillSize);
	PromptCol->AddChildToVerticalBox(CenterPromptProgressTrack)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	CenterPromptCard->SetContent(PromptCol);
	UCanvasPanelSlot* PS = Root->AddChildToCanvas(CenterPromptCard);
	PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PS->SetAlignment(FVector2D(0.5f, 0.f));
	PS->SetAutoSize(true);
	PS->SetPosition(FVector2D(0.f, 62.f)); // net onder het crosshair (bij het object - dat is z'n functie)
}

void UHotkeyHintWidget::BuildRowPool(int32 Count)
{
	if (!List) { return; }
	for (int32 i = 0; i < Count; ++i)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

		// Omschrijving LINKS (vult de rij, dus alle tekst begint op één verticale lijn). 1x gebouwd; tekst in-place.
		UTextBlock* Label = WeedUI::Text(WidgetTree, TEXT(""), 11, FLinearColor(0.86f, 0.89f, 0.95f));
		// ND7.12: prompt-teksten ("LOCKED - ...") kunnen lang zijn -> wrappen binnen de kaart i.p.v. clippen.
		Label->SetAutoWrapText(true);
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
		RowPool.Add(Row); RowLabels.Add(Label); RowKeys.Add(KeyT); RowKeyPills.Add(Chip);
	}
}

void UHotkeyHintWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!List) { return; }

	// In de instellingen uit te zetten. "Controls overlay" bestuurt de kaart rechtsonder; "Interaction prompt"
	// (ND7.12) de center-popup. Popup UIT -> de prompt-tekst verhuist als rij naar de kaart rechtsonder.
	const bool bControlsEnabled = AreHintsEnabled();
	const bool bCenterPrompt = UInteractionComponent::IsInteractPromptEnabled();
	if (Card) { Card->SetVisibility(bControlsEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }

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
			// ND7.12: popup uit -> de beschrijvende actie-tekst i.p.v. het generieke "Interact".
			Hints.Emplace(K(TEXT("Interact")), bCenterPrompt ? FString(TEXT("Interact")) : CornerPromptLabel(Prompt));
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
			// ND7.12 (setting "Interaction prompt" UIT): geen popup onder het crosshair -> toon de
			// beschrijvende actie-tekst ("Go to floor 3", "Open door") als rij in de kaart rechtsonder.
			if (!bCenterPrompt) { Hints.Emplace(K(TEXT("Interact")), CornerPromptLabel(Prompt)); }
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
		Hints.Emplace(TEXT("Scroll"), TEXT("Switch slot"));
		Hints.Emplace(TEXT("Esc"), TEXT("Pause / menu"));
	}

	bool bHasContextHint = false;
	for (const TPair<FString, FString>& H : Hints)
	{
		if (!IsAmbientWalkHint(H.Value)) { bHasContextHint = true; break; }
	}
	if (Card) { Card->SetRenderOpacity(bHasContextHint ? 1.f : 0.64f); }

	// Gecentreerde interactie-popup bijwerken (onafhankelijk van de hoek-kaart).
	// Changed-check: SetText/visibility alleen als de prompt echt wijzigde.
	const bool bFocusPickable = Focus && Build && Build->IsPickable(Focus);
	// ND7.12: de aan/uit-vlag zit mee in de sig zodat een live toggle in de settings direct doorwerkt.
	const FString PromptSig = FocusPrompt + TEXT("|") + (bFocusPickable ? TEXT("pick") : TEXT("interact")) + TEXT("|") + K(TEXT("Interact")) + (bCenterPrompt ? TEXT("|on") : TEXT("|off"));
	if (CenterPromptCard && CenterPromptTitle && CenterPromptKey && CenterPromptAction && PromptSig != LastFocusPrompt)
	{
		LastFocusPrompt = PromptSig;
		if (bCenterPrompt && !FocusPrompt.IsEmpty())
		{
			FString PromptTitle, PromptKey, PromptAction;
			BuildCenterPromptParts(FocusPrompt, K(TEXT("Interact")), bFocusPickable, PromptTitle, PromptKey, PromptAction);
			CenterPromptTitle->SetText(FText::FromString(PromptTitle));
			CenterPromptKey->SetText(FText::FromString(PromptKey));
			CenterPromptAction->SetText(FText::FromString(PromptAction));
			if (CenterPromptKeyPill)
			{
				CenterPromptKeyPill->SetBrush(HintKeyBrush(false, PromptKey.StartsWith(TEXT("Hold"))));
			}
			CenterPromptCard->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else { CenterPromptCard->SetVisibility(ESlateVisibility::Collapsed); }
	}
	if (CenterPromptProgressTrack && CenterPromptProgressFillSize && CenterPromptProgressFill)
	{
		float HoldAlpha = 0.f;
		FLinearColor HoldCol = WeedUI::ColAccentDim(0.95f);
		if (!FocusPrompt.IsEmpty() && Build && Focus)
		{
			HoldAlpha = FMath::Max(HoldAlpha, Build->GetPickupAlpha());
			if (const AGrowPlant* FocusPot = Cast<AGrowPlant>(Focus))
			{
				if (FocusPot->IsPlanted())
				{
					const float DiscardAlpha = Build->GetDiscardAlpha();
					if (DiscardAlpha > HoldAlpha)
					{
						HoldAlpha = DiscardAlpha;
						HoldCol = FLinearColor(0.95f, 0.36f, 0.20f, 0.95f);
					}
				}
			}
		}
		if (HoldAlpha > 0.f)
		{
			CenterPromptProgressFillSize->SetWidthOverride(136.f * FMath::Clamp(HoldAlpha, 0.f, 1.f));
			CenterPromptProgressFill->SetBrush(WeedUI::Rounded(HoldCol, 2.f));
			CenterPromptProgressTrack->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			CenterPromptProgressTrack->SetVisibility(ESlateVisibility::Collapsed);
		}
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
			const bool bAmbient = IsAmbientWalkHint(Hints[i].Value);
			const bool bHold = Hints[i].Key.StartsWith(TEXT("Hold"));
			if (RowLabels[i])
			{
				RowLabels[i]->SetText(FText::FromString(Hints[i].Value));
				RowLabels[i]->SetColorAndOpacity(FSlateColor(bAmbient ? FLinearColor(0.58f, 0.61f, 0.70f, 1.f) : FLinearColor(0.92f, 0.95f, 1.f, 1.f)));
			}
			if (RowKeys[i])
			{
				RowKeys[i]->SetText(FText::FromString(Hints[i].Key));
				RowKeys[i]->SetColorAndOpacity(FSlateColor(bAmbient ? FLinearColor(0.70f, 0.73f, 0.84f, 1.f) : WeedUI::ColAccent()));
			}
			if (RowKeyPills.IsValidIndex(i) && RowKeyPills[i])
			{
				RowKeyPills[i]->SetBrush(HintKeyBrush(bAmbient, bHold));
			}
			if (RowPool[i])
			{
				RowPool[i]->SetRenderOpacity(bAmbient ? (bHasContextHint ? 0.34f : 0.58f) : 1.f);
				RowPool[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
		else if (RowPool[i]) { RowPool[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

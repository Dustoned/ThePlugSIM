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

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HintCard"));
	Card->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.07f, 0.78f), 14.f));
	Card->SetPadding(FMargin(12.f, 10.f, 12.f, 10.f));
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Rechtsonder verankerd.
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Card);
	CS->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
	CS->SetAlignment(FVector2D(1.f, 1.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(-18.f, -18.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(VB);

	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("CONTROLS"), 11, FLinearColor(0.55f, 0.75f, 1.f), false, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	List = WidgetTree->ConstructWidget<UVerticalBox>();
	VB->AddChildToVerticalBox(List);
}

void UHotkeyHintWidget::AddRow(const FString& Key, const FString& Action)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

	// Toets-"chip": afgerond vakje met de toets erin, vaste breedte zodat de labels uitlijnen.
	USizeBox* ChipSz = WidgetTree->ConstructWidget<USizeBox>();
	ChipSz->SetMinDesiredWidth(64.f);
	UBorder* Chip = WidgetTree->ConstructWidget<UBorder>();
	Chip->SetBrush(WeedUI::Rounded(FLinearColor(0.16f, 0.18f, 0.24f, 0.95f), 6.f));
	Chip->SetPadding(FMargin(7.f, 3.f, 7.f, 3.f));
	Chip->SetContent(WeedUI::Text(WidgetTree, Key, 11, FLinearColor(1.f, 0.95f, 0.7f), true, true));
	ChipSz->SetContent(Chip);
	UHorizontalBoxSlot* CSlot = Row->AddChildToHorizontalBox(ChipSz);
	CSlot->SetVerticalAlignment(VAlign_Center);
	CSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

	UHorizontalBoxSlot* LSlot = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Action, 12, FLinearColor(0.88f, 0.9f, 0.95f)));
	LSlot->SetVerticalAlignment(VAlign_Center);

	List->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
}

void UHotkeyHintWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!List) { return; }

	APawn* P = GetOwningPlayerPawn();
	if (!P) { return; }

	UPhoneClientComponent* Phone = P->FindComponentByClass<UPhoneClientComponent>();
	UBuildComponent* Build = P->FindComponentByClass<UBuildComponent>();
	UInteractionComponent* Interact = P->FindComponentByClass<UInteractionComponent>();
	UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>();

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
		// Vrij rondlopen: context van wat je aankijkt + wat je vasthoudt.
		AActor* Focus = Interact ? Interact->GetFocusedActor() : nullptr;
		if (Focus)
		{
			FString Prompt = TEXT("Interact");
			if (Focus->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
			{
				const FText T = IInteractable::Execute_GetInteractionPrompt(Focus);
				if (!T.IsEmpty()) { Prompt = T.ToString(); }
			}
			const FName ActiveF = Inv ? Inv->GetActiveItemId() : NAME_None;
			const bool bJointHand = ActiveF.ToString().StartsWith(TEXT("Joint_"));
			// Klant + joint in de hand -> korte hold om 'm een hit te geven; anders normaal interacten.
			if (Cast<ACustomerBase>(Focus) && bJointHand) { Hints.Emplace(TEXT("Hold LMB"), TEXT("Give them a hit")); }
			else { Hints.Emplace(K(TEXT("Interact")), Prompt); }
			if (Cast<AGrowPlant>(Focus)) { Hints.Emplace(K(TEXT("PotUpgrade")), TEXT("Upgrade pot")); }
		}

		const FName Active = Inv ? Inv->GetActiveItemId() : NAME_None;
		const FString AS = Active.ToString();
		if (AS.StartsWith(TEXT("Papers_")))
		{
			if (Phone->IsRollLoadedUI()) { Hints.Emplace(TEXT("Hold RMB"), FString::Printf(TEXT("Roll joint (%dg)"), Phone->GetRollLoadGramsUI())); }
			else { Hints.Emplace(K(TEXT("RollLoad")), TEXT("Load with weed")); }
		}
		else if (AS.StartsWith(TEXT("Joint_"))) { Hints.Emplace(TEXT("Hold RMB"), TEXT("Smoke joint")); }

		Hints.Emplace(K(TEXT("Phone")), TEXT("Phone"));
		Hints.Emplace(K(TEXT("Inventory")), TEXT("Inventory"));
		Hints.Emplace(TEXT("1-8"), TEXT("Hotbar slot"));
	}

	// Signature -> alleen herbouwen bij wijziging (geen flicker).
	FString Sig;
	for (const TPair<FString, FString>& H : Hints) { Sig += H.Key; Sig += TEXT("="); Sig += H.Value; Sig += TEXT("|"); }
	if (Sig == LastSig) { return; }
	LastSig = Sig;

	List->ClearChildren();
	for (const TPair<FString, FString>& H : Hints) { AddRow(H.Key, H.Value); }
}

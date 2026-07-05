#include "UI/WeedToast.h"

#include "UI/WeedUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Phone/PhoneClientComponent.h"
#include "WeedShopCore.h"

#include <initializer_list>

TWeakObjectPtr<UWeedToast> UWeedToast::Active;

// Icoon-sentinel: het icoon reist als prefix "[[i:<stem>]]" mee in de Msg-string, zodat de bestaande
// Toast/ClientToast-RPC-route (PhoneClientComponent) ONGEWIJZIGD blijft. Notify parseert 'm er weer uit.
static const TCHAR* GToastIconOpen = TEXT("[[i:");
static const TCHAR* GToastIconClose = TEXT("]]");

// Haal een eventuele icoon-prefix uit Msg. Geeft de schone tekst terug; OutIcon = de icoon-stem (of leeg).
static FString WeedToast_ParseIcon(const FString& Msg, FString& OutIcon)
{
	OutIcon.Reset();
	if (!Msg.StartsWith(GToastIconOpen)) { return Msg; }
	const int32 Close = Msg.Find(GToastIconClose, ESearchCase::CaseSensitive, ESearchDir::FromStart, 4);
	if (Close == INDEX_NONE) { return Msg; }
	OutIcon = Msg.Mid(4, Close - 4);
	return Msg.Mid(Close + 2);
}

static bool WeedToast_HasAny(const FString& Lower, std::initializer_list<const TCHAR*> Needles)
{
	for (const TCHAR* Needle : Needles)
	{
		if (Lower.Contains(Needle)) { return true; }
	}
	return false;
}

static FString WeedToast_AutoIcon(const FString& Msg, const FLinearColor& Color)
{
	const FString L = Msg.ToLower();
	if (WeedToast_HasAny(L, { TEXT("goal complete"), TEXT("level up"), TEXT("new level"), TEXT("license") })) { return TEXT("ui_level"); }
	if (WeedToast_HasAny(L, { TEXT("eur"), TEXT("cash"), TEXT("sold"), TEXT("paid"), TEXT("rent") })) { return TEXT("ui_coin"); }
	if (WeedToast_HasAny(L, { TEXT("bank"), TEXT("deposit"), TEXT("withdraw"), TEXT("transfer"), TEXT("launder") })) { return TEXT("ui_bank"); }
	if (WeedToast_HasAny(L, { TEXT("message"), TEXT("contact"), TEXT("appointment"), TEXT("chat") })) { return TEXT("ui_message"); }
	if (WeedToast_HasAny(L, { TEXT("customer"), TEXT("npc"), TEXT("someone else"), TEXT("turned you down") })) { return TEXT("ui_person"); }
	if (WeedToast_HasAny(L, { TEXT("joint"), TEXT("roll"), TEXT("smok") })) { return TEXT("joint_rolled"); }
	if (WeedToast_HasAny(L, { TEXT("seed"), TEXT("plant"), TEXT("harvest"), TEXT("soil"), TEXT("fertiliz"), TEXT("mold"), TEXT("pest") })) { return TEXT("weedleaf"); }
	if (WeedToast_HasAny(L, { TEXT("water"), TEXT("bottle"), TEXT("sink") })) { return TEXT("drop"); }
	if (WeedToast_HasAny(L, { TEXT("inventory"), TEXT("picked up"), TEXT("backpack"), TEXT("slot"), TEXT("shelf"), TEXT("fridge"), TEXT("drying rack"), TEXT("pack") })) { return TEXT("ui_package"); }
	if (WeedToast_HasAny(L, { TEXT("heat"), TEXT("bust"), TEXT("robbery"), TEXT("police"), TEXT("locked"), TEXT("blocked"), TEXT("overdue") })) { return TEXT("ui_flame"); }

	const bool bWarnColor = Color.R > 0.75f && Color.G < 0.65f;
	if (bWarnColor || WeedToast_HasAny(L, { TEXT("can't"), TEXT("cannot"), TEXT("not enough"), TEXT("only "), TEXT("wrong "), TEXT("no ") })) { return TEXT("ui_flame"); }
	return TEXT("ui_message");
}

static bool WeedToast_IsWarnColor(const FLinearColor& Color)
{
	return Color.R > 0.75f && Color.G < 0.65f;
}

static FString WeedToast_LabelFor(const FString& Icon, const FString& Msg)
{
	const FString L = Msg.ToLower();
	if (Icon == TEXT("ui_coin") || Icon == TEXT("cash")) { return TEXT("CASH"); }
	if (Icon == TEXT("ui_bank") || Icon == TEXT("bank")) { return TEXT("BANK"); }
	if (Icon == TEXT("ui_level")) { return L.Contains(TEXT("goal")) ? TEXT("GOAL") : TEXT("LEVEL"); }
	if (Icon == TEXT("ui_message") || Icon == TEXT("phone") || Icon == TEXT("phone_vibrate")) { return TEXT("MESSAGE"); }
	if (Icon == TEXT("ui_person") || Icon.StartsWith(TEXT("t_face"))) { return TEXT("CUSTOMER"); }
	if (Icon == TEXT("joint") || Icon == TEXT("joint_rolled")) { return TEXT("JOINT"); }
	if (Icon == TEXT("weedleaf") || Icon == TEXT("seed") || Icon == TEXT("ui_leaf")) { return TEXT("GROW"); }
	if (Icon == TEXT("drop") || Icon == TEXT("faucet")) { return TEXT("WATER"); }
	if (Icon == TEXT("ui_package") || Icon == TEXT("rack") || Icon == TEXT("baggie")) { return TEXT("ITEM"); }
	if (Icon == TEXT("ui_flame")) { return WeedToast_HasAny(L, { TEXT("heat"), TEXT("bust"), TEXT("robbery"), TEXT("police") }) ? TEXT("HEAT") : TEXT("WARNING"); }
	if (Icon == TEXT("ui_upgrade")) { return TEXT("UPGRADE"); }
	return FString();
}

static bool WeedToast_IsRoutineToast(const FString& Label, const FString& Msg, const FLinearColor& Color)
{
	if (WeedToast_IsWarnColor(Color) || Label == TEXT("WARNING") || Label == TEXT("HEAT")) { return false; }
	const FString L = Msg.ToLower();
	if (Label == TEXT("WATER")) { return true; }
	if (Label == TEXT("ITEM") && WeedToast_HasAny(L, { TEXT("picked up"), TEXT("placed"), TEXT("collected"), TEXT("loaded ") })) { return true; }
	return WeedToast_HasAny(L, { TEXT("water "), TEXT("bottle "), TEXT("bottle full"), TEXT("filling") });
}

static int32 WeedToast_CoalesceKey(const FString& Msg, const FString& Icon, const FLinearColor& Color)
{
	if (WeedToast_IsWarnColor(Color)) { return INDEX_NONE; }
	const FString L = Msg.ToLower();
	if (Icon == TEXT("drop") || WeedToast_HasAny(L, { TEXT("water "), TEXT("bottle "), TEXT("bottle full"), TEXT("filling") }))
	{
		return 70101;
	}
	return INDEX_NONE;
}

static FString WeedToast_CompactText(const FString& Msg)
{
	FString Out = Msg;
	Out.ReplaceInline(TEXT("GOAL COMPLETE:"), TEXT("Goal complete:"));
	Out.ReplaceInline(TEXT("  ->  "), TEXT(" - "));
	Out.ReplaceInline(TEXT("Inventory too heavy - sell or drop something."), TEXT("Inventory too heavy"));
	Out.ReplaceInline(TEXT("No free inventory slots."), TEXT("Inventory full"));
	Out.ReplaceInline(TEXT("No room in your inventory."), TEXT("Inventory full"));
	Out.ReplaceInline(TEXT("Water bottle is empty - fill it at the sink."), TEXT("Water bottle empty"));
	Out.ReplaceInline(TEXT("Only edibles and cooking items go in the fridge."), TEXT("Fridge takes food only"));
	Out.ReplaceInline(TEXT("The fridge only keeps food fresh - store that on a shelf."), TEXT("Store that on a shelf"));
	Out.ReplaceInline(TEXT("Only wet weed can be dried."), TEXT("Only wet weed can dry"));
	return Out;
}

static FSlateBrush WeedToast_PillBrush(const FString& Label, const FString& Msg, const FLinearColor& Color, float Alpha)
{
	const bool bWarn = Label == TEXT("WARNING") || Label == TEXT("HEAT");
	const bool bMilestone = Label == TEXT("GOAL") || Label == TEXT("LEVEL");
	const bool bRoutine = WeedToast_IsRoutineToast(Label, Msg, Color);
	const float FillA = (bWarn ? 0.92f : (bMilestone ? 0.90f : (bRoutine ? 0.72f : 0.82f))) * Alpha;
	const float StrokeA = (bWarn ? 0.46f : (bMilestone ? 0.34f : (bRoutine ? 0.12f : 0.18f))) * Alpha;
	FSlateBrush B = WeedUI::Rounded(WeedUI::ColPanel(FillA), bMilestone ? 9.f : 7.f);
	B.OutlineSettings.Width = (bWarn || bMilestone) ? 1.f : 0.6f;
	B.OutlineSettings.Color = FSlateColor((bWarn || bMilestone) ? FLinearColor(Color.R, Color.G, Color.B, StrokeA) : WeedUI::ColStroke(StrokeA));
	return B;
}

static UWidget* WeedToast_IconWidget(UWidgetTree* Tree, const FString& Icon, float Size, const FLinearColor& Tint)
{
	if (!Tree || Icon.IsEmpty()) { return nullptr; }
	if (Icon.StartsWith(TEXT("t_")) || Icon.StartsWith(TEXT("/Game/")))
	{
		return WeedUI::KitIcon(Tree, Icon, Size, FLinearColor::White);
	}
	return WeedUI::UiGlyph(Tree, Icon, Size, Tint, WeedUI::EIcon::Message);
}

void UWeedToast::NotifyPawn(AActor* ForActor, int32 Key, float Time, const FColor& Color, const FString& Msg, const FString& IconStem)
{
	const FString Routed = IconStem.IsEmpty() ? Msg : (GToastIconOpen + IconStem + GToastIconClose + Msg);
	if (ForActor)
	{
		if (UPhoneClientComponent* Ph = ForActor->FindComponentByClass<UPhoneClientComponent>())
		{
			Ph->Toast(Routed, Color, Time); // routeert naar de juiste client (co-op)
			return;
		}
	}
	Notify(Key, Time, Color, Routed); // geen speler-component -> lokaal (host)
}

void UWeedToast::Notify(int32 Key, float Time, const FColor& Color, const FString& Msg)
{
	FString Icon;
	const FString Clean = WeedToast_ParseIcon(Msg, Icon);
	if (UWeedToast* T = Active.Get())
	{
		T->Push(Key, Time, FLinearColor(Color), Clean, Icon);
		return;
	}
	// Geen toast-widget (nog) -> val terug op de engine-melding (zonder icoon).
	if (GEngine) { GEngine->AddOnScreenDebugMessage(Key, Time, Color, Clean); }
}

void UWeedToast::NotifyAllPawns(const UObject* WorldContext, int32 Key, float Time, const FColor& Color, const FString& Msg, const FString& IconStem)
{
	// Co-op-brede melding: itereer alle verbonden speler-pawns en route per pawn (NotifyPawn -> host lokaal,
	// joiners via Client-RPC). Een kaal Notify() zou op een listen-server alleen het host-scherm bereiken.
	UWorld* W = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	bool bAny = false;
	if (W)
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (!P) { continue; }
			NotifyPawn(P, Key, Time, Color, Msg, IconStem);
			bAny = true;
		}
	}
	// Vangnet: (nog) geen pawns (bv. pre-spawn) -> toon lokaal zodat de melding niet stil verdwijnt.
	if (!bAny) { Notify(Key, Time, Color, Msg); }
}

TSharedRef<SWidget> UWeedToast::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UWeedToast::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	Stack = WidgetTree->ConstructWidget<UVerticalBox>();
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Stack);
	CS->SetAnchors(FAnchors(0.5f, 0.14f, 0.5f, 0.14f)); // midden-boven
	CS->SetAlignment(FVector2D(0.5f, 0.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, 0.f));
}

void UWeedToast::NativeConstruct()
{
	Super::NativeConstruct();
	Active = this;
}

void UWeedToast::NativeDestruct()
{
	if (Active.Get() == this) { Active = nullptr; }
	Super::NativeDestruct();
}

void UWeedToast::Push(int32 Key, float Time, const FLinearColor& Color, const FString& Msg, const FString& Icon)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const FString CleanMsg = WeedToast_CompactText(Msg);
	const FString EffectiveIcon = Icon.IsEmpty() ? WeedToast_AutoIcon(CleanMsg, Color) : Icon;
	const FString Label = WeedToast_LabelFor(EffectiveIcon, CleanMsg);
	const bool bRoutine = WeedToast_IsRoutineToast(Label, CleanMsg, Color);
	const float Dur = FMath::Clamp(Time, bRoutine ? 1.05f : 2.6f, bRoutine ? 2.2f : 7.f);
	const int32 EffectiveKey = (Key >= 0) ? Key : WeedToast_CoalesceKey(CleanMsg, EffectiveIcon, Color);
	// Zelfde key (>=0) -> bestaande melding bijwerken i.p.v. stapelen (bv. heat/teller).
	if (EffectiveKey >= 0)
	{
		for (FEntry& E : Entries)
		{
			if (E.Key == EffectiveKey) { E.Msg = CleanMsg; E.Icon = EffectiveIcon; E.Color = Color; E.Born = Now; E.Expire = Now + Dur; return; }
		}
	}
	// Dubbele identieke tekst kort na elkaar niet opnieuw stapelen.
	for (FEntry& E : Entries)
	{
		if (E.Msg == CleanMsg) { E.Icon = EffectiveIcon; E.Color = Color; E.Born = Now; E.Expire = Now + Dur; return; }
	}
	FEntry NewE; NewE.Msg = CleanMsg; NewE.Icon = EffectiveIcon; NewE.Color = Color; NewE.Key = EffectiveKey; NewE.Born = Now; NewE.Expire = Now + Dur;
	Entries.Add(NewE);
	while (Entries.Num() > 3) { Entries.RemoveAt(0); } // max 3 meldingen tegelijk op beeld
	LastSig.Reset(); // forceer herbouw
}

void UWeedToast::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	if (!Stack) { return; }

	// Geen toasts over het HOOFDMENU of tijdens het LAADSCHERM: meldingen horen pas te verschijnen als je echt
	// in-game bent (na de loading screen). We onderdrukken zolang de LOAD nog loopt (load-einde nog niet gepasseerd)
	// OF de laad-cover nog op is - NIET op !RoomReady: die vlag (kamer-vloer klaar) blijft op de OUTDOOR beach false,
	// waardoor toasts daar NOOIT verschenen (fridge-weigering/deal-melding onzichtbaar). Menu apart afvangen.
	bool bSuppress = (WeedShop_SecondsSinceLoadEnd() < 0.0) || WeedShop_IsCoverUp();
	if (!bSuppress)
	{
		if (const APawn* P = GetOwningPlayerPawn())
		{
			if (const UPhoneClientComponent* Ph = P->FindComponentByClass<UPhoneClientComponent>())
			{
				if (Ph->IsMainMenuOpen()) { bSuppress = true; }
			}
		}
	}
	if (bSuppress)
	{
		if (Entries.Num() > 0) { Entries.Reset(); LastSig.Reset(); Stack->ClearChildren(); }
		// KRITISCH: NIET Collapsed! Een Collapsed UMG-widget TICKT NIET MEER -> NativeTick draait nooit meer ->
		// de widget kan zichzelf niet un-collapsen -> permanent onzichtbaar (hij wordt toegevoegd terwijl de
		// laad-cover nog op is, dus zit meteen vast). HitTestInvisible blijft ticken; de lege stapel toont niets.
		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// Verlopen meldingen opruimen.
	bool bRemoved = false;
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		if (Now >= Entries[i].Expire) { Entries.RemoveAt(i); bRemoved = true; }
	}
	if (bRemoved) { LastSig.Reset(); }

	// Alleen herbouwen als de set meldingen wijzigt (anders alleen opacity updaten).
	FString Sig;
	for (const FEntry& E : Entries) { Sig += E.Icon + TEXT("@") + E.Msg + TEXT("|"); }
	if (Sig != LastSig)
	{
		LastSig = Sig;
		Stack->ClearChildren();
		for (const FEntry& E : Entries)
		{
			const FString Label = WeedToast_LabelFor(E.Icon, E.Msg);
			const bool bWarn = Label == TEXT("WARNING") || Label == TEXT("HEAT");
			const bool bMilestone = Label == TEXT("GOAL") || Label == TEXT("LEVEL");
			const bool bRoutine = WeedToast_IsRoutineToast(Label, E.Msg, E.Color);
			const bool bItem = Label == TEXT("ITEM");
			const float IconSize = bMilestone ? 24.f : (bRoutine ? 15.f : (bItem ? 16.f : 18.f));
			const int32 LabelSize = bMilestone ? 10 : 9;
			const int32 TextSize = bMilestone ? 14 : (bRoutine ? 12 : (bItem ? 12 : 13));
			const float MaxTextW = bMilestone ? 420.f : (bRoutine ? 260.f : (bItem ? 280.f : 340.f));

			UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
			Pill->SetBrush(WeedToast_PillBrush(Label, E.Msg, E.Color, 1.f));
			Pill->SetPadding(bRoutine ? FMargin(8.f, 5.f, 10.f, 5.f) : (bItem ? FMargin(9.f, 6.f, 11.f, 7.f) : (bMilestone ? FMargin(13.f, 8.f, 15.f, 9.f) : FMargin(11.f, 7.f, 13.f, 8.f))));

			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (!E.Icon.IsEmpty())
			{
				UWidget* Ico = WeedToast_IconWidget(WidgetTree, E.Icon, IconSize, E.Color);
				if (Ico)
				{
					UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Ico);
					IS->SetVerticalAlignment(VAlign_Center);
					IS->SetPadding(FMargin(0.f, 0.f, bRoutine ? 7.f : 8.f, 0.f));
				}
			}

			UVerticalBox* TextCol = WidgetTree->ConstructWidget<UVerticalBox>();
			if (!Label.IsEmpty() && !bRoutine)
			{
				UTextBlock* K = WeedUI::Text(WidgetTree, Label, LabelSize, bWarn ? E.Color : WeedUI::ColTextDim(0.90f), false, true);
				TextCol->AddChildToVerticalBox(K)->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
			}
			UTextBlock* T = WeedUI::Text(WidgetTree, E.Msg, TextSize, bRoutine ? WeedUI::ColTextDim(0.98f) : WeedUI::ColText(), false, true);
			T->SetAutoWrapText(true);
			T->SetWrapTextAt(MaxTextW);
			TextCol->AddChildToVerticalBox(T)->SetPadding(FMargin(0.f));

			USizeBox* TextBox = WidgetTree->ConstructWidget<USizeBox>();
			TextBox->SetMaxDesiredWidth(MaxTextW + 30.f);
			TextBox->SetContent(TextCol);
			UHorizontalBoxSlot* TS = Row->AddChildToHorizontalBox(TextBox);
			TS->SetVerticalAlignment(VAlign_Center);
			Pill->SetContent(Row);
			Stack->AddChildToVerticalBox(Pill)->SetPadding(FMargin(0.f, bRoutine ? 2.f : (bItem ? 3.f : 4.f), 0.f, 0.f));
		}
	}

	// Opacity per pil: rustig inkomen + korte fade-out, zonder neon pop.
	const int32 N = Stack->GetChildrenCount();
	for (int32 i = 0; i < N && i < Entries.Num(); ++i)
	{
		const FEntry& E = Entries[i];
		const float In = FMath::Clamp((Now - E.Born) / 0.22f, 0.f, 1.f);
		const float Out = FMath::Clamp((E.Expire - Now) / 0.42f, 0.f, 1.f);
		const float Alpha = FMath::Min(In, Out);
		if (UWidget* W = Stack->GetChildAt(i))
		{
			W->SetRenderOpacity(Alpha);
			W->SetVisibility(Alpha > 0.02f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (UBorder* Pill = Cast<UBorder>(W))
			{
				const FString Label = WeedToast_LabelFor(E.Icon, E.Msg);
				Pill->SetBrush(WeedToast_PillBrush(Label, E.Msg, E.Color, Alpha));
			}
		}
	}
}

#include "UI/WeedToast.h"

#include "UI/WeedUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Phone/PhoneClientComponent.h"
#include "WeedShopCore.h"

TWeakObjectPtr<UWeedToast> UWeedToast::Active;

void UWeedToast::NotifyPawn(AActor* ForActor, int32 Key, float Time, const FColor& Color, const FString& Msg)
{
	if (ForActor)
	{
		if (UPhoneClientComponent* Ph = ForActor->FindComponentByClass<UPhoneClientComponent>())
		{
			Ph->Toast(Msg, Color, Time); // routeert naar de juiste client (co-op)
			return;
		}
	}
	Notify(Key, Time, Color, Msg); // geen speler-component -> lokaal (host)
}

void UWeedToast::Notify(int32 Key, float Time, const FColor& Color, const FString& Msg)
{
	if (UWeedToast* T = Active.Get())
	{
		T->Push(Key, Time, FLinearColor(Color), Msg);
		return;
	}
	// Geen toast-widget (nog) -> val terug op de engine-melding.
	if (GEngine) { GEngine->AddOnScreenDebugMessage(Key, Time, Color, Msg); }
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

void UWeedToast::Push(int32 Key, float Time, const FLinearColor& Color, const FString& Msg)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Dur = FMath::Clamp(Time, 2.5f, 6.f);
	// Zelfde key (>=0) -> bestaande melding bijwerken i.p.v. stapelen (bv. heat/teller).
	if (Key >= 0)
	{
		for (FEntry& E : Entries)
		{
			if (E.Key == Key) { E.Msg = Msg; E.Color = Color; E.Born = Now; E.Expire = Now + Dur; return; }
		}
	}
	// Dubbele identieke tekst kort na elkaar niet opnieuw stapelen.
	for (FEntry& E : Entries)
	{
		if (E.Msg == Msg) { E.Color = Color; E.Born = Now; E.Expire = Now + Dur; return; }
	}
	FEntry NewE; NewE.Msg = Msg; NewE.Color = Color; NewE.Key = Key; NewE.Born = Now; NewE.Expire = Now + Dur;
	Entries.Add(NewE);
	if (Entries.Num() > 5) { Entries.RemoveAt(0); } // hou de stapel kort
	LastSig.Reset(); // forceer herbouw
}

void UWeedToast::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	if (!Stack) { return; }

	// Geen toasts over het HOOFDMENU of tijdens het LAADSCHERM: meldingen horen pas te verschijnen als je
	// echt in-game bent (na de loading screen). Tijdens het menu/laden de wachtrij leeggooien zodat ook geen
	// opgespaarde melding er net na opploft. (Bv. bij joinen kwamen host-toasts over het main menu heen.)
	bool bSuppress = WeedShop_LoadElapsedSeconds() > 0.0 && !WeedShop_IsRoomReady(); // laad-cover nog actief
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
		SetVisibility(ESlateVisibility::Collapsed);
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
	for (const FEntry& E : Entries) { Sig += E.Msg + TEXT("|"); }
	if (Sig != LastSig)
	{
		LastSig = Sig;
		Stack->ClearChildren();
		for (const FEntry& E : Entries)
		{
			UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
			Pill->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.07f, 0.88f), 10.f));
			Pill->SetPadding(FMargin(16.f, 9.f, 16.f, 9.f));
			UTextBlock* T = WeedUI::Text(WidgetTree, E.Msg, 15, E.Color, true, true);
			T->SetJustification(ETextJustify::Center);
			Pill->SetContent(T);
			Stack->AddChildToVerticalBox(Pill)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}
	}

	// Opacity per pil: inkomen (eerste 0.15s) + uitfaden (laatste 0.6s).
	const int32 N = Stack->GetChildrenCount();
	for (int32 i = 0; i < N && i < Entries.Num(); ++i)
	{
		const FEntry& E = Entries[i];
		const float In = FMath::Clamp((Now - E.Born) / 0.15f, 0.f, 1.f);
		const float Out = FMath::Clamp((E.Expire - Now) / 0.6f, 0.f, 1.f);
		if (UWidget* W = Stack->GetChildAt(i)) { W->SetRenderOpacity(FMath::Min(In, Out)); }
	}
}

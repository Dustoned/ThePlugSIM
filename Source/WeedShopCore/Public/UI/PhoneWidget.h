// UPhoneWidget — de telefoon als echte UMG-UI (volledig in C++ opgebouwd), voor een iPhone-feel:
// afgeronde frame + scherm, nette Roboto-font, app-iconen met afgeronde hoeken + hover/press, en
// app-schermen met een back-knop. Wordt door UPhoneClientComponent aangemaakt en getoond.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "UI/WeedUiStyle.h"
#include "PhoneWidget.generated.h"

class UPhoneClientComponent;
class UPhoneWidget;
class UCanvasPanel;
class UBorder;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UUniformGridPanel;

// Knop die een actie-id + parameter onthoudt en bij klik terugroept naar de telefoon-widget.
UCLASS()
class WEEDSHOPCORE_API UPhoneButton : public UButton
{
	GENERATED_BODY()

public:
	int32 ActionId = 0;
	int32 ActionParam = 0;
	TWeakObjectPtr<UPhoneWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

UCLASS()
class WEEDSHOPCORE_API UPhoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

	// Door de knoppen aangeroepen (Action: 0=open app[Param], 1=home, 2=sluit, 3=koop[Param],
	// 5=accept bericht, 6=decline bericht).
	void HandlePhoneButton(int32 Action, int32 Param);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// Settings-app: welke categorie (0 = Status [default], 1 = Controls) en de rebind-staat.
	int32 SettingsCat = 0;
	bool bRebinding = false;
	bool bRebindAlt = false;     // herbinden we de alternatieve toets?
	FName RebindAction;
	FString RebindMsg;
	UPROPERTY() TObjectPtr<UVerticalBox> SettingsBody;
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> SettingsTabBtns;
	void BuildSettingsApp();          // bouwt tabs + body één keer
	void FillSettingsBody();          // vult alleen de body (geen flash bij tab-wissel/rebind)
	void RefreshSettingsTabs();       // herkleurt de categorie-knoppen

	// --- Live light-tuning sliders (Settings/Test): sturen de lokale DayNightController direct aan ---
	UPROPERTY() TObjectPtr<class USlider> LMoon;
	UPROPERTY() TObjectPtr<class USlider> LSun;
	UPROPERTY() TObjectPtr<class USlider> LSkyN;
	UPROPERTY() TObjectPtr<class USlider> LSkyD;
	UPROPERTY() TObjectPtr<class USlider> LPitch;
	UPROPERTY() TObjectPtr<class USlider> LLamp;
	UPROPERTY() TObjectPtr<class USlider> LExp;
	UPROPERTY() TObjectPtr<UTextBlock> LMoonV;
	UPROPERTY() TObjectPtr<UTextBlock> LSunV;
	UPROPERTY() TObjectPtr<UTextBlock> LSkyNV;
	UPROPERTY() TObjectPtr<UTextBlock> LSkyDV;
	UPROPERTY() TObjectPtr<UTextBlock> LPitchV;
	UPROPERTY() TObjectPtr<UTextBlock> LLampV;
	UPROPERTY() TObjectPtr<UTextBlock> LExpV;
	class USlider* AddLightSlider(const FString& Label, float Norm, TObjectPtr<class USlider>& OutS, TObjectPtr<UTextBlock>& OutV);
	void ApplyLightSliders(); // leest de sliders en zet de DayNightController-waardes (live, elke tick)

	// Test-tools: tijd-versnelling (global time dilation 1x-8x) zodat je niet hoeft te wachten bij het testen.
	UPROPERTY() TObjectPtr<class USlider> TimeSpeedSlider;
	UPROPERTY() TObjectPtr<UTextBlock> TimeSpeedV;

	TWeakObjectPtr<UPhoneClientComponent> Phone;

	// Opgebouwde onderdelen.
	UPROPERTY() TObjectPtr<UBorder> Frame;
	UPROPERTY() TObjectPtr<UTextBlock> TimeText;
	UPROPERTY() TObjectPtr<UTextBlock> LevelText;
	UPROPERTY() TObjectPtr<UTextBlock> CashText;
	UPROPERTY() TObjectPtr<class UProgressBar> LevelXpBar; // XP-voortgang naar het volgende level (onder de statusbalk)
	UPROPERTY() TObjectPtr<UTextBlock> LevelXpText;        // "120 / 300 XP" naast de balk
	UPROPERTY() TObjectPtr<UVerticalBox> ContentBox;
	// Ongelezen-badge op de Messages-app in het home-rooster (live bijgewerkt, geen rebuild -> geen flash).
	UPROPERTY() TObjectPtr<UBorder> MsgAppBadgePill;
	UPROPERTY() TObjectPtr<UTextBlock> MsgAppBadgeText;
	UPROPERTY() TObjectPtr<UBorder> GoalsAppBadgePill;
	UPROPERTY() TObjectPtr<UTextBlock> GoalsAppBadgeText;
	int32 GetClaimableGoals() const;  // aantal behaalde-maar-niet-geclaimde doelen (voor de badge)

	// Staat-tracking voor het verversen van de inhoud.
	bool bLastHome = true;
	int32 bLastApp = -1;
	bool bContentDirty = true;
	float LastStatsRefresh = 0.f; // throttle voor de live leaderboard-refresh

	void BuildShell(UCanvasPanel* Root);
	void RefreshContent();

	// Bouw-helpers.
	UTextBlock* MakeText(const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter = false);
	UWidget* MakeAppCell(int32 AppIndex, const FString& Name, const FString& IconKey, WeedUI::EIcon IconFallback, const FLinearColor& Col);
	UPhoneButton* MakeButton(const FString& Label, int32 Action, int32 Param, const FLinearColor& Col);
	void AddInfoRow(const FString& Txt, const FLinearColor& Col, int32 Size = 13);

	// Knop met een vrije callback (voor de winkel-app: categorie/qty/cart/checkout).
	class UWeedActionButton* MakeActionBtn(const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 FontSize = 12);

	// --- Berichten-app (echte chat) ---
	// NAME_None = gesprekkenlijst; anders is de chat-thread van dat contact open.
	FName OpenChatContact = NAME_None;
	bool bOfferStrainView = false; // toont de "Offer instead"-strain-lijst in de open chat
	int32 LastMsgSig = -2;
	int32 ProposeMins = -1; // gekozen kloktijd (min van de dag) voor een eigen-tijd-voorstel; -1 = nog niet gezet
	int32 LastChatMin = -1; // klok-minuut bij de laatste chat-rebuild (om de tijd-kiezer-ondergrens live mee te laten lopen)
	// Live-refs van de tijd-kiezer (bijwerken zonder rebuild -> geen flash).
	UPROPERTY() TObjectPtr<UTextBlock> PickerClockText;
	UPROPERTY() TObjectPtr<class UWeedActionButton> PickerProposeBtn;
	FName PickerContact = NAME_None;
	// Bouwt de Berichten-app: gesprekkenlijst of de open chat-thread.
	void BuildChatApp();
	// Verandert als er een bericht bijkomt of een status wijzigt (voor live verversen).
	int32 MessagesSignature() const;
	// True als dit bericht voor de LOKALE speler is (competitive = eigen telefoon; co-op = altijd).
	bool IsMsgForLocal(const struct FPhoneMessage& M) const;

	// Bouwt de Map-app: een mini stadskaart + knop naar de fullscreen-kaart (M).
	void BuildMapApp();
	void BuildGoalsApp();             // milestone-doelen met rewards
	void BuildStatsApp();             // competitive leaderboard (stats per speler)

	// Bouwt de Suppliers-app (in-telefoon webshop) in de gegeven container.
	void BuildStoreApp(class UVerticalBox* Into);
	// Vult alleen de scrollbare lijst (catalogus/cart/sell) — geen volledige herbouw, dus geen flash.
	void FillStoreList();
	// Werkt de winkel bij na een actie (tab-kleuren, cart-tekst, lijst) zonder de hele app te herbouwen.
	void RefreshStore();

	// Markeer dat de app-inhoud opnieuw opgebouwd moet worden (na een winkel-actie).
	void MarkDirty() { bContentDirty = true; }

	bool bCartView = false; // toont de winkelwagen i.p.v. de catalogus
	bool bSellApp = false;  // de store-app draait als losse Sell-app (alleen verkopen, geen koop-tabs)
	TArray<int32> AppCats;  // categorie-indices die de huidige winkel-app als tabs toont

	// Referenties zodat winkel-acties (qty/add) labels direct kunnen updaten zonder de hele
	// app te herbouwen (geen flash).
	UPROPERTY() TMap<FName, TObjectPtr<UTextBlock>> StoreQtyTexts;
	UPROPERTY() TObjectPtr<UTextBlock> StoreCartText;
	UPROPERTY() TObjectPtr<class UScrollBox> StoreScroll;
	UPROPERTY() TObjectPtr<class UVerticalBox> StoreFooter;
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> StoreTabBtns;
	UPROPERTY() TObjectPtr<class UWeedActionButton> StoreCartToggle;
	UPROPERTY() TObjectPtr<class UWeedActionButton> StorePackagesToggle;
	UPROPERTY() TObjectPtr<UTextBlock> StorePackagesLabel;
	int32 DeliveryOpt = 0;
	void UpdateStoreCartText();

	// Packages-app (onderweg zijnde bestellingen): voortgang + ETA + annuleren.
	bool bPackagesView = false;
	UPROPERTY() TMap<int32, TObjectPtr<class UProgressBar>> PkgBars;   // per OrderId
	UPROPERTY() TMap<int32, TObjectPtr<UTextBlock>> PkgEtas;           // per OrderId
	UPROPERTY() TObjectPtr<class UScrollBox> PackagesScroll;           // scroll van de losse Packages-app
	int32 LastPkgSig = -1;
	int32 PackagesSignature() const;   // verandert als de set bestellingen wijzigt
	void UpdatePackagesLive();         // werkt bars/ETA's live bij zonder herbouw

	// Chat: live aftellende afspraak-balk (loopt naar 0 tot de NPC opgeeft).
	UPROPERTY() TObjectPtr<class UProgressBar> ApptBar;
	UPROPERTY() TObjectPtr<UTextBlock> ApptBarLabel;
	FName ApptBarContact = NAME_None;
	void UpdateApptBarLive();          // werkt de afspraak-balk live bij

	// Wacht-balk onder een open deal-bericht: telt af tot de klant opgeeft (150s) -> je ziet hoelang je hebt.
	UPROPERTY() TObjectPtr<class UProgressBar> WaitBar;
	UPROPERTY() TObjectPtr<UTextBlock> WaitBarLabel;
	float WaitBarSentTime = -1.f;      // SentRealTime van het open bericht (basis voor de aftelbalk)
	void UpdateWaitBarLive();          // werkt de wacht-balk live bij

	// --- Afspraak-urgentie in de gesprekkenlijst: slanke aftelbalk + kleur per contact, gesorteerd op urgentie ---
	// Urgentie-fractie (1 -> 0): fase B = live klant ApptTimeout; fase A = tijd tot het afspraak-moment
	// (AppointmentTimeOfDay vs dag-klok). False = geen lopende afspraak met dit contact.
	// Phase 0=onderweg (toon kloktijd OutClockMins), 1=wacht-aan-deur (aftel OutSecsLeft), 2=wacht-op-antwoord.
	bool GetApptUrgency(FName ContactId, float& OutFrac, int32& OutSecsLeft, int32& OutPhase, int32& OutClockMins) const;
	// bReply=true -> BLAUW vers (jouw beurt: accept/decline); anders GROEN vers (afspraak loopt). Beide -> geel -> rood.
	static FLinearColor UrgencyColor(float Frac, bool bReply = false);
	UPROPERTY() TMap<FName, TObjectPtr<class UProgressBar>> ListApptBars; // per contact in de lijst
	UPROPERTY() TMap<FName, TObjectPtr<class UBorder>> ListCards;         // per contact (live re-tint)
	UPROPERTY() TMap<FName, TObjectPtr<UTextBlock>> ListPreviews;         // per contact (live "arrives in / left"-tekst)
	void UpdateListBarsLive();         // werkt de lijst-balken + kleuren + preview live bij (geen rebuild)

	void BuildPackagesApp();           // bouwt de losse Packages-app in ContentBox
	void FillPackagesInto(class UScrollBox* Scroll); // vult de bestellingen-kaarten in een scroll
	void FillPotUpgradesInto(class UScrollBox* Scroll); // "Pot Upgrades"-tab: per geplaatste pot

	// Bank-app (na de telefoon-upgrade): storten (witwassen) + geld sturen naar co-op vrienden.
	void BuildBankApp();
	int32 LastBankSig = -1;
};

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
class UOverlay;
class UHorizontalBox;
class UTextBlock;
class UUniformGridPanel;
class UWidget;
class UWeedItemPickGrid;

// Per-winkel-app snapshot van de gedeelde winkel-member-refs (StoreScroll/pools/tabs/AppCats/...). Zo kan een
// switch tussen winkel-apps (Grow/Supplies/Sell/Hash) de refs HERSTELLEN uit de cache i.p.v. het paneel te
// herbouwen -> geen ClearChildren, geen flash. De winkel-panelen zelf blijven persistent gecacht in AppPanels.
USTRUCT()
struct FPhoneStoreRefs
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<class UScrollBox> Scroll = nullptr;
	UPROPERTY() TObjectPtr<class UVerticalBox> Footer = nullptr;
	UPROPERTY() TObjectPtr<class UTextBlock> CartText = nullptr;
	UPROPERTY() TObjectPtr<class UWeedActionButton> CartToggle = nullptr;
	UPROPERTY() TObjectPtr<class UTextBlock> PackagesLabel = nullptr;
	UPROPERTY() TArray<TObjectPtr<class UBorder>> RowPool;
	UPROPERTY() TArray<TObjectPtr<class UBorder>> FooterPool;
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> TabBtns;
	UPROPERTY() TMap<FName, TObjectPtr<class UTextBlock>> QtyTexts;
	TArray<FString> RowSigs;   // geen UObject -> geen UPROPERTY nodig
	TArray<FString> FooterSigs;
	TArray<int32> Cats;
	bool bSell = false;
	int32 LastCat = -1;
};

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
	// In-place refs voor Settings-body-acties die geen hele body-herbouw nodig hebben (geen flash).
	UPROPERTY() TObjectPtr<class UWeedActionButton> SkinMaleBtn;   // Male/Female-toggle: alleen herkleuren
	UPROPERTY() TObjectPtr<class UWeedActionButton> SkinFemaleBtn;
	UPROPERTY() TObjectPtr<UTextBlock> ShopTypeLabel;             // "Shop type: X (tap)": alleen de tekst wisselt

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
	UPROPERTY() TObjectPtr<UOverlay> ContentBox; // Overlay: de per-app panelen stapelen -> inactieve panelen blijven gearrangeerd (Hidden) i.p.v. Collapsed, dus app-open is instant zonder her-layout-flikker
	// Persistente per-app panelen: één VerticalBox per app (Key = tab-index, -1 = home) die in ContentBox blijft
	// hangen. App-wissel = puur zichtbaarheid togglen (geen ClearChildren -> geen flash); alleen een ECHTE
	// data-wijziging (bContentDirty) herbouwt het panel van die ene app. ActiveContent = het panel dat nu bouwt/toont.
	UPROPERTY() TMap<int32, TObjectPtr<class UVerticalBox>> AppPanels;
	UPROPERTY() TObjectPtr<class UVerticalBox> ActiveContent;
	// Pre-warm: we bouwen ALLE app-panelen alvast op, GESPREID (1 paneel per tick) en TERWIJL DE TELEFOON DICHT
	// IS — dus tijdens/vlak na de load, achter het boot-scherm, zonder runtime-hitch. Zo is óók de eerste open
	// van elke app flash-vrij (anders bouwt de lazy cache pas bij de eerste open per sessie, en die eerste-open-
	// flash komt na elke game-restart terug). Tijdens het bouwen van één stap sturen bPrewarmHome/PrewarmApp de
	// key-keuze in RefreshContent i.p.v. de live Phone-status.
	bool bPrewarming = false;   // true alleen tijdens het bouwen van één stap (key-override actief)
	bool bPrewarmHome = false;
	int32 PrewarmApp = 0;
	bool bPrewarmDone = false;  // true zodra alle panelen gespreid zijn voorgebouwd
	int32 PrewarmCursor = 0;    // welke prewarm-stap we nu bouwen (0 = home, 1.. = app-tabs, Lab overgeslagen)
	int32 SavedSupplierCat = -1;// winkel-categorie-snapshot (hersteld na de laatste prewarm-stap; -1 = nog niet genomen)
	void PrewarmStep();         // bouwt het volgende ENE paneel deze tick (of finaliseert)
	// Ongelezen-badge op de Messages-app in het home-rooster (live bijgewerkt, geen rebuild -> geen flash).
	UPROPERTY() TObjectPtr<UBorder> MsgAppBadgePill;
	UPROPERTY() TObjectPtr<UTextBlock> MsgAppBadgeText;
	UPROPERTY() TObjectPtr<UBorder> GoalsAppBadgePill;
	UPROPERTY() TObjectPtr<UTextBlock> GoalsAppBadgeText;
	int32 GetClaimableGoals() const;  // aantal behaalde-maar-niet-geclaimde doelen (voor de badge)

	// --- Goals-app: persistente kaart-pool (1x gebouwd, daarna ALLEEN in-place updates -> geen flash) ---
	// De goals-set is statisch per sessie (UGoalsComponent::Goals()), dus er staan N vaste kaarten in de scroll.
	// RefreshGoalsApp sorteert per refresh: claimbaar BOVENAAN, dan in-progress (bijna-klaar eerst), claimed
	// ONDERAAN. Per POSITIE een sig (goal-index + status): alleen posities waarvan de toewijzing/status wijzigt
	// worden hervuld/gerestyled; de voortgangstekst is changed-checked en de balk loopt live mee.
	UPROPERTY() TArray<TObjectPtr<UBorder>> GoalCards;                     // kaart-border per positie
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> GoalTitles;                 // titel per positie
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> GoalProgTexts;              // voortgangstekst ("EUR X / Y") per positie
	UPROPERTY() TArray<TObjectPtr<class UProgressBar>> GoalBars;           // voortgangsbalk per positie
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> GoalRewardTexts;            // reward-regel per positie
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> GoalClaimBtns; // Claim-knop per positie (Hidden-toggle in Overlay)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> GoalStatusTexts;            // "Claimed"/"In progress" per positie (Hidden-toggle)
	TArray<int32> GoalCardGoalIdx;    // per positie: de goal-index die er nu in zit (-1 = nog leeg); de klik leest DEZE
	TArray<int32> GoalCardSigs;       // per positie: goal-index*4 + status -> restyle alleen bij een echte wijziging
	TArray<FString> GoalCardProgStrs; // per positie: laatst gezette voortgangstekst (SetText alleen bij verschil)
	float NextGoalsLiveRefresh = 0.f; // throttle (~4x/s) voor de live pool-refresh zolang de app open is
	void RefreshGoalsApp();           // sorteert + werkt de kaart-pool in-place bij (NOOIT ClearChildren)

	// Staat-tracking voor het verversen van de inhoud.
	bool bLastHome = true;
	int32 bLastApp = -1;
	bool bContentDirty = true;
	float LastStatsRefresh = 0.f; // throttle voor de live leaderboard-refresh

	// Perf: statusbalk + bank-app changed-checks -> SetText alleen bij een echt verschil.
	double LastPhoneCash = -1.0e18;
	double LastPhoneBank = -1.0e18;
	int32 LastPhoneTimeKey = -1;      // dag*10000 + H*60+M
	int32 LastPhoneLevel = -1;
	int32 LastPhoneXp = -1;
	int32 LastPhoneXpNext = -1;
	long long LastBankBalShown = -1;  // getoonde hele euro's (bank)
	long long LastBankCashShown = -1; // getoonde hele euro's (cash to deposit)
	int32 LastBankUnlocked = -1;      // -1 onbekend (reset bij bank-panel-rebuild)
	int32 LastBankSendVis = -1;
	FString LastBankSendLabel;
	// Welk store-app-paneel de gedeelde winkel-member-refs (StoreScroll/StoreTabBtns/AppCats/...) nu beschrijven.
	// De 4 store-apps delen die refs; hiermee herbouwen we bij terugkeer naar een ANDER store-paneel de refs.
	int32 StoreMembersOwnerApp = -1;

	// Per-app cache van de winkel-refs, zodat switchen tussen winkel-apps de refs herstelt i.p.v. herbouwt (geen flash).
	UPROPERTY() TMap<int32, FPhoneStoreRefs> StoreRefsByApp;
	void SaveStoreRefs(int32 App);    // huidige gedeelde winkel-refs -> cache[App]
	void RestoreStoreRefs(int32 App); // cache[App] -> gedeelde winkel-refs

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
	// Pre-gebouwde alternatieven-box in de open chat: alleen tonen/verbergen bij de "Offer instead..."-toggle
	// (geen RefreshContent-flash). Wordt in BuildChatApp één keer gevuld voor de open thread.
	UPROPERTY() TObjectPtr<class UVerticalBox> OfferBox;
	UPROPERTY() TObjectPtr<class UWeedActionButton> OfferToggleBtn;
	// Offer-box binnenwerk: header-tekst (in-place SetText), icoon-grid (diff-SetItems -> geen flash bij
	// thread-refresh) en een "geen voorraad"-tekst die alleen zichtbaarheid toggelt.
	UPROPERTY() TObjectPtr<class UWeedItemPickGrid> OfferGrid;
	UPROPERTY() TObjectPtr<UTextBlock> OfferHeadText;
	UPROPERTY() TObjectPtr<UTextBlock> OfferEmptyText;
	int32 LastMsgSig = -2;
	int32 ProposeMins = -1; // gekozen kloktijd (min van de dag) voor een eigen-tijd-voorstel; -1 = nog niet gezet
	int32 LastChatMin = -1; // klok-minuut bij de laatste chat-rebuild (om de tijd-kiezer-ondergrens live mee te laten lopen)
	// Live-refs van de tijd-kiezer (bijwerken zonder rebuild -> geen flash).
	UPROPERTY() TObjectPtr<UTextBlock> PickerClockText;
	UPROPERTY() TObjectPtr<class UWeedActionButton> PickerProposeBtn;
	FName PickerContact = NAME_None;
	// Bouwt de Berichten-app: één keer een persistente shell (lijst-scroll + thread-root met alle vaste
	// sub-secties). Daarna verversen RefreshChatViews/List/Thread ALLES in-place -> nooit meer een paneel-rebuild
	// bij open/back/accept/decline/propose/offer of een binnenkomend bericht (geen flash).
	void BuildChatApp();
	// Toggelt lijst vs thread (zichtbaarheid) en ververst de zichtbare kant in-place.
	void RefreshChatViews();
	// Reconcilieert de gesprekkenlijst-kaarten in-place (nieuw/weg contact); NOOIT ClearChildren.
	void RefreshChatList();
	// Werkt de open thread in-place bij (header/tier/afspraak/bubbels-pool/wacht/accept/offer/picker); NOOIT ClearChildren.
	void RefreshChatThread();
	// Verandert als er een bericht bijkomt of een status wijzigt (voor live verversen).
	int32 MessagesSignature() const;

	// --- Persistente chat-shell (één keer gebouwd in BuildChatApp; daarna alleen in-place ververst) ---
	UPROPERTY() TObjectPtr<class UScrollBox> ChatListScroll;   // gesprekkenlijst-container (kaart-pool per contact)
	UPROPERTY() TObjectPtr<UVerticalBox> ChatThreadRoot;       // thread-container met alle vaste sub-secties
	// Thread-sub-widgets (persistent; getoond/verborgen + tekst/inhoud in-place bijgewerkt).
	UPROPERTY() TObjectPtr<UTextBlock> ChatHeaderName;         // contact-naam in de thread-header
	UPROPERTY() TObjectPtr<UBorder> ChatTierBox;               // klant-tier-box (verborgen als geen registry)
	UPROPERTY() TObjectPtr<UTextBlock> ChatTierLabel;
	UPROPERTY() TObjectPtr<class UProgressBar> ChatTierBar;
	UPROPERTY() TObjectPtr<UBorder> ChatApptBox;               // afspraak-balk-box (arrives/at-door; wraps ApptBar)
	UPROPERTY() TObjectPtr<class UScrollBox> ChatBubbleScroll; // bubbel-container (bubbel-pool)
	UPROPERTY() TObjectPtr<UTextBlock> ChatNoMsgText;          // "No messages with this contact yet."
	UPROPERTY() TObjectPtr<UBorder> ChatWaitBox;               // wacht-balk-box (wraps WaitBar)
	UPROPERTY() TObjectPtr<UHorizontalBox> ChatRespondRow;     // Accept/Decline-rij
	UPROPERTY() TObjectPtr<UVerticalBox> ChatOfferSection;     // offer-toggle + OfferBox + tijd-kiezer (samen tonen/verbergen)
	UPROPERTY() TObjectPtr<UTextBlock> ChatPickerPrompt;       // "Can't make it? Pick a time:"
	UPROPERTY() TObjectPtr<class UWeedActionButton> ChatProposeBtn;   // "Propose this time"
	// Bubbel-pool (patroon van FillStoreListRows): per-bericht-signatuur; alleen gewijzigde bubbel krijgt nieuwe inhoud.
	UPROPERTY() TArray<TObjectPtr<UBorder>> ChatBubblePool;
	TArray<FString> ChatBubbleSigs;                            // geen UObject -> geen UPROPERTY nodig
	// True als dit bericht voor de LOKALE speler is (competitive = eigen telefoon; co-op = altijd).
	bool IsMsgForLocal(const struct FPhoneMessage& M) const;

	// (DOOD, zoals de Lab-app) Map-app is van de telefoon af (uit home-rooster + prewarm); de M-toets
	// fullscreen-kaart blijft gewoon bestaan. Bouwde een mini stadskaart + fullscreen-knop.
	void BuildMapApp();
	void BuildGoalsApp();             // milestone-doelen met rewards
	void BuildStatsApp();             // competitive leaderboard (stats per speler)

	// Bouwt de Suppliers-app (in-telefoon webshop) in de gegeven container.
	void BuildStoreApp(class UVerticalBox* Into);
	// Vult alleen de scrollbare lijst (catalogus/cart/sell) — geen volledige herbouw, dus geen flash.
	void FillStoreList();
	// Diff-vulling van de scroll-pool: alleen rijen met een gewijzigde signatuur worden herbouwd (geen ClearChildren).
	void FillStoreListRows(TArray<FString> Sigs, TArray<TFunction<UWidget*()>> Builders);
	// Diff-vulling van de vaste voettekst (alleen in de cart) via een eigen pool -> bezorgoptie/deliver-to-home
	// wijzigt enkel de footer, niet de scroll-lijst (geen scroll-sprong).
	void FillStoreFooter();
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
	// Persistente rij-pool voor de winkel-scroll -> per FillStoreList alleen gewijzigde rijen vervangen
	// (geen ClearChildren -> geen flash/scroll-sprong bij tab-wissel, cart +/-, pot-buy, bezorgoptie, ...).
	UPROPERTY() TArray<TObjectPtr<UBorder>> StoreRowPool;
	TArray<FString> StoreRowSigs;
	// Persistente pool voor de vaste voettekst (bezorgopties + totaal + checkout) -> zelfde diff-truc.
	UPROPERTY() TArray<TObjectPtr<UBorder>> StoreFooterPool;
	TArray<FString> StoreFooterSigs;
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
	UPROPERTY() TMap<int32, TObjectPtr<UBorder>> PkgCards;            // per OrderId (Cancel = alleen deze kaart weg)
	UPROPERTY() TObjectPtr<UBorder> PkgEmptyRow;                      // "No packages on the way."-placeholder (gericht tonen/verbergen)
	// "Delivered"-historie onder de pending-kaarten: één vaste container (kop + inner-cards) die alleen
	// z'n INHOUD herbouwt als de historie wijzigt (sig) -> geen flash op de pending-kaarten.
	UPROPERTY() TObjectPtr<UBorder> PkgDeliveredBox;
	int32 PkgDeliveredSig = -1;                                       // #records-sig van de laatst getekende historie
	UPROPERTY() TObjectPtr<class UScrollBox> PkgScrollOwner;          // in welke scroll de PkgCards staan (reset bij scroll-wissel)
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
	UPROPERTY() TMap<FName, TObjectPtr<UTextBlock>> ListPreviews;         // per contact (live "arrives in / left"-tekst + laatste bericht)
	UPROPERTY() TMap<FName, TObjectPtr<class UBorder>> ListBadges;        // per contact: ongelezen-badge (altijd gebouwd, live getoggled)
	UPROPERTY() TMap<FName, TObjectPtr<UTextBlock>> ListBadgeTexts;       // per contact: het ongelezen-aantal (live)
	UPROPERTY() TMap<FName, TObjectPtr<UTextBlock>> ListStamps;           // per contact: tijdstempel laatste bericht (live)
	void UpdateListBarsLive();         // werkt de lijst-balken + kleuren + preview live bij (geen rebuild)

	void BuildPackagesApp();           // bouwt de losse Packages-app in ContentBox
	void FillPackagesInto(class UScrollBox* Scroll); // vult de bestellingen-kaarten in een scroll
	void FillPotUpgradesInto(class UScrollBox* Scroll); // "Pot Upgrades"-tab: per geplaatste pot (thans niet aangeroepen)

	// Bank-app (na de telefoon-upgrade): storten (witwassen) + geld sturen naar co-op vrienden.
	// PERSISTENT: de ontgrendelde bank-kaart wordt ÉÉN keer gebouwd; alleen de twee dynamische teksten (saldo +
	// cash-to-deposit) werken IN-PLACE bij in NativeTick (geen paneel-herbouw -> geen flash). De sig hieronder dekt
	// ALLEEN de structurele unlock/ATM-overgang (upgrade-prompt <-> bank-kaart); dan mag het paneel één keer herbouwen.
	void BuildBankApp();
	UPROPERTY() TObjectPtr<UTextBlock> BankBalanceText; // grote saldo-waarde (bank cents) - in-place
	UPROPERTY() TObjectPtr<UTextBlock> BankCashText;    // "Cash to deposit: EUR X" (cash cents) - in-place
	UPROPERTY() TObjectPtr<UVerticalBox> BankLockedBox;   // upgrade-prompt (geblokkeerd) - vast gebouwd, getoggled
	UPROPERTY() TObjectPtr<UVerticalBox> BankUnlockedBox; // bank-kaart (ontgrendeld) - vast gebouwd, getoggled
	UPROPERTY() TObjectPtr<UVerticalBox> BankSendBox;     // "Send to <vriend>"-sectie: alleen zichtbaar met een 2e speler (co-op) - getoggled in NativeTick
	UPROPERTY() TObjectPtr<UTextBlock> BankSendLabel;     // "Send to <naam>   (X% fee)" - naam in-place bijgewerkt
	int32 LastBankSig = -1;      // structurele unlock/ATM-staat (0 = geblokkeerd, 1 = ontgrendeld/ATM)
	int32 BankUnlockSignature() const; // ALLEEN de mobile-banking/ATM-beschikbaarheid (upgrade-prompt <-> bank-kaart)

	// Contacts-app (app-index 2): EIGEN structurele sig = alleen de contact-SET (aantal + hash van de contact-ids).
	// Zo herbouwt Contacts NIET bij een nieuw bericht/afspraak-fase van een BESTAAND contact (dat gaf flits via de
	// gedeelde MessagesSignature-tak); alleen een NIEUW/weg contact herbouwt 't één keer.
	int32 LastContactsSig = 0;
	int32 ContactsSignature() const;

	// Upgrades-app (app-index 0): structurele sig = woning-bezit + backpack-tier van de lokale speler.
	// Zo herbouwt het paneel IN-PLACE (via MarkDirty) na een woning-koop/verkoop of een backpack-upgrade,
	// zonder dat de speler de app opnieuw hoeft te openen (zelfde idioom als de Contacts/Packages-sig).
	int32 LastUpgradesSig = -1;
	int32 UpgradesSignature() const;
};

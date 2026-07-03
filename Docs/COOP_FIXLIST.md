# CO-OP FIXLIJST — per-speler-pariteit (audit 2026-07-02)

> Uitkomst van de brede co-op-audit (9 read-only agents) + de gerichte crowd-deepdive. Bron: speler test
> co-op competitive met een vriend (2 PC's, packaged). Volgorde = severity. `[C]`=competitive, `[N]`=normaal,
> `[B]`=beide. Elke regel: probleem -> file:regel -> fix. Kernpatroon van bijna alles: server-side code toont
> meldingen met `UWeedToast::Notify(-1)` (host-lokale static) of pakt de portemonnee via `GetFirstPlayerController()`
> = altijd de HOST, nooit de joiner. De juiste helpers bestaan al: `NotifyPawn(pawn,...)` (Client-RPC) en
> `NotifyAllPlayers()` (HeatComponent.cpp:25-40).

## AL OPGELOST (alleen roadmap bijwerken)
- **E.2 Waterfles-vulling per-speler** — AL correct sinds commit 263f2a6b (water leeft per-fles in `Quality` van
  elke stack in de per-pawn `UInventoryComponent`, gerepliceerd). Niet reproduceerbaar. -> roadmap E.2 afvinken.
  (Klein cosmetisch restje: `WeedUiStyle.cpp:965` valt voor een water-icoon zonder override terug op
  `GetPlayerPawn(0)` -> catalogus/shelf-flesicoon kan meeliften op lokaal fles-niveau; puur cosmetisch, laag.)

## MAJEUR

### Placement / furniture (competitive)
- **E.1a Joiner kan NIKS plaatsen in eigen kamer** `[C]` — `GetCompetitiveHomeBoxes` (DoorRetrofitter.cpp:4922-4925)
  kiest host-vs-joiner-box op `W->GetNetMode()==NM_Client`. In een Server-RPC (ServerPlace) is netmode
  NM_ListenServer -> server geeft ALTIJD de host-box -> joiner-locatie valt buiten -> geweigerd
  (BuildComponent.cpp:1570-1577), terwijl de client-preview blauw is (client=NM_Client geeft wel joiner-box).
  **Fix:** speler-expliciet i.p.v. netmode: overload `GetCompetitiveHomeBoxes(bool bJoiner, ...)`; in BuildComponent
  bepaal `bJoiner = P && !P->IsLocallyControlled()` (owner-pawn) en geef door aan InCompHome. (GetCompDeliverySpot
  doet dit al goed met een bJoiner-param.)
- **E.1b-stale Competitive furniture scheef in packaged** `[C]` — de meubel-layout komt uit string-geladen
  `Saved/*.txt` (StarterFurniture/CompSpawns/HomeSpawn/BuildArea); `WeedData::RestoreAll` (WeedShopCore.h:105-159)
  kopieert alleen ONTBREKENDE files uit `Content/BakedData/`, werkt ze NOOIT bij. Editor-fix landt in Saved/ maar
  NIET in de gebakken kopie -> packaged spiegelt de OUDE layout = scheef, en cook-parity ziet 't niet (checkt alleen
  asset-paden, niet .txt-inhoud). 5e cook-miss. **Fix:** release-flow byte-resync Saved->BakedData voor de geometrie-
  files + content-hash-diff in check-cook-parity.ps1 (hard fail bij stale snapshot).

### Economie (data-verlies)
- **Late joiner wist de gedeelde bank** `[N]` — `UEconomyComponent::BeginPlay` doet onvoorwaardelijk
  `SetBank(0)` (EconomyComponent.cpp:25-29) -> via `BankOwner()` = gedeelde GameState-bank. P2 die mid-sessie
  joint/respawnt nult al het crew-bankgeld. **Fix:** bank-init niet via de pawn-economy in co-op (guard
  `BankOwner()!=this`, of `bBankInitialized`-vlag, of bank-start alleen op de GS-economy).
- **Bank-transfer naar co-op-vriend = fee-verbrander no-op** `[N]` — beide delen 1 bank; transfer haalt bedrag+fee
  van de gedeelde bank en stort het bedrag terug op DEZELFDE bank -> netto -fee, verder niks (PhoneClientComponent.cpp:3296).
  **Fix:** transfer-tab verbergen in co-op (alleen `IsCompetitive()`), of ServerTransfer vroeg terugkeren in co-op.
- **Server-side portemonnee = altijd host** `[B]` — `AWeedShopGameState::GetEconomy()` (WeedShopGameState.cpp:124-142)
  resolvet via `GetFirstPlayerController()` = host-pawn. Alle server-side callers muteren de HOST-wallet:
  - **Bust/overval-boete alleen host** (HeatComponent.cpp:175-201) — joiner verliest nooit cash, wel z'n apartment.
  - **UpgradeStation koopt van host-bank** (UpgradeStation.cpp:27-38) — joiner gebruikt station -> host betaalt.
  **Fix:** server-side callers betalen via de instigator-pawn z'n `UEconomyComponent` (zoals de phone-route al doet
  met `GetOwnerEconomy()`); geef bust/robbery/upgrade een instigator/target-pawn mee. `GetEconomy()` alleen als
  client-side lokale-UI-helper laten (evt. hernoemen naar `GetLocalEconomy()`).

### Planten (joiner ziet z'n plant niet sterven)
- **Autonome plant-meldingen host-only/verkeerde speler** `[B]` — GrowPlant.cpp:452/500/533 (rot-dood, besmetting,
  mold/pest-dood) doen `NotifyPawn(GetOwner())`; op een uit-save herstelde pot is owner=nullptr -> valt terug op
  host-lokale `Notify()`. Joiner ziet nooit dat z'n plant besmet raakt/sterft; in competitive ziet de host de
  waarschuwing van de JOINER-plant. **Fix:** broadcast naar de relevante speler(s) — in competitive alleen de speler
  wiens mirror-box de pot bevat; anders `NotifyAllPlayers`.

### Competitive contacten/afspraken (cluster — hangt samen)
- **#Pid-contactsleutel breekt NPC-matching** `[C]` — in competitive registreert `WriteStatsToRegistry` de contact
  met de per-speler-sleutel `NpcId#Pid` (NpcRegistryComponent.cpp:540-546, CustomerBase.cpp:1018-1031). Daarna matcht
  `SendRandomAppointment/SpawnAppointmentCustomer` `Cb->NpcId (basis)` nooit tegen `ContactId (#Pid)` ->
  adres-lookup faalt -> ALTIJD een nieuwe fallback-NPC gespawnd met `NpcId#Pid`.
- **Deal landt op fantoom-entry** `[C]` — die fallback-NPC (`NpcId#Pid`) gaat via `SubmitOfferProduct` de key
  `NpcId#Pid#BuyerPid` bouwen (dubbele suffix, CustomerBase.cpp:3088-3097) -> respect/loyaliteit/afpak-detectie
  werken niet. **Fix (cluster):** contacten op BASIS-NpcId registreren + eigenaar apart als ForPlayerId-veld op
  `FPhoneContact`; matching/deal-keys op basis-NpcId; SendRandomAppointment eigenaar uit de sleutel halen.
- **Afspraak/quick-respond raakt de verkeerde speler** `[C]` — `RespondTopPending` (ContactsComponent.cpp:663-678)
  en `RespondToContact/ProposeTime/ProposeStrain` (680-875) matchen op `FromContactId` ZONDER `ForPlayerId`-filter
  -> joiner kan de afspraak van de host accepteren/weigeren/muteren. **Fix:** CallerId (StablePlayerId van de RPC-owner)
  doorgeven en matchen op `Msg.ForPlayerId leeg || == CallerId`.
- **Chat-regels lekken tussen spelers** `[C]` — afgeleide berichten (Reply/Mine/Theirs, PushInfoMessage) krijgen
  `ForPlayerId` LEEG -> `IsMsgForLocal` = true voor beiden -> host ziet joiner's chat en omgekeerd
  (ContactsComponent.cpp:726/780/844/899, PhoneWidget.cpp:955). **Fix:** ForPlayerId overnemen van het bron-bericht.
- **Afspraak-aankondiging host-only** `[B]` — CheckAppointments (ContactsComponent.cpp:376-382) `Notify(-1)`;
  joiner ziet niet dat z'n klant is gearriveerd. **Fix:** NotifyPawn naar de doelspeler(s) uit `Msg.ForPlayerId`.

## MEDIUM

### Meldingen host-only (mechanisch: Notify(-1) -> NotifyPawn/NotifyAllPlayers)
- Pakket oppakken (DeliveryPackage.cpp:149/160) `[joiner]`
- Slapen "Slept - saved here." (PlaceableProp.cpp:413) `[joiner]`
- Telefoon-store koop/verkoop (StoreComponent.cpp:94/103/321/605/617/626) `[joiner]`
- Upgrade-koop (UpgradeComponent.cpp:60/73/84) `[joiner]`
- **Level-up + shop-licentie** (LevelComponent.cpp:63/72) — gedeelde mijlpaal -> `NotifyAllPlayers` `[joiner]`
- Contact-nummer-unlock (NpcRegistryComponent.cpp:551) `[joiner]`
- Quick-sell deal-toast (CustomerBase.cpp:3335) `[joiner]`
- Afspraak accept/decline-feedback (ContactsComponent.cpp:741/794) `[joiner]`

### Competitive-eigendom-filters
- **Home-Stash telt ALLE shelves op** (InventoryWidget.cpp:819-830) — joiner ziet opslag van tegenstander. Fix:
  filter op comp-home-box in competitive. `[C]`
- **ProposeContactStrain scant alle shelves** (PhoneClientComponent.cpp:3614-3646) — biedt strain van tegenstander
  aan. Fix: idem comp-home-box-filter. `[C]`
- **Compass toont elke afspraak-NPC aan iedereen** (CompassWidget.cpp:157-174) — host ziet joiner's klant-poppetje.
  Fix: gerepliceerd `ApptForPlayerId` op ACustomerBase + filter. `[C]`
- **MarkThreadSeen zonder ForPlayerId** (ContactsComponent.cpp:914-923) — host lezen wist joiner's badge. `[C]`
- **E.1b-yaw Meubel-spiegel-yaw** (DoorRetrofitter.cpp:5215-5216) — binaire +180/0-heuristiek i.p.v. echte
  reflectie `v'=v-2(v.N)N`; mirror-normaal is -179.95 graden -> meubels ~0-1 graden scheef + onvoorspelbare flip bij
  yaw~45/135. Fix: echte facing-reflectie. `[C]`

## MINOR
- Light-switches client-lokaal herberekend (APackLightSwitch bReplicates=false) — 2e plek waar host/joiner-geometrie
  kan divergeren (DoorRetrofitter.cpp:5228-5246). `[C]`
- Overval leegt planten van BEIDE spelers (HeatComponent.cpp:209-231) — competitive fairness. `[C]`
- Delivery-marker/pakket zonder eigenaar (WeedShopGameState.h:39, DeliveryPackage.cpp:116) — host ziet joiner's
  pakket-marker; iedereen kan oppakken. `[C]`
- Transfer-ontvanger = eerste andere speler (PhoneClientComponent.cpp:3302) — pas nodig bij 3+ spelers. `[C]`
- Toast static `Active` per-proces (WeedToast.cpp:18) — alleen 1-proces-tests (PIE). `[B]`
- DayNight light-cull anker = GetFirstPlayerController (DayNightController.cpp:977) — PIE-only render-cull. `[B]`
- PhoneWidget appt-urgency pakt eerste NPC met ContactId (PhoneWidget.cpp:971) `[C]`

## E.4 NPC-crowd desync (+ E.6 joints) — ARCHITECTUREEL

**Definitieve oorzaak (3 agents unaniem):**
1. `ACustomerBase` zet NERGENS `bReplicates=true`/`SetReplicateMovement` (ctor CustomerBase.cpp:49-116). De klasse
   heeft WEL 17-19 DOREPLIFETIME-props + OnRep_Appearance + overal "server=waarheid, client herbouwt"-comments ->
   de replicatie-infra is compleet gebouwd maar **DOOD** (actor repliceert niet). Host-NPC's bereiken de joiner nooit.
2. De crowd-materialisatie in `ADoorRetrofitter` draait ONGEGATE op host EN joiner: BeginPlay start CrowdMoveTimer
   (:178) + ScanAndConvert (:524) zonder HasAuthority; seed (:770-820) met `FMath::Rand` (geen gedeelde seed);
   `TickVirtualCrowd` (:3394) doet `SpawnActorDeferred<ACustomerBase>` (:3553) lokaal per proces. -> host en joiner
   bouwen twee RANDOM, onafhankelijke crowds.
3. Bevroren/zwevend/geen-collision op de joiner = gevolg: de body krijgt geen AIController (AutoPossessAI alleen op
   authority) + de patrouille-drive is HasAuthority-gated (CustomerSpawner.cpp:214/406) -> geen movement -> settelt
   niet vanaf spawn +100cm -> zweeft + inerte capsule. Niet-op-host-kaart: MapWidget/CompassWidget itereren lokale
   `TActorIterator<ACustomerBase>` -> elk proces z'n eigen set.
4. **Co-op DEALEN werkt niet voor de joiner**: DealingPawn-exclusiviteit/SubmitOffer/stats staan in DOREPLIFETIME maar
   repliceren dus niet -> de deal-laag is stuk voor de joiner (los van het cosmetische crowd-probleem).
5. **E.6 joints** = zelfde klasse: joint-scatter is per-proces lokaal (DoorRetrofitter, `ScatterJoints`) -> joiner ziet
   andere/geen joints. Pickup moet server-authoritative + per-speler zichtbaar.

**Fix (aanbevolen door alle 3: optie A - echt repliceren):**
- ACustomerBase ctor: `bReplicates=true` + `SetReplicateMovement(true)` + NetUpdateFrequency ~15 + NetCullDistance-tuning.
- Crowd-body-materialisatie (TickVirtualCrowd-spawn + keeper/deal/activity-spawns 1764/2271/3553) GATE op HasAuthority
  -> alleen server spawnt bodies, die repliceren naar de joiner. De 10Hz virtuele-loop mag client-side blijven voor
  niet-gematerialiseerde walker-dots, mits de markerbron gedeeld is.
- Joints (E.6): scatter server-authoritative + gerepliceerde lijst (of GameState-marker-lijst), pickup via Server-RPC.
- Map/kompas-markers: worden vanzelf consistent zodra de crowd 1 gerepliceerde set is; voor NPC's buiten
  net-relevantie evt. een lichtgewicht gerepliceerde positielijst op GameState (a la ActiveDeliveries).
- **Perf-let-op** (speler-regel: markers smooth + NPC's altijd aanwezig): ~70 replicerende characters is fors ->
  NetUpdateFrequency + afstands-relevantie/NetCull verplicht; evt. verre NPC's alleen als kaart-dot.

**Opties (DESIGN-KEUZE speler):** (A) volledig repliceren [aanbevolen: enige die ook co-op-dealen fixt] |
(B) deterministisch client-side met gedeelde seed [goedkoper net, maar deals + markers blijven stuk] |
(C) crowd op joiner verminderen/uit tot A/B af [simpelst, joiner ziet lege straat].

---

## FIX-CLUSTERS (voor de golven — disjuncte files)
- **Cluster A — meldingen host-only -> NotifyPawn/NotifyAllPlayers** (mechanisch, laag risico): DeliveryPackage,
  PlaceableProp, StoreComponent, UpgradeComponent, LevelComponent, NpcRegistryComponent, ContactsComponent,
  CustomerBase. Grootste cluster, maar puur `Notify(-1)`->`NotifyPawn(<instigator/eigenaar>)`.
- **Cluster B — server-side wallet-routing**: WeedShopGameState (GetEconomy), HeatComponent (bust/robbery),
  UpgradeStation/UpgradeComponent -> instigator-pawn-economy.
- **Cluster C — placement + bank**: BuildComponent + DoorRetrofitter (E.1a), EconomyComponent (bank-wipe),
  PhoneClientComponent/AtmWidget (transfer-tab in co-op verbergen).
- **Cluster D — competitive contacten-rework** (grootste denkwerk): ContactsComponent + NpcRegistryComponent +
  CustomerBase + FPhoneContact (#Pid-key -> basis-NpcId + ForPlayerId, alle ForPlayerId-filters, compass-ownership).
- **Cluster E — competitive opslag/scan-filter**: InventoryWidget + PhoneClientComponent (comp-home-box-filter).
- **Cluster F — release-flow**: upload-build.ps1 (Saved->BakedData resync) + check-cook-parity.ps1 (hash-diff).
- **Cluster G — crowd (E.4)**: zie deepdive.

---

## HER-AUDIT 2026-07-03 (na de crowd/lift/bank-golven; 8 agents + adversariele verificatie)

> Doel: bewijzen dat multiplayer VOLLEDIG klopt. Uitkomst: de kern is met bewijs "ok" (crowd nu 2-instance
> geverifieerd host==joiner, lift/WorldSync, economie/bank live, plaatsen via ServerPlace, deals, per-speler
> meldingen via NotifyPawn, save-mechaniek, dag-nacht). Onderstaande zijn NIEUWE bevindingen die de vorige
> golven niet raakten. `[CONFIRMED]` = adversarieel geverifieerd langs het echte code-pad.

### Echte gameplay-bugs (fixbaar, veilige fix bekend)
- **H.6 Verse co-op: joiner dupliceert de starter-meubels** `[N]` `[GAP]` — het StarterFurniture-blok in
  DoorRetrofitter.cpp:2178-2249 (ScanAndConvert) staat NIET achter `!IsNetMode(NM_Client)`, anders dan alle
  crowd/joint/resident-blokken. Gate is `bFresh = Loaded==nullptr`, en op de joiner is Loaded ALTIJD nullptr ->
  joiner spawnt z'n EIGEN AStorageShelf/APlaceableProp (bReplicates=true) bovenop de gerepliceerde host-set ->
  dubbele/overlappende meubels, alleen bij de joiner. Zelfde klasse als de crowd-bug (valkuil #5). Competitive
  ontsnapt (bCompStarter skipt). **Fix:** wrap het fresh-furniture-blok (+ Fridge/Shelf/prop) in `!IsNetMode(NM_Client)`.
  **Verifieren:** 2-speler verse boot -> ziet de joiner dubbele meubels?
- **H.7 Late joiner overschrijft de gedeelde crew-bank** `[N]` `[CONFIRMED]` — SaveGameSubsystem.cpp:463 roept
  onvoorwaardelijk `E->SetBankCents(Data.BankCents)`; in co-op routeert dat via BankOwner() naar de GEDEELDE
  GS-economy. Een terugkerende vriend met een oudere save-record overschrijft mid-session de LIVE crew-bank ->
  host verliest live verdiend bankgeld. De BeginPlay-init heeft wel een `BankOwner()==this`-gate, dit restore-pad
  niet. **Fix:** `if (E->IsBankOwner()) E->SetBankCents(...)` (helper toevoegen); cash (SetBalanceCents) is per-pawn en blijft ongemoeid.
- **H.8 Joiner ziet z'n Packages-app nooit** `[B]` `[CONFIRMED]` — PendingDeliveries/DeliveredHistory
  (PhoneClientComponent.h:1030/1034) zijn geen UPROPERTY(Replicated); alleen server-side gevuld. Joiner leest de
  lege lokale kopie -> geen ETA/annuleren/historie (drone vliegt + marker + ophalen werkt wel). **Fix:** structs
  -> USTRUCT met UPROPERTY, arrays UPROPERTY(ReplicatedUsing=OnRep) met COND_OwnerOnly + DOREPLIFETIME (dekt late joiner).
- **H.9 Bezorg-marker OrderId botst tussen spelers** `[B]` `[CONFIRMED]` — NextOrderId is per-component (start 1);
  de gedeelde ActiveDeliveries op GameState wordt op OrderId gekeyed -> tegelijk bestellen = markers overschrijven/
  verdwijnen (PhoneClientComponent.cpp:2736 + WeedShopGameState.cpp:55). **Fix:** server-uniek OrderId (GameState-teller
  met HasAuthority) of FActiveDelivery een tweede owner-sleutel geven.

### Competitive-lekken (medium, competitive-only)
- **H.10 Contacten-app toont de contacten + relatie% van de tegenstander** `[C]` — PhoneWidget.cpp:3216 itereert
  alle contacten zonder OwnerPlayerId-check (Messages-app filtert wel via IsMsgForLocal). **Fix:** contact-lijst
  op OwnerPlayerId==lokale speler filteren.
- **H.11 Bezorg-marker (map+kompas) toont de bestelling van de tegenstander** `[C]` — FActiveDelivery draagt geen
  owner-id; compass/map renderen alle deliveries (CompassWidget.cpp:286, MapWidget.cpp:498). **Fix:** ForPlayerId op
  FActiveDelivery + competitive-filter in de render. (Hangt samen met H.9's owner-sleutel.)

### Klein / cosmetisch / design-keuze
- **H.12 Weer verschilt per speler** `[B]` — PickAndApplyWeather kiest lokaal random (DayNightController.cpp:674),
  nooit gesynct. Puur cosmetisch (weer raakt geen gameplay). Fix: weer server-kiezen + via GameState/WorldSync repliceren.
- **H.13 Mede-speler-marker is CYAAN, niet goud** — MapWidget.cpp:446 / CompassWidget rendert FLinearColor(0,1,1);
  patchnote v1.19.4 claimt "goud". Of kleur naar goud, of patchnote-tekst bijwerken (cyaan is wel goed zichtbaar).
- **H.14 Huur-model niet co-op-ontworpen** `[N]` — twee onafhankelijke huur-timers per proces; op de joiner-client
  faalt RemoveMoney (server-gate) -> joiner-deur kan onterecht op slot (SetRentOverdue). (De eerdere "huur alleen bij
  host"-finding was REFUTED: GetFirstPlayerController is per-proces, dus elk proces pakt z'n eigen speler.)
- **H.15 Joiner 'Load'/'Quit' in pauze -> uit de sessie** `[B]` `[GAP]` — client-lokale OpenLevel (PauseMenu ->
  OpenMainMenuLoad -> ReloadCurrentLevel) scheurt de joiner naar een solo lokale save. **Fix:** Load/Quit-to-menu op
  een client verbergen of via Server-RPC laten lopen.
- **H.16 Speler-matching zonder OnlineSubsystem** `[GAP]` — StablePlayerId valt zonder OSS terug op PlayerName
  (vaak 'Player') -> save-record-collisie bij gelijke namen. Fix: per-connectie unieke key (PlayerState->GetPlayerId()).
- **H.17 Geen reconnect / seamless travel / network-failure-handling** `[DESIGN]` — bare LAN; elke mid-session
  host-Load/travel dropt de joiner stil. Design-besluit nodig of dit acceptabel is voor deze release.

### LAATSTE CONTROLE-GOLF (07-03, adversariele verificatie van golf 2 + release-gates)
Uitkomst: 5/6 golf-2-fixes correct+compleet+regressie-vrij geverifieerd. 3 must-fixes alsnog toegepast:
- **H.15 was BROKEN** (guard in BuildShell, WBP_PauseMenu slaat die over) -> GEFIXT: guard naar NativeConstruct
  (Btn_Load/Btn_MainMenu verbergen op NM_Client) + NM_Client-bail in USaveGameSubsystem::ReloadCurrentLevel
  (choke-point van New/Load/Continue/Host -> dekt elk UI-pad).
- **H.14 huur** bleek release-blokkerend (joiner permanent buitengesloten) -> GEFIXT: overdue-inning+lock
  server-only (`!IsNetMode(NM_Client)` op de rent-tak in DoorRetrofitter ~2088).
- Mechanische release-gates (build/boot/cook-parity/smoke) draaien via upload-build.ps1 vlak voor upload.
Release-readiness verder SCHOON: golf 2 introduceert geen nieuwe string-geladen /Game-content en geen editor-only
code; nieuwe USTRUCTs/Replicated-velden correct geregistreerd voor Shipping.
Blijft DEFER (niet-blokkerend, friends-only): ServerBuyProperty dubbel-koop; ServerCallElevator/ServerToggleDoor
reach-validatie; geladen-co-op speler-lichtschakelaars niet op joiner (spiegel van H.6); H.16 OSS-naam-matching;
H.17 reconnect/seamless-travel.

### SYNC-COMPLETENESS-GOLF (07-03) — "synct alles wat zou moeten?"
6 agents trokken per wereld-object de muteer->ziet-de-ander-het-keten na. Uitkomst: de sync is fundamenteel
SOLIDE. Bevestigd gesynct (met per-veld bewijs): planten (groei/water/oogst/ziekte/mest/pot-gear 13/13),
droogrekken, kasten/koelkast/kluis-inhoud, processor/bench-voortgang, NPC's (uiterlijk/deal/afspraak/activiteit/
kaart), speler (skin/outfit/rook-anim/handitem), gedeelde bank + level/XP/heat/goals/milestones/upgrades/
NPC-relaties, klok, weer, bezorg-markers. Deuren/lift via WorldSync. Prive-per-speler (cash/inventory/hotbar/
camera/UI/toasts) blijft terecht prive.
3 restpunten gevonden; speler koos #1 + #2 fixen (#3 blijft by-design):
- **#1 GEFIXT** Stoned XP-bonus was een GEDEELDE crew-multiplier op LevelComponent -> op de listen-server schreven
  host EN joiner 'm elke tick (race) + een nuchtere speler liftte mee op de high-bonus. Fix: per-verdiener via
  IPlayerNpcActions::GetStonedXpMultiplier() -> ULevelComponent::AddXP(Amount, StonedMult) op de 3 callsites
  (deal=PayTo-owner, oogst=InstigatorPawn, werven=this). Gedeelde XpMultiplier verwijderd.
- **#2 GEFIXT** Rent-overdue lock zat na de H.14-fix alleen op de host-deur -> joiner-deur nooit gelockt (kon de
  "vergrendelde" deur openen voor beiden). Fix: overdue-lock is nu een gerepliceerde GameState-vlag
  (bStarterRentOverdue+StarterRentCents); BEIDE DoorRetrofitters passen 'm lokaal toe op hun starter-deur; de
  betaling loopt server-authoritative via nieuwe RPC UInteractionComponent::ServerPayRent(DoorId) (deur is
  bReplicates=false -> id-lookup zoals ServerToggleDoor) zodat de joiner ook kan betalen.
- **#3 DEFER (by-design)** Lichtschakelaars per-speler cosmetisch (bReplicates=false, IsClientLocalInteract);
  host/joiner kunnen verschillende lamp-staat zien. Bewust; alleen sfeer. Kan een WorldSync-id krijgen als
  gedeelde sfeer ooit gewenst is.

### MP-READY-GOLF (07-03) — races/join-timing/save-rondrit/spook-staat + verificatie #1/#2
8 agents. De #1 (XP) + #2 (rent-lock) fixes zijn adversarieel CORRECT bevonden. De bewezen basis (crowd/
planten/economie/deuren/lift/deal-exclusiviteit/place-race/pickup-race/join-timing) is schoon. 4 nieuwe
must-fixes gevonden + toegepast (build groen):
- **ATM dubbel op joiner** `[GEFIXT]` — AAtm (bReplicates=true) werd op beide processen gespawnd zonder
  client-gate -> 2 overlappende ATM's per winkel. Fix: shop-ATM-spawn `!IsNetMode(NM_Client)` (DoorRetrofitter
  ~2319). EXACT hetzelfde patroon als de meubel-dubbelspawn; de comment beweerde ten onrechte "ATM blijft op beide".
- **Gedeelde bank uit stale Players[0]** `[GEFIXT]` — mijn H.7-restore pakte Players[0].BankCents; een stale
  offline-record kon gebankt geld terugdraaien. Fix: bank als TOP-LEVEL authoritative Save->BankCents wegschrijven
  (SaveGame) + direct teruglezen (LoadGame), alleen max-over-records-fallback voor oude saves.
- **Rent in globaal RentState.txt** `[GEFIXT]` — 1 bestand zonder slot-suffix, niet gereset door New Game ->
  lekte tussen slots + kon dag-1 overdue zijn (raakt ook solo). Fix: per-slot RentState_<slot>.txt (DoorRetrofitter
  leest Sv->GetSlot()) + New Game wist het bestand (NewGameInSlot/HostNewGameLan/RequestNewGame).
- **StorageShelf take/cook op stale client-index** `[GEFIXT]` — ServerTake pakte op een rauwe index; een
  gelijktijdige RemoveAt door speler A schoof de indices -> speler B pakte het verkeerde item (evt. Cash). Fix:
  RequestShelfTake/Cook sturen de VERWACHTE item-id mee (client-shelf GetSlotId); ServerTake/ServerShelfCook
  hervalideren Contents[Index].ItemId==ExpectedId en weigeren anders (geen UI-wijziging nodig).
DEFER (niet-blokkerend): competitive per-speler heat/XP/rent + 2-speler-cap (fairness, geen crash/geld-lek);
lamp-late-joiner; DryingRack stale-index (guarded); rent-lock transiente 2-8s window; save-slot-menu-totaal cosmetisch.

### DEFERRED-GOLF (07-03) — lamp-sync gedaan, competitive-progressie = apart project
- **Lamp late-joiner sync** `[GEFIXT]` — APackLightSwitch (bReplicates=false, client-lokale interact) kreeg het
  WorldSync-id-patroon (zoals deuren/weer/CabZ): WorldSyncComponent bewaart per stabiel lamp-id de aan/uit +
  helderheid (LampIds/LampOn/LampBright, gerepliceerd); de client relayet z'n toggle/dim via
  UInteractionComponent::ServerSetLamp (publieke RelayLampState-wrapper); elke schakelaar leest z'n staat elke
  tick + past 'm toe (SyncSuppressUntil = 0.6s eigen-wijziging-window tegen terug-flits). Server publiceert de
  geladen staat in BeginPlay -> late joiners + host/joiner zien dezelfde lampen. 2-instance boot-sanity groen.
- **Competitive per-speler XP/LEVEL** `[DEFER - ARCHITECTUREEL]` — ULevelComponent is een GameState-singleton;
  GetLevel() wordt op ~15+ plekken gelezen (HUD/prijzen/item-unlocks/shop-licentie/klant-checks). Per-speler =
  elke lezer per-speler resolven -> echt een apart project met regressie-risico, geen quick fix. Voor een
  competitive secundaire modus is gedeelde crew-progressie verdedigbaar; anders apart scopen.
- **Competitive per-speler HEAT** `[DEFER - MODERATE/ARCH]` — UHeatComponent GameState-singleton -> per-speler
  map/component + alle lezers per-speler.
- **Competitive per-speler RENT + klant-tier/cooldown** `[DEFER - MODERATE, haalbaar]` — rent-vlag per-speler +
  klant-tier/cooldown op de al-bestaande 'NpcId#spelerId'-sleutel (relatie is al per-speler). Competitive-fairness,
  niet-blokkerend voor normaal co-op.

### UPDATE 07-03: klant-tier/cooldown per-speler = GROTERE KLUS (onderzocht, hoort bij de per-speler-ronde)
NIET een veilige tail-fix. Per-speler-sleutel bestaat (EnsurePlayerNpc -> 'BaseNpc#PlayerId'; relatie/loyalty gebruikt
'm al), maar tier+cooldown per-speler loopt door de deal/crowd-kern:
- COOLDOWN (IsOnCooldown) heeft 3 betekenissen op 3 plekken: afspraak-beschikbaarheid (ContactsComponent.cpp:180,
  per-speler), CROWD-TOEWIJZING (NpcRegistryComponent.cpp:494, MOET gedeeld blijven), en deal-cooldown (per-speler).
  Uit elkaar trekken zonder crowd-toewijzing te breken; MarkDealt moet in comp BEIDE base (crowd) + per-speler zetten.
- TIER (CustomerXP) heeft een TIMING-probleem: order-grootte (GetTierOrderGrams, CustomerBase.cpp:2950) wordt bepaald
  als de klant KOPER WORDT -> VOORDAT een speler gekozen is -> geen "welke speler" om per-speler te resolven. Per-speler
  write + gedeelde order-read = order-tier groeit nooit meer in comp (regressie). Geen schone per-speler-order zonder de
  klant-koper-flow te herontwerpen.
PLAN: PlayerId-param (default leeg = base/ongewijzigd) op MarkDealt/IsOnCooldown/AddCustomerValue/GetCustomerXP/
GetTierProgress01 + helper ResolveNpcKey(BaseNpc,PlayerId)->'BaseNpc#PlayerId' in comp; deal-completie geeft
dealer-StablePlayerId; crowd-assign (494) blijft base; klant-koper-flow herontwerpen zodat de order-tier een speler kent.
VERIFICATIE: echte 2-PC competitive-test (headless niet toereikend). Doe samen met XP/level/heat/rent als 1 coherente
competitive-per-speler-ronde.

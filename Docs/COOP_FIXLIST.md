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

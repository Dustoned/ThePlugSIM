# Speler-dump 07-03 — verkende fixplannen (D1-D28)
Volledige rapporten in de sessie; dit = de essentie per item voor de edit-golven.

## BLOK 1 — UI-flash (D1-D4). Referentie-patroon: InventoryWidget::RebuildContent (r.1041-1075, pool+sig-diff)
- D1 StoreWidget: TabRow->ClearChildren (r.243) bij tab-klik; CheckoutBtn->SetContent per FillBody (r.360);
  Card Collapsed-toggle (r.372) -> Hidden; rij-sub-refs (naam/prijs/qty/knoppen) in-place i.p.v. SetContent.
- D2 Goals (PhoneWidget app 11): Panel->ClearChildren (r.3030) + BuildGoalsApp (r.3352) bij elke completion ->
  persistente kaart-pool met per-kaart refs; sortering claimbaar>in-progress>claimed VOOR de loop; bars mogen live.
- D3 DryingRackWidget = boosdoener: DetailBox->ClearChildren (r.371) + NativeTick-sig bevat HELE inventory+hotbar
  (r.483-495, legacy) -> sig strippen + rijen poolen. ShelfWidget al goed (alleen staart-shift, laag-prio).
  WardrobeWidget: Body->ClearChildren (r.428) alleen bij categorie-wissel -> persistente sub-panes per categorie.
  Bijvangst: InventoryWidget::RebuildStash (r.800) rebuildt home-stash op mutatie.
- D4 StatusHud: GEEN rebuild; wel TimeIcon->SetContent bij dag/nacht-wissel (r.220-225) + SetAutoSize-verspring
  (r.83) -> zon+maan-glyphs eenmalig in Overlay + visibility; SetMinDesiredWidth op Day/tijd/cash/bank.

## BLOK 2 — kweken/plaatsen (D5-D12)
- D5 X-hold discard: kopieer G-hold-poll (BuildComponent.cpp:624-647) -> DiscardHoldAccum op EKeys::X (vrij) bij
  focus op geplante AGrowPlant; ServerDiscardPlant-RPC -> RobClear() uitbreiden met soil/fert-reset; prompt-append
  (GrowPlant.cpp:919-952) + HUD-balk (WeedShopHUD.cpp:193-202-patroon) + hint (HotkeyHintWidget.cpp:217).
- D6 Upgrade-preview vaste as: BuildComponent.cpp:950 gebruikt SPELER-richting -> aim-richting (AimPoint - TL,
  fallback speler); cirkel TL+Dir*Edge (r.952). Server hoeft niets (ServerPlace her-zoekt binnen 220cm).
- D7 Upgrade-interact weg: PlaceableProp.cpp:331-335 + 360-363 forward-takken weg voor gear; UpdateFocus
  (InteractionComponent.cpp:77-116) her-trace door gear heen (AddIgnoredActor-loop). ROOT: ghost-preview-pot
  vervuilt FindUpgradeHostFor/FindUpgradeTarget/NearestPot -> filter !GetActorEnableCollision(). LET OP
  G-pickup-regressie: pickup-tak eigen trace geven.
- D8 Stack-glitch: ServerPlace dedup leunt op 0.5s-stale PotUpgradeMask (BuildComponent.cpp:1486) -> fysieke
  prop-scan in de RPC (patroon r.1508-1513); DryUp_/ProcUp_ (UpKind 2/3) hebben GEEN dedup -> zelfde check.
- D9 DryingRack per-slot: Entries repliceert al; SetupVisual (r.63-103) bundel-posities voor Capacity() slots
  (verdeel over 5 roedes), UpdateDryVisual (r.105-125) per slot uit GetEntries() (bDone amber/groen), 15->25 Parts.
- D10 Muur-snap randen: bWall-check (BuildComponent.cpp:732) faalt op vloer/plafond-hit -> secundaire horizontale
  her-trace (Z geklemd) + Center.Z klemmen [vloer+4, plafond-4]. Filters (r.756-780) op de HER-trace laten draaien.
- D11 Toppen-kleur: GrowPlant.cpp:1214-1240 hardcoded paars -> WeedUI::TagColorForItem("Bud_<strain>") (WeedUiStyle
  r.686-704); rijp=StrainCol, bloei=40%-blend. Let op leesbaarheid rijp (donkerder/verzadigder).
- D12 Fridge-filter: ServerShelfStore (PhoneClientComponent.cpp:1638-1670) heeft geen filter; IsPerishableItem
  (InventoryComponent.cpp:17-22, gedupliceerd in StorageShelf.cpp:215) -> centraliseren + IsFridge()-check + toast.
  Fridge-lijst: perishables + Sugar/Flour/Gelatin (+Cash toestaan).

## BLOK 3 — NPC/economie/inventory (D13-D22)
- D13 "../.." = addiction-gauge deal-venster (DealWidget.cpp:581: "%.0f/%.0f" = Addiction/30-drempel). GEEN gating
  aanwezig (bUnlocked regelt alleen contact/nummer). SPELER-KEUZE: verbergen tot unlocked of duidelijker label.
- D14 Stats-up-toast: deltas bestaan al (ComputeAcceptedDeltas CustomerBase.cpp:3150-3167, toegepast r.3338);
  iconen bestaan (t_medal/t_heart_red/t_flame/t_face_smile in honeti-128, al in cook); UWeedToast optionele
  icon-param (default = no-op voor bestaande callsites) -> "Happy customer +2 respect +3 loyalty".
- D15 Inv-vol verliezen: AddItem-result genegeerd na consume op: ServerStoreBuy/Checkout (PhoneClient 2411/2453),
  oogst (GrowPlant.cpp:869-874!), DryCollect (1539/1556), goal-reward (1717), Pack/Unpack (1779/1855/1809),
  joint-roll (2153), lab (ProcessorMachine.cpp:336). Goede patronen: ServerShelfTake (terug+toast), DeliveryPackage,
  WorldItemPickup. FIX: AWorldItemPickup::SpawnDrop + GiveOrDrop-helper (drop bij voeten + toast); NOOIT Cash.
- D16 Missing-body: alle skin/part-assets bestaan + gecookt; part-loads falen STIL (continue r.409/707); even
  waarschijnlijk = hidden-state-leak (17x SetActorHiddenInGame). EERST DIAGNOSE: [PDIAG] in GetInteractionPrompt
  (IsHidden/bAppearanceBuilt/RepSkinIndex/mesh-naam) + Warning op elke gefaalde part-load; speler doet 1 repro.
- D17 Stoep-stoelen: IsOnStreetSurface (CustomerSpawner.cpp:58-69) NIET toegepast op slenter-goal (r.768-775) +
  whitelist-token "Floor" te breed; blacklist Chair/Stool/Bench/Seat/Sofa/Table toevoegen. Alleen goal-keuze,
  nooit movement (speler-regel).
- D18 Geslacht-naam: DT_NPCs-namen niet gekoppeld aan skin-gender; Resident-rename-patroon bestaat al
  (CustomerBase.cpp:829-844 + ResidentNameForIndex CityDoor.cpp:186-227) -> uitbreiden naar DT-NPC's, ALLEEN
  zolang !bUnlocked (bekende contacten niet hernoemen).
- D19 Bag-gewicht: GetUnitWeight (InventoryComponent.cpp:505-559): Bag_ = 1.5 tarra + 0.03/g -> tarra = container
  (baggie 0.1/jar 0.3) + 0.03/g. Balans: 60kg-cap draagt dan ~10x meer bags -> factor/cap mee-tunen. Lost ook
  D15-bagging-faalpad op.
- D20 Underscore-namen: DealWidget lokale PrettyName (r.37-44) + prompt (CustomerBase.cpp:3512-3518) + toast
  (r.3464) -> WeedUI::PrettyItemName + " bag"-strip (patroon ContactsComponent.cpp:253-254).
- D21 Sample-kiezer: gram-stepper in de joint-kiezer (DealWidget r.240 e.v.); formules delen (JointIntensity
  PhoneClientComponent.cpp:2020-2030 + gains ThePlugSIMCharacter.cpp:1514-1516 -> 1 gedeelde statische functie);
  curve sqrt(g/3) cap 1.1, AddGain-clamp max +12/sample; cooldown blijft.
- D22 Backpack: BackpackTier (Replicated) op UInventoryComponent (pawn=per-speler!) + ApplyBackpackTier
  (10/60 -> 14/85 -> 18/115 -> 24/150; EUR 500/2.5k/10k, geen level); ServerBuyBackpackUpgrade; UI-rij in App 0;
  save-veld in FPlayerSaveData. NIET via gedeelde UpgradeComponent.

## BLOK 4 — wereld/audio/MP (D23-D28)
- D23 Onweer: UDW "Lightning Flash Frequency" (+Timing Randomization) nergens gezet -> SetUdwDouble-helper
  (spiegel SetUdsDouble, target UdsWeatherActor) + in SpawnUDS temperen (bv. 2-3/min); optioneel per-preset
  Thunder/Lightning x0.5 in SetWeatherPreset. Eerst 1 boot met bestaande [UDW-PROP]-introspectie voor defaults.
- D24 Weather-volume: SoundCatKey (WeedUiStyle.cpp:726-745) categorie 3 = VolWeather; AddVol in BuildAudioPanel
  (SettingsWidget.cpp:805-825); DayNightController tick-diff -> reflect-set "Volume Multiplier" op UdsSound +
  ProcessEvent("Update Volume Multiplier"); optioneel UDW Close/Distant Thunder Volume mee.
- D25 Lift-borden: DoorRetrofitter.cpp:3043-3057 (WallFace-aanname 17cm op verkeerde hoogte) -> line-trace naar
  echt muurvlak op bord-hoogte, plaat op Hit+0.5cm, tekst op plaat-voorvlak+0.6cm (PackElevatorButton.cpp:136 nu
  hardcoded +3cm); cabine: CabDigit -192 -> -198.5, tekst +0.8 (PackElevator.cpp:99/128).
- D26 Kompas (CompassWidget.cpp): iconen groter (BuildShell r.98-140) + SetRenderScale op afstand (D.Size2D al
  aanwezig; lerp 1.3->0.55 over 8000cm); winkel-markers via AStoreCounter::GetAll() (GetWorld()-filter!) met
  kleur-tabel centraliseren als AStoreCounter::KindColor (nu dubbel: DoorRetrofitter.cpp:2455 +
  PhoneClientComponent.cpp:4005). Dubbele markers: afspraak-hergebruik matcht alleen IsResident
  (ContactsComponent.cpp:515-526) -> verruimen naar elke levende klant met NpcId==BaseId; eerst 1 [PDIAG]-repro.
- D27 Disconnect: ConnectionTimeout=120 x2 dev-multiplier (DefaultEngine.ini:93-96) = tot 240s freeze ->
  20-30s; nieuwe UWeedGameInstance (OnNetworkFailure/OnTravelFailure): host=toast+doorspelen,
  joiner=toast+ReturnToMainMenu; GameInstanceClass in DefaultEngine.ini r.13.
- D28 Leave-knop joiner: PauseMenuWidget (BuildShell r.102-116 + NativeConstruct r.182-190): op NM_Client
  Btn_MainMenu rebinden naar OnLeaveSession -> GetGameInstance()->ReturnToMainMenu() (H.15-gate in
  ReloadCurrentLevel blijft). WBP-label hertitelen (introspectie nodig) of C++-knop tonen.

## Dwarsverbanden/volgorde
- BuildComponent-cluster (D5/D6/D7/D8/D10) = 1 agent; GrowPlant (D5-prompt/D11) apart van BuildComponent plannen.
- D15+D19 samen (bag-tarra veroorzaakt bagging-verlies); D13+D14+D20 samen (DealWidget/toasts).
- D16 + D26-dubbel eerst DIAGNOSE (PDIAG + repro) voor de fix. D23 eerst introspectie-boot.
- D27+D28 zelfde agent (GameInstance + PauseMenu). DoorRetrofitter overlapt D25+D26-kleurtabel -> zelfde golf serialiseren.

## D29 — LOADING/OPSTART (2 verkenningen, log-bewezen)
### Metingen (dev)
- Boot->menu 40-61s VOLLEDIG ZWART (geen splash.bmp, geen startup-movie, boot-movie bewust uit wegens PSO-race).
- Menu->game 63-66s; join-LoadMap 90s.
- HOOFDVONDST: constante ~27s STILLE stall, 2x per sessie (menu-map EN beach), exact tussen
  "[BOOTMARK] GameState::BeginPlay" en "NPC registry loaded" (26.6-27.8s over 11 samples). Geen shader-jobs,
  geen asset-loads in de gap; verdenking = engine-interne blocking wait op skinned-asset-compile (dev-only;
  bootB toont "Waiting for skinned assets ... Tony_Body_Cloth"; joiner-80s-block hangt aan SM_Flag_01 cloth).
- PACKAGED traagheid = ONGEMETEN (Shipping logt niet) -> aparte Development-config package meten;
  vermoedelijk eerste-run PSO-precache onder de cover (BootCoverWidget r.141, HardCap 120s r.150).
### Fixplan (volgorde)
1. DIAGNOSE 27s (3 regels): BOOTMARK bij NpcRegistryComponent::EnsureSeeded-entry (r.164) +
   EconomyComponent::BeginPlay-entry (r.20) + na Super::BeginPlay in WeedShopGameState.cpp:80; LogSkinnedAsset
   Verbose. 1 boot -> exacte call-site. Dan: Keep()-set uitbreiden (cloth/flag/skeletons) of pre-compile onder
   het laadscherm (PreloadCrowdSkins-patroon, DoorRetrofitter.cpp:175).
2. NAAD-FIX "1 scherm": bar springt naar 100% bij RoomReady (WeedShopCore.cpp:537) -> zelfde formule als cover.
   LET OP: SetCoverUp verplaatsen naar cover-tick = DEADLOCK (game thread zit in WaitForMovieToFinish) - niet doen.
3. JOIN-SCREEN (D29b, log-bewezen): (a) movie-cap E>90 (WeedShopCore.cpp:549) loopt vanaf JoinLan terwijl de
   join-LoadMap zelf ~90s duurt -> movie sterft precies bij load-einde; cap RELATIEF maken aan load-einde
   (PostLoadMapWithWorld-timestamp), niet verwijderen (vangrail). (b) Cover bestaat pas bij pawn-possess
   (EnsureWidget, PhoneClientComponent.cpp:1104-1112; possess = 5-12s NA LoadMap) -> gat met rauwe beach; fix:
   PostLoadMapWithWorld-hook zet cover ZONDER pawn (AddViewportWidgetContent). (c) Cover-fade mist pawn-gate ->
   nieuwe vlag WeedShop_SetLocalPawnPlaced (gezet op DoorRetrofitter vloer-plaats-punten r.1913/2039/3512/5511).
4. SPAWN DIRECT GOED: joiner spawnt op beach-PlayerStart en wordt pas een scan-pass later thuisgezet
   (DoorRetrofitter r.1879-1928/2006-2055/comp 5491) -> HomePawnNow(PC)-helper (homing-logica extraheren) direct
   na RestartPlayer in WeedShopGameMode (r.142-173). Homing SERVER-ONLY maken (client-homing kan op 2 echte PCs
   afwijkende HomeSpawn.txt hebben -> rubber-band); client houdt alleen de vloer-pin.
5. BOOT-INDICATIE: Content/Splash/Splash.bmp toevoegen (nul risico); startup-movie ALLEEN met 3x boot-test-gate
   (zelfde PSO-race-risico als de teruggedraaide poging). Met fix 1 wordt boot->menu ~13s dev.
6. Apart perf-punt: 21s frozen frame op de joiner direct na possession (comp-kamer-bouw/skins op de game-thread).

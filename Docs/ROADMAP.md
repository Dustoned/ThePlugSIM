# ROADMAP — ThePlugSIM

> **Dit is de levende roadmap.** Het oude A–Z stappenplan in de brief is afgerond en vervangen door dit document. Volgorde = prioriteit. Afgeronde items afvinken en (groot werk) loggen in `DECISIONS.md`.
>
> Laatst bijgewerkt: 2026-07-04 - grote afvink-ronde (audit tegen code/commits): D1-D34, sectie 1 (beach-map compleet), 2C/2D, Golf D/E/F/G + H.3, B.15, D.1 afgevinkt. 2A.2-2A.4 afgevinkt (XP-tempo doorgerekend, concentraten-budget gefixt, level 32 gevuld). Nog open: 2B (mid-game content), een paar losse D.x + H.1/H.2/H.4/H.5 (co-op-restanten, deels deferred/accepted), T.x tech-debt, D35.
> (oude notitie 2026-07-02) — tweede notitie-dump vastgelegd als "BACKLOG 07-02b" (14 punten: NPC/placement/QoL). C.1-C.6 afgevinkt (uitgebracht in v1.19.3), co-op-disconnect opgelost + uitgebracht.
> **Detail-uitwerking per bevinding (probleem → file → fix, afvinkbaar): [`FIXLIST.md`](FIXLIST.md).** Dit document = de grote lijn; de fixlist = het systematische afwerk-document.

**Grote lijn:** eerst de **beach-map** echt het spel-wereld maken (daar speelt straks álles), dan **levels 1-50 écht goed** maken, dan pas de **50+ shop-fase**. Co-op-fixes en save-gaten lopen daar dwars doorheen omdat ze klein zijn maar sessies breken.

---

## FASE 1 — Beach-map wordt de echte wereld (NU BEZIG)

De CityBeachStrip-map heeft werkende deuren (DoorRetrofitter), room-replicatie + bakes en save/load van geplaatste objecten. Maar al het stads-leven hangt nog aan de procedurele `ACityGenerator` — op de beach-map bestaat dat dus niet. Dit is de port-backlog, in bouwvolgorde:

- [x] **1.1 Homes-registry voor de beach-map** — een `FApartmentHome`-equivalent (deur-positie, interieur-spawn, bounds) voor de gebakken/gestempelde woningen. Bron: de geplaatste `ACityDoor`s + baked rooms, of een hand-authored lijst. Alles hieronder hangt hiervan af.
- [x] **1.2 Koopbare woningen** — `GetPropertyOffers()`-equivalent op de beach-map (starter gratis + 2 koopbare), gekoppeld aan de homes-registry; `PhoneClientComponent::FindCity()` krijgt een beach-map-pad. Save (`OwnedHomes`) moet op deze map blijven kloppen.
- [x] **1.3 NPC-bewoners zonder CityGenerator** — `CustomerSpawner` laten werken met de nieuwe registry: bewoners toewijzen aan woningen, in/uit lopen, dag/nacht-ritme, park-bezoek (park-zone als marker/actor op de map zetten).
- [x] **1.4 Winkels + leveringen** — `StoreCounter`s in de echte winkelpanden plaatsen/registreren, delivery-posities (voordeur van JE woning) en ATM's op de beach-map.
- [x] **1.5 Navmesh + routing valideren** — dekking over de hele strip checken (soak met `-AutoSoak`); stoep-routing voor bewoners (open punt uit DECISIONS 06-04 #3).
- [x] **1.6 End-to-end op de beach-map** — verse start → kweken → dealen → afspraak → save/load → continue, allemaal op de beach-map. Daarna: beach-map als default new-game map.

> Hardcoded aanname om te bewaken: alles checkt `MapPath.StartsWith("/Game/CityBeachStrip")` — een tweede externe map breekt dit. Pas generaliseren als dat ooit echt speelt.

---

## FASE 2 — Levels 1-50 écht goed (vóór de 50+ shop-fase)

### 2A. Balans-pass (data, geen nieuwe systemen)

- [x] **2A.1 Late Cali-seed-economie repareren** — **OPGELOST door de economy-V4-overhaul; GEVERIFIEERD 07-04 tegen DT_Strains/DT_Products.** De stale getallen (Gary Payton €5.000 zaad -> 8g = verlies) kloppen niet meer: nu €1.013 zaad -> 175g -> ~€5.812 winst (base-yield). ALLE strains winstgevend; winst/min loopt op van ~€67 (lvl1) naar ~€153 (lvl49). ROI-multiple daalt 15x->6,7x maar gecompenseerd door groeitijd+absolute winst. Geen verlies-seeds meer.
- [x] **2A.2 XP-tempo doorgerekend (07-04)** - curve `100 + (lvl-1)*40` (~106k XP tot 50). Conclusie: het tempo is consistent (~1,5-2,5 kweekcycli/level), GEEN vastloper 30->50 - de speler vond het al oke, dus de XP-curve zelf ONGEWIJZIGD gelaten. Wel bleek er een profit-dip lvl 27-35 (hoge-THC-lage-yield strains): opgelost met een THC-premium op de marktprijs (commit 98e3ec38), geen XP-tik nodig.
- [x] **2A.3 Concentraten-rendement (07-04)** - root cause: de vlakke budget-cap (EUR 80/g) sloeg de Mult-geprijsde concentraten (EUR 270-500/g) dood, klanten weigerden alles. Fix: budget is nu markt-relatief (`EffBudget = max(BudgetPerUnit, Market*1.25)`, commit 502e146a), zodat de concentraat-ketens hun hogere prijs echt kunnen vragen. Ketens leveren nu meer per uur dan baggies.
- [x] **2A.4 Level 32 gevuld (07-04, commit 1aa94626)** - twee koopbare items op lvl 32: **Purity coil** (ProcUp_Purity, 120k) = placeable gear naast een hash/press-machine, +20% output-kwaliteit (concentraat-THC zit al aan de willingness-cap, dus kwaliteit is de zinvolle knop -> hogere accept-kans); **Smart pot** (Pot_Smart, 320k) = top-pot met ingebouwde auto-watering + hogere care (1.15)/yield (1.40). Late-game cadans-strooisel (35-49) blijft open voor later.

### 2B. Mid-game variatie (lvl ~20-35)

Tussen lvl 21 (pro-edibles) en lvl 36 (Oil_Pro) komt er geen nieuw spel-werkwoord bij — 15 levels "meer van hetzelfde".

- [ ] **2B.1 Dag-orders / bestellingen** — kleine dagelijkse opdrachten via de telefoon ("VIP wil 20g Sour Diesel ≥25% THC vóór 22:00, bonusprijs"), schaalt met level. Breekt de loop op en gebruikt bestaande chat/contacts-systemen.
- [ ] **2B.2 Bulk-deals** — af en toe een grote afnemer (hele pot / 100g-blok in één keer, lagere prijs per gram, meer heat). Gebruikt bestaand cooldown/NPC-systeem.
- [ ] **2B.3 Goals dynamischer** — de 20 vaste lifetime-goals staan los van level; een dagelijkse/weekse rotatie-laag erbovenop geeft mid-game richting.
- [ ] **2B.4 Concentraten iets naar voren** — eerste hash/oil-stap richting lvl ~22-26 zodat de mid-game een eigen tech-chase heeft (pro-tiers blijven laat).

### 2C. Co-op-pariteit (fixes + de uitgestelde testronde)

- [x] **2C.1 Meldingen naar álle spelers** — `HeatComponent.cpp:137` (en vergelijkbare plekken) stuurt bust/overval-toasts alleen naar `GetFirstPlayerController()` = alleen de host. Omzetten naar per-speler client-melding.
- [x] **2C.2 Deuren/liften in co-op beslissen** — `ACityDoor`/`ACityElevator`/`APackElevator` staan bewust op `bReplicates = false` ("ieder z'n eigen deur"). Voor deuren waarschijnlijk prima; voor liften die spelers vervoeren in co-op echt testen — desyncs hier voelen kapot. Beslissing loggen in DECISIONS.
- [x] **2C.3 Client-save-pad verifiëren** — pauze-menu-save via `RequestSaveGame→ServerRequestSave` bestaat; checken dat een client nette feedback krijgt en niets stil faalt.
- [x] **2C.4 De grote 2-speler PIE-testronde** — wardrobe/skins, kluis, park-wachtrij, chat-balken, winkeliers, competitive-modus, bed-verhuizen, derde-persoon (B). Alles is replication-aware gebóuwd maar nooit met 2 spelers gedraaid. Bevindingen → fixlijst.
- [x] **2C.5 Packaged build (.exe)** — nooit gemaakt, wél nodig: LAN co-op met een vriend vereist een packaged build (zie DECISIONS). Eén keer packagen + firewall/poort 7777 smoke-test. Dit is ook de enige echte "werkt het buiten de editor"-check.

### 2D. Save-gaten dichten (klein maar irritant)

- [x] **2D.1 Waterfles-vulling** — `WaterCanComponent`-vulgraad gaat niet mee in de save; flessen zijn leeg na load → planten verdrogen onverdiend.
- [ ] **2D.2 Open kleine bugs** — `FindHomeForPoint` pakt bij gestapelde flats soms de verkeerde verdieping (fix: 3D-afstand i.p.v. XY — open sinds 06-06); machine/animatie-cosmetiek na load.

---

## FASE 3 — De 50+ shop-fase (GERESERVEERD — pas na fase 1+2)

Level 50 = shop-licentie = halverwege. Levels 51-100 zijn bewust leeg gehouden voor de echte winkel-ervaring. Nog níet bouwen, wel het kader:

- Eigen winkelpand op de beach-map (een van de echte winkel-interieurs).
- Klanten lopen continu binnen, browsen schappen, rekenen af aan de toonbank.
- Schappen vullen met je eigen waar + merch (bongs, grinders, papers).
- Employees die pak/verdeel/kassa-werk overnemen (de in DECISIONS vastgelegde "later in de legale shop"-belofte).
- Heat/risico verschuift: inspecties/reputatie i.p.v. straat-busts; achterdeur-verkoop blijft riskant.
- Unlock-ruimte 51-100 vullen met shop-progressie (assortiment, inrichting, personeel-tiers, franchise-opmaat).

---

## BACKLOG 07-01 — speler-notities (polish & fixes)

> Vastgelegd 2026-07-01 vanuit de notities van de speler. Prioriteit wordt samen gezet; open vragen staan er per item bij.

### Bugs (klein, hoge irritatie)

- [x] **B.1 Texting-animatie stopt niet / speelt waar 't niet hoort** — de sms-animatie hoort ALLEEN bij een open telefoon (Tab): direct stoppen bij sluiten. Nu blijft 'ie doorgaan tijdens lopen → poppetje "slidet" rond. **BESLUIT: upper-body blend** (bovenlijf sms't, benen lopen — AAA-stijl). Én: **inventory openen triggert in third person óók de telefoon-pak-animatie** — inventory hoort géén telefoon-animatie te hebben (gewoon stilstaan).
- [x] **B.2 Cash niet splitbaar/dropbaar** — contant geld in de inventory: kan niet splitten, niet uit de inv draggen om te droppen, en normale drop werkt ook niet.
- [x] **B.3 NPC's blijven stilstaan in de hal** — na naar buiten lopen volgen ze de gezette lines niet meer; blijven staan tot despawn. Onderzoeken (nav/route-handoff).
- [x] **B.4 "Kom langs"-NPC spawnt soms midden in de kamer** — hoort beneden in de hal te spawnen (afspraak-deals).
- [x] **B.5 Wardrobe-belichting wisselt** — eerst goed, na een paar seconden te donker (dag/nacht loopt door). Fix: belichting bevriezen / geen nacht in wardrobe-view.
- [x] **B.6 Bank "Send to a friend" alleen in co-op** — alleen tonen als er écht een mede-speler in de sessie zit, mét diens naam op de knop; verbergen in singleplayer.

### Gameplay-regels

- [x] **B.7 Pest/mold = harde lockout** — bij besmetting: géén water meer kunnen geven, ALLE stats bevriezen (water/health/groei — alles) tot er gesprayed is. Niet op tijd gesprayed → kwaliteit daalt snel. (Open: welke tijdsduur? Voorstel: bestaande gratie-tijd hergebruiken.)
- [x] **B.8 Meer joint-vindplekken** — extra potential spots bij sigaretten-prullenbakken en grote kliko's: `SM_AshtrayBin`, `SM_Dumpster`, `MetalTrashCan`, e.d. Niet overpowered — het blijft "een rondje lopen om joints te zoeken". Héél kleine kans op 2 joints bij één spot (zeldzaam houden).

### Content / 3D

- [x] **B.9 Nieuwe joint-modellen** — in Downloads: `joint-scan-v2.zip` (normale/kleinere joint → 2g/5g) en `fat-joint.zip` (dikke joint → 7g/10g). Joints moeten altijd netjes **op hun zij** op de grond liggen (nu staan ze vaak rechtop op de tip — het procedurele model is langs de Z-as gebouwd) én **HUMAN SIZE blijven** (schaal normaliseren op mesh-bounds naar ~13 cm klein / ~17 cm dik — scans komen vaak op gekke schaal binnen). Joints houden **physics zoals de droppable items** (zelfde AWorldItemPickup-tuimel+settle); met de liggende mesh-oriëntatie wordt de physics-box lang-en-plat → settelen = altijd op de zij. **CC-BY-attributie verplicht in credits:** "Joint scan (v2)" by PreyK (skfb.ly/6RWXB) + "Fat Joint" by streetpharmacy (skfb.ly/p8HON), beide CC Attribution 4.0.

### UI-reworks (overleg eerst)

- [x] **B.10 Deal-scherm opruimen/reworken** — overloaded met tekst, onduidelijk waar je moet kijken. **BESLUIT: eerst opruimen** tot alleen het echt nodige (1 blik = wat wil hij / wat bied ik / kans), daarna pas beoordelen of een volle kit-rework (WBP-route) nog nodig is.
- [x] **B.11 Keuze-/aantal-menu's reworken** — packing bench, joint rollen e.d.: bij veel wiet wordt de keuzelijst gigantisch en onoverzichtelijk. **BESLUIT: icoon-grid** — compact grid van item-slots (icoon + strain-tag + aantal, zelfde look als inventory), klik = selecteer → aantal-stepper. Overal toepassen waar zulke lijsten voorkomen.
- [x] **B.12 Package-delivery herstel + upgrade** — pakketten weer netjes vóór de apartment-deur (zoals eerst), óók voor de competitive mirror-apartments. Dozen krijgen physics (zoals droppable items). Delivery-app: meer details + lijstje van wat je gekocht hebt.
- [x] **B.13 Furniture-placement op muren** — niet half over deuren en niet op ramen kunnen plaatsen.

### UI/UX — tweede notitie-ronde (07-01)

- [x] **B.16 Map-app van de telefoon verwijderen** — nutteloos (de M-fullscreen-kaart bestaat al); app uit het home-rooster + tab eruit.
- [x] **B.17 Settings-app (telefoon) → speler-stats** — **BESLUIT: ombouwen tot speler-stats-pagina** (totaal verdiend, deals gedaan, planten geoogst, beste deal, speeltijd, e.d.); character-switch mag blijven.
- [x] **B.18 Drop tussen icon-gaps snapt naar dichtstbijzijnde slot** — item loslaten tussen/heel dicht bij een slot mag niet "missen": snap naar het dichtstbijzijnde slot (alleen bij kleine afstand, niet van ver).
- [x] **B.19 Minimalist-rebrand: volledige sweep over ALLE schermen** — **"bijna alles nog"**: de eerdere rebrand-ronde is halverwege onderbroken en nooit afgemaakt. Systematisch elk scherm langslopen (inventory-stijl = de referentie: palette + slot-look + spacing) en afvinken welke al goed is en welke nog moet.
- [x] **B.20 Quick/home-stash-view in inventory opschonen** — veel dubbele info én nuttige info mist: bv. hoe vol de water-bottle is, hoeveel soil er nog in zit — dat hoort juist in de quick view.
- [x] **B.21 Hand-preview: tag-kleur + dikkere letters** — de tag op de hand-preview meekleuren met de strain-tagkleur, en de tag-letters bold/duidelijker (nu te dun voor snelle indicatie).

### Test-feedback 07-02 (Blok 2-ronde)

- ~~B.22 Soil-teller op het item~~ — GESCHRAPT door de speler: soil gebruikt gewoon het aantal-badge (x3), teller niet nodig.
- [x] **B.23 Texting-polish** — de upper-body blend werkt, maar: geen telefoon in de hand (prop toevoegen) en de linkerarm staat raar uit (hoort natuurlijk langs het lichaam of aan de telefoon). Bekijken of de clip beter gewoon kan LOOPEN i.p.v. bevriezen op 45%.
- [x] **B.24 Lege pot toont soil-oogsten** — de plant-kaart bij een lege pot (bv. "Clay pot (empty)") moet tonen hoeveel harvests de soil nog kan ("Soil: 3 harvests left" / "No soil").
- ✔ B.13 (furniture over deuren) opnieuw bevestigd in de test — plan ligt klaar in de Blok 3-verkenning.

### Systemen / infra

- [x] **B.14 2 gamemodes eruit → alles naar F10 dev-menu** — **BESLUIT: Sandbox + Testing eruit** (Normaal + Competitive blijven). Alle functies van die modes als dev-menu-functies: level kiezen, reputatie/respect/loyaliteit aan NPC's geven, etc. Sluit aan op het bestaande Unified-Dev-Menu-plan (F10 sidebar).
- [x] **B.15 Loading screen (main menu → game): samenvoegen tot ÉÉN scherm + vasthouden tot de wereld klaar is** — het loading screen bestaat alléén bij de overgang main menu → game, en die ene load is in twee schermen opgeknipt: deel 1 is superlangzaam, deel 2 laat **veel te vroeg los** (je ziet alles om je heen in-spawnen: streaming/DoorRetrofitter/crowd nog bezig). Doel = echte-game-gedrag: één naadloos loading screen dat pas fadet als de wereld-klaar-signalen binnen zijn (streaming levels + bakes + spawns + prewarm), en de trage fase versnellen (onderzoeken: shaders/PSO? map-load? bakes?). **STAND 07-02:** scherm-2-vasthouden + fase-teksten + versnelde opbouw + WBP_PauseMenu-reparatie (−9s ensure-spam) zijn ERIN; de boot ging van ~40s naar ~9,4s tot GameState. **Boot-scherm is TERUGGEDRAAID**: het movie-scherm op de allereerste map-load gaf een flaky D3D12-crash (PSO-worker, @0x260) — een nieuwe poging moet pas starten ná OnFEngineLoopInitComplete (of het standaard startup-movie-systeem gebruiken) i.p.v. in PreLoadMap van de boot.

---

## BACKLOG 07-02 — speler-notities (UI-polish + deal/NPC)

> Vastgelegd 2026-07-02. Joint-fix (blijven liggen + meer over de map) is af en getest-OK; deze batch = UI/deal-polish.

### UI-polish (icoon-grids + knoppen)

- [x] **C.1 Icoon-grids gelijktrekken met de inventory** — de picker-grids (joint rollen, packing bench, offer-picker, koelkast, etc. = `UWeedItemPickGrid`) tonen iconen NIET zoals de inventory: andere sizes, klein, onduidelijk. Alle plekken die iconen tonen moeten de iconen exact zo renderen als in de inventory (zelfde icon-size/look/celstijl).
- [x] **C.2 Geselecteerde knop highlighten** — bij keuze-knoppen (bv. "Grams per bag": 1g/Max/±, "How many bags": Half/Max/±) de ACTIEVE keuze visueel highlighten zodat altijd duidelijk is wat geselecteerd is. Overal toepassen waar zulke keuze-knoppen staan.
- [x] **C.3 Help-/clutter-tekst weg** — de subtitel-regels zoals "2 g per bag (max 2)" bij *2.b Grams per bag* en "1 bag (uses 2g, max 10)" bij *3. How many bags* weghalen. Overal waar zulke uitleg-clutter staat opschonen.

### Deal / NPC-scherm

- [x] **C.4 Respect/loyaliteit/addiction als progress-cirkels** — i.p.v. losse tekst-stats (`R10 - L0 - A6`, "Not a customer yet  Addiction 6/30") de stats tonen als progress-cirkels (zoals de plant-groei-cirkels): nicer/game-ish/duidelijker. De aparte addiction-stat mag dan weg — verwerkt in de cirkel (zie in één blik wanneer hij klant wordt). Huisnummer hoeft niet per se.
- [x] **C.6 "Give joint"-knop verdwijnen na geven + variatie in teksten** — nu: joint vasthouden + "Give joint" opent de NPC-UI maar toont meteen wéér "Give joint". De give-joint-cooldown moet de knop LATEN VERDWIJNEN zodra de NPC net een joint heeft gekregen. Plus: meer verschillende NPC-teksten op die momenten (niet alleen "come on I've had better") en duidelijk aangeven dat hij nu even niet meer hoeft.

### Content

- [x] **C.5 Skin-gebonden namen** — female skins → female (funny) namen, male skins → male namen (indien haalbaar met hoe skins/namen nu toegewezen worden).

---

## BACKLOG 07-02b — speler-notities ronde 2 (NPC / placement / QoL)

> Vastgelegd 2026-07-02 (tweede notitie-dump, 14 punten). Prioriteit + blokken worden samen gezet; per item
> staat de vermoedelijke plek + open vragen. Brede read-only analyse loopt (1 agent per punt: file:regel +
> root-cause + aanpak + effort/risk + cook-parity-risico).

### NPC / gedrag

- [x] **D.1 Strain-vraag in strain-kleur** — als een NPC om een strain vraagt (chat/deal), de strain-NAAM in
  z'n strain-tagkleur tonen (zoals de tag-kleuren elders). [WeedUI tag-kleur + DealWidget/ContactsComponent]
- [ ] **D.4 NPC's met missing body OOK in de compiled build** — er lopen poppetjes zonder lichaam rond; eerder
  gedacht "alleen editor", maar het zit ook in de download. Vermoedelijk een crowd-skin die niet mee-cookt
  (DirectoriesToAlwaysCook) of een modular skin zonder body-fallback. [character-packs / CustomerBase skins]
- [ ] **D.11 NPC buiten-wachtplekken** — NPC's plekken geven om buiten op de speler te wachten: auto-plaatsen
  bij random deuren over de HELE map (langs de weg, garagedeuren, deuren aan de back-alleys). Niet handmatig
  gemarkeerd - automatisch verspreid. [DoorRetrofitter deur-registry + Customer meet/afspraak-systeem]
- [x] **D.12 NPC-onderlinge avoidance te agressief** — NPC's duwen elkaar veel te hard weg bij dichte nadering
  (lijkt of iedereen een grote persoonlijke zone heeft). Ze mogen langs elkaar lopen op de stoep met hooguit een
  zachte verschuiving; het HARDE wegduwen hoort alleen bij vaste obstakels (muren/objecten/map-geometrie) waar
  ze anders vastlopen. [Customer movement / RVO-avoidance-radius, obstacle vs pawn-scheiding]
- [x] **D.13 Afspraak-timing klopt niet met antwoordtijd** — gevraagde tijden lopen niet synchroon met wanneer
  je kunt antwoorden: een NPC vraagt "rond 21:41" terwijl die tijd al bijna voorbij is, of cancelt voordat je
  fatsoenlijk kon reageren. Timer herzien: (a) gevraagde tijd altijd genoeg in de TOEKOMST, (b) genoeg respons-
  venster voor auto-cancel. [ContactsComponent afspraak-timers]

### Placement / bouwen

- [ ] **D.3 Placeables snapping beter (vloer + muur)** — (a) vloer: strakker snappen zodat dingen echt geplaatst
  kunnen worden en BLAUW worden zo dicht mogelijk tegen muur-/vloer-randen; (b) wall-placeables: kan nu nog half
  DOOR de muur / helemaal in de hoek - de hitbox dekt niet de hele shelf. [BuildComponent footprint/no-build +
  PropMeshKit item-bounds]

### Planten / economie-feel

- [x] **D.8 Plant-gram loopt op tijdens groei** — i.p.v. meteen 6 g te tonen, het gram-gewicht laten OPLOPEN naar
  de eindwaarde over de groei (ziet er beter uit). [PlantInfoWidget / plant-groei-model]
- [x] **D.9 Bottle-water schaalt per klik** — grotere fles = meer water per klik (nette progressie). Eerste fles
  doet nu ~60% (te veel) → richting ~25% voor de eerste (mits niet te laag). [WaterCanComponent per-klik-afgifte]
- [x] **D.7 Items missen een eigen weight** — veel items hebben nog geen eigen gewicht; audit + invullen.
  [item-defs / inventory weight]

### Wereld / sfeer

- [ ] **D.5a In-wereld nacht niet pikkedonker** — 's nachts een leesbare min-licht-vloer (nu 0). [DayNightController
  UDS-exposure-clamp + skylight-nacht-vloer + NightPPV min-brightness] — IN BEWERKING (golf 1, agent A2).
- [x] **D.5b Geopende M-kaart zwart 's nachts** — de speler bedoelde de OPEN kaart (screenshot): die is 's nachts
  pikzwart op de gele/blauwe markers na. Root cause: de map-SceneCapture (DoorRetrofitter, 1x via CaptureMapNow +
  ApplyMapPhotoLight) zet een heldere PackSun, maar op de UDS-beach is de PackSun grotendeels inactief -> de
  capture pakt de UDS-NACHTzon -> zwarte foto (lampen-boost toont alleen gebouw-lichtjes). Fix: map dag-helder
  vastleggen ongeacht kloktijd (UDS-zon/exposure naar dag-stand tijdens de capture-frame, of een capture-only
  fill-light / hogere map-exposure-vloer). Raakt DoorRetrofitter + DayNightController -> NA golf 1.

### UI / iconen

- [ ] **D.6 Big-joint-icon (blunt.png)** — de dikke joint (big model) een net icoon geven: `blunt.png` met nette
  achtergrond zoals de andere download-iconen. [item-icons runtime-PNG's, Content/_Project/UI/Icons]
- [x] **D.10 Loading-tekst langzamer + meer random** — de loading-screen-tekst wisselt te snel; trager laten
  roteren en meer randomiseren (elke X). [SWeedLoadingScreen / BootCoverWidget tekst-rotatie]
- [x] **D.14 Compass clean (AC Shadows-stijl)** — de in-game compass strak/clean maken zonder UI-blok/kader;
  N/O/Z/W-letters mogen weg. [HUD-compass-widget]

### Cook-parity / build

- [ ] **D.2 Apartment-doorfix mist in de compiled build** — een deur-fix die in de editor/dev werkt maar niet in
  de download meekomt. Uitzoeken WELKE fix (recente door-commit) + waarom 't niet cookt (string-load zonder
  DirectoriesToAlwaysCook, of authority/runtime-gate die alleen in PIE loopt). [DoorRetrofitter + check-cook-parity]

---

## BACKLOG 07-02c — co-op-pariteit (2 majeure bugs + brede audit)

> Vastgelegd 2026-07-02. Speler test co-op competitive met een vriend (2 PC's, packaged build). Twee majeure
> per-speler-bugs + het verzoek om ALLES na te lopen dat per-speler hoort te werken (competitive EN normaal).

- [x] **E.1 Competitive furniture klopt niet in de packaged build (player-2-kamer)** — twee deelbugs:
  (a) de starter-furniture staat weer SCHEEF in player-2's kamer (regressie t.o.v. de editor-fix - waarschijnlijk
  een mirror-transform/cook-verschil: server stuurt een wereld-transform maar de joiner-spiegelkamer staat elders);
  (b) player 2 (joiner) kan NIETS plaatsen in z'n eigen huis: klikken doet niks, OOK als de preview blauw/valid is.
  Vermoedelijk weigert ServerPlace de plaatsing voor de joiner (IsInOwnedHome/competitive-home-box klopt niet voor
  de joiner-spiegelkamer, of de RPC komt niet aan). [BuildComponent ServerPlace + DoorRetrofitter competitive rooms + StarterFurniture]
- [x] **E.2 Waterfles-vulling niet per-speler** — AUDIT: al opgelost (commit 263f2a6b, water per-fles in het
  Quality-veld van elke stack in de per-pawn InventoryComponent, gerepliceerd). Niet reproduceerbaar. Was stale.
- [x] **E.3 Brede co-op-pariteit-audit (competitive + normaal)** — AUDIT KLAAR -> alle bevindingen in
  [`Docs/COOP_FIXLIST.md`](COOP_FIXLIST.md) (~30 items, per severity + fix-clusters). Nu uitvoeren. systematisch nalopen wat per-speler hoort te
  werken maar dat niet doet: meldingen/toasts (GetFirstPlayerController = alleen host, zie 2C.1), plaatsen/opslag,
  planten/water/oogst, economie/inventory, telefoon/deals, delivery, wardrobe/skins, safe/fridge, statische
  registries (GetWorld()-filter). Per bevinding: host vs joiner, competitive vs normaal, file:regel, fix. Levert
  de echte co-op-fixlijst (vervangt/vult 2C aan).
- [x] **E.4 NPC-crowd desync in co-op (MAJEUR)** — de joiner ziet NPC's die niet syncen: BEVROREN stilstaand,
  sommige ZWEVEND boven de grond, GEEN collision (je loopt er doorheen), en player 1 (host) ziet ze NIET op de
  kaart. Vermoedelijk is de virtuele crowd (DoorRetrofitter TickVirtualMove, 70 walkers) CLIENT-LOKAAL/per-proces
  en niet gerepliceerd -> host en joiner hebben losse, niet-matchende crowds; op de joiner tickt/positioneert de
  crowd niet goed (zwevend/bevroren) en de map-markers komen uit de lokale crowd. Diepgaand: crowd-replicatie/
  localiteit, echte-customer-replicatie (spawn server-side + movement/statemachine gerepliceerd?), Z-offset/
  collision op de client, map-marker-bron per-speler. [DoorRetrofitter virtuele crowd + ACustomerBase/Spawner +
  MapWidget/CompassWidget markers] (screenshots 07-02: zwevende man + bevroren cluster).
- [x] **E.5 Co-op-speler-marker op de kaart duidelijker/andere kleur** — de mede-speler-marker op de M-kaart is
  BLAUW, net als de NPC-dots -> verwarrend. Geef de co-op-speler-marker een eigen, duidelijke kleur (niet blauw).
  [MapWidget + CompassWidget co-op-marker-kleur]
- [x] **E.6 Joints op de grond niet gesynced in co-op** — elke speler moet dezelfde joints zien liggen, en de
  pickup/cooldown moet per-speler kloppen (nu ziet de joiner ze niet / anders). Zelfde klasse als E.4: de joint-
  scatter (DoorRetrofitter) is per-proces lokaal i.p.v. gerepliceerd. Meenemen met de crowd-replicatie-aanpak.
  [DoorRetrofitter joint-scatter + pickup-replicatie]

---

## BACKLOG 07-02d — UI-regressies (07-02, tijdens co-op-test)

- [x] **F.1 Picker pakt ALLE weed i.p.v. 1 strain (REGRESSIE)** — bij de packing bench EN joint rollen selecteert
  'ie soms alle weed i.p.v. dat je 1 specifieke strain kiest om te gebruiken. "Dit kon eerst wel" -> regressie,
  waarschijnlijk uit de C.1/C.2 picker-grid-wijziging (UWeedItemPickGrid). Uitzoeken: waar de picker de selectie/
  het gekozen-item bepaalt en waarom 'ie naar "alles" valt. [UWeedItemPickGrid + PackWidget + joint-roll-UI]
- [x] **F.2 Give-joint-NPC-UI: joint-iconen nog niet zoals inventory** — in de NPC-UI waar je joints geeft zien de
  joint-iconen er nog niet hetzelfde uit als in de inventory (C.1 dekte deze plek nog niet). [DealWidget / give-joint-UI icon-render]

---

## TEST-FEEDBACK 07-02 — co-op live-test (joiner, competitive) — RE-FIXES

> Speler testte lokaal als joiner (2 instances, competitive). Twee items zijn RE-FIXES: de eerdere fix pakte niet.

- [x] **G.1 Lift/deur synct niet in co-op** — de lift-deur gaat visueel OPEN voor de joiner maar 'ie kan niet naar
  BINNEN (collision/trigger/verdieping niet gesynced). Lift/elevator is `bReplicates=false` (2C.2) -> in co-op
  desync. Fix: lift server-authoritative maken (verdieping/deur-staat repliceren) of de joiner-trigger lokaal
  correct afhandelen. [ACityElevator/APackElevator]
- [x] **G.2 M-kaart NOG STEEDS zwart (RE-FIX van D.5b)** — mijn UDS-naar-middag-bij-capture-fix pakte niet: de kaart
  blijft zwart en "verandert niks tot 6u \'s ochtends" -> de capture weerspiegelt nog de live nacht-belichting.
  Robuuste fix nodig: ALTIJD iets zichtbaar ongeacht tijd. Waarschijnlijk UDS "Update Active Variables" is
  async/time-sliced -> de capture in dezelfde frame pakt de zon-update niet. Route: OF base-color/unlit capture
  (SCS_BaseColor -> altijd albedo-helder), OF een gegarandeerde capture-only fill-light + hogere map-exposure.
  [DoorRetrofitter CaptureMapNow + MapCapture CaptureSource + DayNightController ApplyMapPhotoLight]
- [x] **G.3 Joiner mag NOG STEEDS niks plaatsen (RE-FIX van E.1a)** — toast "only in your own house" bij de joiner,
  ondanks de GetCompetitiveHomeBoxes(bJoiner)-fix. Uitzoeken: welke exacte check/toast dit is (is 't een ANDERE
  check dan de E.1a-IsInOwnedHome?), of IsLocallyControlled op de server voor de joiner-pawn wel klopt, of de
  joiner-mirror-kamer-box wel correct berekend is. [BuildComponent ServerPlace + IsInOwnedHome + DoorRetrofitter comp-boxes]
- [x] **G.4 Speler-markers felle hoog-contrast kleuren** — goud (E.5) is niet goed zichtbaar. Alle speler-markers
  fel + goed leesbaar in donker EN licht (evt. outline/contrast-rand). [CompassWidget + MapWidget speler-markers]

---

## TEST-FEEDBACK 07-03 — tweede co-op-ronde

- [x] **H.3 NPC-SYNC NOG STEEDS STUK (PRIO, grondig)** — bReplicates+ReplicateMovement + bandbreedte-fix
  (MaxClientRate 512KB/s) waren nodig maar NIET genoeg: op de joiner staan NPC's nog BEVROREN op verkeerde
  plekken (host loopt normaal). Vermoeden: de crowd-bodies bewegen op de server via SetActorLocation
  (TickVirtualMove) i.p.v. CharacterMovement-velocity -> een gerepliceerde SIMULATED-PROXY Character krijgt
  geen velocity -> beweegt niet op de client (alleen af en toe een snap). Diepe analyse: hoe bewegen
  crowd-bodies server-side (SetActorLocation vs AI->MoveToLocation), hoe repliceert dat naar een simulated
  proxy, en de juiste fix (CMC-velocity op server / expliciete positie-repl / SimulatedProxy-handling).
  Ook: syncen resident/afspraak/deal-NPC's wel? [CustomerBase movement + DoorRetrofitter TickVirtualMove + CustomerSpawner]
- [ ] **H.4 Joiner-pawn wordt teruggeteleporteerd (rubber-band) na de lift (PRIO, grondig)** — de joiner loopt uit
  de lift en wordt soms teruggezet naar de lift tijdens gewoon rondlopen = server-correctie van de EIGEN pawn
  (autonomous proxy). Vermoedelijk: de lift-cabine is een bewegend platform (Movable, SetWorldLocation) dat op
  host vs joiner niet gelijk staat -> de speler wordt "based" op de cab en de server relocaliseert hem; of een
  bredere movement-reconciliatie-desync. Grondig onderzoeken: hoe is de co-op-beweging opgezet (autonomous proxy,
  moving-base/BasedMovement, server-relocaties van de joiner-pawn) - dit hoort met 70 NPC's gewoon te kunnen.
  [PackElevator moving-base + AThePlugSIMCharacter CMC + GameMode/PlayerController co-op-setup]
- [x] **H.3-echt NPC-sync root cause GEVONDEN + gefixt** — de DoorRetrofitter/spawners worden per-pawn lokaal
  gespawnd (IsLocallyControlled) en repliceren niet -> op de joiner is HasAuthority()==TRUE -> de joiner draaide
  z'n EIGEN volledige crowd bovenop de gerepliceerde host-bodies (log-bewezen: "70 wandelaars geseed" op de joiner).
  FIX: crowd/joint/NPC-gates op !IsNetMode(NM_Client) i.p.v. HasAuthority() (DoorRetrofitter + CustomerSpawner +
  ActivitySpotManager). Server-beweging bleek al correct (CMC-velocity via MoveToLocation).
- [ ] **H.5 Speler-rubber-band op interieur-Movable-vloeren (RESTEREND)** — de speler staat op runtime-gespawnde
  Movable AStaticMeshActors (DoorRetrofitter ~4018/4067/4212/4420/4501) die als dynamic movement-base op de joiner
  niet resolven ("on base None", 3413x in de lokale test) -> drift + snap-back. Fix-optie: die vloeren Static/
  Stationary maken (ze bewegen niet) - RISICO: belichting/registratie van runtime-Static-actors. Was in de lokale
  2-instance-test versterkt door de 90s-join-stall (96 saved-moves overflow); op een echte snelle 2-PC-join
  waarschijnlijk milder. Apart oppakken + verifieren na de crowd/lift-fix.

- ✔ G.3 plaatsen joiner werkt, ✔ G.4 markers goed, ✔ G.1 lift werkt (joiner kan naar binnen).
- [ ] **H.1 CONSISTENTE STUTTER elke paar seconden (PRIO)** — GEMETEN (stat dumphitches) + gefixt in 2 delen:
  - [x] **H.1a Skin-load-hitches (300-500ms, de grote freezes)** — body-materialisatie deed een blocking
    skin-mesh-load op de game-thread (Flush Async Loading + skinned-asset-build, 194-236ms per stuk). FIX:
    alle crowd/basis-skins voorladen onder het laadscherm + keep-alive (ACustomerBase::PreloadCrowdSkins,
    aangeroepen in DoorRetrofitter::BeginPlay, host+joiner). Plus: de obsolete UDS-middag-push uit
    ApplyMapPhotoLight (kostte ~370ms per kaart-open via UDW-rain blocking LoadAsset; overbodig sinds BaseColor).
  - [ ] **H.1b Heavy-sweep bij lopen (~10-60ms elke ~2s)** — sweep-bevinding: de bRunHeavy-ombouwsweep draait
    vrijwel elke scan-pass zolang je LOOPT (LevelAdded/Removed-delegates + het 60m-verplaatsings-vangnet zetten
    bWorldDirty bij elke streaming-cell) -> 5+ volle actor-iteraties per pass. FIX-richting: alleen de actors
    van de NIEUW gestreamde level verwerken (delegate geeft ULevel*), LevelRemoved geen sweep, 60m-vangnet
    alleen voor de %20-backstop; evt. time-slicen. LET OP: scan-backstop NIET weghalen (memory). Pas oppakken
    als de speler na H.1a nog stutter voelt bij lopen (eerst meten met een loop-test).
- [ ] **H.2 Lift: sync tussen spelers nog niet af** — de lift werkt nu, "alleen sync nog": de cab/deur-staat
  loopt tussen host en joiner nog niet 100% gelijk. Verifieren of de WorldSync-elevator-staat echt repliceert
  (id-match host/joiner, ServerCallElevator komt aan) + evt. cab-Z sync voor meerijden.

---

## DOORLOPEND — Tech-hygiëne (oppakken tussen features door)

- [ ] **T.1 Template-dead-weight verwijderen** — `Source/ThePlugSIM/Variant_Horror/` + `Variant_Shooter/` zijn nergens gerefereerd; verwijderen + `ThePlugSIM.Build.cs` include-paden opschonen (snellere builds, minder ruis).
- [ ] **T.2 `AMoneyTestPickup` opruimen of editor-only maken** — test-actor uit de speelbare wereld houden.
- [ ] **T.3 Performance/soak-pass** — 40+ bewoners + bakes + liften: af en toe een `-AutoSoak`-run van 15+ min, hitches en log-spam checken (zeker vóór de co-op-testronde).
- [ ] **T.4 Bekende kleine known-issues** — Lola cloth/haar-physics, controller-support afmaken, ultrawide-iconen (uit PATCHNOTES 1.4.0). Laag, maar niet vergeten.

---

## Wat er NIET meer op de roadmap staat (bewust)

- Het oude A–Z stappenplan (A t/m Y afgerond; Z = packaged build leeft door als 2C.5).
- Procedurele-stad-uitbreidingen — de CityGenerator blijft werken als fallback, maar nieuwe wereld-features landen op de beach-map.
- Levels 51-100 vullen met losse unlocks — die ruimte blijft gereserveerd voor de shop-fase.

---

## SPELER-DUMP 07-03 (na release 1.20.0) — genummerd, verkenning loopt

### UI-flash / persistente UI (D1-D4)
- [x] **D1** Fullscreen shops hebben geen UI-overhaul gehad -> flashen nog; ombouwen naar persistent (pool+sig) zoals de rest
- [x] **D2** Goals-app: behaalde goals ALTIJD bovenaan (makkelijk claimen), flash weg, app opknappen (messy, meer een echt goals-menu)
- [x] **D3** Storage-UI's flashen/rebuilden nog (drying rack -> inv slepen e.d.); wardrobe + alle machines grondig nalopen zoals bij inventory
- [x] **D4** Day-counter + bank-overlay (HUD) rebuildt zichtbaar -> flash weghalen

### Kweken / plaatsen (D5-D12)
- [x] **D5** Plant discarden: X inhouden = plant weg (altijd beschikbaar)
- [x] **D6** Pot-upgrade plaatsen: preview op vaste as om de pot laten draaien met de muis (niet meer omheen lopen)
- [x] **D7** Upgrades op pot: interact-prompt (add soil-state) HELEMAAL van upgrades af; kijk-klik kiest altijd de POT, ook achter upgrades
- [x] **D8** Upgrade-stack glitch: snel klikken plaatst meerdere upgrades op 1 pot -> dedup server-side
- [x] **D9** Drying rack: 3D-rek-modellen per SLOT tonen (geen overbodige rekken), netjes verdeeld
- [x] **D10** Muur-snap: bij plafond-/vloerranden snapt wand-plaatsing (bv. drying rack) niet lekker
- [x] **D11** Wiet-toppen kleuren mee met de strain-tag-kleur (nu altijd paars)
- [x] **D12** Fridge: alleen fridge-zinnige items toestaan (edibles-flow), rest weigeren

### NPC / economie / inventory (D13-D21)
- [x] **D13** "../.."-addiction-weergave bij NPC's: uitzoeken wat dit is + hoe het hoort (speler verwachtte respect-tracking tot telefoonnummer)
- [x] **D14** Mooie stats-up-notificatie (minimalist-kit): "Satisfied/Happy customer +2 [Respect-icoon]" i.p.v. kale interactie-tekst
- [x] **D15** Inventory vol (gewicht OF slots) bij shop-koop/oppakken: overal nette notificatie + niet-passende items op de grond droppen (winkel/plek zelf); items mogen NOOIT verdwijnen
- [x] **D16** Missing-body NPC ook in singleplayer (man, lijkt kant-en-klare skin, niet citizen) -> skin-pool checken
- [x] **D17** NPC's zitten soms nog op stoep-stoelen/tafels
- [x] **D18** Geslacht-naam mismatch: "Freek" met vrouwenskin -> naam-pool koppelen aan skin-geslacht
- [x] **D19** Bag-gewicht onlogisch: 1g bag weegt evenveel als 2g bag -> gewicht schalen met inhoud (kleine bags relatief zwaarder per g, grote lichter)
- [x] **D20** NPC-vragen tonen rauwe id's met underscores ("critical_mass_2g") -> nette naam "Critical Mass 2g", zonder bag-vermelding
- [x] **D21** Sample-geven: hoeveelheid kiezen (meer gram = sneller levelen) met duidelijke indicatie per gram + maximum (anti-abuse)
- [x] **D22** Backpack-upgrades als categorie in de telefoon-upgrade-tab; puur geld-gelinkt (geen level-scaling)

### Wereld / audio / MP (D23-D28)
- [x] **D23** Onweer: veel te veel bliksem-flashes achter elkaar -> realistischer (interval/intensiteit)
- [x] **D24** Sound-optie voor weer (volume-slider weather-audio in settings)
- [x] **D25** Lift-nummers zweven van de muur (in de lift + boven de deuren: zwart vlak + verlicht cijfer los ervoor) -> alles strak vlak
- [x] **D26** Kompas-icons: groter/duidelijker, 3D-gevoel (schalen met afstand), winkels toevoegen met de kleur van de toonbank; 2 dubbele deduppen
- [x] **D27** Speler-disconnect freezet de game van de ander lang + geen disconnect-melding -> nette afhandeling
- [x] **D28** Joiner: altijd een "Leave session"-optie -> terug naar hoofdmenu
- [x] **D29** Loading/opstart-overhaul: (a) lange ZWARTE periode bij opstarten -> netter: vroege loading-indicatie
  (let op: eerdere poging in PreLoadMap = flaky D3D12 PSO-race, teruggedraaid; nieuwe poging pas na
  OnFEngineLoopInitComplete); (b) menu->game toont 2 trage loading-screens (ook packaged!) -> 1 mooie vaste,
  en de laadtijd zelf fors omlaag; (c) co-op join-loading werkt maar half (geen vaste screen) + joiner spawnt
  eerst zichtbaar op de beach -> nette join-flow met vaste loading-screen tot de echte spawn klaar is.
  - **[ROOT CAUSE GEVONDEN + GEFIXT 07-04, commit 74f56695]** De trage beach-LoadMap (packaged GEMETEN 44,9s,
    NIET sneller dan dev - oude aanname fout) zat voor ~30s in EEN stil gat: `UNpcRegistryComponent::EnsureSeeded`.
    `ShortFullName`'s achternaam-formule `(Index*37 + Index/7)` had een verborgen periode van 70 (Index en
    Index+70 = zelfde naam) -> effectieve naam-ruimte ~70 i.p.v. 7000 -> 250 unieke namen genereren liet de
    uniekheid-retry-loop naar 199 MILJOEN iteraties exploderen (~27-30s synchroon game-thread, ELKE beach-load).
    Fix: bijectieve mapping `lastIdx=(Index/70)%100` + retry-cap. GEMETEN dev-LoadMap 67,7s->27,5s, EnsureSeeded
    26,7s->0. De co-op-joiner wachtte op deze host-stall en versnelt mee. Rest-load (~14s packaged) = content-load
    + graphics-apply (Lumen/VSM ~5s) + DoorRetrofitter-scan; verder optimaliseren = optioneel.
  - **[RELEASED 1.21.0, build-20260704-0305]** Deze load-fix + Blok 1-4 (D1-D28) + D33/D34 + de kaart-kleur-fix
    hieronder zijn geshipt (cook-parity PASS + smoke-test PASS). GEMETEN packaged beach-load nu 12,5s.
- [x] **MAP winkel-markers gekleurd per soort (gevonden+gefixt 07-04, commit e3d61654)** - de M-kaart toonde ALLE
    winkels hardcoded geel (MapWidget.cpp:614) terwijl de kompas AStoreCounter::KindColor() gebruikt
    (groen/blauw/paars/goud). Bevestigd echte bug via de speler-clue "geel op map, gekleurd op compass" (asset-mis
    zou beide raken). Fix: map gebruikt nu dezelfde KindColor-bron, per pool-slot getint (her-kleur alleen bij
    type-wijziging, geen per-tick flash). Geshipt in 1.21.0.
- [x] **D30** Items flashen nog bij slepen hotbar->drying rack (en mogelijk inv->rack + andere machine-UI's) -> restpad vinden+fixen
- [x] **D31** Overbodige instructie-teksten overal weg (bv. "Drag from your inventory to store...", "Drag weed here to dry it...", "Nothing drying...") - alleen echt nuttige info laten staan
- [x] **D32** Merge/samenvoegen-popup valt soms achter andere UI's -> z-order/topmost fixen
- [x] **D33** [GEFIXT deze sessie] Seeds hadden 0.01kg -> rondde af naar 0.0 in UI; nu 0.05
- [x] **D34** LIFT-REGRESSIE (van Blok4-B D25): nummer BOVEN de lift-deur is VERDWENEN (de line-trace naar het
  muurvlak faalt/misplaatst het bord - DoorRetrofitter ~3043); + de vloer-knop-nummers moeten nog iets dichter
  op de muur (PackElevatorButton text-offset). FIX: trace debuggen of terug naar vaste-offset (bord moet ZICHTBAAR
  blijven) + button-offset natunen. Wacht op D29-B (bezit DoorRetrofitter).
- [ ] **D35** Cancel/"Back to menu"-knop op het laadscherm (join/normaal) zodat je terug kunt als 't hangt.
  Cover-fase (UBootCoverWidget, na LoadMap) = game-thread leeft -> knop werkt. Movie-fase (SWeedLoadingScreen
  tijdens LoadMap) = game-thread geblokkeerd; knop daar alleen mogelijk als de movie-Slate input verwerkt.
  -> ReturnToMainMenu() (zelfde als D28 leave-session).

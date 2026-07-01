# ROADMAP — ThePlugSIM

> **Dit is de levende roadmap.** Het oude A–Z stappenplan in de brief is afgerond en vervangen door dit document. Volgorde = prioriteit. Afgeronde items afvinken en (groot werk) loggen in `DECISIONS.md`.
>
> Laatst bijgewerkt: 2026-07-01 — speler-notities toegevoegd als "BACKLOG 07-01" (polish/fixes-lijst, geprioriteerd).
> **Detail-uitwerking per bevinding (probleem → file → fix, afvinkbaar): [`FIXLIST.md`](FIXLIST.md).** Dit document = de grote lijn; de fixlist = het systematische afwerk-document.

**Grote lijn:** eerst de **beach-map** echt het spel-wereld maken (daar speelt straks álles), dan **levels 1-50 écht goed** maken, dan pas de **50+ shop-fase**. Co-op-fixes en save-gaten lopen daar dwars doorheen omdat ze klein zijn maar sessies breken.

---

## FASE 1 — Beach-map wordt de echte wereld (NU BEZIG)

De CityBeachStrip-map heeft werkende deuren (DoorRetrofitter), room-replicatie + bakes en save/load van geplaatste objecten. Maar al het stads-leven hangt nog aan de procedurele `ACityGenerator` — op de beach-map bestaat dat dus niet. Dit is de port-backlog, in bouwvolgorde:

- [ ] **1.1 Homes-registry voor de beach-map** — een `FApartmentHome`-equivalent (deur-positie, interieur-spawn, bounds) voor de gebakken/gestempelde woningen. Bron: de geplaatste `ACityDoor`s + baked rooms, of een hand-authored lijst. Alles hieronder hangt hiervan af.
- [ ] **1.2 Koopbare woningen** — `GetPropertyOffers()`-equivalent op de beach-map (starter gratis + 2 koopbare), gekoppeld aan de homes-registry; `PhoneClientComponent::FindCity()` krijgt een beach-map-pad. Save (`OwnedHomes`) moet op deze map blijven kloppen.
- [ ] **1.3 NPC-bewoners zonder CityGenerator** — `CustomerSpawner` laten werken met de nieuwe registry: bewoners toewijzen aan woningen, in/uit lopen, dag/nacht-ritme, park-bezoek (park-zone als marker/actor op de map zetten).
- [ ] **1.4 Winkels + leveringen** — `StoreCounter`s in de echte winkelpanden plaatsen/registreren, delivery-posities (voordeur van JE woning) en ATM's op de beach-map.
- [ ] **1.5 Navmesh + routing valideren** — dekking over de hele strip checken (soak met `-AutoSoak`); stoep-routing voor bewoners (open punt uit DECISIONS 06-04 #3).
- [ ] **1.6 End-to-end op de beach-map** — verse start → kweken → dealen → afspraak → save/load → continue, allemaal op de beach-map. Daarna: beach-map als default new-game map.

> Hardcoded aanname om te bewaken: alles checkt `MapPath.StartsWith("/Game/CityBeachStrip")` — een tweede externe map breekt dit. Pas generaliseren als dat ooit echt speelt.

---

## FASE 2 — Levels 1-50 écht goed (vóór de 50+ shop-fase)

### 2A. Balans-pass (data, geen nieuwe systemen)

- [ ] **2A.1 Late Cali-seed-economie repareren** — Gary Payton: €5.000 zaad voor 8g basis-yield (~€320-480 opbrengst) = verlies. Hele staart (lvl 39-49) doorrekenen: seed-prijs vs. yield × marktprijs × care/fertilizer; richtlijn: een topseed verdient zich in 1-2 oogsten terug.
- [ ] **2A.2 XP-tempo doorrekenen** — curve is `100 + (lvl-1)×40` (≈106k XP tot 50); verkoop geeft maar `5 + €/100` XP (deal van €500 = 10 XP), oogst draagt veel zwaarder. Checken of lvl 30→50 niet vastloopt op "alleen oogsten levelt".
- [ ] **2A.3 Concentraten-rendement** — Oil/Moonrock/Rosin/Iso-ketens (machines lvl 30-48) moeten per uur méér opleveren dan baggies draaien, anders zijn ze dood gewicht.
- [ ] **2A.4 Level 32 vullen + late-game cadans** — 32 is echt leeg; 35-49 is strikt "oneven = dure seed, even = pro-machine". Kleine tussenbeloningen strooien (cosmetics, QoL-unlocks, pot-upgrade-tiers) zodat elk level íets geeft. Géén grote content — die ruimte is voor 50+.

### 2B. Mid-game variatie (lvl ~20-35)

Tussen lvl 21 (pro-edibles) en lvl 36 (Oil_Pro) komt er geen nieuw spel-werkwoord bij — 15 levels "meer van hetzelfde".

- [ ] **2B.1 Dag-orders / bestellingen** — kleine dagelijkse opdrachten via de telefoon ("VIP wil 20g Sour Diesel ≥25% THC vóór 22:00, bonusprijs"), schaalt met level. Breekt de loop op en gebruikt bestaande chat/contacts-systemen.
- [ ] **2B.2 Bulk-deals** — af en toe een grote afnemer (hele pot / 100g-blok in één keer, lagere prijs per gram, meer heat). Gebruikt bestaand cooldown/NPC-systeem.
- [ ] **2B.3 Goals dynamischer** — de 20 vaste lifetime-goals staan los van level; een dagelijkse/weekse rotatie-laag erbovenop geeft mid-game richting.
- [ ] **2B.4 Concentraten iets naar voren** — eerste hash/oil-stap richting lvl ~22-26 zodat de mid-game een eigen tech-chase heeft (pro-tiers blijven laat).

### 2C. Co-op-pariteit (fixes + de uitgestelde testronde)

- [ ] **2C.1 Meldingen naar álle spelers** — `HeatComponent.cpp:137` (en vergelijkbare plekken) stuurt bust/overval-toasts alleen naar `GetFirstPlayerController()` = alleen de host. Omzetten naar per-speler client-melding.
- [ ] **2C.2 Deuren/liften in co-op beslissen** — `ACityDoor`/`ACityElevator`/`APackElevator` staan bewust op `bReplicates = false` ("ieder z'n eigen deur"). Voor deuren waarschijnlijk prima; voor liften die spelers vervoeren in co-op echt testen — desyncs hier voelen kapot. Beslissing loggen in DECISIONS.
- [ ] **2C.3 Client-save-pad verifiëren** — pauze-menu-save via `RequestSaveGame→ServerRequestSave` bestaat; checken dat een client nette feedback krijgt en niets stil faalt.
- [ ] **2C.4 De grote 2-speler PIE-testronde** — wardrobe/skins, kluis, park-wachtrij, chat-balken, winkeliers, competitive-modus, bed-verhuizen, derde-persoon (B). Alles is replication-aware gebóuwd maar nooit met 2 spelers gedraaid. Bevindingen → fixlijst.
- [ ] **2C.5 Packaged build (.exe)** — nooit gemaakt, wél nodig: LAN co-op met een vriend vereist een packaged build (zie DECISIONS). Eén keer packagen + firewall/poort 7777 smoke-test. Dit is ook de enige echte "werkt het buiten de editor"-check.

### 2D. Save-gaten dichten (klein maar irritant)

- [ ] **2D.1 Waterfles-vulling** — `WaterCanComponent`-vulgraad gaat niet mee in de save; flessen zijn leeg na load → planten verdrogen onverdiend.
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

- [ ] **B.1 Texting-animatie stopt niet / speelt waar 't niet hoort** — de sms-animatie hoort ALLEEN bij een open telefoon (Tab): direct stoppen bij sluiten. Nu blijft 'ie doorgaan tijdens lopen → poppetje "slidet" rond. **BESLUIT: upper-body blend** (bovenlijf sms't, benen lopen — AAA-stijl). Én: **inventory openen triggert in third person óók de telefoon-pak-animatie** — inventory hoort géén telefoon-animatie te hebben (gewoon stilstaan).
- [ ] **B.2 Cash niet splitbaar/dropbaar** — contant geld in de inventory: kan niet splitten, niet uit de inv draggen om te droppen, en normale drop werkt ook niet.
- [ ] **B.3 NPC's blijven stilstaan in de hal** — na naar buiten lopen volgen ze de gezette lines niet meer; blijven staan tot despawn. Onderzoeken (nav/route-handoff).
- [ ] **B.4 "Kom langs"-NPC spawnt soms midden in de kamer** — hoort beneden in de hal te spawnen (afspraak-deals).
- [ ] **B.5 Wardrobe-belichting wisselt** — eerst goed, na een paar seconden te donker (dag/nacht loopt door). Fix: belichting bevriezen / geen nacht in wardrobe-view.
- [ ] **B.6 Bank "Send to a friend" alleen in co-op** — alleen tonen als er écht een mede-speler in de sessie zit, mét diens naam op de knop; verbergen in singleplayer.

### Gameplay-regels

- [ ] **B.7 Pest/mold = harde lockout** — bij besmetting: géén water meer kunnen geven, ALLE stats bevriezen (water/health/groei — alles) tot er gesprayed is. Niet op tijd gesprayed → kwaliteit daalt snel. (Open: welke tijdsduur? Voorstel: bestaande gratie-tijd hergebruiken.)
- [ ] **B.8 Meer joint-vindplekken** — extra potential spots bij sigaretten-prullenbakken en grote kliko's: `SM_AshtrayBin`, `SM_Dumpster`, `MetalTrashCan`, e.d. Niet overpowered — het blijft "een rondje lopen om joints te zoeken". Héél kleine kans op 2 joints bij één spot (zeldzaam houden).

### Content / 3D

- [ ] **B.9 Nieuwe joint-modellen** — in Downloads: `joint-scan-v2.zip` (normale/kleinere joint → tier 1-2) en `fat-joint.zip` (dikke joint → tier 3-4). Joints moeten altijd netjes **op hun zij** op de grond liggen (nu staan ze vaak rechtop op de tip). **CC-BY-attributie verplicht in credits:** "Joint scan (v2)" by PreyK (skfb.ly/6RWXB) + "Fat Joint" by streetpharmacy (skfb.ly/p8HON), beide CC Attribution 4.0.

### UI-reworks (overleg eerst)

- [ ] **B.10 Deal-scherm opruimen/reworken** — overloaded met tekst, onduidelijk waar je moet kijken. **BESLUIT: eerst opruimen** tot alleen het echt nodige (1 blik = wat wil hij / wat bied ik / kans), daarna pas beoordelen of een volle kit-rework (WBP-route) nog nodig is.
- [ ] **B.11 Keuze-/aantal-menu's reworken** — packing bench, joint rollen e.d.: bij veel wiet wordt de keuzelijst gigantisch en onoverzichtelijk. **BESLUIT: icoon-grid** — compact grid van item-slots (icoon + strain-tag + aantal, zelfde look als inventory), klik = selecteer → aantal-stepper. Overal toepassen waar zulke lijsten voorkomen.
- [ ] **B.12 Package-delivery herstel + upgrade** — pakketten weer netjes vóór de apartment-deur (zoals eerst), óók voor de competitive mirror-apartments. Dozen krijgen physics (zoals droppable items). Delivery-app: meer details + lijstje van wat je gekocht hebt.
- [ ] **B.13 Furniture-placement op muren** — niet half over deuren en niet op ramen kunnen plaatsen.

### UI/UX — tweede notitie-ronde (07-01)

- [ ] **B.16 Map-app van de telefoon verwijderen** — nutteloos (de M-fullscreen-kaart bestaat al); app uit het home-rooster + tab eruit.
- [ ] **B.17 Settings-app (telefoon) → speler-stats** — **BESLUIT: ombouwen tot speler-stats-pagina** (totaal verdiend, deals gedaan, planten geoogst, beste deal, speeltijd, e.d.); character-switch mag blijven.
- [ ] **B.18 Drop tussen icon-gaps snapt naar dichtstbijzijnde slot** — item loslaten tussen/heel dicht bij een slot mag niet "missen": snap naar het dichtstbijzijnde slot (alleen bij kleine afstand, niet van ver).
- [ ] **B.19 Minimalist-rebrand: volledige sweep over ALLE schermen** — **"bijna alles nog"**: de eerdere rebrand-ronde is halverwege onderbroken en nooit afgemaakt. Systematisch elk scherm langslopen (inventory-stijl = de referentie: palette + slot-look + spacing) en afvinken welke al goed is en welke nog moet.
- [ ] **B.20 Quick/home-stash-view in inventory opschonen** — veel dubbele info én nuttige info mist: bv. hoe vol de water-bottle is, hoeveel soil er nog in zit — dat hoort juist in de quick view.
- [ ] **B.21 Hand-preview: tag-kleur + dikkere letters** — de tag op de hand-preview meekleuren met de strain-tagkleur, en de tag-letters bold/duidelijker (nu te dun voor snelle indicatie).

### Systemen / infra

- [ ] **B.14 2 gamemodes eruit → alles naar F10 dev-menu** — **BESLUIT: Sandbox + Testing eruit** (Normaal + Competitive blijven). Alle functies van die modes als dev-menu-functies: level kiezen, reputatie/respect/loyaliteit aan NPC's geven, etc. Sluit aan op het bestaande Unified-Dev-Menu-plan (F10 sidebar).
- [ ] **B.15 Eerste loading screen versnellen** — duurt erg lang; de 2e is juist supersnel. Onderzoeken wat de 1e zo traag maakt (shaders/PSO-warmup? map-load? asset-registry?) en verbeteren.

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

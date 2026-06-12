# FIXLIST — audit 2026-06-12 (systematisch afwerken)

> **Werkdocument bij `ROADMAP.md`.** Alle bevindingen uit de volledige code-audit van 2026-06-12, als afvinkbare lijst. Per item: wat er mis is, waar het zit, en wat de fix is. Afgewerkt = `[x]` + waar relevant een regel in `DECISIONS.md`.
>
> Prioriteit: **P1** = breekt gameplay/sessie of blokkeert de roadmap · **P2** = merkbaar slechter spel · **P3** = hygiëne/polish.
> Items gemarkeerd **(verifiëren)** komen uit de audit maar zijn nog niet 100% in code nagelopen — eerst checken, dan fixen.

---

## 1. BALANS & PROGRESSIE (levels 1-50)

### 1.1 — [ ] P1 · Late Cali-seed-economie is verliesgevend
- **Probleem:** topseeds verdienen zichzelf niet terug. Gary Payton (lvl 49): €5.000 zaad → 8g basis-yield → ~€320-480 opbrengst. Hele staart schaalt scheef: seed-prijs stijgt veel harder dan yield × marktprijs.
- **Waar:** `Data/DT_Strains.csv` (SeedPriceCents vs BaseYieldGrams, lvl 39-49) + marktprijzen in `Data/DT_Products.csv`.
- **Fix:** tabel doorrekenen met regel "topseed verdient zich in 1-2 oogsten terug (incl. care/fertilizer-multipliers)". Seed-prijzen omlaag óf yield/marktprijs omhoog voor Gelato t/m Gary Payton.

### 1.2 — [ ] P2 · XP uit verkoop is bijna niks
- **Probleem:** deal-XP = `5 + €/100` (deal van €500 = 10 XP); oogst geeft `planten×10 + grammen`. Levelen = vooral oogsten; dealen voelt onbeloond, en 30→50 (≈86k XP) dreigt een oogst-grind te worden.
- **Waar:** `Customer/CustomerBase.cpp` (deal-XP, ~regel 3288), `Progression/LevelComponent.cpp` (curve `100+(lvl-1)×40`, regel ~22-28), `World/GrowPlant.cpp` (~regel 812).
- **Fix:** deal-XP zwaarder wegen (bv. schalen met grammen + THC-tier i.p.v. alleen euro's), of de curve 30-50 afvlakken. Doel: dealen ≈ gelijkwaardige XP-bron als oogsten.

### 1.3 — [ ] P2 · Level 32 is leeg + late-game cadans is dun
- **Probleem:** lvl 32 heeft géén enkele unlock. Lvl 35-49 is strikt "oneven = dure seed, even = pro-machine" — elk level geeft technisch iets, maar het voelt als hetzelfde ritme zonder verrassing.
- **Waar:** `Progression/StoreComponent.cpp::RequiredLevelFor` (regel ~323-448) + `DT_Strains.csv`.
- **Fix:** lvl 32 vullen; door 35-49 kleine tussenbeloningen strooien (cosmetics/wardrobe-items, QoL-unlocks, extra pot-upgrade-tier). **Géén grote content** — die ruimte is voor de 50+ fase.

### 1.4 — [ ] P2 · Mid-game variatie-gat (lvl ~21-36)
- **Probleem:** tussen lvl 21 (pro-edibles) en lvl 36 (Oil_Pro) komt er geen nieuw spel-werkwoord bij; 15 levels "meer van hetzelfde groeien/pakken/verkopen".
- **Fix (drie sporen, bestaande systemen hergebruiken):**
  - [ ] **Dag-orders** via telefoon-chat: "VIP wil 20g Sour Diesel ≥25% THC vóór 22:00, bonusprijs" — schaalt met level (ContactsComponent + GoalsComponent).
  - [ ] **Bulk-deals**: af en toe een grote afnemer (hele pot / 100g-blok, lagere prijs/gram, meer heat) — gebruikt bestaand NPC/cooldown-systeem.
  - [ ] **Eerste concentraten-stap naar voren** (~lvl 22-26) zodat de mid-game een eigen tech-chase heeft; pro-tiers blijven laat.

### 1.5 — [ ] P3 · Concentraten-rendement doorrekenen
- **Probleem:** Oil/Moonrock/Rosin/Iso-machines (lvl 30-48) zijn duur en laat; onduidelijk of de keten per uur méér oplevert dan gewoon baggies draaien. Zo niet → dood gewicht.
- **Waar:** machine-prijzen in `StoreComponent.cpp`, output-prijzen in `DT_Products.csv`, verwerkingstijden in `World/ProcessorMachine.*`.
- **Fix:** €/uur per keten naast elkaar zetten; concentraten horen duidelijk boven baggies uit te komen (hoger risico/investering = hogere marge).

### 1.6 — [ ] P3 · Goals zijn statisch
- **Probleem:** 20 vaste lifetime-goals (`Progression/GoalsComponent.cpp` regel ~23-59), zelfde voor lvl 5 en lvl 50; geen dag/week-laag, beloningsschaal inconsistent (targets ×250, rewards ×80).
- **Fix:** kan meeliften op 1.4-dag-orders (die ZIJN de dynamische laag); daarna lifetime-rewards één keer langs een rechte lijn leggen.

---

## 2. CO-OP / REPLICATIE (vóór de 2-speler-testronde)

### 2.1 — [ ] P1 · Heat/overval-meldingen alleen naar de host
- **Probleem:** `World/HeatComponent.cpp:137` stuurt bust/overval-toasts naar `GetFirstPlayerController()` — speler 2 wordt beroofd zonder het te zien.
- **Fix:** per-speler client-melding (Client-RPC of loop over alle PlayerControllers met `UWeedToast::Notify` lokaal). Daarna grep op `GetFirstPlayerController` voor andere gameplay-toasts (DoorRetrofitter/RoomStamper zijn dev-tools, die mogen blijven).

### 2.2 — [ ] P1 · Liften zijn niet gerepliceerd — in co-op testen, dan beslissen
- **Feit (geverifieerd):** `ACityElevator`, `APackElevator` + buttons staan bewust op `bReplicates = false` (`CityElevator.cpp:16`, `PackElevator.cpp:23` — "lokaal/cosmetisch"). Speler 1 op verdieping 3, speler 2 ziet de cabine op 1; een meerijdende speler ziet de ander mogelijk zweven.
- **Fix:** 2-speler PIE-test met beide liften. Breekt het → `bReplicates=true` + repliceer `CurFloor`/`CabZ`/`bDoorOpen`. Werkt het acceptabel → bewuste keuze loggen in DECISIONS.

### 2.3 — [ ] P2 · Deuren zijn niet gerepliceerd — bevestigen dat dat oké is
- **Feit (geverifieerd):** `ACityDoor` `bReplicates = false` (`CityDoor.cpp:16` — "ieder z'n eigen deur"). Speler 1 opent met F, speler 2 ziet 'm dicht.
- **Fix:** in de co-op-test beoordelen. Waarschijnlijk prima (NPC-auto-open werkt via trigger-zone die beide clients zien); zo ja → loggen als bewuste keuze, zo nee → zelfde recept als 2.2.

### 2.4 — [ ] P2 · Client-save-pad verifiëren **(verifiëren)**
- **Feit:** client kán host-save triggeren via pauze-menu (`RequestSaveGame→ServerRequestSave`, DECISIONS 06-01). Audit-claim: directe `SaveGame()`-calls op een client falen stil.
- **Fix:** checken dat álle save-knoppen op een client via de RPC lopen en nette feedback geven ("Saved" / "Host saves"); stil falen wegwerken.

### 2.5 — [ ] P1 · De grote 2-speler PIE-testronde (nooit gedaan)
- **Wat testen:** wardrobe/skins (incl. B = third-person), kluis (cash ↔ safe), park-wachtrij, chat-balken/tijdstempels, winkeliers, competitive-modus (poachen + leaderboard), bed-verhuizen, cash droppen/splitten, liften/deuren (2.2/2.3), save/load/continue als client.
- **Output:** bevindingen → nieuwe items in deze lijst.

### 2.6 — [ ] P1 · Packaged build (.exe) maken + LAN-smoke-test
- **Probleem:** nooit gepackaged; LAN co-op met z'n vriend vereist een packaged build (DECISIONS). Ook de enige echte "werkt het buiten de editor"-check.
- **Fix:** één keer packagen (Development), draaien buiten de editor, host+join over LAN (UDP 7777, firewall-regel). Pad/recept vastleggen in DECISIONS.

---

## 3. SAVE-DEKKING (gaten dichten)

### 3.1 — [ ] P2 · Waterfles-vulling gaat niet mee in de save **(verifiëren)**
- **Claim:** `Cultivation/WaterCanComponent`-vulgraad zit niet in `FPlayerSaveData.Items` → flessen leeg na load → planten verdrogen onverdiend.
- **Fix:** eerst in code bevestigen; zo ja, vulgraad als veld aan de item-stack of als aparte save-entry toevoegen.

### 3.2 — [ ] P3 · `FindHomeForPoint` pakt verkeerde verdieping (open sinds 06-06)
- **Probleem:** bij gestapelde appartementen kiest de furniture-capture soms de unit een etage hoger/lager (XY-only afstand).
- **Fix:** 3D-afstand i.p.v. XY — stond al in DECISIONS als bekende bug, nooit doorgevoerd.

### 3.3 — [ ] P3 · Cosmetische na-load-staat
- **Probleem:** machine-animatiestaat, per-lamp aan/uit en appointment-queue-volgorde resetten na load (batches/messages zelf zijn wél gedekt).
- **Fix:** laag; alleen oppakken als het na 3.1/3.2 nog stoort.

---

## 4. BEACH-MAP PORT (= ROADMAP Fase 1, hier de technische detail-lijst)

> Werkt al: DoorRetrofitter-deuren, room-stamping + VerticalReplicate, bakes (`Tools/bake_rooms.py` → BakedRooms-overlay), top-down map-capture, FreeBuild/dev-tools, `-AutoSoak`, save/load van geplaatste objecten, MapPath-gestuurde level-reload.

### 4.1 — [ ] P1 · Homes-registry voor de beach-map
- **Probleem:** `FApartmentHome` (DoorPos/InteriorPos/bounds) leeft in `ACityGenerator` — bestaat niet op de beach-map. Álles hieronder hangt hiervan af.
- **Fix:** registry opbouwen uit de geplaatste `ACityDoor`s + baked rooms (of hand-authored lijst), gehost door `ADoorRetrofitter` of een nieuwe lichte registry-actor.

### 4.2 — [ ] P1 · Koopbare woningen op de beach-map
- **Probleem:** `GetPropertyOffers()` + `PhoneClientComponent::FindCity()` vinden geen city → woning kopen/`OwnedHomes`/rent werken niet.
- **Fix:** beach-equivalent van GetPropertyOffers (starter gratis + 2 koopbaar) op de registry uit 4.1; FindCity een tweede pad geven. Save-compat checken (OwnedHomes-indices).

### 4.3 — [ ] P1 · NPC-bewoners zonder CityGenerator
- **Probleem:** `CustomerSpawner` + bewoner-routing (in/uit lopen, dag/nacht, park-bezoek) zoeken `ACityGenerator`/`GetMapBlocks()`.
- **Fix:** spawner op de 4.1-registry laten draaien; park-zone als marker/actor op de map; straat-routing via navmesh i.p.v. grid.

### 4.4 — [ ] P2 · Winkels + leveringen + ATM's
- **Probleem:** `StoreCounter`-locaties en delivery-posities (voordeur eigen woning) hangen aan CityGenerator.
- **Fix:** StoreCounters in de echte winkelpanden registreren; delivery-drop = DoorPos uit 4.1; ATM per winkel.

### 4.5 — [ ] P2 · Navmesh-dekking + stoep-routing valideren
- **Fix:** `-AutoSoak`-run over de hele strip; navmesh-gaten dichten; stoep-only-routing (open punt DECISIONS 06-04 #3) hier meenemen.

### 4.6 — [ ] P1 · End-to-end op de beach-map → default map
- **DoD:** verse start → kweken → dealen → afspraak → save/load → continue, alles op de beach-map. Daarna beach-map als default new-game map zetten.
- **Let op:** alles checkt `MapPath.StartsWith("/Game/CityBeachStrip")` (`ThePlugSIMCharacter.cpp` ~1320, `MainMenuWidget.cpp` ~520) — bij een tweede externe map ooit generaliseren, nu laten staan.

---

## 5. OPRUIMEN & TECH-HYGIËNE

### 5.1 — [ ] P3 · Template-dead-weight verwijderen
- `Source/ThePlugSIM/Variant_Horror/` + `Variant_Shooter/` zijn nergens gerefereerd (gecheckt: WeedShopCore, Config, .uproject). Verwijderen + include-paden in `ThePlugSIM.Build.cs` (regel ~27-35) opschonen. Snellere builds, minder ruis.

### 5.2 — [ ] P3 · `AMoneyTestPickup` opruimen
- Test-actor (`Test/MoneyTestPickup.cpp`) uit de speelbare wereld: verwijderen of `#if WITH_EDITOR`.

### 5.3 — [ ] P3 · Klant-placeholder-body
- `Customer/CustomerBase.h:249` — NPC's gebruiken nog een placeholder-lichaam tot er een echte character-mesh is. Art-item, niet urgent; staat hier zodat 'ie niet kwijtraakt.

### 5.4 — [ ] P3 · Soak/performance-pass
- 40+ bewoners + bakes + liften: 15+ min `-AutoSoak`-run, hitches/log-spam checken. Zeker vóór de co-op-testronde (2.5) één keer draaien.

### 5.5 — [ ] P3 · Bekende kleine known-issues (PATCHNOTES 1.4.0)
- [ ] Lola cloth/haar-physics
- [ ] Controller-support afmaken
- [ ] Ultrawide: kleine iconen

### 5.6 — [ ] P3 · Legacy-paden markeren
- Oude `ServerPack` (vaste batch) naast de slider-versie, legacy hasj-keten-categorieën (`StoreComponent.cpp:529`), v1-save-migratie: werkt allemaal, maar markeer als legacy zodat er niet op doorgebouwd wordt. Opruimen pas als zeker is dat geen oude saves meer leven.

---

## Afwerk-volgorde (voorstel)

1. **Quick wins (één sessie):** 2.1 (toasts) → 3.2 (FindHomeForPoint) → 5.1 + 5.2 (opruimen) → 1.3 (level 32 vullen).
2. **Beach-map-spoor:** 4.1 → 4.2 → 4.3 → 4.4 → 4.5 → 4.6 (volgorde is hard, alles hangt aan 4.1).
3. **Balans-spoor (parallel, alleen data/CSV):** 1.1 → 1.2 → 1.5 → 1.4 → 1.6.
4. **Co-op-afsluiter:** 2.4 → 2.6 (packaged build) → 2.5 (grote testronde, incl. 2.2/2.3-beslissing) → bevindingen terug in deze lijst.

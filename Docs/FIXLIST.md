# FIXLIST — audit 2026-06-12 (systematisch afwerken)

> **Werkdocument bij `ROADMAP.md`.** Alle bevindingen uit de volledige code-audit van 2026-06-12, als afvinkbare lijst. Per item: wat er mis is, waar het zit, en wat de fix is. Afgewerkt = `[x]` + waar relevant een regel in `DECISIONS.md`.
>
> Prioriteit: **P1** = breekt gameplay/sessie of blokkeert de roadmap · **P2** = merkbaar slechter spel · **P3** = hygiëne/polish.
> Items gemarkeerd **(verifiëren)** komen uit de audit maar zijn nog niet 100% in code nagelopen — eerst checken, dan fixen.

---

## 1. BALANS & PROGRESSIE (levels 1-50)

### 1.1 — [x] P1 · Late Cali-seed-economie is verliesgevend
- **Probleem:** topseeds verdienen zichzelf niet terug. Gary Payton (lvl 49): €5.000 zaad → 8g basis-yield → ~€320-480 opbrengst. Hele staart schaalt scheef: seed-prijs stijgt veel harder dan yield × marktprijs.
- **Waar:** `Data/DT_Strains.csv` (SeedPriceCents vs BaseYieldGrams, lvl 39-49) + marktprijzen in `Data/DT_Products.csv`.
- **Fix:** tabel doorrekenen met regel "topseed verdient zich in 1-2 oogsten terug (incl. care/fertilizer-multipliers)". Seed-prijzen omlaag óf yield/marktprijs omhoog voor Gelato t/m Gary Payton.
- **✓ Gefixt 2026-06-14:** doorgerekend (seed = per-oogst-kost, yield = base × care × soil≤1.5 × pot≤1.25 × fert). **Extra bug gevonden:** 10 strains (BlueDream, JackHerer, PineappleExpress, BubbaKush, DurbanPoison, PurpleHaze, CookiesCream, Mimosa, AppleFritter, GaryPayton) hadden GÉÉN rij in `DT_Products` → marktprijs €0 → onverkoopbaar. Toegevoegd. Late **seeds verlaagd** (GorillaGlue 28k→ ... GaryPayton 500k→82k) tot ~0.85× base-revenue zodat ze zich in ~1 oogst terugverdienen, en **Cali-tier gram-prijzen verhoogd** (Gelato €34→€50 … GaryPayton €120) zodat premium-strains het meest winstgevend per oogst zijn. Resultaat: elke strain winstgevend zelfs zonder upgrades; geen verlies meer. DataTables (`DT_Strains`/`DT_Products` in Content) gereïmporteerd uit de CSV's (25 strains / 27 producten geverifieerd).

### 1.2 — [x] P2 · XP uit verkoop is bijna niks
- **Probleem:** deal-XP = `5 + €/100` (deal van €500 = 10 XP); oogst geeft `planten×10 + grammen`. Levelen = vooral oogsten; dealen voelt onbeloond, en 30→50 (≈86k XP) dreigt een oogst-grind te worden.
- **Waar:** `Customer/CustomerBase.cpp` (deal-XP, ~regel 3288), `Progression/LevelComponent.cpp` (curve `100+(lvl-1)×40`, regel ~22-28), `World/GrowPlant.cpp` (~regel 812).
- **Fix:** deal-XP zwaarder wegen (bv. schalen met grammen + THC-tier i.p.v. alleen euro's), of de curve 30-50 afvlakken. Doel: dealen ≈ gelijkwaardige XP-bron als oogsten.
- **✓ Gefixt 2026-06-14:** de audit-aanname klopte niet — deal-XP was `5 + Total/100` met Total in **cents** = **5 + verdiende euro's**, dus juist VEEL XP, die met de late strains de XP liet ballonnen → je vloog door lvl 30-50. Omgezet naar **moeite-gebaseerd**: `5 + grammen × (1 + THC/40)` (premium = meer XP/gram, ontkoppeld van geld). Doorgerekend: 0-50 = ~165 oogsten, gelijkmatig 1-6/level (curve `100+(lvl-1)×40` ongewijzigd, past goed).

### 1.3 — [x] P2 · Level 32 is leeg + late-game cadans is dun
- **Probleem:** lvl 32 heeft géén enkele unlock. Lvl 35-49 is strikt "oneven = dure seed, even = pro-machine" — elk level geeft technisch iets, maar het voelt als hetzelfde ritme zonder verrassing.
- **Waar:** `Progression/StoreComponent.cpp::RequiredLevelFor` (regel ~323-448) + `DT_Strains.csv`.
- **Fix:** lvl 32 vullen; door 35-49 kleine tussenbeloningen strooien (cosmetics/wardrobe-items, QoL-unlocks, extra pot-upgrade-tier). **Géén grote content** — die ruimte is voor de 50+ fase.
- **✓ Gefixt 2026-06-14:** lvl 32 was bij nader inzien al gevuld (`Cont_Garbage500`). De echte kwaal waren de **8 lege oneven levels 35-49**. Data-only herspreid in `RequiredLevelFor`: late strains van 36/40/44/48 → **37/41/45/49** (strain-oneven, pro-machine-even wisselen elkaar nu af) + `Bench_Pack3` 30→**35**. Gevuld: 32,35,37,41,45,49. **Resteert** (3 gaten, 39/43/47): vragen kleine cosmetic/QoL-unlocks — bewust doorgeschoven naar de content-pass (die ruimte is grotendeels 50+).

### 1.4 — [~] P2 · Mid-game variatie-gat (lvl ~21-36)
- **Probleem:** tussen lvl 21 (pro-edibles) en lvl 36 (Oil_Pro) komt er geen nieuw spel-werkwoord bij; 15 levels "meer van hetzelfde groeien/pakken/verkopen".
- **✓ Dag-orders gefixt 2026-06-14:** premium-variant op het bestaande afspraak-systeem. Vanaf ~lvl 12 wordt een telefoon-afspraak soms een **VIP-order**: specifieke wiet-strain + **min-THC-eis** + ruimere deadline (3-7 min) + **bonus-uitbetaling** (+50% bij lvl 12 → +100% bij lvl 50). Kans (0→45%) + ordergrootte (6-14g) schalen met level. Vervuld = juiste strain (geen substituut), THC ≥ eis, volle hoeveelheid op tijd → bonus-cash + extra XP + goud-toast. Beloont een diverse, goed gevulde voorraad. `FPhoneMessage` (bOrder/MinThc/BonusMult), `ACustomerBase::SetApptOrder`, generatie in `SendRandomAppointment`, uitbetaling in de deal. Save serialiseert de struct automatisch.
- **Fix (overige sporen):**
  - [x] **Dag-orders** via telefoon-chat: "VIP wil 20g Sour Diesel ≥X% THC vóór HH:MM, bonusprijs" — schaalt met level (ContactsComponent + deal-uitbetaling).
  - [ ] **Bulk-deals**: af en toe een grote afnemer (hele pot / 100g-blok, lagere prijs/gram, meer heat) — gebruikt bestaand NPC/cooldown-systeem.
  - [ ] **Eerste concentraten-stap naar voren** (~lvl 22-26) zodat de mid-game een eigen tech-chase heeft; pro-tiers blijven laat.

### 1.5 — [x] P3 · Concentraten-rendement doorrekenen
- **Probleem:** Oil/Moonrock/Rosin/Iso-machines (lvl 30-48) zijn duur en laat; onduidelijk of de keten per uur méér oplevert dan gewoon baggies draaien. Zo niet → dood gewicht.
- **Waar:** machine-prijzen in `StoreComponent.cpp`, output-prijzen in `DT_Products.csv`, verwerkingstijden in `World/ProcessorMachine.*`.
- **Fix:** €/uur per keten naast elkaar zetten; concentraten horen duidelijk boven baggies uit te komen (hoger risico/investering = hogere marge).
- **✓ Gefixt 2026-06-14:** concentraat-prijs = bud-prijs × premium-factor (`GetMarketPriceForProduct`); waarde-ratio = totaal-conv × mult. **Dead weight gevonden:** Rosin (0.76-1.0×), Bubble (0.53-0.76×), Hash-keten (0.29-0.76×, 2-staps), Crystal (0.33-0.62×) waren MINDER waard dan ruwe bud; **Oil was onverkoopbaar** (stond in de niet-verkoopbaar-lijst, geen ingrediënt) = pure dead weight. **Fix:** mults afgestemd op de lage conv → Hash ×10, Rosin ×9, Bubble ×11, Oil ×9 (+ verkoopbaar), Crystal ×4 (tussenstap). Std/Pro nu: Hash 1.4-2.4×, Rosin 1.6-2.2×, Bubble 1.5-2.2×, Oil 1.4-2.0×, Moonrock 2.4-2.7×, Edibles 3.2-3.6× (+×1.3 met ProcUp_Yield). `GetMarketPriceCents` geünificeerd via `GetMarketPriceForProduct`.

### 1.6 — [ ] P3 · Goals zijn statisch
- **Probleem:** 20 vaste lifetime-goals (`Progression/GoalsComponent.cpp` regel ~23-59), zelfde voor lvl 5 en lvl 50; geen dag/week-laag, beloningsschaal inconsistent (targets ×250, rewards ×80).
- **Fix:** kan meeliften op 1.4-dag-orders (die ZIJN de dynamische laag); daarna lifetime-rewards één keer langs een rechte lijn leggen.

---

## 2. CO-OP / REPLICATIE (vóór de 2-speler-testronde)

### 2.1 — [x] P1 · Heat/overval-meldingen alleen naar de host
- **Probleem:** `World/HeatComponent.cpp:137` stuurt bust/overval-toasts naar `GetFirstPlayerController()` — speler 2 wordt beroofd zonder het te zien.
- **Fix:** per-speler client-melding (Client-RPC of loop over alle PlayerControllers met `UWeedToast::Notify` lokaal). Daarna grep op `GetFirstPlayerController` voor andere gameplay-toasts (DoorRetrofitter/RoomStamper zijn dev-tools, die mogen blijven).
- **✓ Gefixt 2026-06-14:** `NotifyAllPlayers()`-helper in `HeatComponent.cpp` loopt over alle pawns en routeert via `UWeedToast::NotifyPawn` per client; bust + overval gebruiken 'm nu. (Open vervolg: de overval ruimt nog alleen de *host*-woning leeg — `GetFirstPlayerController` op regel ~159 — dat is robbery-target-logica, geen melding; aparte co-op-item.)

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

### 3.2 — [x] P3 · `FindHomeForPoint` pakt verkeerde verdieping (open sinds 06-06)
- **Probleem:** bij gestapelde appartementen kiest de furniture-capture soms de unit een etage hoger/lager (XY-only afstand).
- **Fix:** 3D-afstand i.p.v. XY — stond al in DECISIONS als bekende bug, nooit doorgevoerd.
- **✓ Gefixt 2026-06-14:** primaire zoek deed al 3D (`D.SizeSquared()`); de **fallback** in `FurnitureTemplateLib.cpp` stond nog op `DistSquared2D` → nu `DistSquared` (3D), zodat ook de fallback de Z-afstand meeweegt en niet de verkeerde verdieping pakt.

### 3.3 — [ ] P3 · Cosmetische na-load-staat
- **Probleem:** machine-animatiestaat, per-lamp aan/uit en appointment-queue-volgorde resetten na load (batches/messages zelf zijn wél gedekt).
- **Fix:** laag; alleen oppakken als het na 3.1/3.2 nog stoort.

---

## 4. BEACH-MAP PORT (= ROADMAP Fase 1, hier de technische detail-lijst)

> Werkt al: DoorRetrofitter-deuren, room-stamping + VerticalReplicate, bakes (`Tools/bake_rooms.py` → BakedRooms-overlay), top-down map-capture, FreeBuild/dev-tools, `-AutoSoak`, save/load van geplaatste objecten, MapPath-gestuurde level-reload.

### 4.1 — [x] P1 · Homes-registry voor de beach-map
- **Probleem:** `FApartmentHome` (DoorPos/InteriorPos/bounds) leeft in `ACityGenerator` — bestaat niet op de beach-map. Álles hieronder hangt hiervan af.
- **Fix:** registry opbouwen uit de geplaatste `ACityDoor`s + baked rooms (of hand-authored lijst), gehost door `ADoorRetrofitter` of een nieuwe lichte registry-actor.
- **▸ Stap 1 gedaan 2026-06-14:** registry op `ADoorRetrofitter` (`BeachHomes` + `BeachHomePrices`), `GetBeachHomes()`/`GetBeachPropertyOffers()`, en een **marker-tool** (`WeedRegisterHome`, **F6**, dev-only): je loopt een kamer in, drukt F6 → wanden gemeten + regel naar `Saved/BeachHomes.txt` + toast. Index 0 = starter (gratis), 1.. = koopbaar (prijs ~oppervlakte). Auteur-workflow gekozen: in-game marker.
- **▸ Stap 2 gedaan 2026-06-14:** `PhoneClientComponent` heeft nu `FindRetro()` + `GetHomesUnified()`/`GetOffersUnified()` (City OF beach-registry). Herbedraad: `GetPropertyOffers`, `PropertyTick` (starter-toewijzing), `MoveOwnerToHome`, `ServerBuyProperty`, `ServerSellProperty`, `GetHomeSellValueCents`, `SetActiveHomeFromLocation`, `GetActiveHomeLocation`, `GetHomePlayerIsInside`, `GetHomeLabel`, `GetHomeInfoLine`, `FindDeliveryPoint`. DoorRetrofitter roept bij het thuiszetten `PropertyTick()` aan zodat de starter wordt toegewezen. **Koop/bezit/intrekken/verkopen/bezorgen werken nu op de beach-map.** Build groen.
- **▸ Rest (= 4.3-territorium, bewust uitgesteld):** `ApplyLocalDoors` (deur-visuals LOCKED/te-koop) doet op de beach-map nog niets — beach-`FApartmentHome` heeft geen `ACityDoor`-ref. En bewoners worden nog ad-hoc per deur toegewezen i.p.v. uit de registry. Voor de koop-woning-feature niet blokkerend; oppakken bij 4.3.

### 4.2 — [x] P1 · Koopbare woningen op de beach-map
- **Probleem:** `GetPropertyOffers()` + `PhoneClientComponent::FindCity()` vinden geen city → woning kopen/`OwnedHomes`/rent werken niet.
- **Fix:** beach-equivalent van GetPropertyOffers (starter gratis + 2 koopbaar) op de registry uit 4.1; FindCity een tweede pad geven. Save-compat checken (OwnedHomes-indices).
- **✓ Gedaan 2026-06-14 (met 4.1):** beach-offers komen uit `ADoorRetrofitter::GetBeachPropertyOffers()` (index 0 = starter gratis, 1.. = koopbaar, prijs ~oppervlakte). `PhoneClientComponent` valt via `GetOffersUnified()`/`GetHomesUnified()` terug op de registry; koop/bezit/intrekken/verkopen werken. `OwnedHomes`-indices = registry-indices (consistent in de save). Woningen voeg je toe met de **F6**-marker (zie 4.1). Open: prijzen/namen tunen + (4.3) deur-visuals.

### 4.3 — [~] P1 · NPC-bewoners zonder CityGenerator
- **Probleem (oorspr.):** `CustomerSpawner::SpawnResidents` zoekt `ACityGenerator`/`GetApartmentHomes()`.
- **Bleek al opgelost via een ANDER pad (geverifieerd 2026-06-14):** op de beach-map draait het bewoners-systeem NIET via `CustomerSpawner::SpawnResidents` (dat returnt zonder CityGenerator) maar via de **deur-scan in `DoorRetrofitter`** (regel ~888+, "BEWONERS-LITE"):
  - Elke `ACityDoor` krijgt een stabiele `Resident_<idx>`-naam op POSITIE → zelfde persoon per deur, elke sessie; persoonlijkheid deterministisch via `PredictPersonality`.
  - Tot 12 bewoners (`LiteCap`) spawnen als rondlopende wandelaars; de **TOREN-woningen bij het starter-appartement** (binnen 4000u, óók hogere verdiepingen) tellen al mee en krijgen voorrang.
  - **Per-NPC geheugen blijft over save/load** (`SaveGameSubsystem` slaat `Reg->GetStatesForSave()` op + herstelt).
- **✓ Trap-fix 2026-06-14 (de echte klacht: bewoners liepen vast op de trap):** `StairsPath.txt` had **twee losse kettingen** (top→bordes Z928-578, en bordes→straat Z578-98). Een bovenverdieping-bewoner kreeg maar één keten en strandde op het bordes. Nu in `DoorRetrofitter` (ScanPass 1): kettingen worden **genormaliseerd (afdalend) + aaneengeknoopt** tot één doorlopende afdaling, de `bInside`-check kijkt naar **elk** ketting-punt (niet alleen Ch[0]), en een binnen-bewoner krijgt het **afdalende suffix vanaf z'n eigen verdieping tot de straat**. Loopt punt-voor-punt (geen pathfinding) met de bestaande hop-vangrail bij vastlopen (die vuurt alleen buiten zicht). Build groen. Vereist dat de speler de trap-route met markers heeft gezet (staat er al voor het starter-gebouw).
- **Resteert (later, hangt op meer map-content):** park-zone als marker/actor; algemene straat-navmesh-dekking voor de rest van de strip; dag/nacht-naar-huis-routing. Voor nu lopen ze vooral rond — bewust.

### 4.4 — [x] P2 · Winkels + leveringen + ATM's
- **Probleem:** `StoreCounter`-locaties en delivery-posities (voordeur eigen woning) hingen aan CityGenerator.
- **Fix:** StoreCounters in de echte winkelpanden registreren; delivery-drop = DoorPos uit 4.1; ATM per winkel.
- **✓ Bleek al volledig gebouwd (geverifieerd 2026-06-14):**
  - **Winkels + ATM's + verkoper:** `DoorRetrofitter` (regel ~1787, `bShopsPlaced`) leest `Saved/ShopSpots.txt` en spawnt per plek een `AStoreCounter` (kind+kleur) **+ `AAtm` ernaast + een shopkeeper-NPC** (`bShopkeeper`). Vloer-getraced, 180° gedraaid zodat de klant-kant naar jou kijkt. **Er staan al 4 winkels** in `ShopSpots.txt` (Supplies, Grow, Furniture, Supplies).
  - **Authoring:** F9-marker(s) zetten → telefoonmenu **"Save shops (at counter)"** met gekozen soort (`SelectedShopKind` 0-2 = Grow/Supplies/Furniture) → `SaveShopSpots()` schrijft `ShopSpots.txt`; "Clear" wist. `SetShopTypeInCrosshair()` wijzigt de soort van een bestaande balie via het vizier.
  - **Leveringen:** `PhoneClientComponent::FindDeliveryPoint()` gebruikt al `GetHomesUnified()` → `DoorPos` uit de 4.1-registry, dus de bezorg-drone vliegt al naar je voordeur op de beach-map.
- **Let op:** een parallel F7/`BeachShops.txt`-systeem dat hier eerst voor begonnen werd is teruggedraaid — het bestaande ShopSpots-systeem is compleet (en heeft óók een verkoper). Gebruik dát.
- **Open (klein, optioneel):** GasStation-soort wordt door de spawn-code niet afgehandeld (alleen 0/1/2); pas oppakken als een 4e winkelsoort echt nodig is.

### 4.5 — [~] P2 · Navmesh-dekking + stoep-routing valideren
- **Fix:** `-AutoSoak`-run over de hele strip; navmesh-gaten dichten; stoep-only-routing (open punt DECISIONS 06-04 #3) hier meenemen.
- **Stoep-only-routing: bewust NIET gedaan (2026-06-14):** de crowd loopt op door de speler gemarkeerde ring-waypoints (`NetNodes`), niet vrij over de navmesh — nav-area-kosten op wegen zouden niets doen aan hoe wandelaars daadwerkelijk lopen. DECISIONS 06-04 #3 blijft OPEN maar is grotendeels moot voor het huidige waypoint-systeem.
- **Resteert = jouw soak-run:** start de beach-map met `-AutoSoak` (skipt het titelscherm, tickt de wereld door) en kijk ~15 min naar log-spam/hitches + NPC's die door de map zakken of vastlopen. Navmesh-gaten die je ziet → markeer ze (zelfde marker-workflow) of meld de plek.

### 4.6 — [~] P1 · End-to-end op de beach-map → default map
- **DoD:** verse start → kweken → dealen → afspraak → save/load → continue, alles op de beach-map. Daarna beach-map als default new-game map zetten.
- **✓ Default omgezet 2026-06-14:** `PendingNewMap` default 0→**1** + labels (Beach = "main", Apartment = "classic"). Nieuw spel start nu standaard op de beach-strip; apartment blijft selecteerbaar via de toggle (zero regressie). Build groen.
- **✓ Save/load/continue-codepaden geverifieerd:** de save bewaart `MapPath` van de huidige world (`SaveGameSubsystem` regel 647), Continue herstelt 'm (284/302→213→OpenLevel), en de MapPath-gates (`ThePlugSIMCharacter` 1255-1265, `DayNightController` 130/209) behandelen `/Game/CityBeachStrip` apart. End-to-end komt in code dus rond.
- **Resteert = jouw playtest (de DoD):** verse start (nu default beach) → kweken → dealen → afspraak (dag-order incl.) → save → load/continue. Slaagt dat zonder rare dingen, dan is 4.6 volledig [x].
- **Let op:** alles checkt `MapPath.StartsWith("/Game/CityBeachStrip")` — bij een tweede externe map ooit generaliseren, nu laten staan.

---

## 5. OPRUIMEN & TECH-HYGIËNE

### 5.1 — [x] P3 · Template-dead-weight verwijderen
- `Source/ThePlugSIM/Variant_Horror/` + `Variant_Shooter/` zijn nergens gerefereerd (gecheckt: WeedShopCore, Config, .uproject). Verwijderen + include-paden in `ThePlugSIM.Build.cs` (regel ~27-35) opschonen. Snellere builds, minder ruis.
- **✓ Gefixt 2026-06-14:** geverifieerd dat geen enkel bestand buiten de variant-mappen een variant-class/header includet; beide mappen verwijderd (36 bestanden) + `ThePlugSIM.Build.cs` `PublicIncludePaths` teruggebracht tot alleen `"ThePlugSIM"`. Build groen.

### 5.2 — [x] P3 · `AMoneyTestPickup` opruimen
- Test-actor (`Test/MoneyTestPickup.cpp`) uit de speelbare wereld: verwijderen of `#if WITH_EDITOR`.
- **✓ Gefixt 2026-06-14:** alleen 1 geplaatste instance in de oude template-map `Lvl_FirstPerson` (niet de speelmap). C++-class (.cpp/.h) + die ene external-actor-uasset verwijderd. Geen overige referenties.

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

1. ~~**Quick wins (één sessie):** 2.1 (toasts) → 3.2 (FindHomeForPoint) → 5.1 + 5.2 (opruimen) → 1.3 (level 32 vullen).~~ **✓ Gedaan 2026-06-14** (1.3 op 3 gaten na — wachten op cosmetic/QoL-content).
2. **Beach-map-spoor:** 4.1 → 4.2 → 4.3 → 4.4 → 4.5 → 4.6 (volgorde is hard, alles hangt aan 4.1).
3. **Balans-spoor (parallel, alleen data/CSV):** 1.1 → 1.2 → 1.5 → 1.4 → 1.6.
4. **Co-op-afsluiter:** 2.4 → 2.6 (packaged build) → 2.5 (grote testronde, incl. 2.2/2.3-beslissing) → bevindingen terug in deze lijst.

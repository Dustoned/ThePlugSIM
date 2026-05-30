# Codex Brief — Weedshop Game (Unreal Engine)

> **Status:** v2 (definitief concept, beginner-modus). Alle kernkeuzes staan vast — zie de **Legenda** hieronder. Klaar als startpunt voor Codex. Een paar kleine details (specifieke archetypes, edibles-crafting) staan nog open in Sectie 11 en kunnen later.

---

## 0. Hoe je deze brief gebruikt

Deze brief is bedoeld om in de repo te zetten (`/docs/CODEX_BRIEF.md`) als de leidraad voor de AI-agent. Codex leest dit, jij verwijst ernaar in je prompts ("volg de architectuur en conventies uit de brief"). Hou de brief levend: elke grote beslissing die jullie nemen, schrijf je hier of in `/docs/DECISIONS.md` bij, zodat de agent consistent blijft over meerdere sessies.

---

## Legenda — het complete plaatje in één blik

- **Genre & perspectief:** first-person winkel-/dealer-sim, PC (muis/toetsenbord).
- **Co-op:** solo speelbaar, maar alle features ontwerpen met **2-3 speler co-op** in gedachten.
- **Pitch:** begin als straatdealer in je appartement → word een legale wietwinkel → groei naar een franchise.
- **Structuur:** endless met **milestones**; milestones unlocken producten/gear en sturen fase-overgangen én subfases binnen elke hoofdfase.
- **Wereldlevel:** milestones zijn gebaseerd op gedeelde wereld-/party-progressie (zaakniveau), niet alleen op een individuele speler; NPC-relaties levelen daarnaast per persoon.
- **3 hoofdfases + subfases:** (1) straatdealer/appartement **= MVP**, (2) legale winkel, (3) franchise; tussenstappen zoals betere papers, betere kweekgear en winkel-upgrades zijn belangrijk.
- **Kern-loop:** kweken → oogsten → op straat **samples** uitdelen → klant komt naar appartement → **deal via prijs-slider** → cash → herinvesteren.
- **Deal-mechaniek:** prijs-slider t.o.v. markt; live **acceptatie-%** uit prijs + **respect/loyaliteit/verslaving**; te duur → **afdingen** → geen akkoord of geduld op → **boos weg** (−respect).
- **Voorraad:** **zelf kweken** (geen inkoop). Planten groeien in real-time, oogst = voorraad.
- **Producten:** start met **wiet + simpele papers**; betere papers unlocken binnen de appartementfase. Bongs, smartshop/coffeeshop-accessoires en vergelijkbare winkelproducten unlocken pas vanaf je eigen kleine legale shop.
- **Klanten:** een paar archetypes; **wachtrij + geduld-timer**.
- **NPC-geheugen:** stads-NPC's/prospects/klanten krijgen een eigen opgeslagen id en profiel, zodat personen herkenbaar terugkomen en apart geleveld kunnen worden.
- **Dag/nacht:** real-time, **20 min licht / 10 min donker**, doorlopend (geen "volgende dag"). Nacht = meer/schichtigere klanten, hoger politierisico (**heat → lichte bust**), overval-risico. Risico verdwijnt nooit volledig, ook niet in de legale winkel.
- **Upgrades:** kweek-gear, opslag/voorraad, beveiliging, pand uitbreiden/personeel.
- **Plaatsbare objecten:** koopbare furniture, kweekpotten, decor en shop-objecten moeten spelers zelf kunnen plaatsen met goede vloer-/muur-collision, no-overlap preview en co-op zichtbaarheid wanneer iemand aan het plaatsen is.
- **TAB/telefoon-hub:** Map, Contacts, Suppliers en Shops voor online/remote bestellen met delivery fee, winkels bezoeken als goedkoper alternatief, appartementen/panden kopen, upgrades/staff beheren en gedeelde co-op klantcontacten/messages/belletjes.
- **Fase 1 → 2:** geld-drempel halen + **vergunning kopen**.
- **Verhaal:** licht (intro + milestone-beats). **Onboarding:** leren door te doen, geen tutorial.
- **Art:** stylized/cartoon, **Amsterdamse coffeeshop-vibe**. **Audio:** chill/lo-fi/reggae.
- **Fail-state:** busted-risico licht (kleine straf), failliet mogelijk, geen harde game-over.
- **Scope:** kleinst mogelijke eerste versie (alleen de kern-loop), daarna uitbouwen.
- **Tech:** Unreal (nieuwste stabiele 5.x), C++ voor logica + Blueprint voor koppeling. **Beginner-modus:** Codex doet het zware werk (Sectie 1).

---

## 1. Samenwerkingsmodel — wie doet wat

Een Unreal-game bestaat uit **tekst** (C++, config, CSV) en **binaire editor-assets** (Blueprints, levels, materials, UMG-widgets). Codex kan alleen tekst aanraken en kan de editor niet openen of op Play drukken. Daarom dit model:

**Codex bezit (tekst):**
- Alle gameplay-logica in C++ (classes, components, subsystems, interfaces)
- Build-files (`*.Build.cs`, `*.Target.cs`), config (`*.ini`)
- Data in CSV (voor DataTables)
- Save/Load-systeem
- Git-config (`.gitignore`, `.gitattributes`)
- **Fallback-werk:** Python-editor-scripts en stap-voor-stap editor-instructies (zie hieronder)

**Jij bezit (editor):**
- Project aanmaken, level grey-boxen, NavMesh, licht
- Blueprint-subclasses maken van Codex' C++-classes en in het level slepen
- UMG-widgets tekenen en koppelen aan de C++-logica
- Enhanced Input opzetten
- **Compileren** en de feel testen → fouten/gevoel terugkoppelen aan Codex

### De compile-loop (belangrijk)
Codex heeft normaal géén Unreal-build-toolchain (dat is tientallen GB). Codex schrijft C++ dus "blind". **Jij** compileert in de editor of via je lokale toolchain en plakt eventuele build-errors terug in de chat. Codex fixt ze. Reken op een paar van die rondjes per systeem — dat is normaal, geen falen.

### Twee fallbacks als editor-werk je niet lukt
1. **Stap-voor-stap instructies.** Codex kan exacte klik-voor-klik instructies schrijven ("rechtsklik in Content Browser → Blueprint Class → kies parent `ACustomerBase` → noem 'm `BP_Customer`").
2. **Python-editor-scripts.** Unreal heeft een Python API (Editor Scripting Utilities). Codex kan een `.py`-script schrijven dat jij in de editor draait (Tools → Execute Python Script) voor bulk-werk: assets importeren, DataTables genereren, mappen aanmaken, properties zetten. Handig als hetzelfde 50× moet of als handmatig te foutgevoelig is.

> **Vuistregel:** simpel & eenmalig → jij doet het in de editor. Repetitief, foutgevoelig, of je loopt vast → vraag Codex om instructies of een Python-script.

### Beginner-modus (je hebt nog geen Unreal/C++-ervaring)

Dit verschuift de werkverdeling: **Codex doet zoveel mogelijk**, jij leert al doende.
- Codex schrijft **elke regel C++** en levert kopieer-klare code + exacte stappen. Je hoeft geen C++ te kunnen — alleen plakken, compileren, testen.
- Voor editor-werk is de **default**: Codex geeft klik-voor-klik instructies, of (bij iets lastigs/repetitiefs) een Python-script dat het voor je doet. "Jij doet het zelf" geldt alleen voor de echt simpele dingen.
- **Compileren** doe je in Unreal (komt vanzelf bij het openen van het project of via de compile-knop). Rode fouten? Plak de héle foutmelding terug in de chat — Codex fixt en legt uit. Reken op meerdere rondjes; dat hoort erbij en betekent niet dat je iets fout doet.
- We houden bewust **C++** (geen Blueprint-only) zodat Codex het zware werk kan doen; de prijs is dat je voor compileren en setup op Codex leunt.

> **Realistische verwachting:** dit is een ambitieus eerste project. Prima — de aanpak is: begin bij de **MVP** (fase-1 appartement, grijze blokjes), krijg dát werkend en leuk, en bouw pas daarna uit. Nooit alles tegelijk.

---

## 2. Tech-keuzes & aannames

| Onderwerp | Keuze | Status |
|---|---|---|
| Engine | Unreal Engine | vast |
| Engine-versie | nieuwste **stabiele 5.x** (geen preview/beta), daarna vastpinnen — vraag mij de exacte versie als je gaat installeren | aanbevolen |
| Template | **First-Person** (stripped) — je staat achter de toonbank/balie | **gekozen** |
| Taal | C++ voor logica, Blueprint voor koppeling/UI | vast |
| Input | Enhanced Input | vast |
| Platform | PC (Windows), muis/toetsenbord | default (controller later optioneel) |
| Multiplayer | Solo + co-op voor 2-3 spelers; features replication/co-op-bewust ontwerpen | vast |
| Art-niveau | **Stylized / cartoon**, **Amsterdamse coffeeshop-vibe** als referentie | **gekozen** |

**Waarom versie vastpinnen:** het meeste "breekt" bij engine-upgrades mid-project en bij Marketplace/Fab-assets die voor een andere versie gemaakt zijn. Kies één versie, blijf erop, en check bij elke gratis asset/plugin de ondersteunde versie.

---

## 3. Game-overzicht & core loop

**Co-op uitgangspunt:** de game moet solo speelbaar blijven, maar alle systemen worden ontworpen met 2-3 spelers in gedachten. Denk per feature na over: gedeelde party-state (geld, voorraad, heat, milestones), per-speler state (input, UI, interact focus), en conflictsituaties (twee spelers willen dezelfde plant/klant/object bedienen). Gameplay-regels horen server-authoritative of minimaal replication-ready te zijn; tijdelijke single-player shortcuts moeten in `Docs/DECISIONS.md` worden gelogd.

**Perspectief:** First-person — je staat zelf achter de toonbank/balie. In fase 1 (appartement) "deal" je vanaf je deur of een tafeltje; de echte toonbank komt in de winkel-fase.

**Pitch in één zin:** Begin als straatdealer vanuit je appartement, bouw met je winst een legale wietwinkel op en groei uit tot een franchise.

**Progressie-ruggengraat (3 hoofdfases + subfases):**
1. **Straatdealer (appartement)** — discreet verkopen vanuit huis, beperkte voorraad, risico (politie/oplichters), cash is alles. Je begint klein met simpele papers en basale wiet; hogere levels/milestones geven betere papers, betere kweek en meer verkoopkracht. *Dit is de MVP-fase.*
2. **Legale winkel** — je "wordt legaal": eigen pand met toonbank, vergunning, meer assortiment & upgrades. Politierisico en criminaliteit verdwijnen niet: straat-/achterdeurverkoop kan nog verdacht zijn, politie kan langskomen als je opvalt, en klanten kunnen blijven stelen, liegen, afdingen of goedkoop proberen te krijgen.
3. **Franchise** — uitbreiden naar meerdere vestigingen, personeel, eigen merk.

**Subfases:** elke hoofdfase mag meerdere tussenstappen hebben. Je gaat dus niet ineens van "appartement" naar "alles kan"; eerst kleine verkoopmiddelen en betere papers, daarna pas grotere winkelproducten. Producten en upgrades krijgen later daarom niet alleen een milestone, maar ook een toegestane fase/subfase of verkooplocatie.

> **Belangrijk inzicht:** de kern-werkwoorden blijven door alle fases gelijk — *klant bedienen → product leveren → geld ontvangen*. Dezelfde C++-systemen (economy, inventory, customer, order-flow) dragen dus de héle game. De fases verschillen vooral in dressing, content, risico-profiel, product-toegang en "fase-poorten" (bv. vergunning als gate naar fase 2). Codex bouwt de systemen één keer; de arc is grotendeels data + setting.

**Structuur:** endless met **milestones**. Milestones unlocken nieuwe producten en gear, sturen subfases, en sturen de fase-overgangen (dealer → winkel → franchise). Je begint met **wiet + simpele papers**; betere papers en kweekgear horen nog in de appartementfase, maar bongs, smartshop-items en winkelaccessoires horen pas bij de legale shop.

**Wereldlevel / party-progressie:** milestones worden later gebaseerd op het gedeelde niveau van de wereld/zaak/party: totaal verdiend, reputatie, vergunningen, unlocks, panden en andere gezamenlijke voortgang. In co-op deelt iedereen deze progressie. Dit staat los van per-speler UI/input en los van per-NPC relatielevels zoals respect/loyaliteit/verslaving.

**Voorraad = zelf kweken.** Je teelt je eigen wiet: planten groeien in real-time (gekoppeld aan de dag/nacht-klok), je oogst, en die oogst is je verkoopvoorraad. Geen inkoop bij een leverancier.

**Hoe je klanten werft (straat → appartement):** voordat iemand klant wordt, maak je hem op **straat** warm. Je deelt gratis **joint-samples** uit; dat verhoogt zijn **verslaving, respect en loyaliteit**. Een tevreden prospect geeft op straat aan dat hij het lekker vindt en meteen iets wil — en komt dan naar je **appartement** om te kopen. Samples kosten je eigen oogst, maar leveren loyale, verslaafde klanten op die hogere prijzen accepteren (zie de deal-formule).

**Persistent NPC-geheugen:** iedereen die in de stad rondloopt moet uiteindelijk een eigen opgeslagen id krijgen met persoonlijke gegevens: archetype, relatie-stats, productverslaving/voorkeuren, encounter-history, laatste ontmoeting en eventuele flags zoals "lastpak", "diefstalrisico" of vaste klant. Daardoor voelt iemand niet als wegwerp-spawn: dezelfde persoon kan terugkomen, jou herkennen, beter of slechter reageren, en door spelers over tijd geleveld worden. In co-op is dit gedeelde party-state: de host/server beslist welke NPC-id wordt aangepast als meerdere spelers dezelfde persoon helpen, samplen of bedienen.

**TAB/telefoon-hub:** de speler krijgt een `TAB`-view met apps/secties voor Map, Contacts, Suppliers en Shops. Via Suppliers/Shops kun je spullen online/remote bestellen met een **delivery fee**; zelf naar winkels in de stad gaan blijft goedkoper/anders, maar kost tijd/risico. Dezelfde hub kan appartementen/panden, winkel-upgrades, staff-management, unlock-overzichten en klantcontacten tonen. Klanten/prospects die je leert kennen kunnen hun telefoonnummer geven; daarna kunnen ze bellen of appen om te vragen of ze langs mogen komen, of juist aangeven dat ze onderweg zijn. In co-op is de hub een per-speler UI, maar aankopen, delivery orders, panden, staff, unlocks, klantcontacten, messages en bel-afspraken zijn gedeelde party-state en moeten server-authoritative worden.

**Plaatsbare furniture/potten/build-mode:** alles wat je koopt en fysiek in je appartement, winkel of franchise kan staan, moet later door spelers zelf geplaatst kunnen worden. Dat geldt voor furniture, kweekpotten, lampen/gear, opslag, schappen, decor en vergelijkbare shop-objecten. Plaatsen gebruikt een ghost-preview met groen/rood feedback, correcte collision tegen floor/walls, geen overlap met muren/deuren/andere objecten, en later surface-regels zoals "alleen op vloer", "tegen muur" of "op toonbank". In co-op is placement gedeelde party-state: andere spelers zien dat iemand aan het plaatsen is, inclusief preview/eigenaar/reservering, en de uiteindelijke plaatsing wordt server-authoritative bevestigd.

**Core loop fase 1 (MVP):** starter gear/stock via de `TAB` hub regelen → kweken → oogsten → op straat **samples uitdelen** (verslaving/respect/loyaliteit↑) → geïnteresseerde klant komt naar je appartement → leveren via de prijs-slider → cash → herinvesteren in meer/betere kweek → herhaal, richting de milestone voor je eerste legale winkel. Grijze blokjes, geen art.

**Verhaal & instap:** lichte verhaallijn — een korte intro die je als straatdealer neerzet, en die verder verteld wordt via de milestones (geen zware cutscenes). Géén tutorial: de speler leert door te doen, dus UI en vroege moeilijkheid moeten intuïtief en mild zijn.

**Win/fail/scope:** `[TE BESLISSEN — zie Sectie 11]`

### De deal-mechaniek (het hart van de game)

Elke verkoop verloopt zo:
1. Klant arriveert en toont zijn wens: **welk product** en **hoeveel** (uit `DT_Products` + klant-AI).
2. Jij zet met een **prijs-slider** je vraagprijs t.o.v. de marktprijs (bereik bv. 0.5× t/m 2.0× markt).
3. Het spel toont **live een acceptatie-%**: de kans dat de klant toehapt.
4. Eén klik = bod doen.
5. Klant **accepteert**, óf **dingt af** als de prijs te hoog is (hij wil lager) — dan kun je de slider bijstellen en opnieuw bieden.
6. Komt er geen akkoord, of raakt zijn **geduld** op (te lang gedraal of een te lange rij), dan loopt de klant **boos weg** (−respect).

**Wat het acceptatie-% bepaalt:**
- **Prijs t.o.v. markt** — hoofdfactor. Onder markt = hoger %, boven markt = lager %.
- **Respect** — hoe serieus de klant je neemt (groeit door eerlijke deals, daalt door slechte service, weigering, boos weglopen of duidelijke misleiding).
- **Loyaliteit** — vaste-klant-binding (groeit door herhaling & goede service).
- **Verslaving** — drang naar het product (groeit per aankoop van dat product; hoog = accepteert duurdere prijzen).

**Voorstel-formule (tunebaar — zet in config/CSV zodat aanpassen makkelijk is):**
```
prijsRatio = vraagprijs / marktprijs
basisKans  = 70 − (prijsRatio − 1.0) × 100        // markt → 70%, goedkoper → hoger, duurder → lager
eindKans   = basisKans + respect×0.15 + loyaliteit×0.15 + verslaving×0.25   // attributen 0–100
eindKans   = clamp(eindKans, 0, 100)
```

**Feedback-loops (hier zit de spanning):**
- Deal op/onder markt → +respect, +loyaliteit.
- Deal flink boven markt maar tóch geaccepteerd → cash nu; géén automatische respect/loyaliteit-straf puur omdat hij akkoord ging. Als de klant het te duur vindt, moet hij weigeren, afdingen of boos weglopen. Straf komt vooral door geen akkoord, slechte service of duidelijk irritant gedrag.
- Elke aankoop van product X → +verslaving voor X (met plafond) → klant tolereert later hogere prijzen.
- Te duur / weigering → klant kan boos weglopen, −respect.

> Per fase verschilt de balans: als straatdealer is risico en cashdruk hoger; in de legale winkel wegen reputatie, beveiliging, assortiment en klantmix zwaarder. Codex bouwt één formule; fase- en subfase-verschillen zijn tunebare waarden.

---

## 4. Architectuur & mapstructuur

### Source (C++)
Twee modules, zodat jouw gameplay gescheiden blijft van template-boilerplate en Git-diffs schoon blijven:

```
/Source
  /WeedShop          <- primaire game-module (van template)
  /WeedShopCore      <- jouw gameplay-module (Codex bezit dit)
    /Public
    /Private
```

### Aanbevolen klasse-opzet
- `AWeedShopGameMode` — regels van de dag/sessie
- `AWeedShopGameState` — gedeelde staat (huidige dag, winkel open/dicht)
- `AWeedShopPlayerController` — input → acties
- `UEconomySubsystem` (`UGameInstanceSubsystem`) — geld, transacties; overleeft level-load
- `UInventorySubsystem` (`UGameInstanceSubsystem`) — voorraad & producten (gevuld door je oogst; later ook crafting/merch)
- `UCultivationSystem` + `AGrowPlant` — planten die in real-time groeien (gekoppeld aan de klok); oogst → voorraad
- `UMilestoneSubsystem` (`UGameInstanceSubsystem`) — milestones die producten/gear unlocken en fase-overgangen triggeren (bv. fase 1 → 2 = geld-drempel + vergunning kopen)
- `UDayCycleSubsystem` (`UWorldSubsystem`) — doorlopende real-time klok (20 min licht / 10 min donker), stuurt de lichting + een "is nacht"-signaal voor andere systemen
- `UHeatSubsystem` (`UGameInstanceSubsystem`) — "heat"/politierisico; stijgt 's nachts en bij riskant gedrag, kan politiebezoek/busts triggeren. Dit risico wordt anders per fase, maar verdwijnt nooit volledig.
- `URobberyEventManager` (`UWorldSubsystem`) — kans op een overval 's nachts (verlies cash/voorraad), als los event
- `ACustomerBase` (`AActor`/`ACharacter`) — prospect/klant met **C++ state machine** (straat-prospect → geïnteresseerd → komt naar appartement → koopt)
- `UNpcMemorySubsystem` (`UGameInstanceSubsystem`) — later: persistent geheugen voor stads-NPC's met opgeslagen id, relatie-stats, voorkeuren en encounter-history
- `UPhoneAppSubsystem` (`UGameInstanceSubsystem`) — later: `TAB`/telefoon-hub management voor Map, Contacts, Suppliers, Shops, online orders, delivery fees, panden, upgrades, staff en gedeelde klantcontacten/messages
- `UPlacementSubsystem` (`UGameInstanceSubsystem` of later GameState-backed) — later: koopbare furniture/potten/shop-objecten plaatsen, valideren, reserveren en opslaan als gedeelde co-op party-state
- `APlaceableItemActor` — later: fysieke geplaatste objecten met footprint/collision, surface-regels, interact hooks en replicated transform/state
- `USamplingSystem` — gratis samples uitdelen op straat; verhoogt verslaving/respect/loyaliteit en zet prospects om in kopers (kost oogst)
- `IInteractable` (`UInterface`) — alles waar de speler op kan interacten (toonbank, kassa, schap)
- `UWeedShopSaveGame` (`USaveGame`) — opslag van geld, voorraad, dag/klok en persistent NPC-profielen

> **Agent-vriendelijke keuze:** gebruik een **C++ state machine** voor klant-gedrag, géén Behaviour Tree. BT en Blackboard zijn binaire editor-assets die Codex niet kan bewerken. Een state machine in C++ houdt de hele klant-logica in Codex' handen.

### Content-mappen (jij beheert in de editor)
```
/Content
  /Maps
  /Blueprints   (BP-subclasses van Codex' C++)
  /UI           (UMG-widgets)
  /Art          (meshes, materials, textures)
  /Audio
  /Data         (DataTables, geïmporteerd uit CSV)
```

---

## 5. Naamconventies & code-stijl voor Codex

- Unreal-prefixes: `A` (Actor), `U` (UObject/Component), `F` (struct), `E` (enum), `I` (interface), `BP_` (Blueprint), `WBP_` (Widget Blueprint), `DT_` (DataTable), `S_` (struct-row).
- Alles wat jij in Blueprint moet kunnen instellen of aanroepen, moet Codex exposen:
  - Data die je tweakt: `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WeedShop")`
  - Functies die je in BP aanroept: `UFUNCTION(BlueprintCallable, Category="WeedShop")`
  - Events waar BP/UI op luistert: `DECLARE_DYNAMIC_MULTICAST_DELEGATE...` + `UPROPERTY(BlueprintAssignable)`
- Eén systeem = één commit. Kleine, reviewbare diffs.
- Comments boven elke class: wat het doet + wat jij in de editor moet koppelen.
- Denk bij elke feature aan co-op voor 2-3 spelers: bepaal welke data gedeeld wordt, welke data per speler is, en hoe interacties/conflicten later gerepliceerd worden.

---

## 6. Data-driven design (CSV → DataTables)

Alle balans en content komt uit CSV, zodat Codex content kan toevoegen en balanceren **zonder de editor aan te raken**. Per CSV een `USTRUCT : public FTableRowBase` in C++.

Geplande tabellen:
- `DT_Products` — producten: naam, categorie (wiet/papers/edibles/accessoires/smartshop), marktprijs, populariteit, **unlock-milestone**, en later toegestane fase/subfase/verkooplocatie
- `DT_Strains` — wietsoorten: naam, groeitijd, opbrengst, kwaliteit/effect, benodigde gear
- `DT_Milestones` — milestones: drempel (bv. totaal verdiend), wat het unlockt (product/gear/fase)
- `DT_CustomerTypes` — klanttypes: voorkeur, geduld, budget, spawn-gewicht (apart voor dag/nacht — nacht trekt schichtigere types), start-waarden respect/loyaliteit/verslaving + groeisnelheid per type

> Respect, loyaliteit en verslaving zijn **runtime per klant** (en blijven bewaard voor terugkerende vaste klanten). De CSV bepaalt alleen de startwaarden en hoe snel ze stijgen/dalen. Zodra de stad/prospect-laag gebouwd wordt, hangen deze runtime-waarden aan een opgeslagen NPC-id in plaats van alleen aan een tijdelijke actor.
- `DT_Upgrades` — upgrades (categorieën: kweek-gear, opslag/voorraad, beveiliging, pand/personeel): naam, categorie, kosten, effect, vereiste milestone
- `DT_Placeables` — koopbare plaatsbare objecten: furniture, kweekpotten, lampen/gear, opslag, schappen en decor; kosten, unlock, fase/subfase, footprint, surface-type (floor/wall/counter), collision-regels en max-aantal
- `DT_DayConfig` — per dag: aantal klanten, spawn-tempo, moeilijkheid

> Codex schrijft de struct + de CSV. Jij importeert de CSV als DataTable in `/Content/Data` (of laat Codex een Python-importscript schrijven).

---

## 7. Kernsystemen (per stuk)

| Systeem | Doel | Codex (C++) | Jij (editor) | Klaar wanneer |
|---|---|---|---|---|
| Interaction | Speler interact met objecten | `IInteractable` + trace/knop-logica | knop in Enhanced Input, BP op toonbank | knop op toonbank vuurt event |
| Economy | Geld erin/eruit | `UEconomySubsystem` | — | geld op/af + event naar HUD |
| Inventory | Voorraad & producten (gevuld door oogst) | `UInventorySubsystem` + `DT_Products` | DataTable importeren | voorraad uit oogst bestaat |
| Kweek | Planten groeien in real-time → oogst → voorraad | `UCultivationSystem` + `AGrowPlant` + `DT_Strains` | plant-mesh/plek in level | plant groeit, oogst vult voorraad |
| Customer AI | Spawnt (meer 's nachts), wacht in de rij met geduld, bestelt, vertrekt (boos bij te lang) | `ACustomerBase` + state machine + wachtrij + geduld-timer; spawn per tijd-van-dag | NavMesh in level, `BP_Customer` | klanten wachten in rij, geduld telt af |
| Sampling/werving | Gratis samples op straat → verslaving/respect/loyaliteit↑ → prospect wordt koper | `USamplingSystem` + `ACustomerBase` (straat-state) | straat-gebied in level | sample uitdelen verhoogt attributen, prospect komt langs |
| Deal/verkoop | Prijs-slider + live acceptatie-% + afdingen | `UDealSystem`: formule, slider→%, accept/afdingen/weiger + attribuut-updates | prijs-slider in `WBP_Deal` | deal end-to-end; afdingen + boos weglopen werkt |
| Dag/nacht | Real-time cyclus 20 min licht / 10 min donker; stuurt lichting én tijd-van-dag voor andere systemen | `UDayCycleSubsystem`: looping klok + licht/donker-fase + "is nacht"-delegate | lichting koppelen (directional light / skylight) | cyclus loopt, lichting + nacht-flag wisselen |
| Politie/heat | 's Nachts hoger risico; straat-/achterdeurverkoop en verdacht gedrag kunnen politiebezoek/busts triggeren in elke fase | `UHeatSubsystem`: heat stijgt bij risico, fase/subfase bepaalt ernst en kans | — | heat loopt op en lichte bust/politiebezoek is mogelijk |
| Overval-events | Kans op overval 's nachts (verlies cash/voorraad) | `URobberyEventManager`: nacht-roll → overval-event | event-feedback in UI | overval kan 's nachts gebeuren |
| Progressie/milestones | Upgrades, subfases, product-unlocks, verkooplocatie-gates, fase-overgangen | `UMilestoneSubsystem` + upgrade-logica + `DT_Upgrades`/`DT_Milestones` | upgrades/unlocks koppelen in UI | milestone unlockt iets; min. 1 upgrade/subfase werkt |
| Placement/build mode | Koopbare furniture, potten en shop-objecten zelf plaatsen | later `UPlacementSubsystem` + `APlaceableItemActor`: ghost preview, collision/surface-validatie, save-data, co-op reservation | preview material, meshes/collision, placement input/UI | min. 1 pot en 1 furniture item kunnen alleen op geldige plek geplaatst worden; andere co-op spelers zien de preview/reservering |
| TAB/telefoon-hub | Map, Contacts, Suppliers, Shops; online bestellen met delivery fee, panden/upgrades/staff beheren, gedeelde klantcontacten/messages/belletjes | later `UPhoneAppSubsystem` + TAB/telefoon UI; gedeelde aankopen/contacten server-authoritative | hub-widget + stad/winkel-alternatief | unlocked items zichtbaar; online order rekent delivery fee; bekende klant kan bellen/app'en |
| HUD | Geld/bestelling/dag tonen | data + delegates | `WBP_HUD` tekenen & binden | info live op scherm |
| Save/Load | Voortgang bewaren | `UWeedShopSaveGame` | — | geld+dag overleven herstart; save-schema heeft ruimte voor persistent NPC-profielen |

---

## 8. A–Z STAPPENPLAN

Volgorde van leeg project → speelbare basis → assets → polish. Kolom **Wie**: *Jij* (editor), *Codex* (tekst), *Samen* (Codex levert, jij koppelt). Voor elke *Jij*-stap geldt: lukt het niet, vraag Codex om instructies of een Python-script.

| # | Fase | Stap (en hoe) | Wie | Klaar wanneer (DoD) |
|---|---|---|---|---|
| A | Fundament | Core loop in 1 zin + MVP-scope op papier | Jij | één zin + 3-5 MVP-features genoteerd |
| B | Fundament | Engine-versie kiezen & vastpinnen (UE Launcher) | Jij | versie genoteerd in `/docs/DECISIONS.md` |
| C | Fundament | Project aanmaken vanuit template (Top Down / FP) | Jij | leeg project opent & speelt |
| D | Fundament | Git + Git LFS + `.gitignore` + `.gitattributes` | Samen | eerste commit gepusht, LFS actief |
| E | Fundament | C++ gameplay-module `WeedShopCore` opzetten | Codex | module compileert leeg |
| F | Fundament | Conventies & mapstructuur vastleggen | Codex | `CONVENTIONS.md` in repo |
| G | Vertical slice | Grey-box level: appartement + stukje straat (simpele meshes) | Jij | je kunt door appartement en straat lopen |
| H | Vertical slice | Player/camera/input via template testen | Jij | bewegen + interact-knop werkt |
| I | Vertical slice | Interaction-systeem (`IInteractable`) | Codex | knop op toonbank triggert C++ event |
| J | Vertical slice | Economy/Wallet subsystem | Codex | geld op/af, zichtbaar in log |
| K | Vertical slice | Inventory + `DT_Products` + minimale kweek (1 plant groeit → oogst → voorraad) | Samen | je kunt oogsten en hebt voorraad om te verkopen |
| L | Vertical slice | Klant-actor + spawn + NavMesh + wachtrij/geduld | Samen | klant spawnt, wacht in rij, geduld telt af |
| M | Vertical slice | Deal-flow: wens → prijs-slider → accept/afdingen/weiger (boos weg) | Codex | deal-loop draait 1× incl. afdingen |
| N | Vertical slice | Real-time dag/nacht-cyclus (20 min licht / 10 min donker) + lichting | Samen | cyclus loopt door, licht↔donker wisselt |
| O | Vertical slice | HUD-logica + `WBP_HUD` koppelen | Samen | geld/bestelling/dag op scherm |
| P | Vertical slice | Save/Load | Codex | geld+dag overleven herstart; save-schema is klaar voor NPC-profielen |
| Q | **CHECK** | **MVP-speeltest: is de loop leuk?** | Jij | go/no-go beslissing genomen |
| R | Content | Volle kweek (strains, kwaliteit, gear, meerdere planten) + producten/klanttypes via CSV | Codex | meerdere strains/producten & klanttypes in spel |
| S | Content | Milestones + unlocks (nieuwe producten/gear) + upgrades | Samen | milestone unlockt iets; min. 1 upgrade werkt |
| S2 | Content | Plaatsbare furniture/potten/build-mode: kopen → ghost preview → geldige plaatsing | Samen | min. 1 kweekpot en 1 furniture item plaatsbaar met vloer-/muur-collision; co-op placement-preview/reservering is ontworpen en gehaakt |
| T | Content | Balans-pass (prijzen/geduld/spawn-tempo) | Samen | speelt fair, niet te makkelijk/saai |
| T2 | Content | TAB/telefoon-management: Map, Contacts, Suppliers, Shops, online orders, delivery fee, panden/upgrades/staff en gedeelde klantcontacten/messages | Samen | hub toont alleen unlocked opties; bestelling/afspraak beïnvloedt shared state |
| U | Assets | Meshes/materials importeren, grey-box vervangen | Samen | ruimte ziet er "echt" uit |
| V | Assets | Audio & VFX hooks (events vanuit C++) | Samen | geluid bij verkoop & dagwissel |
| W | Feel | Juice: feedback, UI-animatie, camera-shake, tweens | Samen | acties voelen "sappig" |
| X | Polish | Menu's: main menu, pause, settings | Samen | starten/pauzeren/afsluiten werkt |
| Y | Polish | Bugfix + performance pass | Samen | 15 min spelen zonder crash |
| Z | Ship | Packaged build maken (standalone `.exe`) | Jij | `.exe` draait buiten de editor |

> **Nacht-gameplay** (meer/schichtigere klanten, politie-heat → bust, overval-events) landt in de **Content-fase (rond stap R–T)**, nadat de kern-loop werkt. De dag/nacht-cyclus zélf zit al in stap N; de gevólgen ervan voeg je later toe, zodat de MVP klein blijft.
>
> **Straat-werving** (samples uitdelen → prospect komt naar appartement) hoort bij fase 1 en sluit aan op stap L–M. De allereerste slice mag klanten nog direct laten verschijnen; de sample-funnel voeg je er kort daarna aan toe.

---

## 9. Git + LFS setup (concreet)

Verplicht vanaf dag één — anders raak je werk kwijt of wordt de repo onwerkbaar groot.

**`.gitignore` (Unreal):**
```
Binaries/
DerivedDataCache/
Intermediate/
Saved/
.vs/
*.sln
*.suo
*.VC.db
*.opensdf
*.sdf
```

**`.gitattributes` (LFS voor binaire assets):**
```
*.uasset filter=lfs diff=lfs merge=lfs -text
*.umap   filter=lfs diff=lfs merge=lfs -text
*.fbx    filter=lfs diff=lfs merge=lfs -text
*.png    filter=lfs diff=lfs merge=lfs -text
*.tga    filter=lfs diff=lfs merge=lfs -text
*.wav    filter=lfs diff=lfs merge=lfs -text
*.mp3    filter=lfs diff=lfs merge=lfs -text
```

Codex levert deze files; jij doet `git lfs install`, commit en push.

---

## 10. Werkafspraken met Codex

- **Eén systeem per ticket/commit.** Niet alles tegelijk.
- **`/docs/DECISIONS.md`** bijhouden: elke architectuur- of balans-keuze in 1-2 regels.
- **Build-errors** plak je terug in de chat; Codex fixt en legt uit wat er mis was.
- **Geen editor-aannames:** als Codex iets nodig heeft uit de editor (een asset-pad, een class-naam die jij maakte), vraagt het ernaar i.p.v. te gokken.
- **Blueprint-exposure** controleren: na elk systeem checkt Codex of de juiste `UPROPERTY`/`UFUNCTION`-flags erop staan zodat jij het kunt koppelen.

---

## 11. Ontwerpbeslissingen — JOUW INPUT

Vul zoveel mogelijk in (typ gewoon je antwoorden, hoeft niet netjes). Tussen `[ ]` staat mijn voorstel/default — laat staan = akkoord, of verander het. Hoe meer je invult, hoe scherper Codex kan bouwen.

**A. Perspectief & fantasie** — ✅ first-person, jij bedient zelf (personeel pas in de franchise-fase)

**B. Setting & toon** — ✅ arc: straatdealer → legale winkel → franchise; stylized/cartoon; **Amsterdamse coffeeshop-vibe** als look-referentie
- ✅ Verhaal: lichte verhaallijn — korte intro + beats via de milestones (geen zware cutscenes)
- ✅ Onboarding: leren door te doen, géén tutorial → UI en vroege moeilijkheid moeten intuïtief/mild zijn
- ✅ Audio: chill / lo-fi / reggae-sfeer

**C. Core loop & ritme** — ✅ real-time dag/nacht-cyclus: 20 min licht + 10 min donker (30 min per dag), tijd loopt door, géén "volgende dag"-scherm/functie
- Nacht-gameplay — ✅ ja: meer & schichtigere klanten, hoger politierisico (heat → politiebezoek/bust), en overval-risico (cash/voorraad). Gedoseerd. Komt in de Content-fase, niet de MVP.
- Doel — ✅ endless met milestones; milestones unlocken producten/gear, subfases en fase-overgangen
- Fail-state — ✅ busted-risico **licht** aan het begin (heat loopt op, kleine straf); failliet kan; geen harde game-over. Risico wordt per fase anders, maar gaat nooit helemaal weg.

**D. Producten & voorraad** — ✅ start met wiet + simpele papers; betere papers unlocken in de appartementfase. Bongs, smartshop/coffeeshop-accessoires en vergelijkbare winkelproducten unlocken pas vanaf de kleine legale shop. Voorraad = **zelf kweken** voor wiet (geen inkoop).
- Hebben producten/strains eigenschappen die klanten belangrijk vinden (kwaliteit, soort, effect)? `[ja — speelt mee in de deal-acceptatie]`
- Worden edibles later gemaakt van je eigen oogst (crafting), en accessoires los verkocht als merch? `[ ]`

**E. Klanten** — ✅ een paar duidelijke archetypes; wachtrij met geduld; bij te hoge prijs **dingt** de klant eerst af → bij geen akkoord (of te lang wachten) loopt hij **boos weg** (−respect). Ook in de legale winkel blijft de klantmix rommelig: naast nette klanten zijn er altijd lastpakken, dieven en klanten die goedkoop proberen te krijgen; max respect/verslaving garandeert dus niet dat iemand zich netjes gedraagt.
- Welke archetypes precies (bv. kenner, toerist, stamgast, lastpak)? `[ ]`
- ✅ Werving: op straat gratis **joint-samples** uitdelen verhoogt verslaving/respect/loyaliteit; tevreden prospects komen naar je appartement om te kopen (kost oogst)
- ✅ Persistent NPC-geheugen: stads-NPC's/prospects/klanten krijgen een opgeslagen id en profiel, zodat terugkerende personen herkenbaar blijven en individueel geleveld kunnen worden.

**F. Progressie & upgrades** — ✅ upgrade-categorieën: **kweek-gear** (betere/meer planten), **opslag/voorraad**, **beveiliging** (tegen heat/overval) en **pand uitbreiden/personeel**. Fase 1 → 2: **geld-drempel halen + vergunning kopen**.
- Concurrenten/rivalen `[later misschien — niet in MVP, mogelijk latere toevoeging]`
- ✅ TAB/telefoon-hub: managementscherm met Map, Contacts, Suppliers en Shops voor online/remote bestellen met delivery fee, zelf naar winkels gaan als alternatief, appartementen/panden kopen, winkel-upgrades, staff-management, unlocks en gedeelde co-op klantcontacten/messages/belletjes.
- ✅ Plaatsbare furniture/potten: alles wat fysiek gekocht wordt voor appartement/winkel/franchise moet zelf plaatsbaar zijn met goede floor/wall collision, duidelijke ghost-preview, geen overlap, en co-op zichtbaarheid/reservering terwijl iemand plaatst.

**G. Interactie-diepte / minigames** — ✅ één klik om te bieden, met een **prijs-slider** t.o.v. markt; acceptatie-% uit prijs + respect/loyaliteit/verslaving (zie deal-mechaniek in Sectie 3)
- Andere minigames later (afwegen, kweken, mixen)? `[ ]`

**H. Tech & jij** — ✅ geen Unreal/C++-ervaring → beginner-modus, Codex doet het zware werk (Sectie 1); PC + muis/toetsenbord

**I. Scope & tijd** — ✅ kleinst mogelijke eerste versie: alleen de kern-loop (kweken → samples → deal → cash). Daarna pas content en uitbouw.

---

## 12. Eerste stappen (v2 staat — je kunt beginnen)

**Jouw allereerste stap (editor):**
0. Unreal installeren (vraag mij de exacte nieuwste stabiele versie), een **First-Person C++-project** aanmaken (stap C), en Git + LFS opzetten met de files die Codex aanlevert (stap D).

**Daarna kan Codex starten met:**
1. `.gitignore` + `.gitattributes` aanleveren (Sectie 9).
2. Lege `WeedShopCore`-module opzetten die compileert (stap E).
3. `IInteractable`-interface + interactie-trace (stap I).
4. `UEconomySubsystem` met `AddMoney`/`RemoveMoney` + delegate (stap J).
5. `DT_Products`-struct + start-CSV met 3 dummy-producten (stap K).

---

## 13. Bewust nog open (later beslissen — blokkeert de start niet)

Dit hoort er nog niet in, en dat is goed — je beslist het tijdens het bouwen of in een latere fase:

**Kleine details (Sectie 11):**
- Welke klant-archetypes precies (kenner, toerist, stamgast, lastpak, …).
- Of edibles van je eigen oogst gemaakt worden (crafting) en accessoires als merch verkocht worden.

**Balans-getallen (tijdens spelen tunen, staan in CSV/config):**
- Startgeld, zaad-/plantkosten, groeitijden, marktprijzen, upgrade-kosten, geduld-tijden, sample-effecten, heat- en overval-kansen. Bijstellen tot het goed voelt.

**Fase 2 & 3 (pas uitwerken als fase 1 werkt):**
- Hoe de legale winkel precies anders speelt (toonbank, vergunning-regels, gemengde klantmix, diefstal/security, politiebezoek bij verdachte achterdeurverkoop).
- Franchise: meerdere vestigingen, personeel-diepte, merk.

**Content & feel (volgt op de systemen):**
- Exacte intro + milestone-verhaalbeats, specifieke art-assets, en de SFX-lijst (verkoop, oogst, dagwissel, etc.).

> Regel: niet vooruit ontwerpen op iets dat je nog niet bouwt. Eerst de kern-loop spelen, dán de volgende laag beslissen.

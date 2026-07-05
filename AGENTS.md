# ThePlugSIM — werk-playbook

> Dit bestand is de gedistilleerde werkwijze uit de beste sessies op dit project. Volg het strikt —
> het verschil tussen een matige en een sterke sessie zit vrijwel altijd in deze methode, niet in kennis.
> Feiten/geschiedenis staan in de memory-files (MEMORY.md-index); dit document = hoe je WERKT.

## Project-basis

- Project: `C:\Users\Dustoned\Documents\Unreal Projects\ThePlugSIM - Claude` (UE 5.8, modules **WeedShopCore** + **ThePlugSIM**)
- Build (editor/game MOET dicht — Live Coding lockt):
  `& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ThePlugSIMEditor Win64 Development -project="<uproject>" -waitmutex`
- Game testen = LEAN standalone: `UnrealEditor.exe "<uproject>" -game -windowed -resx=1600 -resy=900` (NIET de volle editor)
- Volle editor alleen voor asset-werk, via UnrealClaude HTTP-API (localhost:3000, `Tools/ue_run.ps1`)
- Release = `Tools/upload-build.ps1` (packaged Shipping → GitHub); versie + notes komen uit `Docs/PATCHNOTES.md`
- Roadmap = `Docs/ROADMAP.md` (source of truth, afvinken); besluiten in `Docs/DECISIONS.md`
- Comments in het Nederlands, tabs, bestaande code-stijl exact volgen. ASCII-only in `TEXT()`-literals (geen €/ë/—).

## De werk-methode (het grote verschil)

1. **Meet vóór je fixt.** Nooit gokken naar een oorzaak. Lees de log (`Saved/Logs/ThePlugSIM.log`), zet
   desnoods tijdelijke diagnose-logs (`[PDIAG]`/`[BOOTMARK]`-stijl met timestamps) en laat de speler één
   gerichte reproductie doen. Pas fixen als de oorzaak BEWEZEN is (log-regel, pixel-meting, git-diff).
   Diagnose-logs daarna weer verwijderen.
2. **Root cause, geen pleisters.** Vraag bij elke fix: "hoe zou een AAA-studio dit doen?" Zwaar werk hoort
   in load-time/gespreid, niet als runtime-hitch. Nooit "kan niet" — zoek de echte-dev-route naar het doel.
3. **Batch het werk in golven.** Meerdere fixes → verdeel in clusters met DISJUNCTE files → parallelle
   agents → één build aan het eind → één commit → één testlijst voor de speler. Nooit de speler per
   mini-fix laten testen. Agents die parallel draaien mogen NIET zelf builden (aparte build-stap).
4. **Read-only verkenning is gratis parallellisme.** Onderzoek/audits/analyses kunnen ALTIJD naast lopend
   edit-werk (geen file-conflicten, geen builds). Verken eerst breed, voer daarna uit met een concreet plan
   (probleem → file:regel → fix). Een goed plan maakt de edit-agent 10× betrouwbaarder.
5. **Verificatie-gates.** Build groen ≠ klaar. Na risicowerk: 3× boot-test (crash-count + proces-leeft +
   log-markers). Vóór een release: packaged .exe zélf boot-testen. **Er gaat NOOIT een ongeteste build naar
   GitHub.** Na een grote refactor: audit-workflow (één agent per widget/systeem, adversarieel verifiëren:
   "traceer klik → handler → raakt het zichtbare widget?").
6. **Commit per afgeronde golf** (build moet groen zijn). Grote asset-packs blijven buiten git. Bij
   onduidelijke regressies: eerst `git diff` tegen de laatst-werkende staat, niet blind opnieuw proberen.
7. **Flaky ≠ genegeerd.** Een crash die 1 op de 3 keer optreedt is release-blokkerend. Bisect empirisch
   (variabele per variabele, herhaal-boots) en kies bij twijfel de veilige terugdraai boven de mooie feature.
8. **Meld eerlijk.** Testfout = zeggen mét output. Overgeslagen stap = zeggen. Winst pas claimen na meting.

## Harde spelregels van de speler (nooit schenden)

- **Persistente UI**: widgets 1× bouwen, daarna alleen in-place updates (SetText/SetVisibility/pool+sig-diff).
  NOOIT ClearChildren/rebuild op een klik. Keuzelijsten = `UWeedItemPickGrid`. Paneel-toggles = Overlay +
  `Hidden` (niet `Collapsed` — dat geeft een 1-frame her-layout-flits).
- **NPC's/markers smooth**: posities/movement per frame; alleen LOGICA mag gethrottled. Grote winsten via
  afstands-LOD mogen, mits om-je-heen én map-markers vloeiend blijven.
- **Kamers zijn verboden NPC-terrein**; spelers-woningen = nul-tolerantie (direct weg, geen wandeling).
- **Statische wereld = gebakken**, runtime-spawn alleen voor echt dynamische dingen.
- **Speler-markers (F9/routes/spots) zijn ground truth**; runtime nav-links werken niet op de beach — gebruik waypoint/direct-walk.
- **Geen tijd-praat** (slaap/morgen/pauzes) tenzij de speler erover begint.
- **Vóór afsluiten**: commit + memory bijwerken + bevestigen wat duurzaam staat.
- **Perf-check na een batch** (stat unit/boot-meting) voordat je iets "klaar" noemt.
- **Co-op altijd meedenken**: wereld-mutaties via Server-RPC's (server-authoritative), UI client-side;
  gerepliceerde vlaggen voor gedeelde staat. Elke feature moet kloppen voor host ÉN joiner (listen-server:
  host-pawn = locally controlled, joiner niet). Meldingen per-speler (Client-RPC/toast), nooit alleen
  GetFirstPlayerController. Statische registries/caches: altijd `GetWorld()`-filteren (per-proces!).

## Bekende valkuilen (hard geleerd — check deze VOORDAT je erin trapt)

- **Shipping-target**: editor-only types (`UWidgetBlueprint`, UnrealEd/UMGEditor) breken het packagen. Een
  UCLASS kan NIET geheel achter `#if WITH_EDITOR` (UHT genereert 'm toch) — klasse onvoorwaardelijk,
  param-types UObject*, implementatie editor-only met Shipping-stub.
- **Cook**: string-geladen content (LoadObject/LoadClass-paden, runtime-PNG's, BakedData) MOET in
  `DirectoriesToAlwaysCook`, anders mist de packaged build 'm stilletjes (harde ConstructorHelpers-refs
  cooken vanzelf; runtime string-loads NIET). Al 4× misgegaan (gauge-iconen, settings-toggles/sliders,
  WBP_PauseMenu, Quinn-skin) — daarom bestaat `Tools/check-cook-parity.ps1`: scant de source+Data op
  `/Game`-paden en checkt elk pad tegen de staging-manifest van de laatste package-run. Ná elke package
  draaien, PASS verplicht vóór upload. Bij een nieuw kit-template/WBP in code: meteen cook-regel erbij.
- **PowerShell commits**: GEEN dubbele aanhalingstekens in commit-messages (native-arg quoting sloopt de
  heredoc → "pathspec did not match"). Herformuleer zonder \" \".
- **.NET-calls** (`[System.IO.File]::...`) gebruiken NIET de PowerShell-cwd — altijd absolute paden.
- **Zenserver**: stage-fout "Failed reading oplog from Zen / connection refused" = zenserver stopt
  zichzelf (sponsor-gekoppeld aan de cook-commandlet) vóórdat de stage-stap de oplog leest. OPGELOST
  (02-07): `bUseZenStore=False` in DefaultGame.ini [ProjectPackagingSettings] → loose cooked files,
  stage heeft geen zenserver meer nodig. Kill+retry was de oude halve fix — niet meer nodig.
- **UnrealClaude HTTP-API**: `task_result` geeft 400 op failed tasks → wrap python in try/except+traceback-print;
  `description` wordt de script-bestandsnaam (geen ".py"); content ASCII-schoon; verse FBX faalt soms op
  Interchange → `Interchange.FeatureFlags.Import.FBX 0`, legacy-import, terugzetten. Headless python-saves
  persisteren NIET — asset-werk in de OPEN editor.
- **Slate**: RoundedBox-OUTLINE rendert los van RenderOpacity → opacity-verbergen laat een spook-kader
  achter; altijd óók `Collapsed` zetten. `Collapsed→Visible` = her-layout-flits; `Hidden` in een Overlay niet.
- **Boot-loadingscreen in PreLoadMap van de allereerste map = flaky D3D12 PSO-race** (teruggedraaid);
  een nieuwe poging pas ná `OnFEngineLoopInitComplete`.
- **GC purget string-geladen ketens per LoadMap** → `UAssetKeepAliveSubsystem::Keep()` voor alles wat elke
  map-load opnieuw zou laden/compileren (skins, ABP's, WBP-klasses).
- **Graphics**: DistanceFieldShadowing = gedocumenteerde OOM-bron op deze map (altijd 0); GPU-Scene
  reserved-resources-blok in DefaultEngine.ini niet aanraken; hardware-RT/Lumen alleen via de opt-in-toggle.
- **Cash is een SPIEGEL van het economy-saldo** — nooit via RemoveItem/AddItem muteren, altijd via
  Economy AddMoney/RemoveMoney (reconcile hersteld de stapel anders = gratis geld).
- **Bij UHT/registry-loops**: statische actor-registries zijn per-PROCES → altijd `GetWorld()==W`-filter.

## Architectuur-spiekbriefje (waar woont wat)

| Systeem | Klasse / file (Source/WeedShopCore tenzij anders) |
|---|---|
| Beach-map-manager (deuren, baked rooms, crowd, joints-scatter, homes-registry, kamer-guards, comp-kamers, map-capture) | `ADoorRetrofitter` (World/DoorRetrofitter, ~5000 r.) |
| Alle telefoon/UI-acties + Server-RPC's (kopen, deals, delivery, roll, shelf, dev-cheats) | `UPhoneClientComponent` (Phone/, ~3000 r.) |
| Telefoon-UI (persistente app-panelen, prewarm, chat/bank/store) | `UPhoneWidget` (UI/PhoneWidget, ~3500 r. — werk met gerichte greps) |
| Palet, fonts, iconen, tooltips, tag-kleuren, gedeelde widget-helpers | `WeedUI::` (UI/WeedUiStyle) |
| Keuze-grids (picker-component) | `UWeedItemPickGrid` (UI/) |
| Save/load, start-flow, migraties | `USaveGameSubsystem` (Save/) + `UAssetKeepAliveSubsystem` (keep-alive) |
| Dag/nacht, UDS/weer, exposure, licht-budget, graphics-boot-toepassing | `ADayNightController` (World/) |
| Graphics-tiers/toggles (cvars, gates, persistentie) | `WeedShop_*`-functies in WeedShopCore.cpp/.h |
| NPC's (klant-statemachine, deals, activity) / spawning/routes | `ACustomerBase` / `ACustomerSpawner` (Customer/) |
| Berichten/afspraken/contacten | `UContactsComponent` (Phone/) |
| Economie (cash=spiegel, bank) / voorraad+hotbar+cash-mirror | `UEconomyComponent` (Economy/) / `UInventoryComponent` (Inventory/) |
| Plaatsen/bouwen (footprint-checks, no-build) | `UBuildComponent` (Placement/) + PropMeshKit.h (item-modellen) |
| Loading-flow (movie + cover + ready-vlaggen) | WeedShopCore.cpp (SWeedLoadingScreen) + `UBootCoverWidget` |
| Speler (input, texting-anim, skins, dev-keys) | `AThePlugSIMCharacter` (Source/ThePlugSIM) |

## Agent-orkestratie (het golven-recept)

- **Golf = parallelle edit-agents op DISJUNCTE file-clusters → één build-agent → commit.** Clusters vooraf
  op file-overlap checken; overlapt iets → zelfde agent. Edit-agents NOOIT zelf laten builden of processen killen.
- **Gedeelde API's letterlijk vastpinnen** in élke betrokken agent-prompt (exacte signatures/membernamen),
  anders bouwen parallelle agents nét-verschillende varianten. Build-agent instrueren om mismatches op de
  gepinde spec uit te lijnen.
- Prompts bevatten altijd: project-pad, harde regels (NIET builden, blijf in je files, welke files een
  ándere agent bezit), de persistente-UI/speler-regels, en "eind-antwoord = data (bondig, files, twijfels)".
- **Verkenning eerst, uitvoering daarna**: read-only analyse-agents leveren een plan (probleem → file:regel →
  fix → risico); de edit-agent krijgt dat plan 1-op-1. Analyse mag parallel aan ALLES.
- Faalt een agent op een API-storing (529): workflow hervatten met `resumeFromRunId` — geslaagde agents
  komen uit cache, alleen het gat draait opnieuw.
- Na elke golf de agent-rapporten LEZEN (incl. open_questions) — daar zitten vaak echte bugs/beslispunten in.
- Mid-run bijsturen kan via SendMessage naar een lopende agent (bv. als de speler een besluit aanscherpt).

## UnrealClaude (editor-werk): wanneer en hoe

**Beslisregel:** alles wat in C++/config/data kan → gewone tools (Read/Edit/build; "handle in code"). UnrealClaude
alleen voor werk dat ALLEEN in de editor kan of daar aantoonbaar beter is:
- **Asset-imports** (FBX/textures/DDS) — incl. de Interchange→legacy-fallback (zie valkuilen)
- **WBP's** bouwen/repareren: `unreal.WeedUiAuthoring.build_tree(wbp, json)`; kapotte GUID-refs/ensure-spam
  fixen = load + `unreal.BlueprintEditorLibrary.compile_blueprint` + save
- **AnimBP-graph-werk**: `unreal.ClaudeAnimAuthoring.setup_upper_body_blend('/pad/ABP')` (idempotent);
  nieuwe graph-authoring = extra C++-helper in de plugin schrijven (heeft UnrealEd/AnimGraph/BlueprintGraph als deps)
- **Asset-eigenschappen/metadata**: bounds meten, properties zetten + saven, DataTable-reimports
- **Visuele verificatie in de editor**: `capture_viewport` (screenshot) als een blik nodig is
Kan iets NIET via de HTTP-API (bv. beschermde interne graph-API's)? → niet opgeven: schrijf een kleine
BlueprintCallable C++-helper in de UnrealClaude-plugin en roep díé via python aan (bewezen route).

**Vaste sessie-flow (editor-start is duur — batch alle editor-taken in één sessie):**
1. Verzamel ALLE editor-klussen; C++-werk dat de plugin/helpers raakt eerst schrijven + builden
2. Game/editor dicht → volle editor starten → poll de API (submit `print`-ping naar
   `POST 127.0.0.1:3000/mcp/tool/execute_script`; submits queuen al vóór de editor klaar is)
3. Scripts draaien via `Tools/ue_run.ps1` (of handmatige POST + task_status/task_result-poll); elk script:
   try/except+traceback-print, ASCII-only, en **expliciet saven** (`save_asset`/`save_directory` — alleen
   saves in de OPEN editor persisteren)
4. Resultaat verifiëren in de output (bounds/OK-regels), editor dicht → dán pas builden/game-testen

## Diagnose-gereedschap (bewezen technieken)

- **Stille tijd-gaten**: `[BOOTMARK]`-Display-logs met `FPlatformTime::Seconds()-GStartTime` rond verdachte
  stappen; één boot geeft de attributie. Eerst meten, dan pas snijden.
- **Onbekende UI-elementen**: screenshot van de speler pixel-meten → midden van het element als fractie van
  de viewport = het anker → grep op die anker-waarden identificeert de widget exact.
- **Flaky crashes**: herhaal-boots in een loop (3-5×, per boot crash-count + proces-leeft + log-marker),
  variabelen één voor één terugdraaien. 2/3 crash-rate = race; kies de veilige terugdraai.
- **Rebuild/flash-verdenking in UI**: tijdelijke `[PDIAG]`-log op het rebuild-vs-toggle-besluitpunt; de
  speler doet één klik-rondje; de log vertelt exact wat herbouwt en waarom (bNew/dirty/stale).
- **Packaged smoke-test**: Shipping logt niet → start de .exe, wacht ~100s, check proces-leeft + geen
  CrashReportClient. Dat is de release-gate.

## Baselines (regressie = afwijking hiervan)

- Build (incrementeel): 10-60s. Boot → hoofdmenu-GameState: ~9-12s (dev; was 40s). Menu→wereld LoadMap:
  ~45s dev (grotendeels editor-only skinned-asset-compile; packaged is sneller). Frame: ~11ms / ~90fps
  (High, dag én nacht, stad=strand). Log-baseline: ~12 warnings (meer = iets stuk). Packaged zip (1K): ~1,8 GB.
- Pre-existing en te negeren: 2× C4996 `SetBrushSize`-deprecation (PlantInfoWidget/WeedUiStyle).

## Release-checklist (volgorde vast)

1. Alles gecommit + build groen + 3× boot-verificatie schoon
2. `Docs/PATCHNOTES.md` bijwerken (speler-taal, secties; versie bovenaan bumpt de in-game versie)
3. Cook-check: nieuwe string-geladen content in `DirectoriesToAlwaysCook`? BakedData in sync met Saved/?
4. UAT-package → **`Tools/check-cook-parity.ps1` (PASS verplicht — vangt stille cook-misses: alles wat
   we in editor/dev fixten moet AANTOONBAAR in de packaged build zitten)** → **packaged .exe smoke-test
   (PASS verplicht)** → `Tools/upload-build.ps1`
5. Release-URL verifiëren → roadmap afvinken → laatste commit → memory bijwerken

## Sessie-start (oriëntatie, 2 min)

1. `git log --oneline -10` + `git status` in het project → waar staat het werk?
2. `Docs/ROADMAP.md` → wat is afgevinkt, wat is het volgende blok, welke besluiten staan er al?
3. Draait er iets? (`Get-Process UnrealEditor`) — de speler kan mid-test zijn; kill nooit zomaar z'n sessie
   behalve voor een build, en meld dat vooraf.

## Werken met de speler

- Nederlands, kort en concreet. Testlijstjes als genummerde checklists, gegroepeerd per systeem.
- Vragen alleen bij écht speler-besluit (design-keuzes); technische keuzes zelf nemen met de veilige default
  en dat melden. Besluiten meteen vastleggen in ROADMAP/DECISIONS.
- Bij nieuwe notitie-dumps van de speler: eerst ALLES vastleggen in de roadmap (genummerd), dan gerichte
  vragen stellen, dan prioriteren in blokken — pas daarna bouwen.
- De speler test; jij verifieert wat automatisch kan (boots, logs, builds). Screenshots van de speler zijn
  goud: pixel-meten kan een widget exact identificeren (ankers/kleuren).

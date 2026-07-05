# Parallel-werken playbook — veel klussen tegelijk aanpakken (voor Codex)

> Dit is de **werkwijze**, niet de feitenlijst. Het doel: een grote bak wijzigingen snel én betrouwbaar
> afhandelen — zonder halve builds, zonder dat parallelle edits elkaar slopen, en zonder de speler per
> mini-fix te laten testen. Volg de volgorde; het verschil tussen een matige en een sterke sessie zit
> bijna altijd in deze methode, niet in kennis.

---

## 0. Grondhouding (geldt altijd)

1. **Meet vóór je fixt.** Nooit naar een oorzaak gokken. Lees de log (`Saved/Logs/ThePlugSIM.log`), zet
   desnoods een tijdelijke diagnose-log, en bewijs de oorzaak (log-regel / stack / git-diff / pixel-meting)
   vóór je een regel aanraakt. Diagnose-logs daarna weer weg.
2. **Root cause, geen pleisters.** Vraag bij elke fix: "hoe zou een AAA-studio dit doen?" Zwaar werk hoort
   in load-time/gespreid, niet als runtime-hitch. Nooit "kan niet" — zoek de echte-dev-route.
3. **Eind = bewezen, niet "waarschijnlijk".** Build groen ≠ klaar. Een fix is pas af als een meting het
   bevestigt (boot-test, log-marker, herhaalbare repro die nu slaagt).
4. **Meld eerlijk.** Testfout = zeggen mét output. Overgeslagen stap = zeggen. Winst pas claimen na meting.

---

## 1. De levenscyclus van één golf

Een "golf" = één ronde waarin je N losse klussen samen afrondt. Vaste volgorde:

```
Verzamel  →  Verken (read-only)  →  Cluster  →  Uitvoeren  →  Bouwen (1×)  →  Verifiëren  →  Committen  →  Testlijst
```

| Fase | Wat | Waarom |
|---|---|---|
| **Verzamel** | Schrijf ALLE klussen op als genummerde lijst (probleem → verdachte plek). | Overzicht; voorkomt dat je halverwege dingen vergeet. |
| **Verken** | Read-only: grep + lees de betrokken files. Lever per klus een mini-plan: `probleem → file:regel → fix → risico`. | Verkenning is gratis parallellisme (geen build, geen conflict). Een concreet plan maakt de edit-stap 10× betrouwbaarder. |
| **Cluster** | Groepeer de klussen op **disjuncte file-sets**. Overlappen twee klussen op een file → zelfde cluster. | Zo kunnen clusters zonder elkaar te raken tegelijk/achter elkaar worden bewerkt. |
| **Uitvoeren** | Bewerk per cluster de code. Nog NIET builden. | Één build aan het eind is sneller en vangt integratie-fouten in één keer. |
| **Bouwen** | Precies **één** build voor de hele golf. | Live Coding lockt de DLL; per-fix builden is traag en foutgevoelig. |
| **Verifiëren** | Build groen + gerichte boot/log-check op de risicovolle klussen. | Vangt regressies vóór de speler ze ziet. |
| **Committen** | Eén commit per afgeronde golf (build moet groen zijn). | Duidelijke history; makkelijk terugdraaien. |
| **Testlijst** | Eén genummerde checklist voor de speler, gegroepeerd per systeem. | De speler test in één rondje i.p.v. tien keer. |

---

## 2. Clusteren op disjuncte files — de kern van "veel tegelijk"

De enige veilige manier om werk te parallelliseren is op **file-eigendom**. Regels:

- **Één file heeft één eigenaar per golf.** Twee klussen die dezelfde file raken → samenvoegen tot één
  cluster, één bewerker. Nooit twee bewerkers op dezelfde file (ze bouwen nét-verschillende varianten).
- **Check op overlap vóór je begint.** Lijst per cluster de files. Zit een file in twee clusters → herverdeel.
- **Als je runtime parallelle subtaken kan draaien** (meerdere agents/threads): geef elk cluster aan één
  subtaak, elk met een prompt die de eigen files noemt én welke files een *ander* cluster bezit ("blijf hier
  vanaf"). **Kan je alleen serieel werken:** doe de clusters gewoon na elkaar — je houdt nog steeds de grote
  winst (één build, één commit, één testlijst).
- **Bewerkers builden NOOIT zelf** en killen geen processen. Bouwen/verifiëren is een aparte, laatste stap.

Voorbeeld-clustering (3 losse klussen):

```
Klus A: toast blijft onzichtbaar     → WeedToast.cpp/.h
Klus B: shop vult geen hotbar        → InventoryComponent.cpp/.h
Klus C: dubbele deal-meldingen       → PhoneClientComponent.cpp, CustomerBase.cpp
```
Geen file-overlap → 3 disjuncte clusters → parallel (of serieel) → 1 build → 1 commit.

---

## 3. Gedeelde API's letterlijk vastpinnen

Zodra twee clusters een **gedeelde functie/type** gebruiken (bv. een nieuwe helper), pin je de **exacte
signature** vast en zet je die in elk betrokken plan. Anders bouwt cluster A `Foo(int)` en cluster B roept
`Foo(int, bool)` aan → link-error of stille mismatch.

- Beslis de signature vóór het uitvoeren: `bool AddItem(FName, int32, float=-1, float=-1, bool bQuietOnFull=false)`.
- Nieuwe param altijd **achteraan met een default** → bestaande callers blijven compileren (backward-compat).
- Bij de build-stap: mismatches uitlijnen op de gepinde spec, niet op wat een los cluster toevallig deed.

---

## 4. Verkenning = gratis parallellisme

Onderzoek/audits/analyses hebben **geen file-conflict en geen build** → die mogen ALTIJD naast lopend
edit-werk. Gebruik dat:

- Verken eerst breed (meerdere greps/reads tegelijk), voer daarna gericht uit.
- Een read-only analyse levert een plan (`probleem → file:regel → fix → risico`); de bewerker krijgt dat
  plan 1-op-1. Dat is waar de betrouwbaarheid vandaan komt.
- **Adversariële verificatie** voor onzekere bevindingen: laat een tweede pass de claim proberen te
  **weerleggen** ("bewijs dat dit géén bug is"). Overleeft de claim dat, dan pas fixen. (Dit ving in de
  praktijk 5 van 8 "bugs" die loos alarm bleken — fallback-takken en local-only code.)

---

## 5. Bouwen & verifiëren (de gates)

**Build (editor/game MOET dicht — Live Coding lockt de DLL):**
```powershell
# 1) kill alles wat de DLL vasthoudt
Get-Process UnrealEditor,ThePlugSIM,UnrealEditor-Cmd,LiveCodingConsole,CrashReportClientEditor -EA SilentlyContinue | Stop-Process -Force
# 2) build
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ThePlugSIMEditor Win64 Development -project="<pad>\ThePlugSIM.uproject" -waitmutex
```
- Faalt de link met `LNK1104 cannot open ... .dll` → er draait nog een `CrashReportClientEditor.exe` /
  editor die de DLL vasthoudt. Kill die, build opnieuw.
- Pre-existing warnings die je mag negeren: 2× `C4996 SetBrushSize`-deprecation, `GetMovementBase`-deprecation.

**Game testen = LEAN standalone (NIET de volle editor):**
```powershell
UnrealEditor.exe "<pad>\ThePlugSIM.uproject" -game -windowed -resx=1600 -resy=900
```

**Verificatie-gates (build groen ≠ klaar):**
- Na risicowerk: proces leeft + geen `CrashReportClient` + log-markers kloppen.
- Bij een crash-fix: boot tot voorbij de crash-plek en check de log op `0` hits van
  `EXCEPTION_ACCESS_VIOLATION|Fatal error|Assertion failed`, plus dat de relevante marker verschijnt
  (bv. `Virtuele crowd: 70 wandelaars gespreid geseed` = de crowd-spawn overleefde).
- Flaky crash (1 op de N) = **release-blokkerend**. Bisect empirisch, kies bij twijfel de veilige terugdraai.

---

## 6. Committen (harde git-regels)

- **NOOIT `git add -A`.** Asset-packs blijven buiten git. Voeg alleen de specifieke gewijzigde *source*-files toe:
  ```bash
  git add "Source/.../A.cpp" "Source/.../A.h" ...
  ```
- Untracked en te negeren: `Content/`, `_IncomingKits/`, `AGENTS.md`, grote asset-mappen.
- **Geen dubbele aanhalingstekens in commit-messages** (PowerShell/native-arg quoting sloopt de heredoc →
  "pathspec did not match"). Gebruik een **Bash-heredoc**:
  ```bash
  git commit -F - <<'EOF'
  Korte titel in de tegenwoordige tijd

  - wat + waarom (speler-taal waar het kan)

  Co-Authored-By: <jouw agent-naam>
  EOF
  ```
- Eén commit per afgeronde golf; build moet groen zijn.

---

## 7. Anti-patronen (niet doen)

- ❌ Per mini-fix builden en de speler laten testen. → Batch in golven.
- ❌ Twee bewerkers op dezelfde file. → Zelfde cluster, één bewerker.
- ❌ Fixen op een gok. → Meet eerst (log/stack/diff).
- ❌ Pleister op runtime-hitch. → Verplaats zwaar werk naar load-time/gespreid.
- ❌ Ongeteste build naar de speler/release. → Boot-verifieer eerst.
- ❌ `git add -A` of dubbele quotes in de commit. → Zie §6.
- ❌ UI die op een klik `ClearChildren`+rebuild doet. → Persistente UI (in-place update / pool + sig-diff).
- ❌ Non-ASCII in `TEXT()`-literals (€, ë, —). → ASCII-only ("EUR", "-").

---

## 8. Projectspecifieke harde regels (schend nooit)

- **Persistente UI:** widgets 1× bouwen, daarna alleen in-place updates (`SetText`/`SetVisibility`/pool+sig-diff).
  Paneel-toggles = Overlay + `Hidden` (niet `Collapsed` — dat geeft een 1-frame her-layout-flits, én een
  `Collapsed` widget **tikt niet meer**: hij kan zichzelf nooit un-collapsen).
- **Co-op (listen-server) altijd meedenken:** wereld-mutaties via Server-RPC's (server-authoritative), UI
  client-side. Meldingen **per-speler** (Client-RPC/toast), nooit alleen `GetFirstPlayerController`. Een
  kaal proces-lokaal `Notify()` bereikt op een listen-server alleen de host → gebruik per-pawn-routing
  (`NotifyPawn` per verbonden pawn) voor co-op-brede meldingen. Statische registries/caches: altijd
  `GetWorld()`-filteren (per-proces!).
- **NPC's/markers smooth:** posities/movement per frame; alleen LOGICA mag gethrottled.
- **Cook (packaged build):** string-geladen content (`LoadObject`/`LoadClass`-paden, runtime-PNG's, nieuwe
  kit-templates/WBP's) MOET in `DirectoriesToAlwaysCook`, anders mist de packaged build 'm stilletjes.
- **Code-stijl:** Nederlandse comments, tabs, bestaande stijl exact volgen. ASCII-only in `TEXT()`-literals.
- **Getallen:** hash/seed-afgeleide indices nooit met signed-`int32 * const` in een array-modulo → cast eerst
  naar `int64`/`uint` (anders overflow → negatieve index → out-of-bounds → crash).

---

## 9. Uitgewerkt voorbeeld (echte golf)

**Opdracht:** "toasts komen dubbel, hotbar vult niet, en toasts staan te kort."

1. **Verzamel:** (a) dubbele deal-toasts, (b) dubbele inventory-vol-toasts, (c) hotbar-autofill, (d) duur.
2. **Verken:** grep op `NotifyPawn`/`AddItem` → bron van elke dubbele gevonden (deal-toast + popup;
   AddItem-toast + caller-toast). Plan per klus opgeschreven met `file:regel`.
3. **Cluster:** A=WeedToast, B=InventoryComponent(+WorldItemPickup), C=PhoneClientComponent(+CustomerBase).
   Disjunct. Gedeelde API vastgepind: `AddItem(..., bool bQuietOnFull=false)`.
4. **Uitvoeren:** guards + de nieuwe param toegevoegd; geen build tussendoor.
5. **Bouwen:** editor dicht → 1 build → groen (alleen de bekende C4996-warnings).
6. **Verifiëren:** game lean gestart, log schoon, proces leeft.
7. **Committen:** alleen de 6 source-files, Bash-heredoc, Co-Authored-By.
8. **Testlijst:** 5 genummerde checks voor de speler, per systeem gegroepeerd.

Resultaat: vier losse klachten in één golf afgerond, één keer laten testen.

---

### Sessie-start (2 min oriëntatie)
1. `git log --oneline -10` + `git status` → waar staat het werk?
2. `Docs/ROADMAP.md` (source of truth) → wat is af, wat is het volgende blok, welke besluiten staan er?
3. Draait er iets? (`Get-Process UnrealEditor`) — de speler kan mid-test zijn; kill nooit zomaar z'n
   sessie behalve voor een build, en meld dat vooraf.

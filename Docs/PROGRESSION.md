# PROGRESSION — ThePlugSIM

> Visuele progressie-referentie: fases, XP/levels en — de kern — **per level wat je ontgrendelt**, als swimlanes + tech-tree i.p.v. lange lijsten.
>
> Laatst bijgewerkt: 25 juni 2026
>
> Unlock-levels komen rechtstreeks uit de code (`StoreComponent::RequiredLevelFor()`) en zijn vastgelegd. Alleen per-strain detail — exacte THC%, yields, groeitijden, zaadprijzen en eventuele `UnlockLevel`-overrides — zit binair in `/Game/_Project/Data/DT_Strains.DT_Strains` en is niet als tekst leesbaar.

---

## 📊 In één oogopslag

```
╔═════════════════════════════════════════════════════════════════════════╗
║                    ThePlugSIM · PROGRESSIE-DASHBOARD                    ║
╠═════════════════════════════════════════════════════════════════════════╣
║  MAX LEVEL ........ 100                                                 ║
║  SHOP-LICENTIE .... level 50   →  bShopLicensed = true (eenmalig)       ║
║  XP-FORMULE ....... XPForLevel = 100 + (Lvl-1) × 40   (lineair, +40/lvl)║
║  XP tot lvl 50 .... ~52.000                                             ║
║  XP tot lvl 100 ... ~204.000                                            ║
║                                                                         ║
║  XP-BRONNEN                                                             ║
║   • verkoop ....... ~1 XP per €5 omzet (verkoopwaarde, niet gram)       ║
║   • oogsten ....... 10 XP/plant + 1 XP/gram                             ║
║   • multiplier .... 1,0x -> 1,5x (stoned-bonus op THC%)                 ║
╚═════════════════════════════════════════════════════════════════════════╝
```

Bron: `ULevelComponent` (`LevelComponent.h:21-22`, `.cpp:22-28/66-75`). De "fases" zijn een ontwerp-laag bovenop één boolean (`bShopLicensed`) + de XP-curve — er is géén enum `IllegalStreetDealer`/`LegalCoffeeshop`.

---

## 🎮 Fases

```
  FASE 1 — Illegale straatdealer            🏪          FASE 2 — Legale coffeeshop
  ════════════════════════════════════      LVL         ═══════════════════════════
  lvl 1 ■■■■■■■■■■■■■■■■■■■■■■■■■■■ 49        50          50 ░░░░░░░░░░░░░░░░░░░░ 100
  cultivation + straatverkoop          SHOP-LICENTIE     winkel runnen (gereserveerd)
  ▶ SPEELBAAR · in finetune            bShopLicensed     ▶ NOG TE ONTWERPEN · 51-100 leeg
```

**Wat verandert er op level 50** (`LevelComponent.cpp:66-75`):
- `bShopLicensed` → `true` (eenmalig, gerepliceerd + opgeslagen).
- Gouden toast (kleur `255,215,0`), 9 sec: *"SHOP LICENSE EARNED! Level 50 reached - you can now run a legit weed shop."*
- Ontgrendelt de legale weedshop-gameplay — die fase is momenteel nog gereserveerd voor toekomstige content.

---

## 📈 XP-curve

`XPForLevel(Lvl) = 100 + (Lvl-1) × 40` — **lineair, +40 XP per level**. Bar = XP nodig binnen dat level.

```
 lvl   XP/level
   1 │█▏                                  100
  10 │██████▏                             460
  20 │████████████▏                       860
  30 │██████████████████▏               1.260
  40 │████████████████████████▏         1.660
  50 │██████████████████████████████▏   2.060   ◄ shop-licentie
  60 │████████████████████████████████████▏        2.460
  70 │██████████████████████████████████████████▏  2.860
  80 │████████████████████████████████████████████████▏       3.260
  90 │██████████████████████████████████████████████████████▏ 3.660
 100 │███████████████████████████████████████████████████████████████▏ 4.020 → daarna 0
```

```
 Cumulatief:   lvl 1 ●───────────────● lvl 50 ───────────────────● lvl 100
                  0           ~52.000  XP                  ~204.000 XP
```

> **De curve is kwadratisch** — lvl 50 valt al op **~52.000 XP** (≈¼ van de ~204.000 totaal); de tweede helft (50→100) kost **~152.000 XP**, ~3× de eerste. De shop-licentie ligt dus vroeg in de grind.

Hulp-functies (UI/HUD): `GetCurrentXP()` · `GetXPToNext()` (= `XPForLevel(Level)`) · `GetLevelFraction()` (0.0–1.0, geclamped). `Level`/`CurrentXP`/`bShopLicensed` zijn gerepliceerd; `RestoreLevel()` herstelt zonder level-up-events, `GrantLevel()` is test/cheat.

> **XP-formules (correct):** verkoop-XP = `5 + (Total_cents / 500)` ≈ **1 XP per €5 omzet** (€500-deal ≈ 105 XP) · oogst-XP = **10/plant + 1/gram** · alles × stoned-multiplier (1,0–1,5×).

---

## 🧭 Unlock-matrix (track × level)

De consolidatie: per **track** (rij) zie je wat er in elke **level-fase** (kolom) ontgrendelt. Het cijfer vóór elk item = het exacte unlock-level (①=lvl 1 … ㊿=lvl 50). Bron: `StoreComponent::RequiredLevelFor()` (`:340-464`).

| Track | 1–10 | 11–20 | 21–30 | 31–40 | 41–50 |
|---|---|---|---|---|---|
| 🌱 **Pots** | ①Broken ③Clay ⑩Plastic | – | ㉔Fabric | – | – |
| 💧 **Water** | ①Plastic ⑥Steel | ⑮Jerrycan | ㉘Tank | – | – |
| 🌫️ **Droogrek** | ①Cheap | ⑫Std ⑫Fan ⑱Seal | ㉖Pro | – | – |
| ✊ **Hash (mesh)** | – | ⑭Cheap | ㉓Std ㉙Pro | – | – |
| 🗜️ **Persen** | – | ⑯Cheap | ㉕Std | ㉛Pro | – |
| 🧪 **Concentraten** \* | – | – | ㉚Olie | ㉞Moon ㊵Rosin | ㊻Ice |
| 🍪 **Edibles** | ⑨ Butter+Oven+Pan+Fridge | – | – | – | – |
| 📦 **Opslag** | ⑤Chest ⑧Shelf | – | – | – | – |
| 🔒 **Kluizen** | ⑥€10k | ⑱€50k | ㉚€250k | – | ㊷€1M |
| 📜 **Vloei** | ①Small ④Big | ⑪Blunt | ㉒Backwoods | – | – |
| 🥡 **Containers** | ①Bag2 ⑤Bag5 ⑥Jar10 | ⑫Jar15 ⑳Block100 | – | ㉜Garbage500 | – |
| 🛠️ **Pak-bench** | ①Pack | ⑲Pack2 | – | ㉟Pack3 | – |
| 🍳 **Oven/Pan** | ⑨Std | – | ㉑Pro | – | – |

\* Concentraten Pro-varianten: `Oil_Pro` 36 · `Moon_Pro` 38 · `Rosin_Pro` 44 · `Iso_Pro` 48.

---

## 🏭 Productie-keten

```
                          🌿 VERSE WIET (oogst)
                                  │
                                  ▼
                           🌫️ DROGEN (droogrek)
                                  │
        ┌─────────────────┬───────┴────────┬──────────────────────┐
        ▼                 ▼                 ▼                      ▼
   📜 JOINTS          🍪 EDIBLES       ✊ HASH-KETEN          🧪 CONCENTRATEN
   (vloei)            (lvl 9)          (lvl 14+)             (high-end)
        │                 │                 │                      │
   Papers tiers:     gedroogd          kristallen (mesh)      ㉚ Oil_Std (olie)
   ①Small            └▶oven→ BAKED     ──press──▶ HASH               │
   ④Big                 │                                            ▼
   ⑪Blunt            +Butter(9)        3-TIER MESH→PRESS        ㉞ Moon_Std
   ㉒Backwoods       pan→CANNABUTTER   ┌────────────┐          (gedroogd+olie
                        │              │ 14 mesh    │           → MOONROCKS)
                     fridge(9)         │ 16 press   │
                        ▼              ├────────────┤          ㊵ Rosin_Std (40)
                     🍪 EDIBLE         │ 23 mesh    │          (solventless,
                     (géén kit nodig)  │ 25 press   │           hoge THC)
                                       ├────────────┤
                                       │ 29 mesh    │          ㊻ Iso_Std (46)
                                       │ 31 press   │          (bubble/ice hash,
                                       └────────────┘           top THC)
```

Hash-keten in 3 fases: instap **14–16** · mid **23–25** · high **29–31**. Balans-open (ROADMAP 2A.3/2B.4): concentraten-rendement checken en eerste stap eerder (~lvl 22-26) zetten.

**Edible-ingrediënten:** Butter · Flour · Sugar · Gelatin — allemaal op **lvl 9** (€3,00 / €2,00 / €1,50 / €2,50) → cookies & gummies via de koelkast-keuken (samen met de edibles-gear).

---

## 🌱 Kweek-mechanics (kwaliteit · yield · drogen · ziektes)

> De grow-loop in één plek: van zaad → groeifase → kwaliteit (CareAvg) → oogst-yield → drogen → verse/droge bud. **Alle tunables staan hier expliciet** zodat je kunt balanceren. Bron: `GrowPlant.cpp` / `DryingRack.cpp` / `PotTypes.cpp` / `SoilTypes.cpp`. Per-strain `GrowMinutes`, `BaseYieldGrams` en `BaseThcPercent` zitten binair (?, staat in `DT_Strains`).

### 🌿 Groeifases & groeitijd

Fase = fractie `F = SlotGrowth / SlotMaxSeconds` (`GrowPlant.cpp:480-496`). `SlotMaxSeconds = GrowMinutes × 60` (`:255-262`, `GrowMinutes` = ?, staat in `DT_Strains`).

```
 F=0.00 ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 1.00  →  oogstbaar (F ≥ 1.0)
        │Seedling│ Vegetative │PreFlower│ Flower │
         <0.15     0.15-0.45   0.45-0.70  0.70-1.0
```

Groei-snelheid per tick (`:352-358`):
```
SlotGrowth += dt × Speed
Speed = GrowthSpeedMultiplier × (1 + GrowthBonus) × LampMul
```
| Factor | Waarde | Bron |
|---|---|---|
| `GrowthSpeedMultiplier` | **1.0** (TUNABLE, EditAnywhere) | `GrowPlant.h:95` |
| `GrowthBonus` | 0.0 (global upgrade-effect) | UpgradeComponent |
| `LampMul` | geen 1.00× · Lamp I 1.15× · II 1.30× · III 1.50× | `:349-351` / `PotTypes.cpp:86-94` |

> ⚠️ Een **zieke** plant (afflict ≠ 0) groeit NIET tot 'ie genezen is (`:356`).

### 💧 Kwaliteit-formule (CareAvg = wat je oogst)

`CareAvg` = tijd-gewogen gemiddelde "verzorging" over de hele groei → dít is de quality 0..1 bij oogst (`GrowPlant.h:134-144`). Stijgt tijdens groei, daalt alleen bij rot.

```
─ WATER ─────────────────────────────────────────────────────
WaterAll() bijvullen           +0.60 water/beurt   (:736)
Water-decay (geen auto-water)  −0.02/sec × (1 − CareRetention×0.5) × LeakMul   (:370-372)
  LeakMul = 0.5 met Insulation (bit1) · anders 1.0
Auto-water I/II (bit 9/10)     WaterLevel = 1.0 (altijd vol)
─ CARE-MULTIPLIER (momentaan, "natheid") ────────────────────  (:376-388)
Target = (Water ≥ 0.25) ? MaxCare : 0.12
Care   = Clamp( FInterpTo(Care, Target, dt, 0.12), 0.05, MaxCare )
─ KWALITEIT (geaccumuleerd) ──────────────────────────────────  (:385-387)
CareSum += Care × dt ; CareTime += dt ; CareAvg = CareSum / CareTime
```

**MaxCare (CareCap per pot)** — `:877-896`, basis uit `PotTypes.cpp:8-12`, opgehoogd met upgrades, eind-clamp **[0.40, 1.00]**:
```
Cap = Pot.CareCap
    + CareRetention × 0.3          (global upgrade, clamp 0..0.9)
    + 0.10  als Drainage (bit0)
    + Tent  0.08 / 0.15 / 0.22     (bit 6 / 7 / 8 = Tent I / II / III)
```
| Pot | CareCap (basis) | Slots |
|---|---|---|
| `Pot_Broken` | 0.55 | 1 |
| `Pot_Clay` | 0.70 | 1 |
| `Pot_Plastic` | 0.85 | 2 |
| `Pot_Fabric` | 1.00 | 6 |

### 🌾 Yield-formule

```
YieldGrams = max(1, round( BaseYieldGrams × CareQ × SoilYield × PotYield × FertYieldMult ))   (:770)
```
| Term | Waarden | Bron |
|---|---|---|
| `BaseYieldGrams` | per-strain (?, staat in `DT_Strains`) | — |
| `CareQ` | `Clamp(CareAvg,0,1)` ← de kwaliteit hierboven | `:768` |
| `SoilYield` | Basic 1.00× · Rich 1.25× · Premium 1.50× | `SoilTypes.cpp:7-9` |
| `PotYield` | Broken 0.90× · Clay 1.00× · Plastic 1.10× · Fabric 1.25× | `PotTypes.cpp:8-12` |
|  | ×1.2 als Bloom booster (bit2) · ×1.1 als Auto-water II (bit10) | — |
| `FertYieldMult` | géén 1.00× · Growth 1.15× · Bloom 1.30× (reset na oogst) | `:659` / `:824` |

> **Soil-harvests:** Basic 3 · Rich 4 · Premium 6 oogsten per zak (`SoilTypes.cpp:7-9`).

### 🧪 THC% bij oogst (kwaliteits-mix)

```
GearQ  = Clamp( 0.35·CareQ + 0.25·SoilQ + 0.22·PotQ + 0.18·FertQ , 0, 1 )   (:777-789)
ThcRaw = BaseThcPercent × GearQ × FRand(0.94 … 1.06)        ← variantie-ruis
ThcPercent = round( min(46, max(BaseThc×0.15, max(1.0, ThcRaw))) )   ← softcap 46%
```
| Component | Weging | Waarden |
|---|---|---|
| Care | **35%** | `CareQ` (0..1) |
| Soil | **25%** | Basic 0.50 · Rich 0.75 · Premium 1.00 |
| Pot | **22%** | Fabric 0.85 · Plastic 0.70 · Clay 0.55 · overig 0.40 · +Drainage 0.04 · +Tent I/II/III 0.04/0.07/0.11 · clamp [0.40,1.0] |
| Fert | **18%** | `Clamp(0.6 + (FertMult−1), 0.6, 1.0)` |

`BaseThcPercent` = per-strain (?, staat in `DT_Strains`). Oogst maakt `WetBud_<strain>` met `QualityPct = round(max(5, CareQ × SoilQuality × 100))` (`:790-797`).

### 🌫️ Drogen (verse bud → droge, verkoopbare bud)

Oogst geeft `WetBud_<strain>`; het droogrek zet dat om naar `Bud_<strain>` (`GrowPlant.cpp:794`, `DryingRack.cpp:250`). **Gram & THC blijven gelijk; alleen kwaliteit kan zakken bij over-drogen.**

| Rek | Capaciteit | DrySeconds (basis) | Bron |
|---|---|---|---|
| `DryRack_Cheap` | 2 | 180 s | `DryingRack.cpp:18-22` |
| `DryRack_Std` | 5 | 120 s | — |
| `DryRack_Pro` | 10 | 60 s | — |

```
DrySeconds_effectief = basis × UpSpeedMult
  UpSpeedMult = ×0.7 per DryUp_Fan in de buurt (30% sneller)       (:198)

OVERDRY (kwaliteitsverlies als je 'm laat hangen):                 (:163-167)
  grace = 60 s na klaar (:24)  ·  decay-window = 600 s (:27)  ·  max loss = 70% (:28)
  LossFrac = Clamp((OverTime − 60) / 600, 0, 1) × 0.70
  DryUp_Seal in de buurt  →  LossFrac = 0  (0% verlies)            (:165)
  OutQual = max(1, Quality × (1 − LossFrac))                       (:260)
```
> Upgrade-detectie scant elke **0.5 s** (`:179`) binnen **175 cm horizontaal / 280 cm verticaal** van het rek (`:175-202`).

### 🦠 Ziektes — schimmel & ongedierte (`GrowPlant.cpp:425-477`)

| | 🍄 Schimmel (mold) | 🐛 Ongedierte (pest) |
|---|---|---|
| Verschijnt vanaf | crew-level **12** (`:435`) | crew-level **18** (`:436`) |
| Geneest met | `Spray_Fungicide` (lvl 11, €25,00) of `Spray_Broad` (lvl 33, €60,00) | `Spray_Pesticide` (lvl 17, €25,00) of `Spray_Broad` |
| Type-id | 1 | 2 (50/50 als beide unlocked) |

```
Kans per tick (:465-466):
  Risk = 0.00009 × (0.5 + 1.2 × (1 − Clamp(Care,0,1)))     ← perfecte zorg ≈0.5× · verwaarloosd ≈1.7×
  infecteren als  FRand() < Risk × dt
Timeline:
  ┌─ grace 180 s ─────────────┬─ death 150 s ───────────────┐
  │ plant STOPT met groeien,  │ langzame dood, daarna:       │
  │ wél te genezen (1× spray) │ PLANT DOOD = zaad weg (100%) │
  └───────────────────────────┴──────────────────────────────┘
  totaal tot dood = 330 s (5,5 min)   (:437-438, :451-459)
```
> Op tijd sprayen = **géén** kwaliteits-/yield-straf, alleen gestopte groei (`:356`). Te laat = totale verlies (`:838`). Genezen verwijdert de afflict en hervat de groei (`:635/:645`); elke spray is **1× gebruik** (`:643`). Spray-prijzen/unlocks: `StoreComponent.cpp:219-221/429-431`.

### ⏬ Over-rijpheid (oogstbaar maar niet geplukt) — `:389-401`

```
BulkRate = 0.90 / (240 × max(0.25, RotBulkFactor×0.5))     RotBulkFactor=2.0 → 0.00375/s (~4 min tot 10%)
SlowRate = 0.10 / (180 × max(0.25, RotSlowFactor×0.33))    RotSlowFactor=3.0 → 0.00056/s (~3 min tot dood)
Care -= dt × (Care > 0.10 ? BulkRate : SlowRate)   ·   Care ≤ 0 ⇒ plant dood, zaad weg
```

### 🎛️ Tunable-constants — kort lijstje (zet híer aan om te balanceren)

| Constant | Waarde | Bron |
|---|---|---|
| `GrowthSpeedMultiplier` | **1.0** | `GrowPlant.h:95` |
| `RotBulkFactor` / `RotSlowFactor` | **2.0 / 3.0** | `GrowPlant.h:101/104` |
| Water-decay basis | **0.02/s** | `GrowPlant.cpp:371` |
| Water-bijvulling per beurt | **+0.60** | `:736` |
| Wet/dry-drempel water | **0.25** | `:381` |
| Care-interp-snelheid · bounds | **0.12** · [0.05, 1.0] | `:382` |
| Dry-target care | **0.12** | `:381` |
| CareCap eind-clamp | **[0.40, 1.00]** | `:877-896` |
| Affliction basis-kans | **0.00009/s** | `:441` |
| Affliction grace · death | **180 s · 150 s** | `:437-438` |
| Mold · pest unlock-level | **12 · 18** | `:435-436` |
| Drogen grace · window · max-loss | **60 s · 600 s · 70%** | `DryingRack.cpp:24/27/28` |
| Fan-speed-mult | **0.7×** | `DryingRack.cpp:198` |
| THC-softcap (oogst) | **46%** | `GrowPlant.cpp:789` |

---

## 🏭 Verwerkings-rendement

> De volledige conversie-economie: 9 machine-ketens, elk Cheap/Std/Pro. **Yield** = gram-uit ÷ gram-in. **THC×** = vermenigvuldiger op het THC%, daarná **hard gecapt op 90%** (`ProcessorMachine.cpp:287`). Bron: `ProcessorMachine.cpp:70-100` (defs). Per-product marktprijs = ?, staat in `DT_Products` (enum-categorie, géén binaire flags).

```
                              🌿 DROGE BUD  (Bud_<strain>)
                                       │
   ┌──────────┬──────────┬─────────────┼───────────┬──────────┬──────────┐
   ▼          ▼          ▼             ▼           ▼          ▼          ▼
 ✊ MESH    🥖 OVEN    🛢️ OIL-PERS   🌹 ROSIN    🧊 ISO     (Bud+Oil)   …
   │          │          │             │           │          ▼
   ▼ Crystal  ▼ Baked    ▼ Oil ───────┐│           ▼ Bubble  🌑 MOON
 🗜️ PRESS   🍳 PAN      coats moonrock││         (ice hash)  (moonrocks)
   ▼ Hash     ▼ ButterMix              │
            ❄️ FRIDGE  ◄─ Mix+Sugar(±Flour/Gelatin)
              ▼ Edible / Cookie / Gummy
```

### Per-machine conversie-tabel

| Machine | Keten (in → uit) | Tier | Batches | Tijd | Yield | THC× | Koop € | Bron |
|---|---|---|---|---|---|---|---|---|
| ✊ **Mesh** | Bud → Crystal | Cheap | 1 | 60 s | 15% | 1.80× | 30,00 | `:70` |
| | | Std | 2 | 45 s | 20% | 2.00× | 60,00 | `:71` |
| | | Pro | 3 | 30 s | 28% | 2.20× | 110,00 | `:72` |
| 🗜️ **Press** | Crystal → Hash | Cheap | 1 | 90 s | 60% | 1.10× | 50,00 | `:74` |
| | | Std | 2 | 70 s | 70% | 1.20× | 90,00 | `:75` |
| | | Pro | 3 | 50 s | 85% | 1.30× | 160,00 | `:76` |
| 🥖 **Oven** | Bud → Baked (decarb) | Std | 2 | 40 s | 92% | 1.15× | 70,00 | `:81` |
| | | Pro | 4 | 25 s | 96% | 1.15× | 160,00 | `:85` |
| 🍳 **Pan** | Baked + Butter → ButterMix | Std | 2 | 55 s | 88% | 1.25× | 110,00 | `:82` |
| | | Pro | 4 | 35 s | 94% | 1.25× | 180,00 | `:86` |
| ❄️ **Fridge** | ButterMix + Sugar (±Flour/Gelatin) → Edible/Cookie/Gummy | Std | 4 | 180 s | 100% | 1.55× | 40,00 | `:83` |
| | | Pro | 8 | 110 s | 100% | 1.55× | 110,00 | `:87` |
| 🛢️ **Oil** | Bud → Oil | Std | 1 | 80 s | 16% | 1.90× | 80,00 | `:93` |
| | | Pro | 2 | 60 s | 22% | 2.00× | 220,00 | `:94` |
| 🌑 **Moon** | Bud + Oil → Moonrock | Std | 2 | 60 s | 70% | 1.30× | 120,00 | `:95` |
| | | Pro | 4 | 45 s | 80% | 1.40× | 280,00 | `:96` |
| 🌹 **Rosin** | Bud → Rosin (solventless) | Std | 1 | 75 s | 18% | 1.85× | 70,00 | `:97` |
| | | Pro | 2 | 55 s | 24% | 1.95× | 200,00 | `:98` |
| 🧊 **Iso** | Bud → Bubble (ice/bubble hash) | Std | 1 | 95 s | 14% | 2.05× | 90,00 | `:99` |
| | | Pro | 2 | 70 s | 20% | 2.20× | 240,00 | `:100` |

> `Oven`/`Pan`/`Fridge` hebben géén Cheap-tier. `Oil`/`Moon`/`Rosin`/`Iso` zijn `bOutIsPress=true` (concentraat-type). `Oil` is een **tussenproduct** (coat voor moonrocks, niet los verkocht). Unlock-levels: zie de Unlock-matrix hierboven (`Oil_Std` 30 / `Pro` 36 · `Moon_Std` 34 / `Pro` 38 · `Rosin_Std` 40 / `Pro` 44 · `Iso_Std` 46 / `Pro` 48).

### 🔧 Verwerk-upgrades & THC-cap

| Upgrade | Effect | Bron |
|---|---|---|
| `ProcUp_Motor` | snelheid **×0.7** (30% sneller), vereist nabijheid | `:131` |
| `ProcUp_Yield` | output **×1.3** (30% meer), via `UpYieldMult` | `:132/286` |
| **THC-cap** | `E.Thc = min(90, (Thc>0 ? Thc : 15) × ThcMult)` — hard op **90%**, default-THC 15% | `:287` |

### 📦 Pak-bench (geen conversie — verpak-tool)

| Bench | Containers per actie | Bron |
|---|---|---|
| `Bench_Pack` | 1 | `PackBench.cpp:102` |
| `Bench_Pack2` | 3 | `:103` |
| `Bench_Pack3` | 6 | `:108` |

### 🧮 Uitgewerkte ketens (100 g @ 20% THC)

```
✊ HASH (beste ratio):   Mesh_Pro 100g→28g @44%  →  Press_Pro 28g→23,8g @57,2%
🍪 EDIBLES (richt cap):  Oven_Pro 100g→96g @23%  →  Pan_Pro 96g→90,2g @28,75%  →  Fridge_Pro →90,2g @44,6% (richting 90%-cap)
🌑 MOONROCK (volume):    Oil_Pro 100g→22g @40% (olie)  +  Moon_Pro 100g+olie→80g @28%
🧊/🌹 CONCENTRAAT:        Iso_Pro 100g→20g @44%   ·   Rosin_Pro 100g→24g @39%
```
> Patroon: **hash** = beste THC-ratio (press stapelt), **edibles** = duwt richting de 90%-cap, **moonrock** = veel gram / lagere THC, **iso/rosin** = compacte high-THC concentraten. Balans-open: concentraten-rendement vs baggies/uur checken (ROADMAP 2A.3) — zie ook de Productie-keten-sectie.

---

## 💰 Geld verdienen (deal-economie)

> Eén inkomstenbron: **directe verkoop aan NPC-klanten**. Geen passief inkomen, geen bezorgloon, geen fooien — `FActiveDelivery` bestaat voor de gameplay maar betaalt niks uit. Drugs kun je **alleen aan NPC's** kwijt, nooit aan de leverancier.

```
╔══════════════════════════════════════════════════════════════════════╗
║                   ThePlugSIM · GELD-DASHBOARD                          ║
╠══════════════════════════════════════════════════════════════════════╣
║  START-CASH ........ €100      (StartingBalanceCents = 10.000)         ║
║  START-BANK ........ €0        (StartingBankCents = 0)                 ║
║  DEAL-OPBRENGST .... AskPrijs/gram × verkochte grammen                 ║
║  BUDGET-CAP ........ €20/gram  (BudgetCentsPerUnit = 2000) → erboven    ║
║                                 onderhandelt de klant (Haggle)         ║
║  VIP-ORDER-BONUS ... +(BonusMult−1)  meestal +20…+40% (?, in DT_*)     ║
║  SUPPLIER-BUYBACK .. 70% v/d koopprijs — ALLEEN zaden/supplies         ║
║  DRUGS BUYBACK ..... €0  (Bud_/Bag_/Joint_/Hash_/Edible_/Rosin_/…)     ║
╚══════════════════════════════════════════════════════════════════════╝
```

Bron: `CustomerBase.cpp:3753-3777` (uitbetaling) · `WeedDealLibrary.cpp:3-26` (acceptatie) · `StoreComponent.cpp:262-289` (buyback) · `EconomyComponent.h:36-40` (start-cash).

### De deal-waarde-formule

```
Total = AskPriceCentsPerUnit × SoldGrams                       (CustomerBase.cpp:3753)
   └─ VIP-order vervuld? +OrderBonus = Total × (BonusMult−1)   (:3761)
PayTo->AddMoney(Total)  +  NoteLegitIncome(Total)              (:3768-3769)
```

Hoeveel je per gram **mag** vragen hangt niet aan een vaste prijs maar aan de **acceptatiekans** — vraag te veel en de klant koopt niet (of onderhandelt). De kans wordt opgebouwd uit prijs, klant-stats, kwaliteit en THC:

| Stap | Formule | Effect | Bron |
|---|---|---|---|
| **Prijs-ratio** | `Base = 70 − (Ask/Markt − 1)×100` | marktprijs ⇒ 70% · half ⇒ 120% · 1,5× ⇒ 20% | `WeedDealLibrary.cpp:11-12` |
| **Relatie-stats** | `+ Respect×0,15 + Loyalty×0,15 + Addiction×0,25` | verslaving weegt 't zwaarst | `:13` |
| **Kwaliteit** | `+ (Q − 0,60) × 55 × (1 − Addiction/100)` | 60% = neutraal; niet-verslaafden pietluttiger | `:18-22` |
| **THC-willingness** | `+ Clamp((OfferTHC − ExpectTHC) × 2,5, 0, 45)` | +2,5%/1% THC boven verwacht, cap +45% | `CustomerBase.cpp:3519-3524` |
| **Tier-korting** | `EffAsk = Ask × (1 − TierTolerance)` | hogere tier "voelt" je prijs lager (zie klant-tabel) | `:3568-3576` |
| **Budget-cap** | `Ask > BudgetCentsPerUnit(2000)` ⇒ Haggle | klant betaalt nooit > €20/gram, gaat onderhandelen | `:3713` |
| **Beslissing** | `Accept als Random(0,100) ≤ min(100, Base+THCbonus)` | — | `:3574-3576` |

`ExpectTHC` valt terug op **15%** als onbekend (?, exacte verwachting per strain staat in `DT_Strains`). De marktprijs per product staat binair in `DT_Products` (?, via `GetMarketPriceCents`).

### Relatie-groei per geslaagde deal (`CustomerBase.cpp:3603-3620`)

```
dRespect   = (1,00 − PrijsRatio) × 6  + (Q − 0,5) × 3      eerlijke prijs ⇒ +respect
dLoyalty   = (1,15 − PrijsRatio) × 7  + (Q − 0,5) × 4      substituut ⇒ ×0,6
dAddiction = 0,5 + (THC%/100) × 11                         hoge THC ⇒ +verslaving
```
Over-levering (meer grammen dan gevraagd, door hele zakjes): +0,4 respect / +0,6 loyaliteit per extra gram, **hard gecapt op 4 grammen** zodat je niemand instant maxt (`:3788-3800`).

### 🧮 Uitgewerkt voorbeeld — Regular-klant (Tier 2)

```
Klant   : Respect 50 · Loyalty 40 · Addiction 25
Product : Bag_Kush · kwaliteit 70% · THC 18%   (markt €18/g aangenomen)
Vraag   : 10 g @ €15/gram (1500 cents)

① Tier-2-korting 6%  → EffAsk = 1500 × 0,94 = 1410 cents
② Prijs-ratio        → 1410/1800 = 0,783 → Base = 70 − (−21,7) = 91,7%
③ Stats              → +50×0,15 +40×0,15 +25×0,25 = +19,75  → 111,4%
④ Kwaliteit          → (0,70−0,60)×55×(1−0,25) = +4,1%      → 115,5%
⑤ THC                → (18−20)×2,5 = −5 → clamp 0           → +0%
   ────────────────────────────────────────────────────────────────
   Acceptatie = min(100, 115,5) = 100%  → ALTIJD raak

💶 Uitbetaling: 1500 × 10 = 15.000 cents = €150
📈 Stats:  Respect +1,9 · Loyalty +3,4 · Addiction +2,5
```

### Supplier-buyback (`StoreComponent.cpp:262-289`)

| Categorie | Buyback | Detail |
|---|---|---|
| 🌿 Drugs (`Bud_`,`WetBud_`,`Bag_`,`Joint_`,`Hash_`,`Crystal_`,`Edible_`,`Rosin_`,`Bubble_`,`Moonrock_`,`Oil_`) | **€0** | `GetSellValueCents()` = 0 — alleen aan klanten te slijten |
| 🌱 Zaden | **70%** v/d koopprijs | `BuyPrice × 0,70`, min €1,00 (`:279`) |
| 🛠️ Supplies | **70%** per-unit | `(packprijs ÷ packgrootte) × 0,70`, min €1,00 (`:286`) |

> Doel van buyback: een deel van je verlies op mislukte teelt / supply-overschot terugpakken — níét om drugs te dumpen.

---

## ⭐ XP-economie (alle bronnen)

> Er zijn **twee gescheiden XP-systemen**: (a) jouw **speler-level** (`ULevelComponent`, lvl 1-100, shop op 50) en (b) per-klant **CustomerXP** die de klant-tier laat klimmen (Casual→Whale). Verwar ze niet — de twee tellen los.

### (a) Speler-XP — bronnen + multiplier

| Bron | XP-formule | Bedrag-voorbeeld | ×Mult? | Bron |
|---|---|---|---|---|
| 🌿 **Oogsten** | `Harvested × 10 + TotalGrams` | 2 planten / 45 g = **65 XP** | ✅ | `GrowPlant.cpp:812` |
| 💶 **Verkoop (basis)** | `5 + RoundToInt(Total / 500)` *(Total in **cents**)* | deal €25 = **10 XP** · €150 = **35 XP** | ✅ | `CustomerBase.cpp:3857` |
| 👑 **Verkoop (VIP-order vervuld)** | `+ 15 + RoundToInt(Total / 1000)` bovenop basis | €50-order vervuld = **35 XP** | ✅ | `CustomerBase.cpp:3858` |
| 🚬 **Sample geven** | `3` (normaal) | **3 XP** | ✅ | `ThePlugSIMCharacter.cpp:1340` |
| 🔁 **Sample → koper-conversie** | `25` (prospect wordt koper) | **25 XP** | ✅ | `ThePlugSIMCharacter.cpp:1340` |

> 💡 **Belangrijk:** `Total` is in **cents**, deler = `500`, dus de regel is **≈ 1 XP per €5 omzet** (`Total[cents]/500 = €/5`). De code-comment zegt zelf "~EUR5 omzet = 1 XP" (`:3850`). Een €500-deal levert dus ≈ 105 XP. **Ontwerp-keuze:** XP volgt de verkoop**waarde** (euro’s), niet de grammen — dure premium-wiet (weinig gram, hoge waarde) levert méér XP dan een bak goedkope street-wiet (`:3847-3851`).

### XP-multiplier (stoned-bonus)

```
Amount = Max(1, RoundToInt(Amount × XpMultiplier))           (LevelComponent.cpp:44)
XpMultiplier = 1,0 + GetStonedXpFrac()                       (ThePlugSIMCharacter.cpp:957)
StonedXpFrac = Clamp(THC / 100, 0,0 , 0,5)                   (ThePlugSIMCharacter.cpp:2289)
```

| THC van je joint | Multiplier | Effect |
|---|---|---|
| niet stoned / 0% | `1,00×` | geen bonus (default) |
| 25% | `1,25×` | +25% op álle XP-bronnen |
| 30% | `1,30×` | +30% |
| ≥ 50% | `1,50×` | **cap** — max +50% |

> Bonus hangt aan de **THC-kwaliteit** van je wiet, níét aan hoe lang/hoe high je bent — anders kon je de timer exploiten (`:2287-2310`). Verloopt de stoned-status, dan `StonedXpFrac = 0` → multiplier terug naar 1,0. Alle multipliers hebben een `Max(1, …)`-vloer: XP kan nooit negatief of weg-vermenigvuldigd worden. Duur is intensiteit-geschaald, gecapt op `StonedMaxSeconds` (?, exacte waarde niet in extractie).

### (b) Klant-CustomerXP (tier-climb) — apart systeem

```
Gain = Max(1, RoundToInt(GramsSold × (2,0 + Loyalty/50) × ValueMult))   (NpcRegistryComponent.cpp:363)
```
Per deal groeit de **klant** richting de volgende tier — grotere/loyalere klanten klimmen vanzelf sneller. `ValueMult` = persoonlijke honger 0,6-1,6 (?, per-NPC, stabiele seed). Voorbeeld: 10 g bij Loyalty 50, ValueMult 1,0 = `10 × 2,5 × 1,0` = **25 CustomerXP**. Tier-grenzen: zie volgende sectie.

### Level-curve (recap)

`XPForLevel(N) = 100 + (N−1) × 40` — lineair. **Level 50 = shop-licentie** (`LevelComponent.cpp:22-28`, unlock `:66-75`). Volledige curve + dashboard: zie `📈 XP-curve` hierboven.

---

## 👥 Klant-types & stats

> Klanten klimmen via **CustomerXP** door 5 tiers (`TierFromXP`, `NpcRegistryComponent.cpp:305-312`). Hogere tier = grotere orders, hogere prijs-tolerantie, lucratiever. Alle klanten: patience **30 s** (`PatienceSeconds`), her-order-cooldown **240 s** (`OrderCooldownSeconds`).

| Tier | Type | CustomerXP | Order (g) | Prijs-tol. | "voelt prijs als" | Productmix (grof) | Lucratief? |
|---|---|---|---|---|---|---|---|
| 1 | 🧍 **Casual** | 0–79 | 1–3 | **0%** | 100% = marktprijs | 100% wiet | laag |
| 2 | 🚶 **Regular** | 80–219 | 3–6 | **6%** | mag €1,06/€1 | 100% wiet · 12% hash · 4% edible | ── |
| 3 | 🏃 **Heavy User** | 220–499 | 6–12 | **12%** | €1,12/€1 | 65% wiet · 28% hash · 14% edible · 12% moon | mid |
| 4 | 🤵 **VIP** | 500–999 | 10–20 | **20%** | €1,20/€1 | 45% wiet · 32% hash · 22% edible · 18% moon · 16% rosin · 12% bubble | hoog |
| 5 | 🐋 **Whale** | 1000+ | 20–50 | **30%** | €1,30/€1 | 35% wiet · 38% hash · 28% edible · 22% moon · 20% rosin · 16% bubble | **top** |

> Order-grammen worden nog × `ValueMult` (0,7-1,5 geclamped) per NPC geschaald. "Prijs-tol." = `TierPriceTolerance` (`NpcRegistryComponent.cpp:546-556` / `CustomerBase.cpp:3546-3556`): de klant ervaart je vraagprijs als `Ask × (1 − tol)`, dus hogere tiers slikken een hogere marge. Whales = weinig stuks, grote grammen, hoge marge → veruit het lucratiefst per deal.

### Persoonlijkheids-stats (`NpcRegistryComponent.cpp:112-124`)

```
RESPECT    init 10-15 → max 100   ·  ≥45 = contact ontgrendeld (UnlockRespect)
                                   ·  +1,0 s afspraak-wachttijd per punt
LOYALTY    init 0     → max 100   ·  stuwt tier-climb + substituut-acceptatie
ADDICTION  bij geboorte verdeeld  ·  ≥30 (AddictionToBuy) = koopt direct
```

**Addiction-verdeling bij spawn** (bepaalt de mix prospects/kopers):

```
 ████████████████████████████████████████████  55%  prospect   Addiction  5-28
 ████████████████████                          25%  koper      Addiction 32-60
 ████████████████                              20%  zware junk  Addiction 60-95
```

### Dag-crowd vs nacht-crowd

| | Dag-crowd | Nacht-crowd (junkies) |
|---|---|---|
| Wie | álle bewoners | alleen `Addiction ≥ 30` (`NightAddictThreshold`) |
| Gedrag | dag-only (Addiction <30) blijft 's nachts thuis | zware verslaafden blijven buiten |
| Risico | straat-deal **+0,5 heat** | straat-deal **+3,0 heat** (6× riskanter) |

Bron: `DoorRetrofitter.h:156` / `CustomerSpawner.h:51`. Virtuele crowd-cap = 70 bodies (`BodyCap`).

### Walk-in vs afspraak · Prospect → koper

```
WALK-IN                              AFSPRAAK (alleen residents)
──────────────                       ──────────────────────────
spawnt op vaste plek, zwerft         telefonisch ingepland (jij komt / klant komt)
240 s cooldown tussen aankopen       max 2 afspraken/NPC/dag (MaxApptsPerDay)
                                     wachttijd = 110s + afstand/120 + (Resp+Loy)×1,0
                                                  → clamp 90-420 s
                                     no-show: Loyalty −8 · Respect −10 (LeaveAngry)
```

**Prospect → koper (gratis samples)** — `Addiction < 30` = prospect (krijgt eerst gratis samples, betaalt niet). Per sample (`ThePlugSIMCharacter.cpp:1197-1372`):

| Sample-kwaliteit | +Addiction | +Loyalty | +Respect | +Heat |
|---|---|---|---|---|
| zwak (Q 0,2) | +1,8 | 4 + Q×12 (≈6,4) | 1 + Q×4 (≈1,8) | **+5,0** |
| degelijk (Q 0,6) | +8,4 | ≈11,2 | ≈3,4 | +5,0 |
| top (Q 0,95) | +11,55 | ≈15,4 | ≈4,8 | +5,0 |

- 🚀 **Instant-conversie:** één sample met `Q ≥ 0,45` (en niet "te zwak") forceert Addiction direct naar 30 → koper (`:1293-1304`) + **25 XP** i.p.v. 3.
- ⚠️ **Backfire:** sample met `Addiction < 20` **én** `Q < 0,5` → klant vindt 'm te zwak: **−3 loyalty, −2 respect, +1 addiction** (`:1252-1262`).
- ⏱️ **Tempo:** best-case 1 sample (top-joint) · typisch 3-4 · worst-case 5+ (lage kwaliteit, picky). Elke sample kost **+5 heat** — recruiten is een heat-investering.

---

## 🔥 Heat & risico

> Heat (0-100) is politie-aandacht. Boven **80** kan 's nachts een **bust** of **overval** triggeren. Heat zakt overdag, **'s nachts NIET** — nacht-operaties stapelen permanent risico. Bron: `HeatComponent.h/.cpp`, `EconomyComponent.cpp:215-263`.

```
╔══════════════════════════════════════════════════════════════════════╗
║                    ThePlugSIM · HEAT-DASHBOARD                          ║
╠══════════════════════════════════════════════════════════════════════╣
║  SCHAAL ........... 0–100 (clamp)                                       ║
║  DECAY DAG ........ −0,5/sec (≈30/min)      DECAY NACHT ... 0 (niets)   ║
║  BUST-DREMPEL ..... Heat ≥ 80                                           ║
║  EVENT-CHECK ...... elke 12 s · kans 12%  (alleen nacht & ≥80 & geen   ║
║                     cooldown)  →  ~40% bust / ~60% overval              ║
║  EVENT-COOLDOWN ... 3 dagen na een event (maar heat blijft staan)      ║
║  POT-CAP .......... 6 potten (1e appartement) → erboven heat-VLOER     ║
╚══════════════════════════════════════════════════════════════════════╝
```

### Wat heat VERHOOGT

| Bron | Heat | Detail | Bron |
|---|---|---|---|
| 🌙 Straat-deal 's nachts | **+3,0** | 6× riskanter dan dag | `CustomerBase.cpp:3865` |
| ☀️ Straat-deal overdag | **+0,5** | geen cooldown tussen deals | `:3865` |
| 🚬 Sample geven | **+5,0** | dag/nacht gelijk | `ThePlugSIMCharacter.cpp:1280` |
| 💸 Storten — schoon geld | **+0,3 / €1000** | "schoon" = tot je verkoop-omzet (`NoteLegitIncome`) | `EconomyComponent.cpp:139` |
| 🧨 Storten — vuil geld | **+22,0 / €1000** | alles bóven je schone ruimte — **73× heter** | `:144` |
| 📈 Dump-ramp | `×(1 + €gestort_vandaag/€100k × 0,025)` | latere storten dezelfde dag = verdachter | `:148` |
| 🪴 Pot-cap-overschrijding | **heat-vloer**, zie onder | permanent tot je downsized | `HeatComponent.cpp:121-162` |

> 💀 **Witwas-val:** €100k cash maar maar €10k verkoop-omzet → €10k schoon = +3 heat, €90k vuil = **+1980 heat** = gegarandeerde bust. Genereer eerst verkoop (= schone ruimte) vóór je grote stapels stort. Dag-limiet storten = **€50.000/dag** (`DailyDepositLimitCents`), depositobelasting **25%** (`DepositTaxPct`).

### Wat heat VERLAAGT

- ☀️ **Dag-decay** −0,5/sec (≈30/min) — 's nachts **0**.
- 🔒 **HeatResist** (security-upgrade, 0,0–0,9 cap): dempt heat-winst, event-kans én pot-vloer met `×(1−Resist)`. Max 0,9 = 10× zachter (`HeatComponent.cpp:55-64`). Vroeg-game = 0 resist = extreem kwetsbaar.
- ⛓️ Een bust verlaagt heat **−40**, een overval **−15** (de straf zélf koelt af).

### 🪴 Pot-cap heat-straf (`HeatComponent.cpp:121-162`)

```
Vloer = min(ExcessPots × 16 , 90)  × (1 − HeatResist)
        PotCap = 6 · HeatPerExcessPot = 16 · MaxPotHeat = 90 (cap)

  7 potten (1 over)   →  +16 vloer
 10 potten (4 over)   →  +64 vloer
 12+ potten (6 over)  →  +90 vloer  (gecapt)
```
De vloer scant elke 3 s alle potten binnen ~16 m (1600² units) van je appartement. Heat **zakt nooit onder deze vloer** — kleine ruimte = permanent hoge heat → forceert upgrade naar groter appartement. Eénmalige waarschuwing bij overschrijding (`:156-157`).

### Bust vs Overval (server-side, `HeatComponent.cpp:175-239`)

| | 🚓 **Bust** (~40%) | 🥷 **Overval** (~60%) |
|---|---|---|
| Cash-verlies | `max(€5, 20% v/d cash)` | `max(€3, 15% v/d cash)` |
| Heat-reductie | **−40** | **−15** |
| Spullen | veilig | **APPARTEMENT LEEG**: alle planten / droogrekken / machines binnen ~16 m gewist |
| Kluis | onaangetast | onaangetast (`SafeCents` immuun) |
| Bank | onaangetast | onaangetast |

> 🛟 **Kluis als verdediging:** `SafeCents` en bank zijn immuun voor overval — alleen on-hand cash gaat eraan. Grote werkkapitalen in de kluis parkeren (`DepositToSafe`). Na een event 3 dagen rust, maar de heat-vloer (potten) blijft — bouw je daarna weer op, dan loopt de heat opnieuw vol.

---

## 📋 Tier-stats

### 🌱 Pots (`PotTypes.cpp:7-13`)

| Pot | CareCap | YieldMult | Slots | Koop € | Verkoop € | MinPhase | Unlock-lvl |
|---|---|---|---|---|---|---|---|
| `Pot_Broken` | 0.55 | 0.90× | 1 | 15 | 6 | 0 | 1 |
| `Pot_Clay` | 0.70 | 1.00× | 1 | 40 | 18 | 0 | 3 |
| `Pot_Plastic` | 0.85 | 1.10× | 2 | 100 | 45 | 1 | 10 |
| `Pot_Fabric` | 1.00 | 1.25× | 6 | 350 | 160 | 2 | 24 |

> MinPhase (gating) en RequiredLevelFor (shop-unlock) zijn aparte gates. Upgrade-kosten schalen met tier: `basis × (tier+1)` → Broken ×1, Clay ×2, Plastic ×3, Fabric ×4.

### 🌫️ Droogrekken

| Rek | Batches | Snelheid | Unlock-lvl |
|---|---|---|---|
| `DryRack_Cheap` | 2 | traag | 1 |
| `DryRack_Std` | 5 | sneller ~2 min | 12 |
| `DryRack_Pro` | 10 | snel ~1 min | 26 |

Hulp: `DryUp_Fan` (12, ~30% sneller nabij rek) · `DryUp_Seal` (18, houdt kwaliteit hoger).

### 🔒 Kluizen

| Kluis | €-cap | Unlock-lvl |
|---|---|---|
| `Safe_Small` | €10k | 6 |
| `Safe_Medium` | €50k | 18 |
| `Safe_Large` | €250k | 30 |
| `Safe_Vault` | €1M | 42 |

### 💧 Waterflessen

| Fles | Beurten/vulling | Unlock-lvl |
|---|---|---|
| `WaterBottle_Plastic` | 3 | 1 |
| `WaterBottle_Steel` | 6 | 6 |
| `WaterBottle_Jerrycan` | 12 | 15 |
| `WaterBottle_Tank` | 25 | 28 |

### 🌿 Mest & Sprays

| Item | Effect | Unlock-lvl |
|---|---|---|
| `Fertilizer_Basic` | +15% yield deze oogst | 17 |
| `Fertilizer_Bloom` | +30% yield deze oogst | 27 |
| `Spray_Fungicide` | geneest schimmel | 11 |
| `Spray_Pesticide` | geneest ongedierte | 17 |
| `Spray_Broad` | geneest schimmel ÉN ongedierte | 33 |

> Afflictie-gates (`GrowPlant.cpp:429-477`): schimmel ab lvl 12, ongedierte ab lvl 18. Basis-kans `0.00009`/sec; genade-periode 180s, daarna sterf-periode +150s.

### 🪴 Pot-upgrades (`PotTypes.cpp:64-84`)

| Upgrade | Basis € | MinLvl | MinTier | Effect | Prereq |
|---|---|---|---|---|---|
| Drainage layer | 3.000 | 1 | 0 | +10% max kwaliteit | — |
| Insulation | 4.000 | 2 | 0 | droogt 2× trager | — |
| Bloom booster | 5.000 | 6 | 0 | +20% yield | — |
| Grow lamp I | 6.000 | 5 | 1 | +15% groei | — |
| Grow lamp II | 12.000 | 16 | 1 | +30% groei | lamp I |
| Grow lamp III | 22.000 | 30 | 2 | +50% groei | lamp II |
| Grow tent I | 7.000 | 7 | 1 | +8% max kwaliteit | — |
| Grow tent II | 13.000 | 18 | 1 | +15% max kwaliteit | tent I |
| Grow tent III | 24.000 | 32 | 2 | +22% max kwaliteit | tent II |
| Auto-water I | 14.000 | 13 | 2 | zelf bewaterd | — |
| Auto-water II | 26.000 | 27 | 3 | bewaterd + nutriënt (+10% yield) | water I |

---

## 🌿 Strain THC-ladder

Strain-unlock = afgeleid van `BaseThcPercent` (uit `DT_Strains`). Expliciete `UnlockLevel` wint boven de THC-berekening (`StoreComponent.cpp:446`). Klim van laag → hoog:

```
 ≥36%  lvl 49 ●  Zkittlez (Cali)            ████████████████████████  ◄ net vóór licentie
 <36%  lvl 45 ●  Runtz (Cali)               ██████████████████████
 <34%  lvl 41 ●  Gelato (Cali)              ████████████████████
 <32%  lvl 37 ●  Wedding Cake               ██████████████████
 <30%  lvl 31 ●  Girl Scout Cookies         ████████████████
 <28%  lvl 26 ●  Gorilla Glue               ██████████████
 <25%  lvl 21 ●  Amnesia Haze · OG Kush     ████████████
 <23%  lvl 17 ●  Sour Diesel                ██████████
 <21%  lvl 13 ●  White Widow                ████████
 <19%  lvl  9 ●  Northern Lights · Big Bud  ██████
 <17%  lvl  4 ●  Silver Haze                ████
 <14%  lvl  1 ●  Streetweed · Critical Mass ██
```

> Unlock-levels + tier-namen komen uit de code (`RequiredLevelFor`); exacte THC%, yields, groeitijden en zaadprijzen per strain staan in `DT_Strains` (binair). Een strain met een expliciet `UnlockLevel` in `DT_Strains` overschrijft de THC-afleiding. Struct: `Source/WeedShopCore/Public/Data/WeedStrain.h:12-43`; uitlezen via `DT_Strains` → CSV met `Tools/reimport_datatables.py`.

---

## 🔮 Post-50 & open punten

**Post-50 — gereserveerd / nog te ontwerpen** (ROADMAP Fase 3): levels **51-100 zijn bewust leeg**.
- Eigen winkelpand op de beach-map (echt interieur); klanten browsen schappen + rekenen af aan toonbank.
- Schappen vullen met eigen waar + merch (bongs, grinders, papers); employees voor pak/verdeel/kassa.
- Heat verschuift naar inspecties/reputatie i.p.v. straat-busts; achterdeur-verkoop blijft riskant.
- Unlock-ruimte 51-100 vullen (assortiment, inrichting, personeel-tiers, franchise-opmaat). **Franchise-plan is geparkeerd.**

**Open balans-punten fase 1** (ROADMAP Fase 2):
- **2A.1** Late Cali-seed-economie kapot (€5.000 zaad voor ~8g = verlies); staart lvl 39-49 doorrekenen.
- **2A.2** XP-tempo lvl 30→50 (mag niet vastlopen op "alleen oogsten levelt").
- **2A.3** Concentraten-rendement: olie/moonrock/rosin/iso > baggies per uur.
- **2A.4** Lvl 35-49 strikt "oneven = dure seed, even = pro-machine" — tussenbeloningen strooien. Levels **39, 43, 47** hebben geen shop-unlock (open balans-ruimte).
- **2B** Mid-game variatie lvl ~20-35: dag-orders, bulk-deals, dynamische goals, concentraten eerder.
- **2C** Co-op-pariteit: per-speler meldingen (nu host-only), deuren/liften-beslissing, client-save, 2-speler PIE-test, packaged build.
- **2D** Save-gaten: waterfles-vulling gaat niet mee in save (planten verdrogen na load); `FindHomeForPoint` 3D-afstand-fix.

---

## 🛠️ Hoe dit doc bijhouden

- **Bij ELKE progressie-wijziging dit doc mee-updaten** (unlock-level/item in `RequiredLevelFor`/`GSupplies` → swimlanes + tier-tabellen; XP/curve/max in `LevelComponent` → dashboard + curve; pots/upgrades/grow in `PotTypes.cpp`/`GrowPlant.cpp` → tier-stats). Zet de datum bovenaan.
- **Strain-detail verifiëren:** unlock-levels staan vast (uit `RequiredLevelFor`); voor exacte THC%, yields, groeitijden en zaadprijzen lees `DT_Strains` uit — exporteer naar CSV en herimporteer via `Tools/reimport_datatables.py`. Een expliciete `UnlockLevel` in `DT_Strains` overschrijft de THC-afleiding.
- **Bij progressie-wijzigingen** die de balans raken (incl. de open levels 39/43/47): groot werk → ook `DECISIONS.md`; changelog → `PATCHNOTES.md`.

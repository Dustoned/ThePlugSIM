# KOSTEN — ThePlugSIM

> Wat alles kost in de shop. **Bron:** `StoreComponent.cpp` → `GSupplies[]` (`:113-234`), prijs via `GetCatalogPriceCents()` → `GetSupplyDisplay()` (`:485-491` / `:247-260`). Alle bedragen = `PriceCents ÷ 100`.
>
> Laatst bijgewerkt: 25 juni 2026
>
> **Balansstatus:** bestaande shopprijzen en upgrade-effecten blijven in deze pass ongewijzigd. De nieuwe straincurve én klant-ordergroottes staan vast, maar nieuwe zaad- en marktprijzen zijn nog niet berekend; oude strainprijzen mogen daarom niet als definitief worden gebruikt.
>
> - **Start-kapitaal:** €100 cash · €0 bank (`EconomyComponent`).
> - **Afronding:** de shop toont `WeedRoundEuros(PriceCents)`, ondergrens **€1** — hele-euro-items zijn exact, de paar centen-items kunnen 1 euro afronden.
> - **Zaadprijzen:** de 25 strain-unlocks, groeitijden, yields en THC-waarden zijn opnieuw vastgesteld in `OPBRENGST.md`; de nieuwe zaadprijzen worden in de volgende balance-pass berekend op basis van verwachte oogstwaarde. Tot die tijd zijn ze **TBD**.
> - **Terugverkoop:** zaden + supplies = **70%** terug (min €1); **drugs = €0** (alleen aan NPC-klanten). Zie onderaan.
> - **Ordervolume:** de nieuwe ranges en productmultipliers staan in `OPBRENGST.md` en `PROGRESSION.md`; ze veranderen de catalogusprijzen in dit document niet.

---


## 🌿 Zaadprijzen — status na strainbalance V4

De grow/yield/THC-curve is compleet, maar de prijslaag is bewust nog niet vastgezet. Dit voorkomt dat oude prijzen worden gecombineerd met veel hogere late yields.

| Tier | Levels | Prijsstatus |
|---:|---:|---|
| 1 | 1–9 | TBD na marktprijsberekening |
| 2 | 11–19 | TBD na marktprijsberekening |
| 3 | 21–29 | TBD na marktprijsberekening |
| 4 | 31–39 | TBD na marktprijsberekening |
| 5 | 41–49 | TBD na marktprijsberekening |

Nieuwe regel voor de volgende pass:

```text
zaadprijs = deel van verwachte netto-oogstwaarde bij een realistische setup op unlocklevel
```

Daardoor kan een langzame bulkstrain duurder zaad hebben door zijn grote batch, terwijl een fast/potent strain vooral via hogere €/g wordt beloond.

---

## 🌱 Kweek (grow-shop)

### Potten
| Item | Prijs | Slots | Unlock |
|---|---|---|---|
| `Pot_Broken` | **€15** | 1 | lvl 1 |
| `Pot_Clay` | **€40** | 1 | lvl 3 |
| `Pot_Plastic` | **€100** | 2 | lvl 10 |
| `Pot_Fabric` | **€350** | 6 | lvl 24 |

### Aarde · Water
| Item | Prijs | Detail | Unlock |
|---|---|---|---|
| `Soil_Basic` | €15 | 3 oogsten | lvl 1 |
| `Soil_Rich` | €40 | 4 oogsten | lvl 7 |
| `Soil_Premium` | €90 | 6 oogsten | lvl 18 |
| `WaterBottle_Plastic` | €10 | 3 beurten | lvl 1 |
| `WaterBottle_Steel` | €45 | 6 beurten | lvl 6 |
| `WaterBottle_Jerrycan` | €150 | 12 beurten | lvl 15 |
| `WaterBottle_Tank` | €450 | 25 beurten | lvl 28 |

### Pot-gear (accessoires, naast de pot)
| Item | Prijs | Effect | Unlock |
|---|---|---|---|
| `Gear_Drainage` | €30 | +10% max kwaliteit | lvl 1 |
| `Gear_Insulation` | €40 | droogt 2× trager | lvl 2 |
| `Gear_Lamp1` / `2` / `3` | €60 / €120 / €220 | +15/30/50% groei | lvl 5 / 16 / 30 |
| `Gear_Bloom` | €50 | +20% yield | lvl 6 |
| `Gear_Tent1` / `2` / `3` | €70 / €130 / €240 | +8/15/22% kwaliteit | lvl 7 / 18 / 32 |
| `Gear_Water1` / `2` | €140 / €260 | auto-water (+nutriënt II) | lvl 13 / 27 |

### Plant-verzorging
| Item | Prijs | Pack | Effect | Unlock |
|---|---|---|---|---|
| `Fertilizer_Basic` | €20 | 3 | +15% yield | lvl 17 |
| `Fertilizer_Bloom` | €45 | 3 | +30% yield | lvl 27 |
| `Spray_Fungicide` | €25 | 5 | geneest schimmel | lvl 11 |
| `Spray_Pesticide` | €25 | 5 | geneest ongedierte | lvl 17 |
| `Spray_Broad` | €60 | 5 | geneest beide | lvl 33 |

> 🌿 **Zaden:** de exacte V4-unlocks en grow/yield/THC-waarden staan in `OPBRENGST.md` en `PROGRESSION.md`; nieuwe zaadprijzen blijven TBD tot de prijs-pass.

---

## 🌫️ Drogen · verpakken

| Item | Prijs | Detail | Unlock |
|---|---|---|---|
| `DryRack_Cheap` | €80 | 2 batches, ~3 min | lvl 1 |
| `DryRack_Std` | €250 | 5 batches, ~2 min | lvl 12 |
| `DryRack_Pro` | €700 | 10 batches, ~1 min | lvl 26 |
| `DryUp_Fan` | €90 | droogrek ~30% sneller | lvl 12 |
| `DryUp_Seal` | €140 | houdt kwaliteit hoog | lvl 18 |
| `Bench_Pack` | €120 | 1 zak tegelijk | lvl 1 |
| `Bench_Pack2` | €400 | 3 zakken | lvl 19 |
| `Bench_Pack3` | €1.100 | 6 zakken | lvl 35 |

### Containers (verpakking)
| Item | Prijs | Pack | Inhoud | Unlock |
|---|---|---|---|---|
| `Cont_Bag2` | €8 | 10 | tot 2g | lvl 1 |
| `Cont_Bag5` | €15 | 10 | tot 5g | lvl 5 |
| `Cont_Jar10` | €25 | 5 | tot 25g | lvl 6 |
| `Cont_Jar15` | €60 | 5 | tot 50g | lvl 12 |
| `Cont_Block100` | €90 | 3 | tot 100g (bulk) | lvl 20 |
| `Cont_Garbage500` | €300 | 2 | tot 500g (bulk) | lvl 32 |

---

## 🏭 Machines (verwerking)

### Hash-keten
| Item | Prijs | Detail | Unlock |
|---|---|---|---|
| `Mesh_Cheap` / `Std` / `Pro` | €90 / €280 / €750 | wiet → kristallen | lvl 14 / 23 / 29 |
| `Press_Cheap` / `Std` / `Pro` | €180 / €450 / €1.200 | kristallen → hash | lvl 16 / 25 / 31 |
| `ProcUp_Motor` | €160 | hash-machine ~30% sneller | lvl 22 |
| `ProcUp_Yield` | €220 | hash-machine +30% yield | lvl 28 |

### Edibles-keten + ingrediënten
| Item | Prijs | Detail | Unlock |
|---|---|---|---|
| `Oven_Std` / `Pro` | €120 / €300 | wiet → baked (decarb) | lvl 9 / 21 |
| `Pan_Std` / `Pro` | €140 / €360 | baked+butter → cannabutter | lvl 9 / 21 |
| `Fridge` | €150 | zet edibles + koelt | lvl 9 |
| `Butter` | €3 | ingrediënt | lvl 9 |
| `Flour` / `Sugar` / `Gelatin` | €2 / €1,50 / €2,50 | bak-ingrediënten | lvl 9 |

### Concentraten
| Item | Prijs | Detail | Unlock |
|---|---|---|---|
| `Oil_Std` / `Pro` | €300 / €750 | wiet → olie (moonrock-coat) | lvl 30 / 36 |
| `Moon_Std` / `Pro` | €450 / €1.100 | wiet+olie → moonrocks | lvl 34 / 38 |
| `Rosin_Std` / `Pro` | €250 / €700 | wiet → rosin (solventless) | lvl 40 / 44 |
| `Iso_Std` / `Pro` | €350 / €900 | wiet → bubble/ice hash | lvl 46 / 48 |

> Rendementen (gram-in→uit, THC×) staan in `PROGRESSION.md` → 🏭 Verwerkings-rendement.

---

## 📦 Opslag · 🔒 kluizen

| Item | Prijs | Capaciteit | Unlock |
|---|---|---|---|
| `Chest` | €90 | 20 slots (thuis) | lvl 5 |
| `Shelf` | €180 | 24 slots (shop) | lvl 8 |
| `Safe_Small` | **€4.000** | stash tot €10k | lvl 6 |
| `Safe_Medium` | **€18.000** | stash tot €50k | lvl 18 |
| `Safe_Large` | **€80.000** | stash tot €250k | lvl 30 |
| `Safe_Vault` | **€300.000** | stash tot €1M | lvl 42 |

> Kluizen zijn veruit de duurste aankopen — maar overval-immuun (zie Heat & risico in `PROGRESSION.md`).

---

## 📜 Vloei · 🛋️ meubels/decor (lvl 1)

| Vloei | Prijs | Pack | Max/joint | Unlock |
|---|---|---|---|---|
| `Papers_Small` | €5 | 10 | 2g | lvl 1 |
| `Papers_Big` | €15 | 10 | 5g | lvl 4 |
| `Papers_Blunt` | €30 | 10 | 7g | lvl 11 |
| `Papers_Backwoods` | €50 | 5 | 10g | lvl 22 |

| Meubel/decor | Prijs | | Meubel/decor | Prijs |
|---|---|---|---|---|
| `Wardrobe` | €150 | | `Table` | €120 |
| `Mattress` | €80 | | `Furn_Sofa` | €70 |
| `Furn_TV` | €60 | | `Furn_Bookshelf` | €50 |
| `Furn_Bench` | €45 | | `Furn_TVStand` | €45 |
| `Furn_TableRound` | €40 | | `Furn_Dresser` | €40 |
| `Lamp_Ceiling` | €35 | | `Furn_TableSmall` / `Desk` | €35 |
| `Furn_CoffeeTable` | €28 | | `Furn_Plant` | €25 |
| `Furn_ChairWood` | €20 | | `Furn_FloorLamp` | €20 |
| `Furn_ChairGarden` | €18 | | `Furn_Planter` | €18 |
| `Furn_ChairPlastic` / `LightSwitch` / `Nightstand` | €15 | | `Furn_Rug` | €12 |
| `Furn_DecoPot` | €7 | | `Furn_Crate` | €5 |

---

## 🏪 Shopkosten level 50–100 — nog te prijzen

De unlockstructuur staat in `PROGRESSION.md`; onderstaande aankopen zijn niet gratis wanneer ze ontgrendelen. Hun exacte prijzen zijn nog open en worden pas gezet nadat de nieuwe strain-economie is doorgerekend.

| Unlock | Aankoop / terugkerende kosten | Prijsstatus |
|---:|---|---|
| 50 | eerste winkelpand | TBD — groot spaardoel na licentie |
| 50 / 65 / 80 / 95 | Basic / Professional / Premium / Flagship toonbanken | TBD |
| 50 / 65 / 80 / 95 | universele merch-stellingen | TBD |
| 50 / 60 / 75 / 90 | kassa-tiers | TBD |
| 60 / 75 / 90 / 100 | vloer- en muur-renovaties | TBD |
| 50 / 60 / 75 / 90 / 100 | decoratie- en themasets | TBD |
| 55+ | werknemers: aanname en maandloon | dynamisch op basis van Moving, Checkout en Weed Skill |
| 52–100 | merchandisevoorraad: aanstekers, grinders, trays, pijpjes, bongs | TBD per tier |

Prijsdoel:

- kleine inrichting: betaalbaar na enkele verkopen;
- nieuwe meubeltier: duidelijke investering die niet direct wordt terugverdiend;
- eerste winkelpand: groot level-50 spaardoel;
- Premium/Flagship renovaties: echte late-game money sinks;
- werknemersloon: hoog genoeg dat sterke kandidaten alleen rendabel zijn bij voldoende winkelomzet.


## 💸 Terugverkoop (buy-back)

| Categorie | Terug | Bron |
|---|---|---|
| 🌱 Zaden | **70%** v/d koopprijs (min €1) | `StoreComponent.cpp:277-281` |
| 🛠️ Supplies | **70%** per stuk (min €1) | `:282-288` |
| 🌿 Drugs (`Bud_`/`Bag_`/`Joint_`/`Hash_`/`Edible_`/`Rosin_`/…) | **€0** — alleen aan NPC-klanten | `:262-273` |

---

> **Onderhoud:** bij prijswijziging in `GSupplies[]` deze lijst mee-updaten. Houd machine- en meubelprijzen in `PROGRESSION.md` gelijk aan dit document. Nieuwe zaadprijzen pas invullen nadat marktprijs en winst per minuut in `OPBRENGST.md` zijn goedgekeurd.

---

## KOOPPRIJS-CUT — TOEGEPAST (25 juni 2026)

> De "shopprijzen blijven ongewijzigd"-status hierboven is INGEHAALD: met de V4-curve voelde alles te goedkoop laat in het spel (inkomen ~5,6x omhoog). De koopprijzen (StoreComponent GSupplies) zijn nu progressief opgeschaald zodat de prijs/inkomen-verhouding weer klopt.
> - Multiplier per unlock-level: 1-9 x2 . 10-19 x3 . 20-29 x4 . 30-39 x5 . 40-50 x6.
> - Meubels/decor ongewijzigd (cosmetisch). Pot-upgrade-gear schaalt mee via de basis x tier-opslag.
> - Voorbeelden: Pot_Fabric 350 -> 1.400 . Press_Pro 1.200 -> 6.000 . Safe_Vault 300k -> 1.8M.
> - Bron van waarheid = de GSupplies-array (StoreComponent.cpp); de prijstabellen hierboven tonen nog de oude waardes.

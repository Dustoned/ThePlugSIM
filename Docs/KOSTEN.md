# KOSTEN — ThePlugSIM

> Wat alles kost in de shop. **Bron:** `StoreComponent.cpp` → `GSupplies[]` (`:113-234`), prijs via `GetCatalogPriceCents()` → `GetSupplyDisplay()` (`:485-491` / `:247-260`). Alle bedragen = `PriceCents ÷ 100`.
>
> Laatst bijgewerkt: 25 juni 2026
>
> - **Start-kapitaal:** €100 cash · €0 bank (`EconomyComponent`).
> - **Afronding:** de shop toont `WeedRoundEuros(PriceCents)`, ondergrens **€1** — hele-euro-items zijn exact, de paar centen-items kunnen 1 euro afronden.
> - **Zaadprijzen** staan binair in `DT_Strains` (?, niet als tekst leesbaar) — schalen vermoedelijk met THC%/tier. Niet in deze lijst.
> - **Terugverkoop:** zaden + supplies = **70%** terug (min €1); **drugs = €0** (alleen aan NPC-klanten). Zie onderaan.

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

> 🌿 **Zaden:** prijs per strain in `DT_Strains` (?, binair) — zie de strain-ladder in `PROGRESSION.md` voor de unlock-levels.

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

## 💸 Terugverkoop (buy-back)

| Categorie | Terug | Bron |
|---|---|---|
| 🌱 Zaden | **70%** v/d koopprijs (min €1) | `StoreComponent.cpp:277-281` |
| 🛠️ Supplies | **70%** per stuk (min €1) | `:282-288` |
| 🌿 Drugs (`Bud_`/`Bag_`/`Joint_`/`Hash_`/`Edible_`/`Rosin_`/…) | **€0** — alleen aan NPC-klanten | `:262-273` |

---

> **Onderhoud:** bij prijswijziging in `GSupplies[]` (`StoreComponent.cpp:113-234`) deze lijst mee-updaten. Unlock-levels uit `RequiredLevelFor()` (`:340-464`). Zaadprijzen: `DT_Strains` → CSV via `Tools/reimport_datatables.py`.

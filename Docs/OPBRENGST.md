# OPBRENGST & WINST — planten (ThePlugSIM)

> Wat een plant **oplevert**: groeitijd, yield, kwaliteit/THC en de basis voor winstberekeningen. Tegenhanger van [`KOSTEN.md`](KOSTEN.md) en [`PROGRESSION.md`](PROGRESSION.md).
>
> **Balansstatus:** straincurve **V4 compleet** voor level 1–49 en de nieuwe klant-ordergroottes zijn vastgelegd. De bestaande pot-, aarde-, gear-, lamp- en fertilizer-effecten blijven ongewijzigd. Marktprijzen en zaadprijzen worden in de volgende balance-pass definitief berekend; oude winsttabellen zijn daarom bewust verwijderd.
>
> Laatst bijgewerkt: 25 juni 2026

---

## ⚡ De 3 dingen die je écht moet weten

1. **Iedere tier bevat meerdere speelstijlen.** Kortere strains geven minder gram en meer THC; langere strains geven meer gram, minder THC en minder onderhoud per batch.
2. **De bestaande maximale yieldmultiplier blijft ~3,22×.** De strain-baseyield is opnieuw opgebouwd zodat vroege strains niet de endgame domineren en late strains grote, langzame oogsten geven.
3. **Prijs en winst zijn nog niet definitief.** Eerst staat de volledige grow/yield/THC-curve vast; daarna worden marktprijs, zaadprijs, verwerkingswaarde en winst per minuut hierop berekend.

---

## 📐 De bestaande formules

```text
YIELD (gram)   = round(BaseYield × CareQ × SoilYield × PotYield_eff × FertYield)  (min 1)

CareQ          = clamp(CareAvg, 0..1)
SoilYield      = 1.00 / 1.25 / 1.50                    Basic / Rich / Premium
PotYield_eff   = PotBasis × (Bloom? ×1.2) × (AutoWaterII? ×1.1)
PotBasis       = 0.90 / 1.00 / 1.10 / 1.25             Broken / Clay / Plastic / Fabric
FertYield      = 1.00 / 1.15 / 1.30                    geen / Basic / Bloom

GROEITIJD      = GrowMinutes / (1 + research) / Lamp
Lamp           = 1.00 / 1.15 / 1.30 / 1.50

VERKOOP (€)    = vraagprijs_per_gram × verkochte_grammen
```

> De bestaande upgrade-effecten worden niet verlaagd. Volledig geüpgraded blijft de theoretische yieldmultiplier **3,2175×**.

---

## 🌿 Complete straincurve — level 1–49

| Lvl | Strain | Rol | Groei | Met beste lamp bij unlock | Base yield | Standaard yield bij unlock* | Absolute maxyield** | Base THC |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 1 | Streetweed | Quick / starter | 6 min | 6.0 min | 26 g | 13 g | 84 g | 17% |
| 3 | Critical Mass | Productive | 12 min | 12.0 min | 58 g | 46 g | 187 g | 15% |
| 5 | Silver Haze | Fast / potent | 5 min | 4.3 min | 20 g | 16 g | 64 g | 19% |
| 7 | Blue Dream | Balanced | 9 min | 7.8 min | 42 g | 55 g | 135 g | 16% |
| 9 | Northern Lights | Heavy bulk | 15 min | 13.0 min | 72 g | 95 g | 232 g | 14% |
| 11 | Big Bud | Heavy bulk | 20 min | 17.4 min | 88 g | 145 g | 283 g | 18% |
| 13 | White Widow | Fast / potent | 8 min | 7.0 min | 32 g | 53 g | 103 g | 26% |
| 15 | Jack Herer | Balanced | 13 min | 11.3 min | 55 g | 91 g | 177 g | 22% |
| 17 | Sour Diesel | Quick / potent | 10 min | 7.7 min | 42 g | 69 g | 135 g | 24% |
| 19 | Pineapple Express | Productive | 16 min | 12.3 min | 70 g | 139 g | 225 g | 20% |
| 21 | Amnesia Haze | Quick / potent | 15 min | 11.5 min | 63 g | 125 g | 203 g | 30% |
| 23 | OG Kush | Balanced | 18 min | 13.8 min | 78 g | 154 g | 251 g | 28% |
| 25 | Bubba Kush | Heavy bulk | 26 min | 20.0 min | 120 g | 270 g | 386 g | 23% |
| 27 | Durban Poison | Fast / potent | 12 min | 9.2 min | 48 g | 119 g | 154 g | 32% |
| 29 | Gorilla Glue | Productive | 22 min | 16.9 min | 98 g | 243 g | 315 g | 25% |
| 31 | Purple Haze | Fast / potent | 16 min | 10.7 min | 68 g | 168 g | 219 g | 38% |
| 33 | Girl Scout Cookies | Balanced | 23 min | 15.3 min | 102 g | 252 g | 328 g | 34% |
| 35 | Cookies & Cream | Quick / potent | 19 min | 12.7 min | 82 g | 203 g | 264 g | 36% |
| 37 | Wedding Cake | Heavy bulk | 31 min | 20.7 min | 148 g | 366 g | 476 g | 29% |
| 39 | Gelato (Cali) | Productive | 27 min | 18.0 min | 124 g | 307 g | 399 g | 31% |
| 41 | Mimosa (Cali) | Fast / potent | 22 min | 14.7 min | 95 g | 235 g | 306 g | 44% |
| 43 | Runtz (Cali) | Quick / potent | 25 min | 16.7 min | 110 g | 272 g | 354 g | 42% |
| 45 | Apple Fritter (Cali) | Balanced | 29 min | 19.3 min | 130 g | 322 g | 418 g | 40% |
| 47 | Zkittlez (Cali) | Productive | 33 min | 22.0 min | 150 g | 371 g | 483 g | 37% |
| 49 | Gary Payton (Cali) | Heavy bulk | 38 min | 25.3 min | 175 g | 433 g | 563 g | 34% |

\* **Standaard yield bij unlock:** beste permanente yieldsetup die rond dat level beschikbaar is, zonder tijdelijke fertilizer. Dit is een vergelijkingswaarde, geen gratis gear.

\** **Absolute maxyield:** Fabric + Premium soil + Bloom booster + Auto-water II + Bloom fertilizer = **3,2175×**.

### Waarom standaard en max bij late strains verschillen

Vanaf level 27 zijn vrijwel alle permanente yield-upgrades beschikbaar, maar Bloom fertilizer is verbruikbaar en niet altijd actief. Daarom gebruikt de standaardkolom **2,475×** en de absolute maxkolom **3,2175×**. Zo blijven de twee kolommen ook in de late game betekenisvol.

---

## 🧭 Trade-off per tier

Binnen iedere tier geldt strikt:

```text
langere groeitijd → hogere yield → lagere THC
kortere groeitijd → lagere yield → hogere THC
```

### Tier 1 — level 1–9

```text
 5 min →  20 g → 19% THC  ·  Silver Haze
 6 min →  26 g → 17% THC  ·  Streetweed
 9 min →  42 g → 16% THC  ·  Blue Dream
12 min →  58 g → 15% THC  ·  Critical Mass
15 min →  72 g → 14% THC  ·  Northern Lights
```

### Tier 2 — level 11–19

```text
 8 min →  32 g → 26% THC  ·  White Widow
10 min →  42 g → 24% THC  ·  Sour Diesel
13 min →  55 g → 22% THC  ·  Jack Herer
16 min →  70 g → 20% THC  ·  Pineapple Express
20 min →  88 g → 18% THC  ·  Big Bud
```

### Tier 3 — level 21–29

```text
12 min →  48 g → 32% THC  ·  Durban Poison
15 min →  63 g → 30% THC  ·  Amnesia Haze
18 min →  78 g → 28% THC  ·  OG Kush
22 min →  98 g → 25% THC  ·  Gorilla Glue
26 min → 120 g → 23% THC  ·  Bubba Kush
```

### Tier 4 — level 31–39

```text
16 min →  68 g → 38% THC  ·  Purple Haze
19 min →  82 g → 36% THC  ·  Cookies & Cream
23 min → 102 g → 34% THC  ·  Girl Scout Cookies
27 min → 124 g → 31% THC  ·  Gelato (Cali)
31 min → 148 g → 29% THC  ·  Wedding Cake
```

### Tier 5 — level 41–49

```text
22 min →  95 g → 44% THC  ·  Mimosa (Cali)
25 min → 110 g → 42% THC  ·  Runtz (Cali)
29 min → 130 g → 40% THC  ·  Apple Fritter (Cali)
33 min → 150 g → 37% THC  ·  Zkittlez (Cali)
38 min → 175 g → 34% THC  ·  Gary Payton (Cali)
```


Over de volledige progression schuift iedere rol omhoog. Een endgame-bulkstrain heeft dus veel meer yield én meer absolute THC dan een starter-bulkstrain, maar binnen zijn eigen tier blijft hij de langste en minst potente keuze.

---

## ⬆️ Upgrade-effecten — ongewijzigd

| Upgrade | Effect op yield | Effect op kwaliteit/THC | Effect op snelheid |
|---|---:|---|---:|
| Pot Broken → Clay → Plastic → Fabric | ×0,90 / 1,00 / 1,10 / **1,25** | hogere CareCap en PotQ | — |
| Aarde Basic → Rich → Premium | ×1,00 / 1,25 / **1,50** | hogere SoilQ | — |
| Bloom booster | **×1,20** | — | — |
| Auto-water II | **×1,10** | water blijft vol | — |
| Fertilizer Basic / Bloom | **×1,15 / ×1,30** | hogere FertQ | — |
| Drainage / Tenten | — | hoger quality-/THC-plafond | — |
| Lamp I / II / III | — | — | **×1,15 / ×1,30 / ×1,50** |

### Yield-multiplier

| Opzet | Totaal × BaseYield |
|---|---:|
| Kaal: Broken + Basic, geen gear | circa **0,50×** |
| Midden: Clay + Rich + Bloom + Basic fertilizer | circa **1,21×** |
| Permanent endgame zonder fertilizer | **2,475×** |
| Volledig inclusief Bloom fertilizer | **3,2175×** |

---

## 📈 Progressiegevoel

| Onderdeel | Starter | Endgame |
|---|---:|---:|
| Kortste base groeitijd | 5 min | 22 min |
| Langste base groeitijd | 15 min | 38 min |
| Laagste base yield | 20 g | 95 g |
| Hoogste base yield | 72 g | 175 g |
| THC-range binnen tier | 14–19% | 34–44% |

De eindgame draait daardoor om grotere batches en minder vaak planten/oogsten. Snelle premiumstrains blijven bestaan, maar ook die duren later langer dan vroege fast-strains.

---

## 🏷️ Prijs- en winststatus

De oude markt- en zaadprijzen waren gekoppeld aan een curve waarin yield van 52 naar 10 gram daalde. Die waarden mogen niet rechtstreeks op de nieuwe curve worden geplakt; dat zou de economie extreem laten exploderen.

### Vastgelegd doel

- De absolute **€20/g**-cap en **€15/g** telefooncap moeten in het doelontwerp verdwijnen.
- Prijsdruk komt uit marktprijs, kwaliteit, THC, klanttier, Respect en Loyalty.
- Bulkstrains verdienen via volume en hebben een lagere €/g.
- Fast/potent strains verdienen via hogere €/g en snellere cycli.
- Zaadprijs wordt afgeleid van de verwachte oogstwaarde en niet alleen van THC.

### Nog te berekenen

- marktprijs per gram voor alle 25 strains;
- zaadprijs voor alle 25 strains;
- netto winst per oogst en per minuut;
- terugverdientijd van potten, machines en shop-upgrades;
- verwerkingsmarkup voor hash, edibles, olie, moonrocks, rosin en bubble.

Tot die pass is afgerond, zijn oude bruto- en netto-winsttabellen **niet geldig voor V4**.

---

## 👥 Orders en vraag — aangepast aan de nieuwe yields

De oude orders van 1–3 tot 20–50 gram waren afgestemd op veel kleinere oogsten. De V4-straincurve geeft in de late game standaard honderden grammen per plant; daarom gelden de volgende **doel-ranges**:

| Tier | Naam | Basisorder | Na `ValueMult` voor bud* | Gemiddelde basisorder |
|---:|---|---:|---:|---:|
| 1 | Casual | **2–5 g** | circa **1–8 g** | 3,5 g |
| 2 | Regular | **5–12 g** | circa **4–18 g** | 8,5 g |
| 3 | Heavy User | **12–30 g** | circa **8–45 g** | 21 g |
| 4 | VIP | **30–70 g** | circa **21–105 g** | 50 g |
| 5 | Whale | **70–150 g** | circa **49–225 g** | 110 g |

\* `ValueMult` blijft **0,7–1,5** per NPC. De hoeveelheid wordt naar hele grammen afgerond.

```text
TierBaseGrams  = RandomInt(TierMin, TierMax)
RequestedGrams = Max(1, RoundToInt(TierBaseGrams × ValueMult × ProductOrderMult))
```

### Productmultiplier

Verwerkte producten gebruiken minder grammen per order, zodat een Whale niet dezelfde hoeveelheid rosin als gewone bud vraagt:

| Productgroep | Ordermultiplier | Gemiddelde Whale-order bij `ValueMult = 1,0` |
|---|---:|---:|
| Bud / verpakte wiet | **1,00×** | 110 g |
| Hash | **0,60×** | 66 g |
| Edible / cookie / gummy | **0,50×** | 55 g |
| Moonrock | **0,40×** | 44 g |
| Rosin / bubble | **0,30×** | 33 g |

Voorbeeld met een basisorder van 100 g en `ValueMult = 1,2`:

```text
Bud             100 × 1,2 × 1,00 = 120 g
Hash            100 × 1,2 × 0,60 =  72 g
Edibles         100 × 1,2 × 0,50 =  60 g
Moonrock        100 × 1,2 × 0,40 =  48 g
Rosin / Bubble  100 × 1,2 × 0,30 =  36 g
```

### Waarom dit beter aansluit

- Mimosa geeft bij de standaard endgamesetup ongeveer **235 g**: circa twee gemiddelde Whale-budorders.
- Gary Payton geeft standaard ongeveer **433 g**: circa vier gemiddelde Whale-budorders.
- Lagere klanttiers blijven relevant voor kleine batches en frequente verkopen.
- Verwerkte producten blijven verkoopbaar zonder dat één order onrealistisch veel concentraat leegtrekt.
- Shopreputatie verhoogt **klantinstroom en statgroei**, maar niet de ordergrammen. Zo ontstaat geen driedubbele stapeling van tier × `ValueMult` × reputatie.

### CustomerXP-controle

De huidige code berekent CustomerXP rechtstreeks uit verkochte grammen. Met grotere orders kan de tiergroei daardoor sneller worden. Dit wordt een aparte playtestcheck. Als tiers te snel stijgen, moet alleen de gram-bijdrage aan CustomerXP worden gecapt of gedempt; de nieuwe ordergroottes hoeven dan niet opnieuw omlaag.

- Klanttiers blijven Casual, Regular, Heavy User, VIP en Whale.
- Hogere tiers tolereren hogere relatieve prijzen.
- Kwaliteit en THC beïnvloeden acceptatie; de uitbetaling blijft `vraagprijs × grammen`.
- Shopreputatie na level 50 versnelt Respect-, Loyalty- en CustomerXP-groei na winkelaankopen.
- Addiction blijft aan product/THC gekoppeld en krijgt geen decoratiebonus.

> Implementatienotitie: de nieuwe orderranges en productmultipliers zijn het **doelontwerp**. Zolang de oude ranges en absolute budget-cap nog in code staan, blijven die technisch actief totdat ze zijn aangepast.

---

## 🏭 Verwerking — nog opnieuw balanceren

De huidige machine-yields en THC-multipliers blijven als mechaniek bestaan, maar de oude productprijsfactoren kunnen niet worden goedgekeurd vóór de nieuwe strainprijzen zijn vastgesteld. Een 9×–11× prijsfactor bovenop nieuwe premiumprijzen kan de economie breken.

Volgorde:

1. strain-marktprijzen en zaadprijzen;
2. ruwe winst per minuut en per oogst;
3. verwerkingswaarde per inputbatch;
4. machineprijzen en terugverdientijd.

---

## ⭐ XP-waarschuwing

De huidige oogst-XP is `10 XP per plant + 1 XP per gram`. Met een maxyield van honderden grammen kan late oogst-XP veel sneller stijgen dan de bestaande levelcurve. De XP-formule is nog niet aangepast, maar moet na de prijsbalance worden getest met 1, 6 en 36 actieve plantenslots.

---

## 🎯 Takeaways

1. Streetweed is niet langer een 2,5-minuten high-yield endgamekeuze.
2. Iedere tier heeft fast/potent, balanced/productive en heavy-bulk opties.
3. Gary Payton is nu de ultieme langzame bulkstrain: **38 min, 175 base gram, 34% THC**.
4. Mimosa is de elite fast/potent-keuze: **22 min, 95 base gram, 44% THC**.
5. Alle bestaande yieldupgrades blijven sterk en behouden hun huidige effecten.
6. Orders schalen nu van **2–5 g Casual** tot **70–150 g Whale**, met lagere multipliers voor concentraten.
7. De volgende noodzakelijke pass is marktprijs + zaadprijs; pas daarna zijn winsttabellen betrouwbaar.

---

> **Onderhoud:** wijzig bij aanpassing van `GrowMinutes`, `BaseYieldGrams`, `BaseThcPercent`, yieldformules of upgrade-effecten ook [`PROGRESSION.md`](PROGRESSION.md) en [`KOSTEN.md`](KOSTEN.md).

---

## EERSTE PRIJS-CUT — TOEGEPAST (25 juni 2026)

> Markt-eur/g + zaadprijzen staan nu in `Data/DT_Products.csv` (Bud_-rijen) resp. `Data/DT_Strains.csv` (na DataTable-reimport in de .uasset). EERSTE CUT, tunebaar.
> - Markt-eur/g = ~eur13-39, afgeleid van een vloeiende winst/min-curve (eur32 -> eur444/min). Bulkstrains lagere eur/g (volume), potente hoger.
> - Zaadprijs = ~6% van de verwachte oogstwaarde op unlocklevel.
> - Order-ranges (GetTierOrderGrams): 2-5 / 5-12 / 12-30 / 30-70 / 70-150 g (Casual->Whale).
> - Budget-cap (BudgetCentsPerUnit): eur20/g -> eur80/g, zodat de hogere prijzen verkoopbaar zijn; prijsdruk komt uit de acceptatie-formule.
> - XP bewust NOG NIET getuned (oogst 1/gram + verkoop 1/eur5 lopen te snel met de grote V4-yields; te tunen na in-game test).

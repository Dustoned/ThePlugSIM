PATCH NOTES
Version 1.21.0 — 4 juli 2026

Korte intro:
De wereld laadt nu drie tot vier keer sneller - van ~45 naar ~12 seconden - en co-op joinen loopt daardoor
soepel. Daarbovenop een flinke berg quality-of-life: een interface die nergens meer flitst, fijner kweken en
plaatsen, duidelijkere klanten en meldingen, en een opgeruimde wereld met beter weer en een strakkere kaart.

━━━━━━━━━━━━━━━━━━━━

LADEN & OPSTARTEN

• De beach laadt nu in ~12 seconden in plaats van ~45. De grootste oorzaak zat in het opzetten van de
  straat-bevolking en is volledig weggewerkt - je staat dus veel sneller in de wereld, elke keer.
• Het hoofdmenu opent zo goed als meteen.
• Co-op joinen loopt soepel: speler 2 bleef eerst eindeloos "inladen" omdat 'ie op de host stond te wachten -
  die wachttijd is weg.

━━━━━━━━━━━━━━━━━━━━

INTERFACE ZONDER GEFLITS

• De fullscreen winkels, de Goals-app, de opslag-schermen (droogrek, kledingkast, apparaten) en de dag-teller
  + bank bovenin bouwen niet meer zichtbaar opnieuw op - alles ververst nu strak op z'n plek.
• Behaalde goals staan bovenaan en zijn zo geclaimd; de Goals-app is opgeruimd tot een echt doelen-menu.

━━━━━━━━━━━━━━━━━━━━

KWEKEN & PLAATSEN

• Een plant weggooien kan altijd: houd X ingedrukt.
• Pot-upgrades plaatsen: de preview draait nu met je muis om de pot heen (je hoeft er niet meer omheen te lopen).
• Kijk-klik kiest altijd de pot, ook als er upgrades omheen staan.
• Snel klikken plaatst niet meer per ongeluk meerdere upgrades op dezelfde pot.
• Het droogrek toont nu per bezet slot een echt rek-model, netjes verdeeld.
• Wand-plaatsing (bv. het droogrek) snapt beter tegen muur- en plafondranden.
• Wiet-toppen kleuren mee met de strain-kleur (waren altijd paars).
• De koelkast accepteert alleen nog eetbaar-gerelateerde items.

━━━━━━━━━━━━━━━━━━━━

KLANTEN, ECONOMIE & INVENTORY

• Duidelijkere klant-info met een fijne stat-melding ("+respect") bij een geslaagde deal in plaats van een
  kale regel.
• Zit je inventory vol (gewicht of slots) bij kopen of oppakken, dan vallen niet-passende items netjes op de
  grond - items verdwijnen nooit meer.
• Namen passen bij het personage (vrouwelijke skins krijgen vrouwennamen).
• Het gewicht van een baggie schaalt nu met de inhoud (een 1g-zak weegt niet meer hetzelfde als een 2g-zak).
• Klant-vragen tonen nette namen ("Critical Mass 2g") in plaats van rauwe id's met underscores.
• Een sample geven: je kiest nu de hoeveelheid (meer gram = sneller levelen) met een duidelijke indicatie en
  een maximum.
• Rugzak-upgrades zitten nu in de upgrade-tab van de telefoon - puur met geld te kopen (draag meer gewicht/slots).

━━━━━━━━━━━━━━━━━━━━

WERELD & GELUID

• Onweer is realistischer: geen bak vol bliksemflitsen meer achter elkaar.
• Nieuw in Instellingen: een volume-schuif voor het weer-geluid.
• De cijfers in de lift en boven de liftdeuren liggen nu strak vlak op het paneel (zweefden eerst los).
• Kompas-iconen zijn groter en duidelijker, met de winkels erin - in de kleur van het winkeltype.

━━━━━━━━━━━━━━━━━━━━

KAART

• De winkels op de M-kaart hebben nu elk hun eigen kleur per winkeltype (groen = grow, blauw = supplies,
  paars = meubels, goud = appartement), gelijk aan het kompas. Voorheen waren ze allemaal geel.

━━━━━━━━━━━━━━━━━━━━

MULTIPLAYER

• Als een speler wegvalt, bevriest de game van de ander niet meer lang en krijg je een nette melding.
• De gast heeft altijd een "Leave session"-optie om terug naar het hoofdmenu te gaan.

━━━━━━━━━━━━━━━━━━━━

KLEINE FIXES

• Zaadjes tonen weer een gewicht (stond op 0,0 door afronding).
• Het verdiepingsnummer boven de liftdeur is terug.

━━━━━━━━━━━━━━━━━━━━

(Intern: de trage wereld-load zat in de NPC-naamgenerator, die door een wiskundige fout 199 miljoen keer
rondtolde; opgelost met een bijectieve naam-mapping. De M-kaart gebruikt nu dezelfde winkel-kleur-bron als
het kompas.)

━━━━━━━━━━━━━━━━━━━━

Version 1.20.0 — 3 juli 2026

Korte intro:
Multiplayer is nu compleet. In competitive speel je echt ieder-voor-zich: je hebt je eigen level,
geld, klanten en politie-aandacht - niks wordt meer gedeeld met je tegenstander. De lift loopt soepel
voor allebei, en een hele berg kleine multiplayer-dingen die nog scheef stonden is rechtgezet.

━━━━━━━━━━━━━━━━━━━━

COMPETITIVE (ieder voor zich)

• Je hebt nu je EIGEN level en XP. Verkoop je, dan stijgt alleen jouw level - je tegenstander lift niet
  meer mee. Ook de shop-licentie (level 50) verdien je zelf.
• Elke klant onthoudt jou apart: je eigen band/tier per klant en je eigen wachttijden. De ordergrootte
  van een klant past zich aan aan JOUW niveau zodra je 'm aanspreekt.
• Politie-aandacht (heat) is per speler. Een inval of overval treft alleen de speler die het over
  zichzelf afriep - en haalt alleen diens huis leeg, niet dat van de ander.
• Huur is per speler: kun jij je huur niet betalen, dan gaat alleen jouw deur op slot, en je betaalt met
  je eigen geld. Voorheen klopte dit niet in competitive.
• Je kunt niet allebei hetzelfde pand kopen - de tweede krijgt netjes 'al in bezit'.

━━━━━━━━━━━━━━━━━━━━

CO-OP & LIFT

• De lift werkt nu goed samen: de deuren gaan ook bij de gast open, je komt er soepel uit zonder vast te
  komen of teruggetrokken te worden, en de ander schokt niet meer op en neer tijdens het rijden.
• Klanten blijven netjes stilstaan als de gast met ze praat (voorheen liep de klant door).
• Het weer klikt meteen goed als je joint (geen minutenlange overgang meer bij de gast).
• Lichtschakelaars die je plaatst of verzet zie je nu ook als gast (na laden en plaatsen).
• Een sessie is nu maximaal 2 spelers (waar co-op en competitive voor gemaakt zijn).

━━━━━━━━━━━━━━━━━━━━

(Intern: speler-progressie draait nu op per-speler opslag met een stabiele speler-sleutel die ook zonder
Steam werkt; bestaande saves migreren automatisch. Deur/lift/lamp-acties zijn nu server-gevalideerd.)

━━━━━━━━━━━━━━━━━━━━

Version 1.19.4 — 2 juli 2026

Korte intro:
De grote co-op-ronde. Samen (en tegen elkaar) spelen werkt nu zoals het hoort: de mensen op straat
lopen bij allebei dezelfde kant op, de tweede speler kan weer bouwen, meldingen komen bij de juiste
speler aan, en jullie bank raakt niet meer per ongeluk leeg. Plus een stapel polish en een paar fixes
waar je zelf om vroeg.

━━━━━━━━━━━━━━━━━━━━

CO-OP

• De mensen op straat (NPC's) zijn nu gedeeld: host en gast zien exact dezelfde mensen op dezelfde
  plek, ze lopen weer rond, hebben botsing (je loopt er niet meer doorheen) en staan op elkaars kaart.
  Voorheen zag de gast een eigen, bevroren/zwevende set - dat is weg. Ook deals met klanten werken nu
  voor de gast.
• De tweede speler kan weer meubels plaatsen in z'n eigen kamer (in competitive ging dat niet meer,
  ook al was de preview blauw).
• Bij een nieuw co-op-spel staat de start-inrichting niet meer dubbel bij de gast (de gast bouwde
  eerst z'n eigen set bovenop die van de host).
• Meldingen komen bij de juiste speler aan: pakket oppakken, slapen/opslaan, kopen/verkopen, level-up
  en shop-licentie, deal-bevestigingen - de gast zag die eerder niet.
• Een boete (inval/overval) of een upgrade wordt nu bij de juiste speler afgeschreven, niet altijd bij
  de host.
• De gedeelde bank raakt niet meer leeg als een (terugkerende) vriend later in de sessie binnenkomt.
• De Pakketten-app werkt nu ook voor de gast: je ziet je eigen lopende bestellingen, de ETA, de
  historie en je kunt ze annuleren (voorheen bleef die lijst leeg voor de gast).
• Bezorg-markers op kaart/kompas botsen niet meer als host en gast tegelijk iets bestellen, en een
  geannuleerde bestelling laat geen spook-marker meer achter.
• Competitive is netter gescheiden: je ziet niet meer de opslag/klant/afspraak/contacten of de
  bezorging van je tegenstander, chat-berichten lekken niet meer over, en je kunt niet per ongeluk de
  afspraak van de ander accepteren.
• De mede-speler-marker op de kaart heeft nu een eigen felle kleur (viel eerst weg tussen de blauwe
  mensen-stippen).
• Het weer is nu gelijk voor beide spelers (voorheen kon de een regen hebben en de ander zon).
• De gast kan niet meer per ongeluk 'Laden' of 'Naar menu' in het pauzemenu kiezen - dat gooide 'm
  uit de sessie. Alleen de host regelt laden en afsluiten.

━━━━━━━━━━━━━━━━━━━━

INTERFACE & WERELD

• De M-kaart is 's nachts weer leesbaar (was pikzwart) - de gebouwen/straten tonen nu dag-helder,
  ongeacht de tijd.
• Packing bench / joint rollen: je kiest weer netjes 1 strain (die lichtte per ongeluk allemaal op
  als je meerdere oogsten van dezelfde soort had).
• Als een klant om een soort vraagt, staat die soort nu in z'n eigen kleur (chat + deal-scherm).
• Schappen/rekken snappen strak tegen de muur en kunnen er niet meer half doorheen.
• Item-gewichten kloppen (concentraten/edibles wogen absurd zwaar in je draaggewicht).
• Het gram-gewicht van een plant loopt netjes op tijdens het groeien i.p.v. meteen de eindwaarde.
• Grotere waterflessen geven meer water per klik (nette opbouw).
• De laadscherm-teksten wisselen rustiger en willekeuriger.
• Het kompas is strakker (geen kader/windstreek-letters meer).
• Mensen op straat duwen elkaar niet meer van meters afstand weg; ze lopen gewoon langs elkaar.
• Afspraken via de telefoon geven nu altijd genoeg tijd om te antwoorden.

━━━━━━━━━━━━━━━━━━━━

(Intern: de download-controle vangt nu ook stille "wereld-layout niet meegebakken"-fouten, zodat een
editor-fix aan de kamers/meubels altijd aantoonbaar in de download zit.)

━━━━━━━━━━━━━━━━━━━━

Version 1.19.3 — 2 juli 2026

Korte intro:
Co-op werkt weer: een joiner kwam in competitive niet meer binnen (belandde na een lange laadhang
terug in het hoofdmenu). Verder liggen de joints weer overal, werkt het instellingen-scherm weer,
en een flinke ronde UI-polish op de packing bench en het deal-scherm.

━━━━━━━━━━━━━━━━━━━━

CO-OP

• Een vriend kon een competitive-game niet meer joinen: je spawnde midden op straat, het laadscherm
  bleef lang hangen en daarna vloog je terug naar het hoofdmenu. Opgelost - je komt nu netjes in je
  eigen kamer binnen en blijft verbonden.

━━━━━━━━━━━━━━━━━━━━

JOINTS

• Joints op straat lagen wel verspreid maar verdwenen meteen (ze vielen door de grond of rolden weg).
  Nu blijven ze netjes op de grond liggen bij bankjes, asbakken en prullenbakken - en er liggen er
  flink meer over de map, zodat je er tijdens een rondje echt tegenaan loopt.
• Gevonden joints zijn "clean" - geen brandende punt meer, gewoon het joint-model.

━━━━━━━━━━━━━━━━━━━━

INSTELLINGEN

• Alle sliders, toggles en knoppen doen het weer (die reageerden niet meer op je klikken/slepen).
• Slepen aan een slider laat het getal nu meteen meelopen.

━━━━━━━━━━━━━━━━━━━━

UI-POLISH

• Packing bench: iconen zien er nu net zo uit als in je inventory, de gekozen optie (1g/Max, Half/Max)
  licht op, en de overbodige uitleg-regels zijn weg - strakker en sneller te lezen.
• Deal-scherm: respect, loyaliteit en verslaving staan nu als drie ronde meters (net als de plant-
  meters). Bij een nieuwe klant zie je in een oogopslag hoe dicht hij bij "vaste klant" zit.
• Iemand een joint geven: de knop verdwijnt nu zolang hij aan het roken is (kon je eerst per ongeluk
  blijven aanbieden), en de reacties die mensen geven zijn een stuk gevarieerder.
• NPC-namen passen nu beter bij het personage (vrouwelijke skins krijgen vrouwennamen).

━━━━━━━━━━━━━━━━━━━━

Version 1.19.2 — 2 juli 2026

Korte intro:
Nieuwe grafische optie: kies zelf tussen DirectX 12 en DirectX 11. Handig als de game op DirectX 12
hapert, crasht met een videogeheugen-fout, of gewoon zwaar draait op jouw kaart.

━━━━━━━━━━━━━━━━━━━━

GRAPHICS

• Nieuw in Instellingen > Graphics: "Renderer" — wissel tussen DirectX 12 en DirectX 11.
  - DirectX 12 (standaard): alle effecten, mooiste beeld.
  - DirectX 11: lichter en stabieler op sommige pc's (geen Nanite/Lumen), en lost "Out of video
    memory"-crashes op die op DirectX 12 kunnen voorkomen.
  - De wissel geldt na een herstart van de game. De allereerste keer op DirectX 11 duurt het opstarten
    wat langer (de shaders worden eenmalig klaargezet), daarna is het weer snel.

━━━━━━━━━━━━━━━━━━━━

Version 1.19.1 — 2 juli 2026

Korte intro:
Hotfix voor de download-versie: een paar UI-onderdelen die in de ontwikkelversie wél werkten,
kwamen niet mee in de gedownloade build. Nu wel — en er is een automatische controle bij die
dit soort missers voortaan tegenhoudt vóór een build online gaat.

━━━━━━━━━━━━━━━━━━━━

FIXES (alleen de download-versie)

• Settings: alle aan/uit-schakelaars en sliders zijn terug (die ontbraken volledig in de vorige download).
• Het pauze-menu (Esc) doet het weer.
• Speler-skin 1 (Quinn) laadt weer in plaats van een leeg poppetje.

━━━━━━━━━━━━━━━━━━━━

Version 1.19.0 — 2 juli 2026

Korte intro:
De grote polish-update: een telefoon die nergens meer flitst, een opgeruimd deal-scherm, compacte
keuze-grids overal, échte joint-modellen met meer vindplekken, een laadscherm dat pas loslaat als de
stad er echt staat, en pakketjes die weer netjes voor je voordeur landen.

━━━━━━━━━━━━━━━━━━━━

TELEFOON & INTERFACE

• De hele telefoon is flits-vrij: apps openen, wisselen, berichten die binnenkomen, accepteren/weigeren,
  bank kopen — alles wisselt strak zonder herbouw-geflikker.
• Nieuw lettertype (Exo) en één professioneel kleurenpalet door de héle game: settings, pauze-menu,
  pack bench, kledingkast, kaart, koelkast en alle kleine meldingen zijn bijgetrokken.
• Het deal-scherm is opgeruimd: in één blik zie je wie het is, wat 'ie wil, wat jij biedt en de kans —
  dubbele teksten en ruis zijn weg.
• Lange keuzelijsten zijn vervangen door compacte icoon-grids (zoals je inventory): pack bench, joint
  rollen (kies nu zélf je strain!), een andere strain aanbieden in de chat, en de koelkast.
• De item-quick-view toont nu échte info: fles-vulling, soil-oogsten, seed-stats (THC/yield), joint-sterkte,
  papers-maat en winkel-omschrijvingen — zonder dubbele regels.
• Packages-app: per bestelling zie je nu precies wat erin zit + wat je betaald hebt, en er is een
  "Delivered"-historie. De nutteloze map-app is weg (de M-kaart blijft).
• Strain-tags zijn per soort gekleurd en beter leesbaar (dikkere letters); namen kleuren mee.
• Cash werkt nu als echt item: splitsen, droppen (en weer oppakken) en in de safe/kast bewaren om in
  co-op te delen. "Send to a friend" in de bank staat er alleen nog in co-op — mét de naam erbij.

━━━━━━━━━━━━━━━━━━━━

JOINTS

• Twee echte 3D-modellen: een dunne joint (2g/5g) en een dikke (7g/10g) — op menselijke maat.
• Gevonden joints liggen nu netjes op hun zij (met physics) in plaats van rechtop op de punt.
• Meer vindplekken: ook bij asbakken, kliko's en metalen vuilnisbakken — en héél af en toe liggen
  er twee op één plek.

━━━━━━━━━━━━━━━━━━━━

PLANTEN

• Schimmel of ongedierte zet de pot nu écht op slot: water geven kan niet en alle groei/stats bevriezen.
  Spray binnen 3 minuten en er is niks aan de hand; wacht je langer, dan zakt de kwaliteit snel.
• De plant-kaart toont mold/pest-iconen, de naam in de strain-kleur, en bij een lege pot hoeveel
  oogsten de soil nog kan.

━━━━━━━━━━━━━━━━━━━━

ANIMATIE

• Sms'en terwijl je loopt: je bovenlijf blijft appen (mét telefoon in de hand) terwijl je benen gewoon
  lopen — geen glijdend poppetje meer. Telefoon dicht = animatie stopt direct.
• De inventory openen speelt geen telefoon-animatie meer (dat hoorde niet).

━━━━━━━━━━━━━━━━━━━━

LADEN & OPSTARTEN

• Het laadscherm blijft nu staan tot de wereld écht af is — geen gebouwen, deuren of mensen meer die
  voor je neus in beeld ploppen. Met voortgangs-teksten ("Building the city...").
• Het opstarten heeft nu een laadscherm in plaats van een lang zwart venster, en er zijn onnodige
  vertragingen weggehaald (o.a. overbodige shader-compilatie).

━━━━━━━━━━━━━━━━━━━━

WERELD & DIVERSEN

• Pakketjes landen weer netjes voor je eigen voordeur (in competitive: ieder bij z'n eigen kamer) en
  de dozen vallen nu echt uit de drone en tuimelen neer.
• Meubels kunnen niet meer half over deuropeningen of op ramen geplaatst worden.
• NPC's blijven niet meer eindeloos stilstaan in de hal, en een klant die "langskomt" spawnt nu
  beneden bij de ingang in plaats van midden in je kamer.
• De kledingkast-preview blijft nu altijd goed belicht, ook 's nachts.

━━━━━━━━━━━━━━━━━━━━

GRAPHICS & PERFORMANCE

• De graphics-tiers zijn opnieuw gebalanceerd: elke stap (Potato → Low → Medium → High → Epic) is nu
  zichtbaar mooier voor een voorspelbare kost. High is de sweet spot voor een goede PC; Epic is eindelijk
  speelbaar (software-belichting in plaats van de zware ray-tracing-stack).
• Ray tracing is een aparte "experimental"-schakelaar geworden (standaard uit) — voor wie een monster-GPU heeft.
• Je gekozen tier wordt nu onthouden en bij het opstarten volledig toegepast (instellingen "verdwenen" eerst na een herstart).
• Flinke performance-pass door de hele game: minder werk per frame in de HUD/kompas/kaart, slimmere
  NPC-administratie en goedkopere deur/apparaat-logica — zonder dat er iets aan het spel verandert.
• NPC's staan nooit meer zomaar in je huis; wie er toch belandt is direct weg.

━━━━━━━━━━━━━━━━━━━━

GAMEMODES

• Sandbox en Testing zijn als aparte modes verdwenen: elke nieuwe game start gewoon Normaal.
  Alles wat die modes deden zit nu in het dev-menu (Ctrl+Shift+F10): geld, level (incl. shop-licentie),
  NPC-stats, free-build en starter-kits. Oude Sandbox/Testing-saves blijven gewoon werken.

━━━━━━━━━━━━━━━━━━━━

(Intern: alle klikbare UI is omgebouwd naar persistente widgets, het kleurenpalet is gecentraliseerd,
en de CC-BY-credits voor de joint-modellen staan in Docs/CREDITS.md.)

PATCH NOTES
Version 1.19.3 — 2 juli 2026

Korte intro:
Joints liggen weer overal en het instellingen-scherm sluit weer netjes.

━━━━━━━━━━━━━━━━━━━━

FIXES

• Joints op straat: die lagen wel verspreid maar verdwenen meteen (ze vielen door de grond of rolden
  weg). Nu blijven ze netjes op de grond liggen bij bankjes, asbakken en prullenbakken - en er liggen
  er flink meer over de map, zodat je er tijdens een rondje echt tegenaan loopt.
• De gevonden joints zijn "clean" - geen brandende/gloeiende punt meer, gewoon het joint-model.
• Instellingen: Escape sluit het scherm nu meteen. Eerst moest je eerst ergens in klikken (bv. Save)
  voordat Escape werkte.

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

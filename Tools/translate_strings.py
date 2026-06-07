# One-shot: translate visible Dutch UI/help/toast strings to English across Source/**/*.cpp.
# Replaces only EXACT full quoted literals ("dutch" -> "english"), so item-ids/keys are never touched.
# Names (strains etc.) are not in the table, so they stay.
import os, glob

PAIRS = [
    ('%d meubels gewist + meubelset (incl. sink) terug in inventory.', '%d furniture cleared + furniture set (incl. sink) back in inventory.'),
    ('%d zakje(s) x %dg  =  %dg', '%d bag(s) x %dg  =  %dg'),
    ('%d/%d woning-types gemeubileerd (lijst in de log: WeedFurnitureType).', '%d/%d home types furnished (list in log: WeedFurnitureType).'),
    ('%s\\n%dx %dg zakje  -  %.0f%% THC', '%s\\n%dx %dg bag  -  %.0f%% THC'),
    ('Aarde', 'Soil'),
    ('Alleen natte wiet kan drogen.', 'Only wet weed can be dried.'),
    ('Annuleer', 'Cancel'),
    ('Gedroogde wiet', 'Dried weed'),
    ('Gekocht voor EUR %.2f', 'Bought for EUR %.2f'),
    ('Gekocht: %s', 'Bought: %s'),
    ('Het duurt te lang', 'Taking too long'),
    ('Host een spel of vul het host-IP in.', 'Host a game or enter the host IP.'),
    ('Host nieuw co-op spel', 'Host new co-op game'),
    ('Huis %d', 'Home %d'),
    ('In het schap', 'On the shelf'),
    ('Je starter-woning kun je niet verkopen.', "You can't sell your starter home."),
    ('Jouw inventory', 'Your inventory'),
    ('Klant vertrekt boos (geduld op). Respect nu %.0f.', 'Customer leaves angry (out of patience). Respect now %.0f.'),
    ('Klant: geen voorraad van %s (%dg).', 'Customer: no stock of %s (%dg).'),
    ('Klik een toets, druk de nieuwe.  Esc = annuleer.', 'Click a key, press the new one.  Esc = cancel.'),
    ('Koop', 'Buy'),
    ('LADEN  -  kies een slot', 'LOAD  -  pick a slot'),
    ('MoneyTestPickup: +%d cents -> saldo nu %lld cents', 'MoneyTestPickup: +%d cents -> balance now %lld cents'),
    ('NIEUW SPEL  -  kies een modus', 'NEW GAME  -  pick a mode'),
    ('NIEUW SPEL  -  kies een slot', 'NEW GAME  -  pick a slot'),
    ('Natte wiet - eerst drogen', 'Wet weed - dry it first'),
    ('Niet genoeg banksaldo voor dit pand.', 'Not enough bank balance for this property.'),
    ('Niet genoeg cash.', 'Not enough cash.'),
    ('Niet genoeg op de bank.', 'Not enough in the bank.'),
    ('Niets opgeslagen.\\nStop wiet in een shelf/chest.', 'Nothing stored.\\nPut weed in a shelf/chest.'),
    ('Niks aan het drogen. Sleep natte wiet in een slot.', 'Nothing drying. Drag wet weed into a slot.'),
    ('Niks om op te slaan: zet eerst meubels binnen een woning neer.', 'Nothing to save: place furniture inside a home first.'),
    ('Pand gekocht: %s', 'Property bought: %s'),
    ('Pand verkocht: +EUR %.0f', 'Property sold: +EUR %.0f'),
    ('Per zakje', 'Per bag'),
    ('Sleep items tussen het schap en je inventory.', 'Drag items between the shelf and your inventory.'),
    ('Sleep naar de hotbar  ·  Shift+klik = splitsen', 'Drag to the hotbar  ·  Shift+click = split'),
    ('Sleep wiet hierheen om te drogen. Klaar? Sleep terug naar je inventory.', 'Drag weed here to dry it. Done? Drag it back to your inventory.'),
    ('TE KOOP - koop via telefoon (Upgrades)', 'FOR SALE - buy via phone (Upgrades)'),
    ('TE KOOP', 'FOR SALE'),
    ('THC %.0f%%   Kwaliteit %.0f%%', 'THC %.0f%%   Quality %.0f%%'),
    ('Upgrade gekocht: %s (%s)', 'Upgrade bought: %s (%s)'),
    ('Verpakte wiet (zakje)', 'Bagged weed'),
    ('Vloeitjes', 'Rolling papers'),
    ('Vul het host-IP in (bv. 192.168.1.50).', 'Enter the host IP (e.g. 192.168.1.50).'),
    ('Waterfles', 'Water bottle'),
    ('Wiet', 'Weed'),
    ('Woning', 'Home'),
    ('Zaad', 'Seed'),
    ('\\nKlaar - sleep naar je inventory', '\\nReady - drag to your inventory'),
    ('\\nNog aan het drogen', '\\nStill drying'),
    ('alleen binnen', 'indoors only'),
    ('alleen tijdens muisklik', 'only during mouse click'),
    ('cyaan stip = jij', 'cyan dot = you'),
    ('ga naar het save-punt', 'go to the save point'),
    ('geen wiet', 'no weed'),
    ('goud huisje = jouw woning', 'gold house = your home'),
    ('groen poppetje = klant voor jou', 'green figure = customer for you'),
    ('je bent hier', 'you are here'),
    ('niet genoeg', 'not enough'),
    ('sleep hierheen', 'drag here'),
    ('wiet', 'weed'),
    ('Bezorgen bij', 'Deliver to'),
    ('Verpakking', 'Packaging'),
    ('   instant, geen bezorgkosten', '   instant, no delivery fee'),
    ('(stad laadt nog...)', '(city still loading...)'),
    ('Afsplitsen: %d   (van %d)', 'Split off: %d   (of %d)'),
    ('Afspraak met %s is nu.', 'Appointment with %s is now.'),
    ('Betaal met:', 'Pay with:'),
    ('De stad wordt opgebouwd...', 'Building the city...'),
    ('Geen stad gevonden.', 'No city found.'),
    ('Nog geen save', 'No save yet'),
    ('Nr %s  -  %de verdieping', 'No. %s  -  floor %d'),
    ('Rijtjeshuis (rij van 3)', 'Terraced house (row of 3)'),
    ('Rijtjeshuis (rij van 4)', 'Terraced house (row of 4)'),
    ('Sinks zijn vaste fixtures.', 'Sinks are fixed fixtures.'),
    ('Verbinden met %s...', 'Connecting to %s...'),
    ('Verbinden met host (IP):', 'Connect to host (IP):'),
    ('Verkoop EUR %.0f', 'Sell EUR %.0f'),
    ('bij de deur', 'at the door'),
    ('buiten', 'outside'),
    ('Deal: %dx %s%s voor %d cents (resp %.0f loy %.0f ver %.0f).', 'Deal: %dx %s%s for %d cents (resp %.0f loy %.0f rep %.0f).'),
    ('Klant-interactie resultaat: %d', 'Customer interaction result: %d'),
    ('LoadGame: %d speler(s) in save, dag %d hersteld.', 'LoadGame: %d player(s) in save, day %d restored.'),
    ('MoneyTestPickup: geen AWeedShopGameState/Economy. Zet de Game State Class op AWeedShopGameState.', 'MoneyTestPickup: no AWeedShopGameState/Economy. Set the Game State Class to AWeedShopGameState.'),
    ('SaveGame %s (%s): %d speler(s), dag %d, fase %d', 'SaveGame %s (%s): %d player(s), day %d, phase %d'),
    ('SaveGame: geen GameState.', 'SaveGame: no GameState.'),
    ('WeedClearFurniture: draai dit op de host.', 'WeedClearFurniture: run this on the host.'),
    ('WeedSaveFurniture: draai dit op de host.', 'WeedSaveFurniture: run this on the host.'),
    ('[NOG DOEN]', '[TODO]'),
    ('STAD - KAART', 'CITY - MAP'),
    ('AddItem genegeerd: alleen de server mag de voorraad muteren.', 'AddItem ignored: only the server may mutate the inventory.'),
    ('AddMoney genegeerd: alleen de server mag het saldo muteren.', 'AddMoney ignored: only the server may mutate the balance.'),
    ('RemoveItem genegeerd: alleen de server mag de voorraad muteren.', 'RemoveItem ignored: only the server may mutate the inventory.'),
    ('RemoveMoney genegeerd: alleen de server mag het saldo muteren.', 'RemoveMoney ignored: only the server may mutate the balance.'),
    ('Afspraak-klant gespawned voor %s.', 'Appointment customer spawned for %s.'),
    ('FurnishDiag:   woning-type %-12s x%-3d %s', 'FurnishDiag:   home-type %-12s x%-3d %s'),
    ("Level herladen voor save-actie: %s (opts='%s')", "Level reloaded for save action: %s (opts='%s')"),
    ("Speler hersteld uit save: id='%s' naam='%s' (cash %lld, %d items)", "Player restored from save: id='%s' name='%s' (cash %lld, %d items)"),
    ('Swatch: file niet gevonden: %s', 'Swatch: file not found: %s'),
    ('UInteractionComponent hoort op een Pawn te zitten, niet op %s.', 'UInteractionComponent must be on a Pawn, not on %s.'),
    ('Wereld hersteld: %d objecten gespawned.', 'World restored: %d objects spawned.'),
]

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
files = glob.glob(os.path.join(root, 'Source', '**', '*.cpp'), recursive=True)
total = 0
for fp in files:
    with open(fp, encoding='utf-8') as f:
        txt = f.read()
    orig = txt
    for nl, en in PAIRS:
        txt = txt.replace('"' + nl + '"', '"' + en + '"')
    if txt != orig:
        with open(fp, 'w', encoding='utf-8') as f:
            f.write(txt)
        # count replacements
        c = sum(orig.count('"' + nl + '"') for nl, _ in PAIRS)
        total += c
        print(f'{os.path.relpath(fp, root)}: {c} replaced')
print('TOTAL', total)

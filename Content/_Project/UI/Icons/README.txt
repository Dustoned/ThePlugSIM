ITEM & UI ICONS
===============

Hoofdset: game-icons.net (CC BY 3.0). De PNG's hier zijn 256x256 wit-op-transparant,
gerenderd uit de SVG's in Content/_Project/1x1 (script: _AssetStaging/svg2png/render.js).

De inventory + telefoon + HUD laden deze PNG's automatisch op naam. Staat een bestand er
niet, dan tekent de game een nette gekleurde tegel als fallback.

ITEM-ICONS                         UI-ICONS (telefoon-apps / HUD)
--------------------------------   --------------------------------
cash       <- lorc/cash            ui_coin    <- delapouite/coins
bank       <- delapouite/bank      ui_clock   <- delapouite/alarm-clock
weed       <- delapouite/hemp      ui_flame   <- carl-olsen/flame
weed_wet   <- delapouite/monstera  ui_level   <- lorc/flat-star
seed       <- delapouite/seedling  ui_leaf    <- lorc/maple-leaf
joint      <- skoll/joint          ui_upgrade <- delapouite/upgrade
baggie     <- delapouite/chips-bag ui_shop    <- delapouite/shop
packaging  <- cardboard-box        ui_person  <- delapouite/person
papers     <- john-redman/paper    ui_message <- delapouite/chat-bubble
water      <- sbed/water-drop       ui_gear    <- lorc/cog
soil       <- concrete-bag         ui_map     <- lorc/treasure-map
pot        <- full-metal-bucket    ui_home    <- delapouite/house
rack       <- caro/clothesline     ui_sun     <- lorc/sun
lamp       <- ceiling-light        ui_moon    <- lorc/moon
tent       <- camping-tent
spray      <- lorc/spray
fertilizer <- fertilizer-bag
furniture  <- delapouite/house
fridge     <- caro/fridge
bed        <- delapouite/bed
table      <- delapouite/table
shelf      <- delapouite/bookshelf
chest      <- delapouite/chest

Een icoon vervangen: render een andere SVG naar <key>.png (256x256, wit op transparant),
of pas de map in _AssetStaging/svg2png/render.js aan en draai 'm opnieuw met node.
Hoofdletters/suffixen maken niet uit (de loader matcht vergevingsgezind).

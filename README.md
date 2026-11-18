# EmuForwarder (draait op de PS4)

EmuForwarder maakt tegels op het home screen van je PS4 die een game meteen in een emulator starten: GameCube in DolphinPS4, NDS/GBA/3DS en andere Retrocade-games in EmuSuNi. Alles gebeurt op de PS4 zelf (GoldHEN nodig).

## Installeren en bijwerken

- **Eerste keer:** download [`update/EmuForwarder.pkg`](update/EmuForwarder.pkg) en installeer hem via **GoldHEN → Package Installer**.
- **Updates:** bij het starten kijkt EmuForwarder op GitHub (`update/latest.txt`) of er een nieuwere versie is. Kies je **Downloaden**, dan zet hij een update-pakket in `/data/pkg`. Sluit de app en installeer het via **GoldHEN → Package Installer**. Het is een patch, zoals bij een game-update, dus hij installeert over de bestaande app heen: niet eerst verwijderen. Je hebt geen pc of FTP nodig.
- Zelf zoeken of het automatisch zoeken uitzetten: **Vierkant → Instellingen**.
- **Nieuwe versie uitbrengen (pc):** `./publiceer.sh "wat er nieuw is"`. Dat verhoogt `native/VERSION`, bouwt het pakket en zet het op GitHub, voor iedereen tegelijk.
- **Achtergrondmuziek:** zet je eigen `.ogg`- of `.wav`-bestanden (16-bit) in `/data/PS4Tegelmaker/muziek/`. Aan/uit en volume staan onder Instellingen.

## Wat de app doet

- **GameCube-games** krijgen een tegel die DolphinPS4 meelevert. Het startscherm van de PS4 wordt verborgen, dus de hoes blijft niet over het spel hangen.
- **NDS, 3DS, GBA en andere Retrocade-games** (uit `/data/retrocade/games/<console>/`) krijgen een tegel **Retro 1 t/m 4**, die het spel in EmuSuNi start.
- **Zelf het icoon kiezen.** Druk bij een game op **Options**. De app haalt afbeeldingen op en laat zien hoe het icoon wordt.
- **Oude pakketten worden vervangen.** Staat er in `/data/pkg` een oude versie van een tegel, dan zet de app de nieuwe klaar.

## Bediening

Je games staan als kaarten met hun hoes. Voorbeelden van de schermen staan in `voorbeeld/`.

| Knop | Wat het doet |
| --- | --- |
| Pijltjes | Door je games bladeren |
| L1 / R1 | Console kiezen (Alles, GameCube, NDS, GBA, ...) |
| X | Menu voor de game: tegel maken of bijwerken, icoon kiezen, naar de interne schijf zetten |
| Options | Meteen het icoon kiezen |
| Driehoek | Opnieuw zoeken (bijvoorbeeld na het aansluiten van een USB-stick) |
| Rondje (2x) | Afsluiten |

Een groen vinkje op een kaart betekent dat de tegel op je PS4 staat. **USB** betekent dat de game nog naar de interne schijf moet.

## Een tegel maken

1. Kies de game en druk op **X**. Staat hij op USB, kies dan eerst kopiëren of verplaatsen.
2. Kies **Tegel maken en installeren**. EmuForwarder installeert de tegel zelf, geeft hem de naam van de game en zet je gekozen icoon (of de hoes) erop.
3. Zie je nog de oude naam of het oude icoon, herstart dan de PS4.

Lukt zelf installeren niet, dan zet de app het pakket in `/data/pkg` en kun je het via **GoldHEN → Package Installer** installeren. Wat er gebeurde staat in `/data/PS4Tegelmaker/emuforwarder.log`.

## Het icoon kiezen (Options)

De app haalt via internet afbeeldingen op:
- GameCube: van GameTDB (hoes, volledige hoes, 3D-hoes en disc).
- Retro-games: van libretro (boxart, titelscherm en screenshot).
- De hoes die de emulator al had, en je eerder gekozen icoon.

| Knop | Wat het doet |
| --- | --- |
| L1 / R1 | Vorige / volgende afbeelding |
| Pijltjes | Afbeelding verplaatsen |
| L2 / R2 | Uitzoomen / inzoomen |
| Vierkant | De hele afbeelding in het vierkant |
| Driehoek | Terug naar beginstand |
| X | Opslaan als icoon |
| O | Terug zonder opslaan |

Links zie je de hele afbeelding, met een gele rand om het deel dat het icoon wordt. Rechts zie je het icoon zoals het op je PS4 komt. Het gekozen icoon wordt per game bewaard in `/data/PS4Tegelmaker/icons/` en gaat mee als de game later een andere tegel krijgt. Zie je het oude icoon nog, herstart dan de PS4.

## Goed om te weten

- Een algemene tegel ("GameCube 1-4", "Retro 1-4") krijgt de naam en het icoon van de game die erop staat.
- Zijn alle vier algemene tegels van een soort bezet, dan neemt de nieuwe game de tegel over die het langst geleden is toegewezen.
- De tegelpakketten staan op de PS4 in `/data/PS4Tegelmaker/tiles/` (niet in git: ze bevatten de emulators). Elke tegel bevat zijn emulator, ongeveer 200 MB per stuk.

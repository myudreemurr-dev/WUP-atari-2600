# Atari 2600 pour le GamePad Wii U (Homebrew Menu du GamePad)

Un émulateur **Atari 2600** pour le Homebrew Menu qui tourne *sur le GamePad lui-même*
([wup-homebrew](https://github.com/ravensclawx/wup-homebrew), SDK `libwup`) : un launcher qui liste tes ROMs et un player.
Le cœur de l'émulateur (`player/a2600.c`) est écrit pour ce projet : processeur 6507, TIA (image et son), RIOT (RAM, timer, ports).

```
atari/
├── app.cfg, main.c        le launcher (apparaît dans le Homebrew Menu, look du menu)
├── player/
│   ├── a2600.c, a2600.h   le cœur : 6507 + TIA + RIOT + cartouches (C portable, sans dépendance)
│   └── main.c             la couche GamePad : écran 8 bits, manette, son
├── player/draw.c          dessin de l'image (5x, seulement ce qui change)
├── tools/send_to_pad.py   envoie un fichier au pad en FTP, patiemment
├── tests/                 cpu_test.c (test du processeur), run.c (lance une ROM sur PC), draw_test.c (test du dessin)
└── package.py             construit build/atari.zip (à copier sur le GamePad)
```

## État réel (à lire avant d'installer)

| Élément | État |
|---|---|
| Processeur | Passe le test fonctionnel 6502 de Klaus Dormann (30 millions d'instructions, mode décimal compris). |
| Image | Testé sur PC avec deux jeux libres (un snake et un petit jeu à deux joueurs) : les scores, le décor et les sprites sont aux bons endroits (comparé à une capture de Stella). Les cas rares (positionnement très précis en milieu de ligne, effets de HMOVE) sont approximatifs. |
| Son | Les deux voix de la TIA sont émulées (testé sur PC : la fréquence mesurée est la bonne). Pas écouté sur le pad. |
| Cartouches | 2K, 4K, F8 (8K), FA (12K), F6 (16K), F4 (32K), E0 (8K), 3F (jusqu'à 32K), avec RAM « super chip ». Chaque système de banques est testé sur PC avec une petite ROM de test. Pas supportés : DPC (Pitfall II), 64K, E7, FE, paddles, clavier. |
| Vitesse | Sur PC une image demande ~0,2 à 0,3 ms (il en faut 16,7 ms). **Sur le GamePad : non mesuré.** |
| Pas testé sur un GamePad | ni le launcher ni le player (pas de GamePad ici, et wup-emu demande un dump du firmware). |

## Installer (à tes risques)

1. Homebrew Menu sur le GamePad (voir le README de wup-homebrew). Allume le pad en maintenant **A**.
2. Menu : Settings > Network > Wi-Fi, puis **File Sharing**.
3. Dézippe `build/atari.zip` à la racine du pad en FTP : `apps/atari/atari.wup` et `apps/atari/player.wup`.
   Le serveur FTP du pad est minuscule (3 sessions, pas de réessais en rafale) : FileZilla le fait souvent tomber. Utilise plutôt
   `tools/send_to_pad.py` (une seule connexion, réessaie proprement) :
   `python3 tools/send_to_pad.py 192.168.1.49 player.wup apps/atari/player.wup`
4. Copie tes ROMs (`.bin`, `.a26` ou `.rom` de 2 à 32 Ko) dans `apps/atari/roms/`.
5. Ouvre **Atari 2600** dans le Homebrew Menu : A pour lancer un jeu, HOME pour revenir à la liste.

Les ROMs ne sont pas fournies (il faut les posséder, ou utiliser des jeux libres : il y en a beaucoup, en homebrew).

## Commandes

| | |
|---|---|
| Croix ou stick gauche | joystick |
| A, B, ZL ou ZR | bouton de tir |
| + | Reset |
| − | Select (choix du jeu) |
| Y | couleur / noir et blanc |
| L / R | difficulté gauche / droite (A ou B) |
| HOME | retour à la liste |

Un seul joystick (le 1) est branché. Les jeux qui demandent des paddles (Breakout, Pong...) ne sont pas jouables.

## Construire

```
git clone https://github.com/ravensclawx/wup-homebrew
LIBWUP=../wup-homebrew/libwup python3 package.py
```
Il faut `clang`, `ld.lld`, Python 3 avec Pillow. Test du cœur sur PC :

```
gcc -O2 -DA26_FLAT_TEST -o cpu_test tests/cpu_test.c player/a2600.c && ./cpu_test 6502_functional_test.bin
gcc -O2 -o run tests/run.c player/a2600.c && ./run rom.bin 300 sortie 120     # -> sortie_120.ppm
```

## Crédits

- Idée de l'émulateur : nicopia784-blip (issue #19 de wup-homebrew). Demande, tests sur un vrai GamePad et publication : myudreemurr-dev.
- Code : écrit avec Claude (Anthropic).
- Les ROMs ne sont pas fournies : ne les ajoute pas au dépôt (`.gitignore` les exclut).

## Licence

GPL-3.0-or-later, comme libwup. La table de couleurs est celle, classique, des émulateurs NTSC.

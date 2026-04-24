# Cardputer Presentation Remote

Transforme une **M5Stack Cardputer** (ou Cardputer ADV) en télécommande Bluetooth pour présentations (Google Slides, PowerPoint, Keynote, etc.).

La Cardputer se fait passer pour un clavier BLE HID standard, puis envoie des codes de touches configurables quand tu presses les touches fléchées de son clavier physique.

## Matériel

- M5Stack Cardputer ou Cardputer ADV (ESP32-S3)
- Une carte micro-SD (optionnelle, pour personnaliser la config)

## Mapping des touches

Les quatre touches en bas à droite du clavier Cardputer servent de flèches :

| Touche physique | Action par défaut |
|-----------------|-------------------|
| `,`             | Flèche gauche     |
| `.`             | Flèche bas        |
| `;`             | Flèche haut       |
| `/`             | Flèche droite     |

## Build et flash

Le projet utilise [PlatformIO](https://platformio.org/).

```bash
pio run                 # build
pio run -t upload       # flash via USB
```

Un binaire mergé prêt à flasher (`firmware.bin`) est aussi généré à la racine du projet par le script `scripts/merge_bin.py`. Une version pré-buildée (`cardputer-presentation-remote.bin`) est aussi fournie dans le repo pour les utilisateurs qui veulent juste flasher sans installer PlatformIO :

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem* write_flash 0x0 cardputer-presentation-remote.bin
```

## Configuration via carte SD

Au premier démarrage, si une carte SD est insérée, le firmware crée un fichier `/cardputer-presentation-remote.json` à sa racine avec les valeurs par défaut. Tu peux l'éditer pour changer le nom BLE, le fabricant, ou le mapping des touches.

Exemple (`sd/cardputer-presentation-remote.json`) :

```json
{
  "device_name": "Cardputer Remote",
  "manufacturer": "M5Stack",
  "keys": {
    "left":  "LEFT_ARROW",
    "right": "RIGHT_ARROW",
    "up":    "UP_ARROW",
    "down":  "DOWN_ARROW"
  }
}
```

Valeurs acceptées pour chaque touche :
`LEFT_ARROW`, `RIGHT_ARROW`, `UP_ARROW`, `DOWN_ARROW`,
`PAGE_UP`, `PAGE_DOWN`, `HOME`, `END`,
`ESC`, `ENTER`, `TAB`, `SPACE`, `F5`,
ou un caractère simple (par ex. `"a"`, `"b"`).

Astuce : pour Google Slides dans un navigateur, `PAGE_UP` / `PAGE_DOWN` sont souvent plus fiables que les flèches.

Le champ `Config:` affiché à l'écran indique l'état du chargement :
- `loaded` — config SD lue avec succès
- `created` — aucune config trouvée, fichier par défaut créé
- `no SD` — pas de carte SD détectée, valeurs par défaut utilisées
- `json err: ...` — JSON invalide

## Utilisation

1. Allume la Cardputer. L'écran affiche `BLE: WAITING`.
2. Depuis l'OS, appaire le "Cardputer Remote" dans les réglages Bluetooth. Il doit être reconnu comme **clavier**.
3. Une fois appairé, l'écran passe à `BLE: CONNECTED`.
4. Ouvre ta présentation, passe-la en mode présentation, clique dedans pour lui donner le focus OS.
5. Les quatre touches fléchées de la Cardputer font défiler les slides. Le champ `Last:` affiche la dernière touche envoyée.

## Dépannage

**La Cardputer dit `CONNECTED` mais les touches n'ont aucun effet sur la cible.**

Causes classiques :
- L'appairage s'est fait, mais pas en tant que clavier HID. Oublie l'appareil dans les réglages Bluetooth de l'OS et ré-appaire.
- La fenêtre cible n'a pas le focus OS. Clique dedans avant de presser les touches.
- Certaines apps web interceptent différemment les flèches — essaie `PAGE_UP` / `PAGE_DOWN` dans la config.

**Le champ `Last:` ne change pas quand je presse les touches.**

La Cardputer ne capte pas les appuis. Vérifie que tu presses bien les touches `,` `.` `;` `/` (rangées du bas à droite du clavier).

## Stack BLE

Le projet utilise le fork NimBLE [`wakwak-koba/ESP32-NimBLE-Keyboard`](https://github.com/wakwak-koba/ESP32-NimBLE-Keyboard). Le fork T-vK d'origine (Bluedroid) ne finalise pas correctement le pairing HID avec les versions récentes d'arduino-esp32 sur ESP32-S3 — la connexion s'établit mais aucun rapport clavier n'est accepté par l'hôte.

Au moment de l'écriture, le header `BleKeyboard.h` du fork NimBLE oublie un `#include <functional>`. Si la compilation échoue avec `'std::function' does not name a template type`, ajoute manuellement cet include en haut de `.pio/libdeps/cardputer-adv/ESP32 BLE Keyboard/src/BleKeyboard.h`.

## Structure du projet

```
├── platformio.ini                       # config PlatformIO + lib_deps
├── src/main.cpp                         # firmware
├── scripts/merge_bin.py                 # merge bootloader+partitions+app en un seul .bin
├── sd/cardputer-presentation-remote.json  # exemple de config à placer sur la carte SD
└── cardputer-presentation-remote.bin    # binaire pré-buildé prêt à flasher
```

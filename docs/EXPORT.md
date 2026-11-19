# Exporter un jeu

`gectl export` transforme un jeu en **un zip qu'on peut envoyer à quelqu'un d'autre** : le programme, les fichiers du jeu, un `README.txt` et les licences. Le destinataire le dézippe et double-clique ; il n'installe rien, n'ouvre pas de terminal et ne compile rien. Décisions : [ADR 0011](adr/0011-lanceur-et-export.md).

```
scripts\export.cmd games\doodle_jump\game.json               # Windows : compile gectl au besoin, zip dans dist\
gectl export games/doodle_jump/game.json --out C:\Envois    # gectl déjà construit
gectl export --targets                                       # les plateformes possibles
```

| Option | Effet |
| --- | --- |
| `--target NOM` | la plateforme (défaut `windows`) |
| `--out DIR` | où poser le zip (défaut `dist`) |
| `--skip-tests` | ne pas rejouer les scénarios du jeu avant d'exporter |
| `--source DIR` | la racine du moteur, là où se trouve son `CMakeLists.txt` (défaut `.`) |
| `--build-dir DIR` | où compiler le programme (défaut `build/export/<cible>/<jeu>`) |
| `--cmake-arg ARG` | un argument de plus pour CMake, répétable (par exemple `-DFETCHCONTENT_SOURCE_DIR_SDL3=...` pour utiliser une copie locale de SDL3) |

## Depuis le lanceur

`gelauncher` a un bouton **Export** : on choisit le jeu, puis la plateforme, et le lanceur fait la même chose que la commande, en arrière-plan, avec les dernières lignes de sortie à l'écran (voir [LAUNCHER.md](LAUNCHER.md)). `gelauncher --source DIR --dist DIR` règle la racine du moteur et le dossier des zips.

## Pas à pas sous Windows

Une seule fois :

1. **Visual Studio Build Tools** ([visualstudio.microsoft.com/downloads](https://visualstudio.microsoft.com/downloads/), « Build Tools for Visual Studio ») : cocher la charge de travail **« Développement Desktop en C++ »**.
2. **CMake** ([cmake.org/download](https://cmake.org/download/)) : cocher « Add CMake to the system PATH ».
3. **Git** ([git-scm.com](https://git-scm.com/)), puis, dans une invite de commandes :
   ```
   git clone https://github.com/MartinOhresser/GameEngine
   cd GameEngine
   ```

À chaque export :

```
scripts\export.cmd games\doodle_jump\game.json
```

Le script compile `gectl` la première fois, puis lance l'export. Le zip est dans `dist\`. Pas besoin de « x64 Native Tools Command Prompt » : CMake trouve Visual Studio tout seul. `scripts\launcher.cmd` ouvre le lanceur (avec son bouton Export) de la même façon.

Le **premier export** compile SDL3 et les modules : plusieurs minutes, et une connexion Internet (CMake télécharge SDL3). Les suivants réutilisent `build/export/<cible>/<jeu>` et ne refont que ce qui a changé.

Ailleurs que sous Windows, `gectl` se construit avec `cmake --preset dev` (ou `release`) et la cible `linux`, cachée, sert aux essais.

## Sans carte graphique récente

SDL dessine avec le processeur quand la carte graphique ne sait pas le faire : si l'ouverture de la fenêtre échoue avec le rendu matériel, le jeu réessaie en rendu logiciel tout seul. `ge_player --software` le force (utile pour vérifier qu'un jeu reste jouable sans accélération ; la CI exporte et lance Doodle Jump ainsi).

## Récupérer un jeu déjà exporté

Les versions publiées (page « Releases » du dépôt) contiennent les zips Windows des jeux de référence (Doodle Jump, Treasure Run, Coin Dash) : à télécharger et dézipper, sans rien installer.

## Ce que fait la commande

1. **Valide le jeu** (comme `gectl validate`) avec les modules du dépôt.
2. **Rejoue ses scénarios** : un jeu dont un test échoue n'est pas exporté (`--skip-tests` force).
3. **Choisit les modules** : ceux du manifeste et leurs dépendances, rien de plus. `ge_add_player` génère un registre avec cette seule liste et lie le programme `ge_player` (un programme sans console sous Windows, avec la bibliothèque C++ et SDL liées statiquement : ni DLL ni « Visual C++ Redistributable »).
4. **Prépare l'icône** : l'`icon` du manifeste (un PNG d'au plus 256 × 256) devient un `.ico` intégré à l'`.exe`.
5. **Rassemble le paquet** : le programme (nommé d'après le `title` du jeu), `data/` avec les seuls fichiers lus par le jeu (prefabs, scènes, textes, atlas et images, entrées, icône, sons ; ni les scénarios ni les sources d'art) et un `game.json` sans la clé `scenarios`, `README.txt` (comment lancer, touches du jeu, où vont les sauvegardes, avertissement Windows) et `LICENSES.txt`.
6. **Zippe** avec `cmake -E tar --format=zip` : `dist/<Titre>-<version>-<cible>.zip`.

Le zip contient un dossier du même nom : le destinataire n'éparpille rien.

## Côté joueur

- Double-clic sur le programme. Le jeu cherche ses fichiers dans `data/`, à côté de lui, d'où qu'il soit lancé (un chemin avec espaces ou accents convient).
- Les sauvegardes vont dans le dossier de données de l'utilisateur (`%APPDATA%\ge\<jeu>\` sous Windows), jamais à côté du programme.
- **Échap** ou la croix de la fenêtre quitte.
- Windows affiche « Windows a protégé votre PC » : le programme n'est pas signé. « Informations complémentaires », puis « Exécuter quand même » (limite assumée de l'ADR 0011). Le `README.txt` le dit.
- Gmail et Outlook refusent les `.exe`, même zippés : un lien de téléchargement est plus sûr que la pièce jointe.

## Les cibles

Une plateforme est un `ExportTarget` (`tools/gectl/export.hpp`) : un nom, un libellé, ce qu'il faut pour la fabriquer, la partie du README qui en dépend et **deux étapes** : `build` fabrique le programme (CMake et un compilateur sur un ordinateur ; Gradle en plus pour Android) et `assemble` fabrique ce qu'on envoie (un zip, un APK) à partir du programme et des fichiers du jeu. Windows et Linux partagent une base « ordinateur » (`DesktopTarget` : CMake compile `ge_player`, puis dossier, README, licences et zip) ; leurs différences sont le suffixe de l'exécutable, les arguments CMake et le texte du README. Ajouter une plateforme, c'est ajouter une classe et la déclarer dans `export_targets` ; la validation, les tests, le choix des modules et `stage_game_files` (la copie des fichiers du jeu, la même partout) ne changent pas.

| Cible | État |
| --- | --- |
| `windows` | proposée ; se fabrique sous Windows |
| `linux` | cachée (`--targets` ne la montre pas) : sert aux tests, y compris sur la CI |
| `android` | cachée jusqu'à A7 (voir [EXPORT_ANDROID.md](EXPORT_ANDROID.md)) : `gectl export JEU --target android [--abi x86_64,arm64-v8a]` donne un `.apk` (par défaut `arm64-v8a`, les téléphones). Il faut le SDK Android (plateforme et Build-Tools), le NDK, un JDK 17+ et Ninja ; ce qui manque est dit avec où le trouver. Le premier export télécharge Gradle (quelques minutes). Voir `tools/gectl/export_android.cpp` et le gabarit `platform/android/template/`. |

## Vérifié en CI

Le job « Export » de la CI exporte Doodle Jump pour de vrai sous Windows et sous Linux, dézippe le paquet dans un dossier avec espaces et accent, vérifie son contenu (README, licences, pas de scénarios), liste les bibliothèques dont dépend l'`.exe` (aucune bibliothèque d'exécution Visual C++ ni SDL3) et le lance sans fenêtre. Le zip est disponible en téléchargement sur la page du job.

## Dans le code

- `cmake/GeRegistry.cmake` : `ge_write_registry` écrit `registry.cpp` pour une liste de modules (`gerun` en a tous, un export en a quelques-uns).
- `platform/sdl/CMakeLists.txt` : `ge_add_player` ; `platform/sdl/src/player.cpp` et `player_main.cpp` : le programme du joueur (`--data DIR`, `--headless`, `--frames N`, `--screenshot F` servent aux tests).
- `include/ge/game/package.hpp` : `required_modules`, `package_files`, `package_manifest`.
- `platform/sdl/src/tool_export.cpp` : le service d'export du lanceur (un fil qui appelle `export_game`, ses commandes lues ligne par ligne avec `run_process_lines`).
- `tools/gectl/export.cpp` : les cibles, le `.ico`, le README, les licences et l'enchaînement des commandes. Un `CommandRunner` remplace CMake dans les tests (`tests/tools/test_export.cpp`) : aucun programme n'est compilé pendant `ctest`.

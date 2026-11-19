# Exporter un jeu

`gectl export` transforme un jeu en **un zip qu'on peut envoyer à quelqu'un d'autre** : le programme, les fichiers du jeu, un `README.txt` et les licences. Le destinataire le dézippe et double-clique ; il n'installe rien, n'ouvre pas de terminal et ne compile rien. Décisions : [ADR 0011](adr/0011-lanceur-et-export.md).

```
gectl export games/doodle_jump/game.json                     # Windows, zip dans dist/
gectl export games/doodle_jump/game.json --out C:\Envois
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

## Ce qui est nécessaire pour exporter

L'export **compile** le programme du jeu, sur la machine de celui qui exporte (pas sur celle du joueur) :

- **Windows** : à faire sous Windows, avec Visual Studio (ou ses Build Tools) et CMake. Lancer la commande depuis « x64 Native Tools Command Prompt for VS » (le CMake de Visual Studio y est déjà). Pas besoin de Ninja : CMake prend le générateur de Visual Studio.
- Une connexion Internet la première fois (CMake télécharge SDL3), puis rien.

Le **premier export** compile SDL3 et les modules : plusieurs minutes. Les suivants réutilisent `build/export/<cible>/<jeu>` et ne refont que ce qui a changé.

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

Une plateforme est un `ExportTarget` (`tools/gectl/export.hpp`) : un nom, un libellé, ce qu'il faut pour la fabriquer, le suffixe de l'exécutable, les arguments CMake propres à la plateforme et la partie du README qui en dépend. Ajouter Android, macOS ou une console, c'est ajouter une classe et la déclarer dans `export_targets` ; la validation, les tests, le choix des modules et la copie des fichiers ne changent pas.

| Cible | État |
| --- | --- |
| `windows` | proposée ; se fabrique sous Windows |
| `linux` | cachée (`--targets` ne la montre pas) : sert aux tests, y compris sur la CI |

## Vérifié en CI

Le job « Export » de la CI exporte Doodle Jump pour de vrai sous Windows et sous Linux, dézippe le paquet dans un dossier avec espaces et accent, vérifie son contenu (README, licences, pas de scénarios), liste les bibliothèques dont dépend l'`.exe` (aucune bibliothèque d'exécution Visual C++ ni SDL3) et le lance sans fenêtre. Le zip est disponible en téléchargement sur la page du job.

## Dans le code

- `cmake/GeRegistry.cmake` : `ge_write_registry` écrit `registry.cpp` pour une liste de modules (`gerun` en a tous, un export en a quelques-uns).
- `platform/sdl/CMakeLists.txt` : `ge_add_player` ; `platform/sdl/src/player.cpp` et `player_main.cpp` : le programme du joueur (`--data DIR`, `--headless`, `--frames N`, `--screenshot F` servent aux tests).
- `include/ge/game/package.hpp` : `required_modules`, `package_files`, `package_manifest`.
- `platform/sdl/src/tool_export.cpp` : le service d'export du lanceur (un fil qui appelle `export_game`, ses commandes lues ligne par ligne avec `run_process_lines`).
- `tools/gectl/export.cpp` : les cibles, le `.ico`, le README, les licences et l'enchaînement des commandes. Un `CommandRunner` remplace CMake dans les tests (`tests/tools/test_export.cpp`) : aucun programme n'est compilé pendant `ctest`.

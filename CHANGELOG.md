# Changelog

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versions au format [SemVer](https://semver.org/lang/fr/).

## [Unreleased]

### Added

- Export Android (ADR 0012, A3) : `gectl export JEU --target android [--abi a,b]` fabrique un `.apk` (NDK pour `libmain.so`, Gradle pour l'APK, gabarit dans `platform/android/template/`) et dit ce qui manque (SDK, NDK, JDK, Ninja). La cible reste cachée jusqu'à A7. Un workflow `Android export` l'exerce pour de vrai sur un émulateur.

- Tactile (ADR 0012, A4) : les doigts arrivent en sources `touch:down/x/y` (jusqu'à cinq : `touch:down2`…), le premier est aussi une souris (un jeu souris se joue au doigt, compté une fois), les positions sont en pixels du jeu, le bouton Retour d'Android est `key:ac_back` et quitte comme Échap, un hôte mobile ignore les boutons « Quitter ».

- Module `touch_controls` (ADR 0012, A5) : `TouchButton`, `TouchStick` et `TouchKnob` dessinent des contrôles pour un écran tactile (cachés sur un ordinateur jusqu'au premier doigt) et donnent des actions au jeu ; joystick et bouton de tir pour `doodle_jump`, joystick pour `treasure_run` (avec un scénario chacun). Les positions des doigts sont des pixels, pas des axes.

- CI et release Android (ADR 0012, A6) : chaque pull request exporte `doodle_jump` et `treasure_run` en APK, lit le paquet avec `aapt2` puis le joue sur un émulateur (tape sur Jouer, doigt sur le joystick, Retour) ; la release ajoute les APK des jeux de référence. Nouvelle page `docs/EXPORT_ANDROID.md`.

- Export Android (ADR 0012, A7) : la cible `android` est proposée (`gectl export --targets` dit ce qui manque, le lanceur la liste) ; `--install` met l'APK sur le téléphone ou l'émulateur par `adb`, `--keystore` et `--key-alias` signent avec sa propre clé (mots de passe par l'environnement).

- Menus : `ui.Menu` lit ses boutons comme un tableau d'après leur place à l'écran. Nouvelles actions `left` et `right` : elles déplacent le focus le long d'une ligne de boutons côte à côte, `up` et `down` changent de ligne (vers le bouton le plus proche), grilles comprises. Le lanceur : flèches gauche/droite (ou A/D) naviguent, PageUp/PageDown tournent les pages.

### Changed

- Les fichiers d'un jeu exporté se lisent par SDL (`read_file`, `list_game_files`) et l'export écrit `data/files.txt` : de quoi lire dans un APK (ADR 0012, A2). `ge_add_player` crée la bibliothèque `main` sous Android.
- L'export est fait de deux étapes que chaque plateforme fournit (`ExportTarget::build` et `assemble`, ADR 0012, A1) ; Windows et Linux partagent une base `DesktopTarget`. Aucun changement pour un joueur.
- ADR 0012 : plan de l'export Android (APK) en huit jalons A0 à A7 ; `docs/ROADMAP.md` les liste.
- Windows : les chemins sont du UTF-8 (`ge/core/path.hpp`) : un jeu dans un dossier avec accent (`C:\Users\Léa\…`) trouve ses fichiers, ses sons, ses images et sa sauvegarde ; révélé par le job CI « Export ».
- CI : AddressSanitizer, clang-tidy et le build GCC Debug ne tournent plus en CI ; `scripts/check.sh` les exécute avant chaque pull request (le dépôt est public, mais ils doublaient ce que le script fait déjà).
- ADR 0011 : décisions du lanceur (un jeu en JSON avec un hôte mince) et de l'export autonome par plateforme ; `docs/ROADMAP.md` liste les étapes E0 à E7.
- Branches : `main` reste la dernière étape validée, les pull requests visent `dev`. La CI passe sur `dev` ; le workflow « Promote dev to main » avance `main` sur un commit de `dev` déjà vert et pose l'étiquette de version. Les issues « Module gap » et `ci-failure` (ouverte et fermée par un workflow) sont décrites dans `CONTRIBUTING.md`.

### Added

- `gameEngine.cmd` : la version Windows de `./gameEngine.sh` (invite ordinaire, sans Ninja ni « Native Tools ») ; `scripts\launcher.cmd` l'appelle. La CI ouvre le lanceur avec chacun des deux scripts.
- `./gameEngine.sh` : configure, compile (dans `build/launcher`) et ouvre le lanceur en une commande ; `--build-only` compile seulement, les autres arguments vont à `gelauncher`.
- E7 : `scripts/export.cmd` et `scripts/launcher.cmd` (Windows : construisent ce qu'il faut puis exportent ou ouvrent le lanceur, depuis une invite ordinaire) ; `ge_player --software` (`WindowOptions::software`) et repli automatique sur le rendu logiciel de SDL ; la release publie les zips Windows de Doodle Jump, Treasure Run et Coin Dash ; le job CI « Export » passe par `scripts/export.cmd` et lance le joueur en rendu logiciel ; `docs/EXPORT.md` pas à pas pour Windows ; le skill `game-from-prompt` sait exporter.
- E6 : écran « Export » du lanceur (scène JSON `export`, bouton « Export », listes `targets` et `export`, commande `export`), `ExportService` (interface du lanceur), `gelauncher --source --dist` avec un service qui fait tourner `gectl export` en arrière-plan (`tool_export.cpp`, `run_process_lines`), Échap qui revient de l'écran d'export, `LauncherResult::export_lines`.
- E5 : `gectl export GAME.json [--target windows] [--out DIR]` fabrique un zip autonome (programme sans console avec les seuls modules du jeu, `data/`, README, licences, icône `.ico` tirée du PNG du manifeste) après avoir validé le jeu et rejoué ses scénarios ; `gectl export --targets`. `ge_add_player` et `ge_write_registry` (CMake), `ge_player` (`run_player`), `ge::game::required_modules`/`package_files`/`package_manifest`, `SdlBackend` : `base_directory` et `show_error`, job CI « Export » (Windows et Linux). `docs/EXPORT.md`.
- E1 : champs facultatifs `title`, `version`, `id` et `icon` dans `game.json` (validés, présents dans le schéma JSON), `gectl list [--games DIR] [--json]` qui décrit chaque jeu, et `ge::game::summarize` / `GameSummary` ; les six jeux du dépôt ont un `title`.

- Doodle Jump : `games/doodle_jump` (menu, partie sans fin de plus en plus difficile, fin de partie, meilleur score sauvegardé ; 20 scénarios, dont des scènes d'essai `test_*`), `docs/DOODLE_JUMP.md`, ADR 0010.
- Doodle Jump : modules `gravity2d`, `bounce2d` (surfaces à sens unique : plateformes, ressorts, monstres écrasés), `patrol2d`, `spawner2d` (`Generator`, `Cull`, `Shooter`), `combat2d` (`Mortal`, `Target`, `Projectile`), `altitude` et `records`.
- Doodle Jump : `SpawnRequests` (créer un préfab en cours de partie, servi entre deux ticks), liste intégrée `prefab`, `ListNames`, `SaveData`, `geplay --save` et sauvegarde par défaut dans le dossier de données de l'utilisateur, `LoopOptions::after_frame`.
- Doodle Jump : interrupteurs par axe pour `action_control` (`horizontal`, `vertical`), `screen_wrap` (`wrap_x`, `wrap_y`) et `camera2d` (`follow_x`, `follow_y`, `offset_y`, `only_up`).
- M8.2 : `Viewport` et clé `window` du manifeste, position du pointeur (`mouse:x`, `mouse:y`) non bornée, `Actions::raise`, liste intégrée `field` et `FieldTable`, pivot des sprites.
- M8.2 : module `ui` (`Anchor`, `Grid`/`Cell`, `Tag`, `Bind`, `Bar`, `Menu`/`Button`) et module `inventory` (`Slot`, `Item`, `Gatherer`, `Transfer`, `Hand`) : conteneurs, cases et règles configurables par données.
- M8.2 : `treasure_run` avec menu, HUD et barre d'objets ; vérification `scene is NOM` dans les scénarios ; `docs/UI.md`.
- M8.1 : listes nommées du manifeste (`lists`), champs `ref_set` (ensembles de noms), `scene` et `action` comme listes intégrées ; les couches de `collision2d` s'écrivent par noms.
- M8.1 : textes par langue (`strings`, `locale`), polices en atlas PNG (`glyphs`), module `text2d`, drapeau `screen` pour `Sprite` et `Text`, `scripts/make_font.py`.
- M8.1 : changement de scène en cours de partie (`SceneRequest`, `App::set_scene_loader`, `EntityNames`), module `scene_switch` (`GoToScene`, `Persistent`) ; `treasure_run` affiche un titre et une aide, en deux langues.
- M7 : rendu 2D. `Renderer` étendu (`load_atlas`, `set_camera`, commandes de sprite avec cellule, taille, miroir et teinte) ; `end_frame` est appelé une fois par image par la boucle. Le backend SDL charge les PNG (`stb_image`) et dessine sprites et carrés selon la caméra, sans lissage.
- M7 : atlas déclarés dans le manifeste (`atlases`), champs `u32` à `"ref"` qui acceptent un nom dans les fichiers de jeu, `game::load_atlases`.
- M7 : modules `sprite2d` (`Sprite`, `SpriteAnimation`, tri par couche), `camera2d` (`Camera`, `CameraTarget`, `CameraFollow`) et `collision2d` (`Collider`, `Body`, `Contacts`, détection par balayage trié) ; le générateur gère les composants sans champ.
- M7 : jeu de référence `games/treasure_run` (images et scène générées par un script versionné), `scripts/make_png.py`, `docs/RENDERING.md`, ADR 0008.
- `coin_dash`, `pilot` et `sparks` se voient dans une fenêtre (carrés `debug_draw`) ; `coin_dash` et `pilot` ont un `input.json`. `geplay` signale quand un jeu n'a aucun module de dessin au lieu d'ouvrir une fenêtre noire sans explication.
- Système de build CMake (C++20) avec presets `dev`, `release`, `asan`, `tidy`, `coverage`.
- Bibliothèque `ge_core` : version du moteur, macro `GE_ASSERT`.
- Bibliothèque `ge_ecs` : entités à génération, stockage sparse set, `World` (composants, requêtes `each`, ressources).
- Bibliothèque `ge_app` : `App`, étapes, `Schedule`, plugins.
- M1 : générateur pseudo-aléatoire déterministe `ge::Random` (PCG32, vérifié contre les valeurs de référence).
- M1 : ressource `Time`, étape `FixedUpdate` à pas fixe, `App::advance` (temps réel vers ticks, avec plafond de rattrapage) et `App::set_tick_rate`.
- M1 : ordre des systèmes par dépendances (`Order::after`, `Order::before`, `Order::between`) et `Schedule::problems()` (doublons, noms inconnus, cycles).
- M1 : commandes différées `Commands` (insert, remove, despawn, spawn_with, commande libre), appliquées après chaque système.
- M1 : événements `Events<T>` à curseurs de lecture, `App::add_event<T>()` avec cadence par tick pour rester déterministe.
- M1 : `ge::Hasher` (FNV-1a 64 bits, indépendant de la plateforme pour les types à largeur fixe), `World::clone()` et `World::fingerprint()` pour copier et comparer l’état de la simulation ; test de déterminisme (même graine, mêmes actions, même état).
- M2 : modules décrits par `module.json` (schéma `schemas/module.schema.json`), code généré versionné (composants, systèmes, `info()`, `install()`), `App::add_module` et `App::module_problems()`.
- M2 : outil `gectl` (`check`, `catalog`, `generate [--check]`), bibliothèque JSON `ge::json` et trois modules d'exemple : `transform2d`, `lifetime`, `screen_wrap`.
- M3 : les fichiers de jeu acceptent commentaires et virgules finales (`ge::json::relaxed`).
- M3 : `params` dans `module.json` (réglages avec bornes, ressource `Params` générée, exemple `transform2d.speed_scale`) et fonction d'insertion générée pour chaque composant, base du chargement de niveaux depuis des fichiers.
- M3 : bibliothèque `ge_game` : lecture d'un jeu (`game.json`, préfabs avec `extends`, scènes, tout en entités), validation contre les modules (noms, champs, types, bornes, dépendances, cycles de préfabs) avec messages « vouliez-vous dire … ? », puis `setup_game` / `spawn_scene` qui installent les modules, appliquent les réglages et créent les entités.
- M3 : `gectl validate GAME.json` valide un jeu sans le lancer.
- M3 : lanceur universel `gerun GAME.json [--ticks N]` (tous les modules du dépôt liés, sans fenêtre), exemple `games/sparks` fait uniquement de JSON, test de bout en bout.
- M3 : `gectl schema` génère des JSON Schemas (`schemas/game/`) pour l'autocomplétion et la vérification dans l'éditeur ; réglages VS Code fournis. Documentation : `docs/MANIFEST.md`, ADR 0004.
- M4 : actions d'entrée (`ge::app::Actions`, `pressed`/`released`) et entrée scriptée (`ScriptedInput`) appliquée au début de chaque tick ; module d'exemple `action_control` (`move_x`, `move_y`).
- M4 : scénarios de test dans les jeux (`scenarios/*.json` : ticks, entrées scriptées, vérifications comme `hero.transform2d.Position.x > 100`), validés par `gectl validate` ; `gerun --test [--scenario NOM]` les exécute, chacun deux fois pour contrôler le déterminisme ; schéma éditeur des scénarios ; jeux de référence `sparks` et `pilot` testés en CI.
- M4 : chaque composant généré a une fonction `read` (lecture d'un champ par son nom) ; rendu nul (`NullRenderer`) et enregistreur (`RecordingRenderer`) ; le moteur est compilé avec `-ffp-contract=off`. Documentation : `docs/TESTING.md`, ADR 0005.
- M5 : cinq skills dans `.claude/skills/` (`game-from-prompt`, `engine-catalog`, `add-module`, `playtest-headless`, `add-level`) vérifiés par `scripts/check_skills.py` (commandes, options et fichiers cités) en CI et dans `check.sh`.
- M5 : `gectl new-module NOM [--depends A,B]` crée un module qui compile et passe son test ; CMake découvre les modules (`modules/*/module.json`) et leurs tests (`modules/<nom>/tests/`), `gerun` les lie tous : ajouter un module ne modifie aucun fichier CMake.
- M5 : module `pickup` (`Collector`, `Pickup`, système `collect`) et jeu `coin_dash`, premier jeu produit avec les skills ; un scénario peut choisir sa scène (`"scene"`) ; le catalogue annonce les ressources `Actions` et `ScriptedInput`. Documentation : `docs/AUTHORING.md`, ADR 0006.
- M2 : vérifications en CI : schéma, inclusions limitées aux dépendances déclarées, aucun code de plateforme dans les modules, code généré à jour.
- Exemple `hello_ecs`, README et ADR 0001–0002.
- CI : format, builds sur Linux, macOS et Windows, sanitizers, clang-tidy, couverture, WebAssembly, Android, iOS.
- Sécurité : CodeQL, Dependabot pour les actions GitHub.
- Release : publication GitHub sur tag `vX.Y.Z`.
- M6 : couche plateforme sans SDL (`ge_platform`) : interface `Backend` (entrées brutes, temps, rendu, son), `InputMap` et fichier `input.json` (sources `key:`, `mouse:`, réservées `pad:` et `touch:` → actions, validé par `gectl validate`, schéma éditeur), boucle temps réel `run_loop`, `ScriptedBackend`, ressource `LiveInput` appliquée au début de chaque tick, interface audio (`AudioDevice`, nul et enregistreur).
- M6 : backend SDL3 optionnel (`-DGE_BUILD_PLATFORM=ON`, préréglage `platform`) : fenêtre, clavier, souris, rendu 2D de carrés, son `.wav`, mode sans tête avec capture d'image ; `geplay GAME.json [--frames N] [--size WxH] [--headless] [--screenshot FILE.bmp]` ; module `debug_draw` (`Square`) et jeu de référence `move_sprite` ; CI sur Linux, macOS et Windows ; aucun en-tête SDL hors de `platform/sdl/` (vérifié). Documentation : `docs/PLATFORM.md`, ADR 0007.

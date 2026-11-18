# Changelog

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versions au format [SemVer](https://semver.org/lang/fr/).

## [Unreleased]

### Added

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
- M2 : vérifications en CI : schéma, inclusions limitées aux dépendances déclarées, aucun code de plateforme dans les modules, code généré à jour.
- Exemple `hello_ecs`, README et ADR 0001–0002.
- CI : format, builds sur Linux, macOS et Windows, sanitizers, clang-tidy, couverture, WebAssembly, Android, iOS.
- Sécurité : CodeQL, Dependabot pour les actions GitHub.
- Release : publication GitHub sur tag `vX.Y.Z`.

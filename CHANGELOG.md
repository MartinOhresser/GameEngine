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
- Exemple `hello_ecs`, README et ADR 0001–0002.
- CI : format, builds sur Linux, macOS et Windows, sanitizers, clang-tidy, couverture, WebAssembly, Android, iOS.
- Sécurité : CodeQL, Dependabot pour les actions GitHub.
- Release : publication GitHub sur tag `vX.Y.Z`.

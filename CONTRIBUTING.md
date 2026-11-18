# Contribuer

## Mise en place

Prérequis : CMake 3.25 ou plus, Ninja, et un compilateur C++20 (GCC 13, Clang 18, MSVC 2022 ou AppleClang récent). Pour les contrôles complets : `clang-format` et `clang-tidy` en version 18.

```sh
git clone <url-du-dépôt>
cd game-engine
./scripts/check.sh
```

Le script exécute en local les contrôles de la CI : format, builds Debug et Release avec tests, sanitizers (AddressSanitizer et UBSan), clang-tidy. Pour un cycle rapide :

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Les presets disponibles sont `dev`, `release`, `asan`, `tidy` et `coverage` (voir `CMakePresets.json`).

## Ce que la CI contrôle

Une pull request ne peut être fusionnée que si le contrôle **CI success** est vert. Il regroupe :

| Contrôle | Ce qu'il garantit |
| --- | --- |
| Lint des workflows | Les fichiers `.github/workflows` sont valides (actionlint) |
| Format | Le code suit `.clang-format` (clang-format 18) |
| Build et tests | Linux (GCC 13 Debug et Release, Clang 18), macOS (AppleClang), Windows (MSVC), avertissements traités comme des erreurs |
| Sanitizers | Aucune erreur mémoire ni comportement indéfini détecté par AddressSanitizer et UBSan |
| clang-tidy | Le code respecte `.clang-tidy` (bugs probables, règles C++ Core Guidelines, nommage) |
| Couverture | Au moins 90 % des lignes de `include/` et `src/` sont exécutées par les tests |
| WebAssembly, Android, iOS | Le cœur compile pour ces plateformes ; les tests tournent sur le build WebAssembly |

CodeQL analyse en plus le code chaque semaine et sur chaque pull request qui touche `include/`, `src/` ou le build.

## Règles du dépôt

- **Langues.** Code, commentaires et documentation de l'API en anglais ; documentation du projet (README, ADR) en français.
- **Pas de plateforme dans le cœur.** `ge_core`, `ge_ecs` et `ge_app` n'incluent aucun en-tête de système d'exploitation, de fenêtre, de rendu ou de réseau. Tout ce qui touche la plateforme vit dans un module séparé, derrière une interface. La CI le vérifie en compilant pour WebAssembly, Android et iOS.
- **Pas d'exceptions ni de RTTI.** Les moteurs de jeu les désactivent souvent sur mobile et consoles ; le build compile tout avec `-fno-exceptions -fno-rtti` sous GCC et Clang, donc `throw`, `try` et `dynamic_cast` ne compilent pas. Les erreurs se signalent par des valeurs de retour (`bool`, pointeur nul, `std::optional`) et `GE_ASSERT` pour les invariants.
- **Tests.** Tout comportement nouveau a un test dans `tests/`. Un test qui échoue doit dire pourquoi.
- **Style.** `clang-format` décide de la mise en forme ; ne la discutez pas en revue. Nommage : `lower_case` pour fonctions et variables, `CamelCase` pour types, suffixe `_` pour les membres privés.

## Commits et pull requests

## Branches

- **`main`** est la dernière grosse étape validée (aujourd'hui le MVP avec le Doodle Jump). On n'y commite jamais directement.
- **`dev`** reçoit toutes les pull requests. La CI passe sur chaque pull request, puis sur `dev` après la fusion. C'est là qu'on essaie les changements à la main.
- Une branche et une pull request par changement, de la forme `feat/...`, `fix/...`, `chore/...`, avec `dev` pour base. Une PR empilée sur une autre vise la branche de la première.
- Quand `dev` convient, on le **promeut** : onglet Actions, workflow « Promote dev to main », avec le numéro de version. Le workflow vérifie que le dernier commit de `dev` a passé la CI, avance `main` sur ce même commit (sans commit de fusion, donc rien de nouveau à tester), pose l'étiquette et lance la release. La toute première promotion se fait à la main (`git push origin dev:main`), parce que GitHub n'affiche un workflow que s'il existe déjà sur la branche par défaut.

## Commits et pull requests

Le titre de la pull request suit [Conventional Commits](https://www.conventionalcommits.org/) : `feat(ecs): ...`, `fix(app): ...`, `docs: ...`, `chore(ci): ...`. Les pull requests vers `dev` sont fusionnées par squash, donc ce titre devient le message de commit.

## Issues

- **Un manque dans le catalogue** (module, composant, système ou crochet moteur qu'un jeu demande) s'ouvre avec le formulaire « Module gap », avant d'écrire le code. La pull request qui le comble écrit `Closes #12` : l'issue est fermée à la fusion dans `dev`.
- **Un échec de CI** là où personne ne regarde (une poussée sur `dev`, l'analyse hebdomadaire, une release) ouvre tout seul une issue `ci-failure`, qui se ferme quand le workflow repasse au vert. Une pull request rouge n'ouvre pas d'issue.
- Ces workflows (`workflow_run`, `workflow_dispatch`) ne fonctionnent qu'une fois présents sur `main`, donc après la première promotion.

Les changements visibles par les utilisateurs du moteur s'ajoutent à la section « Unreleased » de `CHANGELOG.md`.

## Décisions d'architecture

Un choix structurant (dépendance majeure, API publique, règle de portabilité) se consigne dans `docs/adr/` avant d'être codé : contexte, décision, alternatives, conséquences.

## Publier une version

1. Sur `dev`, mettre à jour la version dans `project(...)` de `CMakeLists.txt` et le journal des changements.
2. Lancer « Promote dev to main » avec `vX.Y.Z` (exactement la version de `CMakeLists.txt`).

Le workflow de promotion pose l'étiquette et lance `release.yml`, qui vérifie la correspondance, relance build et tests, et publie la release GitHub. Pousser soi-même une étiquette `vX.Y.Z` marche aussi. Un tag contenant un tiret (`v0.2.0-rc.1`) est publié comme pré-version.

## Réglages GitHub à activer une fois

- Branche par défaut : `main` (la dernière étape validée).
- Suppression automatique des branches fusionnées (« Automatically delete head branches »).
- Protection de `main` et de `dev` : exiger le contrôle `CI success`. GitHub ne la propose pas sur un dépôt privé avec l'offre gratuite ; sans elle, c'est la discipline (et le workflow de promotion) qui garde `main` propre.
- Fusion par squash pour les pull requests vers `dev`.
- Code scanning (nécessaire à CodeQL ; sur un dépôt privé, il dépend de l'offre GitHub).

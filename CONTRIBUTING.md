# Contribuer

## Mise en place

Prérequis : CMake 3.25 ou plus, Ninja, et un compilateur C++20 (GCC 13, Clang 18, MSVC 2022 ou AppleClang récent). Pour les contrôles complets : `clang-format` et `clang-tidy` en version 18. Conseillé : `ccache`, que CMake utilise tout seul quand il est installé (un nouveau dossier de build ou un changement de branche se recompile alors en secondes ; `-DGE_CCACHE=OFF` pour s'en passer).

```sh
git clone <url-du-dépôt>
cd game-engine
./scripts/check.sh
```

Le script exécute en local les contrôles que la CI ne répète pas : builds Debug et Release avec tests, **sanitizers (AddressSanitizer et UBSan) et clang-tidy**. Il refait aussi le format, les modules et la validité des jeux, que la CI vérifie aussi. **Il doit passer avant chaque pull request** : la CI ne lance plus tidy ni les sanitizers. Pour un cycle rapide :

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Les presets disponibles sont `dev`, `release`, `asan`, `tidy` et `coverage` (voir `CMakePresets.json`).

## Ce que la CI contrôle

Une pull request ne peut être fusionnée que si le contrôle **CI success** est vert. La CI a **deux niveaux**, pour que l'attente reste courte :

- **Une pull request vers `dev`** lance les contrôles qui demandent une autre machine ; les exports réels et l'émulateur ne tournent que si elle touche le moteur ou l'export (`include/`, `src/`, `platform/`, `tools/gectl/`, `runner/`, CMake, `scripts/export*`, les workflows). Un jeu ou un module ajouté est prouvé par ses scénarios (en local) et par les builds.
- **Une pull request vers `main`, un push sur `main` et le workflow « Promote dev to main »** lancent tout, couverture et exports des jeux d'essai compris. Rien ne tourne au push sur `dev` : la pull request qui vient d'être fusionnée a déjà tourné.

| Contrôle | Niveau | Ce qu'il garantit |
| --- | --- | --- |
| Lint des workflows | toujours | Les fichiers `.github/workflows` sont valides (actionlint) |
| Format, modules, skills | toujours | `.clang-format` (clang-format 18) ; `module.json`, inclusions, skills cohérents avec `gectl` |
| Build et tests | toujours | Linux (GCC 13 Release, Clang 18 Debug), macOS (AppleClang), Windows (MSVC), avertissements traités comme des erreurs ; le lecteur SDL sur les trois systèmes |
| WebAssembly, Android (NDK), iOS | toujours | Le cœur et les modules compilent pour ces plateformes (c'est là que `std::to_chars` flottant, absent d'iOS et de WebAssembly, se découvre) |
| Export Windows et Linux | moteur touché | `gectl export` fait un zip d'un jeu qui se joue seul ; sur `main`, les trois jeux d'essai et les scripts du lanceur aussi |
| APK Android | moteur touché | Trois jeux (`doodle_jump`, `game_2048`, `tower_defense`) exportés en APK et joués sur un émulateur : un toucher, un doigt tenu, des glissés, un écran plus étroit que le jeu |
| Couverture | `main` | Au moins 90 % des lignes de `include/`, `src/`, `tools/` et `modules/` sont exécutées par les tests |

**Pas en CI, seulement dans `check.sh`** : AddressSanitizer et UBSan, clang-tidy (`.clang-tidy`), le build Debug avec GCC et les scénarios de tous les jeux, y compris ceux joués sur un écran de téléphone (`"screen"` dans un scénario, voir [TESTING.md](docs/TESTING.md)). Une contribution extérieure qui les enfreint passe donc la CI : à relire en revue.

**Les échecs d'infrastructure** (Maven Central « 429 », miroirs apt « 503 ») sont repris automatiquement (`scripts/ci_retry.sh`, cache de Gradle) ; un job qui échoue encore pour cette raison se relance sans rien changer.

CodeQL analyse le code chaque semaine, sur `main` et sur les pull requests vers `main`.

## Règles du dépôt

- **Langues.** Code, commentaires et documentation de l'API en anglais ; documentation du projet (README, ADR) en français.
- **Pas de plateforme dans le cœur.** `ge_core`, `ge_ecs` et `ge_app` n'incluent aucun en-tête de système d'exploitation, de fenêtre, de rendu ou de réseau. Tout ce qui touche la plateforme vit dans un module séparé, derrière une interface. La CI le vérifie en compilant pour WebAssembly, Android et iOS.
- **Pas d'exceptions ni de RTTI.** Les moteurs de jeu les désactivent souvent sur mobile et consoles ; le build compile tout avec `-fno-exceptions -fno-rtti` sous GCC et Clang, donc `throw`, `try` et `dynamic_cast` ne compilent pas. Les erreurs se signalent par des valeurs de retour (`bool`, pointeur nul, `std::optional`) et `GE_ASSERT` pour les invariants.
- **Tests.** Tout comportement nouveau a un test dans `tests/`. Un test qui échoue doit dire pourquoi.
- **Pas d'heure dans les tests.** Un test ne dépend ni de la vitesse de la machine ni du nombre de ticks qu'une boucle en temps réel a fait tenir dans N images (un runner lent en fait plus, un rapide moins) : il utilise le `ScriptedBackend` (horloge à lui) ou attend une condition (`tant que non vu`, avec une borne) et cherche ce qu'il attend au lieu de le calculer. Un test qui échoue une fois sur dix se corrige tout de suite, il ne se relance pas : le workflow « Flaky tests » (chaque lundi, ou à la demande) répète les tests de la plateforme 30 fois sur les trois systèmes pour les trouver.
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

Le workflow de promotion pose l'étiquette et lance `release.yml`, qui vérifie la correspondance, relance build et tests, et publie la release GitHub, à laquelle des jobs Windows ajoutent les zips autonomes des jeux de référence (`scripts/export.cmd`, voir `docs/EXPORT.md`). Pousser soi-même une étiquette `vX.Y.Z` marche aussi. Un tag contenant un tiret (`v0.2.0-rc.1`) est publié comme pré-version.

## Réglages GitHub à activer une fois

- Branche par défaut : `main` (la dernière étape validée).
- Suppression automatique des branches fusionnées (« Automatically delete head branches »).
- Protection de `main` et de `dev` : exiger le contrôle `CI success`. GitHub ne la propose pas sur un dépôt privé avec l'offre gratuite ; sans elle, c'est la discipline (et le workflow de promotion) qui garde `main` propre.
- Fusion par squash pour les pull requests vers `dev`.
- Code scanning (nécessaire à CodeQL ; sur un dépôt privé, il dépend de l'offre GitHub).

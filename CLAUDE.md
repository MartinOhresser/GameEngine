# CLAUDE.md — Game Engine

Ce fichier donne à Claude Code le contexte du projet, son objectif, les règles de travail et l'historique des demandes de l'utilisateur (Martin). Lis-le en entier avant d'agir, puis `README.md`, `CONTRIBUTING.md` et `docs/ROADMAP.md`.

## 1. Objectif du projet

Un **moteur de jeu en C++20, basé sur un ECS hyper modulable et très générique**. Ajouter un composant ou un système doit être très facile et ne jamais demander de modifier le cœur.

Ambition à terme : pouvoir faire **n'importe quel type de jeu** :

- 2D ou 3D ;
- entrées clavier, souris, manette, tactile ;
- solo ou réseau (lobby, matchmaking) ;
- interfaces : HUD, menus, inventaires ;
- plateformes : Linux, macOS, Windows, Android, iOS, WebAssembly.

Pari central : **un jeu est du JSON pur** (manifeste, préfabs, scènes, scénarios de test, entrées). Claude écrit ou modifie un jeu à partir d'un simple prompt, sans C++, en assemblant des **modules** existants. Quand le catalogue ne couvre pas un besoin, on **crée ou étend un module générique** (jamais du code propre à un jeu).

Ce que le projet n'est pas : un éditeur visuel, ni un concurrent d'Unity/Godot. C'est un socle propre, testé, extensible.

## 2. Architecture

| Dossier | Rôle |
| --- | --- |
| `include/ge/`, `src/` | Cœur : `core`, `ecs`, `app` (boucle, temps, actions, scènes), `json`, `module`, `game` (chargement, scénarios, export), `platform` (interfaces) |
| `modules/<nom>/` | Un module = `module.json` (source de vérité) + `src/` (systèmes) + `tests/` + `gen/` (code **généré**, ne jamais éditer) + `include/` (en-têtes offerts aux dépendants, facultatif) |
| `games/<nom>/` | Un jeu = `game.json` + `prefabs/` + `scenes/` + `scenarios/` + `input.json` (+ `tools/make_assets.py` qui génère images et scènes) |
| `platform/sdl/` | Seul endroit qui connaît SDL3 (fenêtre, rendu, entrées, audio) ; `platform/android/` : gabarit Gradle, script d'émulateur |
| `tools/gectl/` | CLI : `new-module`, `check`, `generate`, `schema`, `catalog`, `list`, `validate`, `export` (qui rejoue aussi les scénarios) |
| `tools/launcher/` | Le lanceur de jeux, lui-même un jeu JSON |
| `runner/` | `gerun`/`geplay` : joue un jeu ou ses scénarios |
| `schemas/` | Schémas JSON (module, jeu, préfab, scène, scénario, entrées), générés par `gectl schema` |
| `docs/` | Docs en français ; `docs/adr/` : décisions (0001 à 0013) ; `docs/MODULES.md`, `AUTHORING.md`, `TESTING.md`, `UI.md`, `EXPORT*.md`, jeux (`GAME_*.md`) |
| `.claude/skills/` | Skills du dépôt : `engine-catalog`, `game-from-prompt`, `add-module`, `add-level`, `playtest-headless` (cohérence vérifiée par `scripts/check_skills.py`) |

Jeux existants : `doodle_jump`, `treasure_run`, `coin_dash`, `sideways_jump`, `sparks`, `pilot`, `move_sprite` (référence), et les trois jeux d'essai de l'ADR 0013 : `game_2048`, `chess`, `tower_defense`.

Modules (37) : `transform2d`, `camera2d`, `sprite2d`, `text2d`, `collision2d`, `combat2d`, `health`, `bounds2d`, `gravity2d`, `bounce2d`, `action_control`, `touch_controls`, `gesture`, `screen_wrap`, `spawner2d`, `lifetime`, `patrol2d`, `pickup`, `progress`, `phase`, `scene_switch`, `tween2d`, `debug_draw`, `host`, `records`, `ui`, `inventory`, `grid2d`, `grid_moves`, `turns`, `slide_merge`, `rules`, `path2d`, `autofire`, `waves`, `economy`, `tag`.

### Règles d'un module (vérifiées par `scripts/check_modules.py` et clang-tidy)

- Composants à champs **scalaires** seulement (`i32 u32 i64 u64 f32 f64 bool`), liens par `ref` / `ref_set` vers des listes du jeu.
- Systèmes déterministes (hasard via `Random`, temps via `Time`), simulation en `FixedUpdate`, entrées via `app::Actions` (jamais un clavier), pas de création/destruction d'entité pendant `world.each` (passer par `Commands`).
- Aucun code de plateforme. Le code livré n'inclut que les en-têtes du moteur, du module et de ses `depends`. **`depends` sert à ce que le code livré utilise vraiment** (un jeu doit lister les dépendances de chaque module qu'il utilise) ; les tests d'un module peuvent inclure n'importe quel module.
- Pas d'exceptions ni de RTTI ; pas de tableaux C, complexité cognitive ≤ 25, pas de `++` dans une expression (clang-tidy) ; pas de `std::to_chars` / `std::from_chars` flottants (absents sur iOS et WebAssembly) ; code portable GCC, Clang, MSVC, AppleClang, NDK, Emscripten.
- Un module doit être **le plus générique possible** : utilisable par n'importe quel jeu, aucun mot propre à un jeu ou à un genre dans son code, ses noms ni ses descriptions (un genre n'y est qu'un exemple parmi d'autres). **Il peut ne servir aucun jeu du dépôt s'il peut servir un futur jeu** (règle de Martin, 21 novembre 2026). Préférer étendre un module existant à un doublon.

## 3. Commandes utiles

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev   # cycle rapide
CLANG_FORMAT=clang-format-18 ./scripts/check.sh                          # contrôles locaux complets (~25-30 min à froid ; incrémental + ccache ensuite)
./build/dev/tools/gectl catalog                  # modules, composants, réglages disponibles
./build/dev/tools/gectl new-module <nom> --depends a,b
./build/dev/tools/gectl generate                 # code généré des modules (obligatoire après toute modif de module.json)
./build/dev/tools/gectl schema                   # schémas JSON
./build/dev/tools/gectl validate games/<jeu>/game.json
./build/dev/runner/gerun games/<jeu>/game.json --test   # rejoue les scénarios sans fenêtre
./build/dev/tools/gectl list                     # jeux du dépôt et validité
./build/dev/tools/gectl export games/<jeu>/game.json --target windows|linux|android
./gameEngine.sh                                  # compile et ouvre le lanceur
```

Scénarios (`docs/TESTING.md`) : entrées scriptées et vérifications sans fenêtre ; la clé facultative `"screen": {"width","height"}` joue le scénario sur un écran de téléphone (vue adaptée, pointeur en pixels de cette vue).

## 4. Workflow git, PR et CI

- **Les PR visent toujours `dev`, jamais `main`.** `main` avance par le workflow « Promote dev to main ». Pas de commit direct sur `dev` ni `main`.
- Avant chaque push : `CLANG_FORMAT=clang-format-18 ./scripts/check.sh` doit passer (AddressSanitizer, clang-tidy, tests, format, modules, skills, validité des jeux et scénarios ne tournent **pas** en CI). Pour un commit qui ne touche aucun C++ (workflows, docs), un contrôle ciblé suffit.
- Commits en conventional commits (le titre de PR aussi, contrôlé par la CI). Pied de commit : `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` et `Claude-Session: <url de session>` ; corps de PR en français, terminé par `🤖 Generated with [Claude Code](https://claude.com/claude-code)` et l'URL de session.
- Dépôt : `MartinOhresser/GameEngine`. Avec `gh`, GraphQL est bloqué dans l'environnement cloud : utiliser l'API REST (`gh api repos/MartinOhresser/GameEngine/pulls ...`, fusion par `gh api -X PUT .../pulls/N/merge -f merge_method=merge`).
- **CI à deux niveaux** (`.github/workflows/ci.yml`, refonte de la PR #61) : une PR vers `dev` lance builds/tests (GCC, Clang, macOS, Windows MSVC, WebAssembly, NDK, iOS, SDL), format, modules, skills, lint des workflows ; exports réels et émulateur Android seulement si le moteur ou l'export changent (job `changes`). `main` / promotion / PR vers `main` : tout, avec couverture ≥ 90 % et exports des trois jeux d'essai. Un push sur `dev` relance le niveau des PR, seulement pour réchauffer les caches (personne ne l'attend ; un échec y ouvre une issue `ci-failure`).
- **Caches** : chaque job qui compile passe par `.github/actions/ccache` (ccache, utilisé tout seul par CMake via `cmake/GeCompilerCache.cmake`, y compris dans les builds lancés par `gectl export`). **Pas de cache apt** : `cache-apt-pkgs-action` a été essayé et retiré (sans `apt-get update`, il prend des 404 pour un succès et met en cache une liste vide). Export Windows : en PR, `gectl export` avec Ninja + ccache (environnement MSVC, `CMAKE_GENERATOR=Ninja`) ; à la promotion seulement, `scripts\export.cmd` comme un utilisateur (générateur Visual Studio, pas de ccache, compilé sur tous les cœurs : `--parallel`, `UseMultiToolTask`). Un nouveau job qui compile doit utiliser l'action `ccache`.
- Échecs d'infrastructure fréquents : Maven Central « 429 », apt « 503 » → `scripts/ci_retry.sh`, cache Gradle, miroir Maven (`scripts/ci_gradle_mirror.sh`, CI seulement), action `.github/actions/sdl-deps`. Un job qui tombe pour cette raison se relance sans rien changer.
- Un gabarit Android livré aux utilisateurs (`platform/android/template`) ne doit pas être modifié pour les besoins de la CI.

## 5. Environnement de travail

Depuis le 20 novembre 2026, Martin travaille dans **Claude Code (application de bureau) sur son PC Windows 11** :

- Dépôt : `C:\Users\marti\Desktop\GameEngine`. Visual Studio 2026 Community (CMake fourni dans `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`, pas dans le PATH d'un PowerShell ordinaire), WSL 2 Ubuntu pour `check.sh`, `gh` dans `C:\Program Files\GitHub CLI\gh.exe` (connecté). Pas de Python côté Windows (seulement dans WSL). L'exécution de scripts `.ps1` est désactivée : passer par des `.cmd` ou bash.
- **Il veut voir les commandes longues dans un terminal visible** (panneau Terminal de l'app) : builds, tests, exports, `check.sh`.
- Les dossiers `build/<preset>` du dépôt Windows sont des builds MSVC : **ne pas lancer de build Linux depuis WSL dans `/mnt/c/...`** (même dossiers, et le disque Windows vu de WSL est lent). Les builds et `check.sh` côté Linux se font dans un clone sur le disque de WSL, `~/ge-verify`, qui récupère la branche du dépôt Windows (`git fetch /mnt/c/Users/marti/Desktop/GameEngine <branche>`). Outils de WSL (installés) : ccache, clang, clang-tidy-18, clang-format-18. **Formater avec le `clang-format-18` de WSL**, pas avec celui de Visual Studio (version 22, qui diffère de la CI sur certaines lignes). clang 18 y prend par erreur la chaîne GCC 14 sans sa libstdc++ : configurer le preset `tidy` avec `-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13`.

Ancien environnement (sandbox cloud du projet Claude, avant le 20 novembre 2026), pour mémoire :

- Réseau du shell coupé : **pas d'installation de paquets** (pip, npm, apt). Travailler avec les outils présents (clang-format-18, g++, cmake, gradle, Python). Si un outil manque, le dire.
- Proxy sortant : ne jamais contourner un refus, le signaler. `sleep` long bloqué : attendre avec une boucle bornée ou une tâche de fond.
- Ne pas éditer pendant qu'un `check.sh` compile.
- Les docs de projet durables vivent aussi dans le projet Claude « Game Engine » (outil Projects) : `claude/adr-0012-export-android.md`, `claude/adr-0013-2048-echecs-tower-defense.md`.

## 6. Historique des demandes de l'utilisateur (dans l'ordre)

1. **Socle** : ECS modulable, jeux décrits en JSON, modules générés (`module.json` → `gectl generate`), scénarios, skills du dépôt (ADR 0001–0006), plateforme SDL3 et rendu 2D (0007–0008), interface et inventaire par données (0009), monde généré et sauvegarde (0010), lanceur et export Windows/Linux (0011).
2. **ADR 0012 — export Android** : `gectl export --target android` (APK via NDK + Gradle), CI avec émulateur qui joue le jeu (tape, glisse, joystick) ; plein écran réel sur Android.
3. **ADR 0013 — trois jeux sans C++** (G1–G11 faits) : `game_2048` (module `slide_merge`), `chess` (échecs complets : `grid_moves`, `turns`, `grid2d`, `rules`), `tower_defense` (`path2d`, `autofire`, `waves`, `economy`, `health`, `bounds2d`). Chaque besoin a donné un module **générique**. PR #60 fusionnée dans `dev`. Optionnel non fait : G12 `sfx`.
4. **« Vérifie que tous les nouveaux modules sont bien génériques… »** puis **« enchaîne toutes les PR »** : audit de généricité des modules créés pour les trois jeux, PR enchaînées vers `dev`.
5. **« La CI a échoué » / « ça a re-fail »** : correction des échecs d'export Android (fenêtre 24 px trop courte à cause de la barre d'état → plein écran ; Maven 429).
6. **Demande en trois parties (20 novembre 2026)** :
   1. **Nettoyer la CI GitHub** : trop longue (≈ 2/3 du temps du projet passé dessus) et trop instable. Chercher les récurrences d'échec et **adapter les skills** (ajout/modification de modules, création de jeux…) pour les éviter ; **ne tester sur GitHub que ce qui ne peut pas l'être en local**. → **Fait** : PR #61 fusionnée dans `dev` (CI à deux niveaux, ~10 min de temps de mur au lieu de ~30 pour l'export Windows seul, reprises et miroir Maven, scénarios `"screen"` pour tester les touchers de téléphone en local). **Reste** : intégrer les leçons de CI dans les skills (voir §7).
   2. **Vérifier, un par un, que tous les skills (et modules) sont génériques**, sans spécialisation à un jeu ou à un genre. Cas cités : **l'inventaire de treasure run et l'échiquier, ou les tuiles en général, semblent faire doublon.** → **En cours** (audit fait, correctifs partiels, voir §7).
   3. **Ensuite seulement** : discuter de pistes pour **améliorer les graphismes** en ajoutant d'autres modules de rendu avec un autre style graphique. Discussion, pas de code avant d'avoir fini 1 et 2.
7. **20 novembre 2026, 21 h 49** : « C'est pas du tout assez efficace. On arrête tout. » → passage du projet Claude (sandbox cloud) à **Claude Code sur son PC** (§5) ; le travail non poussé de la branche `chore/audit-modules` du sandbox a été perdu.
8. **20–21 novembre 2026, Claude Code** : « Je veux que la CI soit plus optimisée et rapide » → PR #62 à #64 (ccache local et en CI, export Windows des PR avec Ninja + ccache, clés de cache) : une PR passe de ~10 min à ~5,5 min. Puis « refait l'audit des modules : le plus générique possible, pouvoir servir à n'importe quel jeu ; un module qui ne sert à aucun jeu mais peut servir un futur jeu, c'est bien aussi » → branche `refactor/audit-modules` (§7).

## 7. État de l'audit des modules (21 novembre 2026)

### Fait sur `refactor/audit-modules`

- Nouveau module **`tag`** (`tag.Tag`, `ge/mod/tag/find.hpp` : `tag::find`, `tag::tagged`) : remplace `ui.Tag`, qui n'avait rien d'UI ; la recherche « entité la plus ancienne portant ce nom » existait en 4 copies (`ui`, `rules`, `records`, `path2d`).
- `depends` réduits au code livré (`autofire`, `economy`, `grid_moves`, `path2d`, `rules`, `slide_merge`) ; `check_modules.py` laisse les tests d'un module inclure n'importe quel module.
- `action_control.Controlled.action_x/action_y` (défaut `move_x`/`move_y`) ; `bounce2d.Bouncy.kills` retiré (identique à `vanish`) ; commentaires d'`economy` sans « tower ».
- Jeux, scènes, préfabs, `make_assets.py`, docs, tests et schémas migrés ; skill `add-module` et règle de généricité (§2) mis à jour. Les ADR 0009 et 0013 citent encore `ui.Tag` (historique).

### Décisions en attente de Martin (ne pas coder avant)

1. **Doublon grille / curseur** (gros changement cassant, ADR 0014 à écrire d'abord) : `ui.Grid`/`ui.Cell` (mise en page de HUD par index, écran) font le travail de `grid2d.Grid`/`GridPos` (cases, monde) ; trois curseurs : `ui.Menu` (clavier/manette + pointeur sur boutons), `inventory.Hand` (touches + pointeur sur les `Slot`), `grid2d.GridCursor` (pointeur seulement, via la caméra). Usages : `ui.Grid/Cell` dans treasure_run et doodle_jump ; `Hand` dans treasure_run ; `GridCursor` dans chess, tower_defense, economy, grid_moves. Piste : `grid2d.Grid` avec espacement et mode écran, `GridCursor` avec navigation clavier/manette, `Hand` qui lit un `GridCursor`, retrait de `ui.Grid/Cell`. Obstacle : ordre `ui.anchor` → placement de grille sans faire dépendre `ui` de `grid2d`/`camera2d`.
2. **`spawner2d.Generator`** : choix de préfabs en emplacements figés (`prefab`, `alt_prefab`, `alt2_prefab`, `extra_prefab`, chacun avec chance de départ et de fin) taillés pour doodle_jump/sideways_jump ; plus générique : une liste d'entités « choix » (préfab, poids, progression), comme `grid_moves.Pattern`. Cassant pour ces deux jeux.
3. **Ramassage** : `pickup.Collector.score` additionne dans son propre champ, `economy.Wallet` et `inventory` ont leurs propres façons de recevoir ; piste : un `Pickup` qui ajoute sa valeur à n'importe quel champ (comme `rules.OnAction`).
4. Petits : `debug_draw.Square` = `Rect` carré (10 fichiers de jeux l'utilisent) ; `patrol2d` (aller-retour entre deux limites) et `path2d` (suite de points) restent distincts.

### Reste à faire ensuite

- **Skills** : créer `docs/GAMES.md` (besoin → jeu de référence → doc) et y faire pointer les skills ; corriger les affirmations périmées (le tactile existe, le README dit encore « ni fenêtre, ni rendu ») ; retirer les listes de modules figées de `engine-catalog` ; dans `add-module`, ajouter les leçons de CI (pas de `to_chars/from_chars`, portabilité GCC/Clang/MSVC/iOS/WASM, tests nécessaires pour la couverture ≥ 90 % sur `main`) ; dans `playtest-headless` / `game-from-prompt`, documenter `"screen"`. Garder `scripts/check_skills.py` vert.
- **Rappels à donner à Martin** : promouvoir `dev` → `main` (workflow « Promote dev to main »), activer « Automatically delete head branches », lancer une fois le workflow « Flaky tests ».
- **Ensuite seulement** : discussion sur les graphismes (modules de rendu alternatifs ; `docs/RENDERING.md`, ADR 0008).

## 8. Façon de travailler avec Martin

- Il parle **français** ; documentation du projet en français, code et commentaires d'API en anglais.
- **Il déteste les longs silences et le temps perdu.** Envoyer régulièrement un court message de statut (une ou deux phrases) quand une tâche dure ; dire d'emblée ce qu'on fait.
- **Efficacité d'abord** : des étapes courtes, vérifiées localement et ciblées ; éviter les refontes tentaculaires lancées d'un bloc et les vérifications de 30 minutes quand un contrôle ciblé suffit. Un `check.sh` complet seulement avant un push de code C++.
- Pour un changement **cassant et large** (migration de nombreux fichiers de jeux, nouvelle architecture), **cadrer et demander** avant de coder, plutôt que de découvrir l'ampleur en cours de route.
- Il a autorisé d'enchaîner les PR vers `dev` quand le travail est clair, mais pas de toucher `main`.
- Les tâches doivent déboucher sur quelque chose d'utilisable (PR fusionnable, doc à jour), pas sur un compte rendu d'efforts.

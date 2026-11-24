---
name: add-module
description: Créer ou étendre un module du moteur (composants, systèmes, tests) quand le catalogue ne couvre pas un besoin de jeu. À utiliser quand engine-catalog ne trouve pas de module adapté.
---

# Ajouter un module

Un module est un dossier `modules/<nom>/` qui ajoute des **composants** (données) et des **systèmes** (logique). Il passe par une branche, une PR et la CI, comme tout code. Un module doit être **le plus générique possible** : utilisable par n'importe quel jeu, sans rien de propre à un jeu ou à un genre (ni dans les noms de composants et de champs, ni dans le code, ni dans les descriptions, où un genre n'apparaît que comme un exemple parmi d'autres). Il n'a pas besoin de servir un jeu du dépôt s'il peut clairement servir un futur jeu ; mais préfère toujours étendre un module existant à créer un doublon.

- Un comportement de jeu se décrit par des **données** (champs, actions, entités qui décrivent des règles), pas par un cas particulier : pas de champ qui ne sert qu'à un jeu (« kills » pour le monstre d'un jeu de plateformes là où « vanish » suffit), pas de nom d'action écrit en dur (un champ `ref: "action"`, avec l'ancien nom par défaut).
- `depends` liste ce que le **code livré** (`src/`, `include/`) inclut, rien d'autre : un jeu doit lister ces modules. Les tests (`tests/`) peuvent inclure n'importe quel module.
- Ce qui sert à plusieurs modules sans appartenir à l'un d'eux (retrouver une entité par son nom : `tag`) est un petit module à part, pas un composant glissé dans un module voisin.

## Étapes

1. **Vérifier qu'il en faut un** : relis le catalogue (skill `engine-catalog`). Un petit ajout à un module existant (un champ, un système) vaut mieux qu'un doublon.
2. **Issue puis branche** : ouvre une issue avec le formulaire « Module gap » (`gh issue create --title "[module gap] <besoin>" --label enhancement`, en disant quel jeu en a besoin et ce que le catalogue offre déjà), puis `git checkout -b feat/module-<nom>` à partir de `dev`. Une issue par manque réel, pas une par petit ajout ; ne t'arrête pas à attendre un tri.
3. **Créer le squelette** : `./build/dev/tools/gectl new-module <nom> --depends transform2d` (liste les modules dont tu utilises les composants). Il crée un module qui compile et passe son test :
   - `modules/<nom>/module.json` : la fiche (source de vérité)
   - `modules/<nom>/src/<nom>.cpp` : la logique des systèmes
   - `modules/<nom>/tests/test_<nom>.cpp` : le test
   - `modules/<nom>/gen/` : code généré, ne le modifie jamais à la main
4. **Décrire dans `module.json`** : composants (`name`, `description`, `replicable`, `fields` avec `type` et `default`), systèmes (`name`, `description`, `stage`, `reads`, `writes`, `after`, `before`), réglages éventuels (`params` avec `min`/`max`). Remplace l'exemple `Example`/`step`. Sois précis dans les `description` : c'est ce que lira le prochain `engine-catalog`.
5. **Générer** : `./build/dev/tools/gectl generate <nom>` puis relancer `cmake --preset dev` (le module est trouvé tout seul, aucun fichier CMake à éditer).
6. **Écrire les systèmes** dans `src/<nom>.cpp` : une fonction par système déclaré, de signature `void <système>(ecs::World& world)`, dans `namespace ge::mod::<nom>`.
7. **Écrire les tests** dans `modules/<nom>/tests/` : au minimum un test par système, qui installe le module (et ses dépendances), crée des entités, fait tourner `app.run_frames(n)` et vérifie le résultat. Le test d'exemple montre la forme.
8. **Utiliser le module** dans un jeu (skill `game-from-prompt`) et prouver son effet par un scénario.
9. **Contrôler** : `TIDY=changed ./scripts/check.sh` (clang-tidy sur ce que la branche change). Puis commit, push, PR **vers `dev`** (jamais vers `main`), avec `Closes #<issue>` dans la description.

## Règles de code d'un système

- **Déterministe** : même graine et mêmes actions donnent le même état. Pas d'horloge, pas de `rand`, pas de `std::random_device` : le hasard vient de `world.resource<Random>()`, le temps de `Time` (`fixed_delta`, `tick`). N'itère pas un conteneur non ordonné (`unordered_map`) d'une façon qui change le résultat.
- **Simulation dans `FixedUpdate`**. `Update` et `PostUpdate` sont pour la présentation.
- **Pendant `world.each`**, on ne crée ni ne détruit d'entité ni de composant : passe par `world.resource<ecs::Commands>()` (`despawn`, `insert`, `remove`), appliqué après le système. Pour comparer des entités entre elles, copie d'abord ce qu'il faut dans un `std::vector`, puis boucle.
- **Entrées** : lis `world.resource<app::Actions>()` (`value`, `down`, `pressed`, `released`), jamais un clavier.
- **Aucun code de plateforme** (pas de `<windows.h>`, SDL, etc.) et seulement des inclusions du module, de ses dépendances déclarées et de `ge/...`. `scripts/check_modules.py` le vérifie.
- Types de champs : `i32 u32 i64 u64 f32 f64 bool`. Pas de texte ni de pointeur. Une autre entité se désigne par une étiquette (`"ref": "tag"`, `ge/mod/tag/find.hpp`) ou par `transform2d.Owner` ; un nombre choisi par le jeu, par un champ `"ref": "field"` lu dans `app::FieldTable`.
- Plusieurs variantes d'un comportement (animations, façons de bouger) : une entité par variante, rangée dans un ensemble nommé par une liste du jeu, pas des champs `truc_1`, `truc_2`.
- Une entité qui a `transform2d.Disabled` ne fait rien : un module qui la fait agir l'ignore (c'est ainsi qu'on fige un personnage pendant une cinématique).
- Ordre entre systèmes : `after`/`before` dans `module.json`, avec des noms complets `module.système`, **seulement** vers le module lui-même ou ses `depends`. Sans dépendance, le résultat doit être le même quel que soit l'ordre : lis un fait stable (`collision2d.Blocked.below`) plutôt qu'une valeur qui change au milieu du tick.
- Nommage : modules et systèmes en `snake_case`, composants en `PascalCase`.

## Ce qui casse la CI

La CI compile sur GCC, Clang, MSVC, AppleClang, le NDK Android, Emscripten et iOS ; `check.sh` ajoute clang-tidy et les sanitizers. Les pièges déjà rencontrés (le détail est dans `docs/MODULES.md`, « Recettes ») :

- pas de `std::to_chars` / `std::from_chars` sur des flottants (absents sur iOS et WebAssembly), pas d'exceptions, de RTTI ni de tableaux C ;
- conversions explicites : un `float` passé où un `double` est attendu demande `static_cast<double>` (clang `-Wdouble-promotion`) ; MSVC refuse les conversions qui perdent de la précision ;
- complexité cognitive ≤ 25 par fonction, **`CHECK` compris** dans les tests : coupe un long test en plusieurs ;
- couverture ≥ 90 % sur `main` : un test par branche d'un système, pas seulement le cas heureux ;
- formate avec `clang-format-18` (une autre version change certaines lignes).

## Après

Indique dans la PR : le besoin de jeu qui a motivé le module, ses composants et systèmes, et le scénario qui le prouve.

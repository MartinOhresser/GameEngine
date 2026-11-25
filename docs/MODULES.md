# Modules

Un module ajoute des composants et des systèmes au moteur sans toucher au noyau. Il est décrit par un fichier `module.json`, dont le moteur génère le code des composants et l'enregistrement des systèmes. Il ne reste à écrire à la main que la logique de chaque système.

## Structure d'un module

```
modules/<nom>/
  module.json            la description (source de vérité)
  src/<nom>.cpp          les systèmes, écrits à la main
  gen/                   code généré par `gectl generate`, versionné
```

Le dossier porte le même nom que le module. Les modules sont des bibliothèques statiques : pas de chargement dynamique, donc le même code fonctionne sur desktop, mobile et web.

## `module.json`

```json
{
  "$schema": "../../schemas/module.schema.json",
  "schema_version": 1,
  "name": "screen_wrap",
  "version": "0.1.0",
  "description": "Les entités qui sortent d'un côté reviennent de l'autre.",
  "depends": ["transform2d"],
  "components": [
    {
      "name": "WrapArea",
      "replicable": false,
      "fields": [
        { "name": "width", "type": "f32", "default": 800 },
        { "name": "height", "type": "f32", "default": 600 }
      ]
    }
  ],
  "systems": [
    {
      "name": "wrap",
      "stage": "FixedUpdate",
      "reads": ["WrapArea"],
      "writes": ["transform2d.Position"],
      "after": ["transform2d.integrate"]
    }
  ]
}
```

| Clé | Rôle |
| --- | --- |
| `name`, `version` | Nom en `snake_case` et version `x.y.z`. |
| `depends` | Modules utilisés. Ils doivent être installés avant celui-ci. |
| `params[]` | Réglages que le manifeste d'un jeu peut changer : `name`, `type`, `default` (obligatoire), `min` et `max` optionnels (types numériques). |
| `components[].replicable` | Obligatoire : le composant fait-il partie de l'état qu'un jeu en réseau répliquerait ? |
| `components[].fields[]` | `name`, `type` (`i32 u32 i64 u64 f32 f64 bool`), `default` optionnel (zéro sinon), `ref` optionnel (champ `u32` seulement, `snake_case`) : le champ désigne un élément d'une liste du jeu (`"ref": "atlas"` pour un atlas) ; les fichiers de jeu peuvent écrire son nom au lieu de son numéro ; `ref_set` est le même principe pour un ensemble de noms (un masque de 32 bits au plus : des couches, des catégories), écrit comme un tableau de noms. |
| `systems[].stage` | `Startup`, `PreUpdate`, `FixedUpdate`, `Update` ou `PostUpdate`. La logique de jeu va dans `FixedUpdate`. |
| `systems[].reads`, `writes` | Composants touchés : `Nom` pour le module, `module.Nom` pour une dépendance. |
| `systems[].after`, `before` | Ordre par rapport à d'autres systèmes, même notation. |
| `systems[].while_paused` | `true` : un système de `FixedUpdate` qui tourne encore quand le jeu est en pause (`Time::paused`, mis par un `toggle.Toggle` qui a `pauses`). Les autres attendent. Réservé à ce qui doit marcher dans un menu de pause : `ui`, `phase`, `scene_switch`, `host`, `toggle`. |

Le schéma `schemas/module.schema.json` donne l'autocomplétion dans les éditeurs. `gectl check` applique les mêmes règles, plus celles qu'un schéma ne sait pas exprimer : références entre modules, dépendances déclarées, cycles, nom du dossier.

## Ce qui est généré

`gectl generate` écrit dans `gen/` :

- un `struct` par composant, avec ses valeurs par défaut, `operator==` et `hash` (donc inclus dans l'empreinte du monde) ;
- une structure `Params` (si le module déclare des `params`), stockée comme ressource : `world.resource<Params>()` ;
- pour chaque composant, une fonction qui le construit sur une entité à partir de valeurs lues dans un fichier de jeu (les champs absents gardent leur défaut) ; c'est ce qui permet de créer des entités depuis du JSON sans code ;
- la déclaration de chaque système, `void nom(ecs::World&)`, que le module doit définir : un système oublié fait échouer l'édition de liens ;
- `info()`, la description du module, et `install(App&)`, qui enregistre le module et ses systèmes sous le nom `module.systeme`, avec leur ordre.

Les fichiers générés sont versionnés pour que les cibles croisées (Android, iOS, WebAssembly) n'aient pas besoin de lancer `gectl`. La CI échoue s'ils ne correspondent plus au `module.json` (`gectl generate --check`).

## Utiliser un module

```cpp
ge::app::App app;
ge::mod::transform2d::install(app);
ge::mod::screen_wrap::install(app); // après transform2d, sa dépendance
// app.module_problems() et app.schedule().problems() doivent être vides.
```

## `gectl`

| Commande | Effet |
| --- | --- |
| `gectl check` | Valide tous les `module.json` et leurs liens. |
| `gectl catalog [--compact]` | Affiche en JSON ce que le moteur et les modules offrent : c'est ce que lit Claude pour choisir des modules. |
| `gectl generate [--check] [module...]` | Écrit (ou vérifie) le code généré. |
| `gectl new-module NOM [--depends A,B]` | Crée `modules/NOM` avec un exemple qui compile et passe son test. |
| `gectl validate JEU.json` | Valide un jeu (voir [MANIFEST.md](MANIFEST.md)). |
| `gectl schema [--check]` | Écrit les schémas JSON des fichiers de jeu. |

Codes de sortie : 0 succès, 1 invalide ou périmé, 2 erreur d'usage. Les erreurs indiquent le fichier, l'endroit dans le document et la raison.

## Ajouter un module

Un module est **un dossier** : CMake trouve tout dossier de `modules/` qui contient un `module.json` (ses `depends` deviennent des dépendances d'édition de liens) et le lanceur `gerun` le lie tout seul. Aucun fichier CMake à modifier.

1. `gectl new-module <nom> --depends <modules utilisés>` crée le squelette : `module.json`, `src/<nom>.cpp`, `tests/test_<nom>.cpp` et le code généré, avec un exemple qui compile et passe son test.
2. Décrire les composants et systèmes dans `module.json`, puis `gectl generate <nom>`.
3. Écrire les systèmes dans `src/<nom>.cpp` et leurs tests dans `modules/<nom>/tests/` (compilés avec les autres tests).
4. Relancer CMake (`cmake --preset dev`), puis `./scripts/check.sh` avant la pull request.

Claude suit la même marche avec le skill `add-module` ; voir [AUTHORING.md](AUTHORING.md).

## Recettes

Ce que les modules du dépôt ont appris, pour ne pas le réinventer.

### Rester générique

- **Un ensemble plutôt que des emplacements figés.** Quand un comportement a plusieurs variantes (plusieurs animations, plusieurs façons de se déplacer), chaque variante est une entité à part qui nomme son ensemble dans une liste du jeu (`"ref": "animation_set"`, `"ref": "mover_kind"`). Ce sont `sprite2d.AnimationClip` et `grid_moves.Pattern`, pas des champs `idle_frame`, `run_frame`, `jump_frame`. Toutes les copies d'un préfab partagent l'ensemble.
- **Un nombre nommé par le jeu.** Un champ `"ref": "field"` laisse le jeu désigner n'importe quel nombre (`"transform2d.Velocity.x"`, `"collision2d.Blocked.below"`). On le lit par la ressource `app::FieldTable` ; un booléen vaut 0 ou 1. Exemples : `sprite2d.FrameBind`, `sprite2d.AnimationClip`, `tween2d.Tween`, `rules.Watch`. « Aucun champ » s'écrit avec le défaut `4294967295`.
- **Une autre entité** se désigne par son étiquette (`"ref": "tag"`, `ge/mod/tag/find.hpp`), ou par `transform2d.Owner` pour une entité créée pour une autre.
- **`Disabled` arrête une entité.** Un module qui fait agir une entité (déplacer, sauter, tirer, compter) ne le fait pas quand elle a `transform2d.Disabled`. C'est ce qui permet à `phase.ActiveIn` de figer un personnage pendant une cinématique ou un menu.

### Entre modules

- **Un en-tête public** (`modules/<nom>/include/ge/mod/<nom>/…`, en-tête seul) offre une aide aux modules qui dépendent de celui-ci, sans appel d'un module à l'autre : `tag/find.hpp` (retrouver une entité), `transform2d/drawn.hpp` (où dessiner entre deux ticks), `sprite2d/layered.hpp` (dessiner sur une couche de sprites).
- **L'ordre ne se règle qu'avec ses dépendances.** `after` et `before` ne peuvent nommer qu'un système du module ou de ses `depends` : un nom inconnu est une erreur du programme (`schedule().problems()`). Un module qui lit l'état d'un autre sans en dépendre doit donc donner le même résultat quel que soit l'ordre. Il lit un fait stable (`collision2d.Blocked.below` : « posé au sol ») plutôt qu'une valeur qui change au milieu du tick (la vitesse verticale, que la gravité vient de modifier).

### Simulation et dessin

- **La simulation** (`FixedUpdate`) ne lit que `Time`, `Random` et les actions. **Le dessin** (`Update`, `PostUpdate`) peut garder ses propres ressources ; il les exclut de l'empreinte du monde par un `hash` vide (`particles2d`, `transform2d::Trail`). Ainsi la vérification du déterminisme ne dépend pas de ce qui est dessiné.
- **Le renderer** reçoit des `DrawCommand` ; un module de dessin n'appelle jamais SDL. Lumière, effets d'écran, couches : voir [RENDERING.md](RENDERING.md).

### Ce qui fait échouer la CI (à vérifier en local)

- Portabilité : le code doit compiler avec GCC, Clang, MSVC, AppleClang, le NDK et Emscripten. Pas de `std::to_chars` / `std::from_chars` sur des flottants (absents sur iOS et WebAssembly). Pas d'exceptions, de RTTI ni de tableaux C.
- Conversions explicites : `-Wdouble-promotion` refuse un `float` passé là où un `double` est attendu sans `static_cast<double>` ; MSVC refuse les conversions qui perdent de la précision.
- clang-tidy : complexité cognitive ≤ 25 par fonction, et **les `CHECK` d'un test comptent**. Un long test se coupe en plusieurs, ou en petites fonctions d'aide. Pas de `++` dans une expression. `auto` pour une variable initialisée par un `static_cast`.
- Couverture ≥ 90 % sur `main` : chaque branche d'un système a son test.
- `TIDY=changed ./scripts/check.sh` lance clang-tidy sur les seuls fichiers que la branche change : quelques minutes au lieu d'une demi-heure.

## Règles vérifiées en CI

`scripts/check_modules.py` échoue si un module :

- inclut un module qui n'est pas dans son `depends` ;
- inclut du code de plateforme (fenêtre, système, graphique) : un module passe par des interfaces du moteur ;
- ne respecte pas `schemas/module.schema.json`.

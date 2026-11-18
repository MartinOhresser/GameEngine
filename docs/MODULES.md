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

## Règles vérifiées en CI

`scripts/check_modules.py` échoue si un module :

- inclut un module qui n'est pas dans son `depends` ;
- inclut du code de plateforme (fenêtre, système, graphique) : un module passe par des interfaces du moteur ;
- ne respecte pas `schemas/module.schema.json`.

---
name: engine-catalog
description: Lire le catalogue des modules du moteur (gectl catalog) et choisir des modules, composants et réglages réellement disponibles. À utiliser avant d'écrire ou de modifier un jeu, ou pour répondre « le moteur sait-il faire X ? ».
---

# Lire le catalogue

Le catalogue est la **seule source de vérité** sur ce que le moteur sait faire. Ne te fie ni à ta mémoire ni à un jeu existant : un module peut avoir changé.

```bash
./build/dev/tools/gectl catalog --compact          # tout, en JSON sur une ligne
./build/dev/tools/gectl catalog                    # même contenu, indenté
```

Si `gectl` n'existe pas encore : `cmake --preset dev && cmake --build --preset dev`.

## Ce que contient la sortie

```
{ "schema_version": 1,
  "engine":  { "version", "stages", "field_types", "resources": [...] },
  "modules": [ { "name", "version", "description", "depends": [...],
                 "params":     [ { "name", "type", "default", "min", "max" } ],
                 "components": [ { "name", "description", "replicable",
                                   "fields": [ { "name", "type", "default" } ] } ],
                 "systems":    [ { "name", "stage", "reads", "writes", "after", "before" } ] } ] }
```

- `engine.resources` : ce que tout module peut lire (`Time`, `Random`, `Actions`…). Le hasard vient de `Random`, jamais d'ailleurs.
- `engine.stages` : ordre d'exécution (`Startup`, `PreUpdate`, `FixedUpdate`, `Update`, `PostUpdate`). La simulation vit dans `FixedUpdate`.
- `field_types` : `i32 u32 i64 u64 f32 f64 bool`. Les champs ne portent que des nombres et des booléens.

## Choisir des modules

1. Pour chaque comportement voulu, cherche dans `description` des modules, des composants et des systèmes. Un composant décrit une donnée, un système la fait vivre : il faut **les deux** pour que ça marche.
2. Ajoute à `modules` du manifeste chaque module utilisé **et** ses `depends` (ex. `screen_wrap` exige `transform2d`). L'ordre de la liste n'importe pas.
3. Dans les fichiers de jeu, un composant s'écrit `module.Composant` (`transform2d.Position`) et ses champs viennent de `fields` ; un champ omis prend son `default`.
4. Un réglage de module (`params`) se pose dans `params` du manifeste : `"params": { "transform2d": { "speed_scale": 1.5 } }`, dans les bornes `min`/`max`.
5. Vérifie les effets croisés : un système qui `writes` un composant que d'autres lisent. `after`/`before` donnent l'ordre voulu entre systèmes.

## Modules disponibles au départ

Lis le catalogue plutôt que cette liste, qui vieillit : `transform2d` (position, vitesse, déplacement), `lifetime` (disparition après N ticks), `screen_wrap` (réapparition de l'autre côté), `action_control` (pilotage par les actions `move_x` et `move_y`), et pour l'affichage `sprite2d`, `text2d`, `camera2d`, pour les niveaux `scene_switch`, pour l'interface `ui` (ancrage, grilles, barres, menus) et `inventory` (conteneurs, cases à règles, objets). Les noms propres au jeu (catégories, conteneurs, menus…) se déclarent dans `lists` de `game.json` : voir `docs/UI.md`.

## Quand rien ne convient

Dis-le clairement (« le catalogue n'a pas de X »), puis suis le skill `add-module`. N'invente jamais un module, un composant, un champ ou un réglage « qui devrait exister » : `gectl validate` le refusera de toute façon.

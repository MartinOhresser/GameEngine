# Écrire un jeu en JSON

Pour tester un jeu sans fenêtre, voir [TESTING.md](TESTING.md) ; pour le faire écrire par Claude, [AUTHORING.md](AUTHORING.md).

Un jeu est un dossier de petits fichiers JSON. **Tout y est une entité** : le joueur, les ennemis, les éléments d'interface, plus tard les cinématiques ou l'histoire. Aucun code n'est écrit pour un jeu : si le catalogue de modules ne suffit pas, on écrit un nouveau module (voir [MODULES.md](MODULES.md)), puis le jeu l'utilise.

```
games/sparks/
  game.json              le manifeste
  prefabs/drifter.json   un modèle d'entité réutilisable
  scenes/main.json       une scène : la liste des entités à créer
  scenarios/idle.json    un scénario de test : entrées scriptées et vérifications
```

Les fichiers acceptent les **commentaires** (`// …`, `/* … */`) et les **virgules finales**. Chaque fichier commence par `"schema_version": 1`.

## Lancer, valider

```bash
./build/dev/tools/gectl validate games/sparks/game.json      # valide, sans lancer
./build/dev/runner/gerun games/sparks/game.json --ticks 120  # lance 120 ticks, sans fenêtre
```

`gerun` affiche le nombre d'entités et une empreinte de l'état final : le même jeu donne toujours la même empreinte. C'est le lanceur de développement, qui contient **tous** les modules du dépôt. L'export d'un jeu avec seulement ses modules viendra plus tard.

## `game.json`

| Clé | Rôle |
| --- | --- |
| `name` | nom du jeu (`snake_case`) |
| `description` | texte libre, facultatif |
| `modules` | modules utilisés, dépendances comprises (`screen_wrap` exige `transform2d`) |
| `simulation` | `tick_rate` (1 à 1000, 60 par défaut) et `seed` (≥ 0, 0 par défaut) |
| `params` | réglages des modules : `{"transform2d": {"speed_scale": 1.5}}`, vérifiés contre les bornes du module |
| `atlases` | images découpées en cellules pour les sprites, facultatif ; l'ordre donne les numéros (le premier est 0) : voir [RENDERING.md](RENDERING.md) |
| `lists` | listes de noms propres au jeu, facultatif : `{"layer": ["player", "door"]}` (voir « Les noms » plus bas) |
| `strings`, `locale` | un fichier de textes par langue, et la langue utilisée (la première par défaut) : voir « Les textes » plus bas |
| `prefabs`, `scenes` | listes de fichiers, chemins relatifs au dossier du jeu |
| `scenarios` | scénarios de test, facultatif : voir [TESTING.md](TESTING.md) |
| `input` | fichier qui associe touches et boutons aux actions, facultatif : voir [PLATFORM.md](PLATFORM.md) |
| `start_scene` | scène créée au démarrage |

## Préfabs

```jsonc
{
  "schema_version": 1,
  "name": "fast_drifter",
  "extends": "drifter",              // reprend les composants de "drifter"
  "components": {
    "transform2d.Velocity": { "x": 9, "y": -4 }   // ne change que ces champs
  }
}
```

Un composant s'écrit `module.Composant`. Les champs omis prennent la valeur par défaut déclarée par le module. Avec `extends`, les valeurs se superposent **champ par champ** : le préfab enfant ne redit que ce qui change.

## Scènes

```jsonc
{
  "schema_version": 1,
  "name": "main",
  "entities": [
    { "name": "fast", "prefab": "fast_drifter",
      "components": { "transform2d.Position": { "x": 790, "y": 10 } } },  // ajustements locaux
    { "prefab": "ember" }
  ]
}
```

Une entité a un `prefab`, des `components`, ou les deux (les composants de l'entité l'emportent). Son `name` est facultatif.

## Les noms

Un champ de composant déclaré avec `ref` désigne **un élément d'une liste** ; un champ avec `ref_set` désigne **plusieurs éléments** (un ensemble, d'au plus 32). Dans un fichier de jeu on écrit les noms, jamais les numéros : `"layer": ["player"]`, `"scene": "credits"`, `"font": "font"`. Une faute de frappe est signalée avec les noms qui existent.

| Liste | D'où viennent ses noms |
| --- | --- |
| `atlas` | `atlases`, dans l'ordre du manifeste |
| `string` | les clés du premier fichier de `strings` |
| `scene` | `scenes` |
| `action` | `actions` de `input`, plus celles que le jeu déclare |
| autre | `lists` du manifeste (par exemple `layer`, `item`, `category`) |

Les listes `atlas` et `string` sont réservées : `lists` ne peut pas les redéfinir.

## Les textes

Un texte n'est pas un champ chaîne : c'est une **clé** d'un fichier de langue, et le champ qui la désigne est un `ref: "string"`. Les mots vivent dans un fichier par langue :

```jsonc
// text/fr.json
{ "schema_version": 1, "locale": "fr", "entries": { "title": "La chasse au trésor" } }
```

Toutes les langues doivent avoir **les mêmes clés** (la validation le vérifie) ; `locale` choisit celle qui est jouée, donc changer de langue ne change aucune scène. Les clés sont en minuscules, chiffres, `_` et `.`. Une police est un atlas qui a un `glyphs` (les lettres, dans l'ordre des cellules) ; `scripts/make_font.py` en fabrique une à partir d'une police TrueType. Voir le module `text2d`.

## Changer de scène en cours de partie

Le module `scene_switch` : une entité `GoToScene` nomme la scène d'arrivée et ce qui la déclenche (une action pressée, un contact avec une couche, un délai). Les entités de l'ancienne scène disparaissent, sauf celles qui ont un composant `Persistent` (le joueur, un score, un inventaire).

## Erreurs

La validation signale tous les problèmes d'un coup, avec le fichier et l'endroit, et propose une correction quand elle en voit une :

```
games/sparks/prefabs/drifter.json: components.transform2d.Positon: unknown component (did you mean "transform2d.Position"?)
games/sparks/game.json: params.transform2d.speed_scale: the value is outside the bounds of the param
```

## Autocomplétion dans l'éditeur

`gectl schema` écrit des JSON Schemas dans `schemas/game/` à partir des modules disponibles. Le dépôt contient un `.vscode/settings.json` qui les associe aux fichiers de `games/` : VS Code complète les noms de modules, de composants et de champs, et souligne les types et bornes erronés pendant la frappe. La CI vérifie que les schémas sont à jour (`gectl schema --check`).

## Pas encore

- Références d'une entité à une autre (une scène ne peut pas encore dire « celle-ci suit celle-là »).
- `gectl fmt` : un formateur qui conserve les commentaires reste à écrire.
- Export d'un jeu avec seulement les modules utilisés, données compactes.
- Modules pilotés par les données pour les cinématiques, l'histoire, les dialogues et les déclencheurs.

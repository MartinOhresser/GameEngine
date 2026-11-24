---
name: playtest-headless
description: Écrire des scénarios de test (entrées scriptées et vérifications) pour un jeu, les lancer sans fenêtre et lire leurs échecs. À utiliser pour vérifier une règle de jeu ou diagnostiquer un scénario qui échoue.
---

# Jouer sans fenêtre

Un **scénario** rejoue une partie avec un « joueur » scripté et vérifie ce que le jeu a fait. Il est joué **deux fois** : deux états finaux différents font échouer le scénario (« not deterministic »).

```bash
./build/dev/runner/gerun games/<nom>/game.json --test                    # tous les scénarios
./build/dev/runner/gerun games/<nom>/game.json --scenario <nom_du_scénario>
./build/dev/runner/gerun games/<nom>/game.json --ticks 300               # état final, pour regarder
./build/dev/tools/gectl validate games/<nom>/game.json                   # valide jeu et scénarios, sans jouer
```

## Écrire un scénario

Fichier `games/<nom>/scenarios/<scénario>.json`, listé dans `"scenarios"` de `game.json` :

```jsonc
{
  "schema_version": 1,
  "name": "fly_right_then_stop",
  "scene": "main",                          // facultatif : sinon la start_scene du jeu
  "ticks": 180,                             // à 60 ticks par seconde, 3 secondes
  "inputs": [                               // le « joueur » ; value vaut 1 si omis
    { "tick": 0,  "action": "move_x", "value": 1 },
    { "tick": 60, "action": "move_x", "value": 0 }
  ],
  "expect": [
    { "at": 30, "check": "ship.transform2d.Velocity.x == 120" },   // après 30 ticks
    "ship.transform2d.Position.x == 220",                          // à la fin
    "rock gone"
  ]
}
```

Vérifications (comparaisons `== != < <= > >=`) :

| Forme | Exemple |
| --- | --- |
| champ d'une entité nommée | `hero.transform2d.Position.x > 100` |
| nombre d'entités | `entities == 3` |
| entité présente / disparue | `hero alive`, `spark gone` |
| empreinte de l'état | `fingerprint == 5f4962bd950cf9b3` (fragile, à éviter) |

Les entités citées sont **nommées** (`"name"`) dans la scène du scénario ; après un changement de scène, on les cherche dans la nouvelle (ce qui permet de vérifier qu'une sortie mène au bon niveau). Les nombres sont comparés à 0,01 % près. Un booléen se compare à `true`/`false` avec `==` ou `!=`.

Deux clés facultatives de plus :

- `"screen": { "width": 360, "height": 800 }` joue le scénario sur un écran de téléphone : un jeu dont la fenêtre suit l'écran (`window.adapt`) en prend la forme, et les touchers sont en pixels de cette zone. On vérifie ainsi en local, sans émulateur, qu'un bouton reste dans l'écran et qu'un doigt l'atteint.
- `"lists"` remplit des listes d'exécution avant le premier tick, comme le ferait un hôte.

Détails : `docs/TESTING.md`.

## Caler un long scénario

Pour un scénario qui traverse un niveau (sauter au bon moment, pousser une caisse jusqu'à un mur), ne devine pas les ticks. Écris d'abord des vérifications volontairement fausses (`hero.transform2d.Position.x > 99999`) aux ticks qui t'intéressent : l'échec affiche la vraie valeur. Place ensuite les entrées d'après ces valeurs, et remplace les sondes par de vraies vérifications. Les scénarios de `games/lueur` (`forest_through`, `hollow_through`) ont été écrits ainsi.

## Que vérifier

- **Une règle = au moins un scénario** : « le score monte quand on touche une pièce », « l'ennemi disparaît après 90 ticks », « sans entrée, rien ne bouge ».
- Vérifie la **valeur attendue calculée à la main**, pas celle que tu viens d'observer. Écris le calcul en commentaire (« 2 unités par tick × 60 ticks »). Une valeur recopiée de la sortie ne prouve rien.
- Ajoute un **cas négatif** (ce qui ne doit pas arriver) : la pièce loin du joueur n'est pas ramassée.
- Place les vérifications à des ticks choisis (`at`) pour couvrir un début, un milieu et une fin.
- Une règle que le hasard ne produit pas à la demande (un ressort sous le joueur, un monstre au-dessus) se teste dans une **petite scène d'essai** `test_*` qui contient la seule situation voulue, avec les vrais préfabs (modèle : `games/doodle_jump/scenes/test_spring.json`). Casse la règle à la main (un chiffre du préfab) pour voir le scénario échouer.
- Évite `fingerprint` : il change dès que n'importe quoi change, même voulu.
- Les vitesses sont en unités par seconde ; un tick vaut `1 / tick_rate` seconde.

## Lire un échec

```
FAIL pilot/wraps (200 ticks)
     tick 200: "ship.transform2d.Position.y > 500" is false (the value is 500)
```

- Le tick, la vérification, et la valeur trouvée. Refais le calcul à la main avant de toucher au jeu : c'est souvent l'attente qui est fausse.
- `"x" no longer exists` : l'entité a été détruite avant ce tick. `"x" has no module.Composant` : le préfab ne le contient pas.
- `not deterministic` : un système utilise autre chose que `Random`, `Time` et les actions (horloge, adresse, ordre d'un conteneur non ordonné). Corrige le module, pas le scénario.
- Erreurs de validation (`no entity named "bal" (did you mean "ball"?)`) : elles arrivent avant toute exécution.

## Le « joueur » et les actions

Un jeu ne lit pas le clavier mais des **actions** nommées. `inputs` les pose à un tick donné (un bouton vaut 0 ou 1, un axe toute valeur). Les noms viennent des modules (`action_control` : `move_x`, `move_y`) : lis leur `description` dans le catalogue.

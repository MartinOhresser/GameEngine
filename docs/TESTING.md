# Tester un jeu sans fenêtre

Un jeu se teste en le **jouant sans écran** : des entrées scriptées font office de joueur, et des vérifications disent ce que le jeu doit avoir fait. Le résultat est le même sur toutes les machines, donc la CI peut lancer les jeux de référence à chaque modification.

```bash
./build/dev/runner/gerun games/pilot/game.json --test                      # tous les scénarios du jeu
./build/dev/runner/gerun games/pilot/game.json --scenario wraps            # un seul
```

```
PASS pilot/fly_right_then_stop (180 ticks)
FAIL pilot/wraps (200 ticks)
     tick 200: "ship.transform2d.Position.y > 500" is false (the value is 500)
2 passed, 1 failed
```

Le code de sortie est 1 si un scénario échoue ou si le jeu est invalide, 0 sinon.

## Les actions : le joueur vu par le jeu

La logique d'un jeu ne lit jamais un clavier : elle lit des **actions** nommées (`move_x`, `jump`), qui valent 0 ou 1 pour un bouton et une valeur quelconque pour un axe. Une fenêtre, une manette ou un script transforment l'entrée brute en actions ; le jeu ne sait pas lequel est branché. C'est ce qui permettra le clavier, la manette et le tactile sans toucher aux jeux (milestone M6).

Dans le code : la ressource `ge::app::Actions` (`value`, `down`, `pressed`, `released`). Le module `action_control` en est un exemple : le composant `Controlled { speed }` fait suivre à la vitesse d'une entité les actions `move_x` et `move_y`.

## Un scénario

Les scénarios sont des fichiers de `scenarios/`, listés dans le manifeste (`"scenarios": ["scenarios/idle.json"]`). Un scénario démarre la `start_scene` du jeu, ou la scène donnée par sa clé facultative `"scene"` (pour tester un autre niveau).

```jsonc
{
  "schema_version": 1,
  "name": "fly_right_then_stop",
  "ticks": 180,
  "inputs": [                                    // le « joueur »
    { "tick": 0,  "action": "move_x", "value": 1 },
    { "tick": 60, "action": "move_x", "value": 0 }
  ],
  "expect": [
    { "at": 30, "check": "ship.transform2d.Velocity.x == 120" },  // à un tick donné
    "ship.transform2d.Position.x == 220",                         // à la fin (tick 180)
    "rock gone"
  ]
}
```

- `ticks` : nombre de ticks à jouer (1 à 10 000 000). Les ticks se comptent à partir de 0 ; une entrée du tick N s'applique au début du tick N.
- `inputs` : changements d'action (`value` vaut 1 si omis).
- `expect` : une vérification est soit un texte (vérifiée à la fin), soit `{ "at": N, "check": "…" }` (vérifiée après N ticks).

### Les vérifications

| Forme | Exemple |
| --- | --- |
| un champ d'une entité nommée | `hero.transform2d.Position.x > 100` |
| le nombre d'entités | `entities == 3` |
| une entité existe / n'existe plus | `hero alive`, `spark gone` |
| la scène courante | `scene is credits` |
| le jeu demande à quitter | `game quits` |
| le jeu a appelé une commande de l'hôte | `host calls play` (liste `host_command`) |
| l'empreinte de l'état | `fingerprint == 5f4962bd950cf9b3` |

Comparaisons : `==`, `!=`, `<`, `<=`, `>`, `>=`. Un champ booléen se compare à `true` ou `false` avec `==` ou `!=`. Les nombres sont comparés à 0,01 % près (relatif) : les valeurs de jeu sont des flottants, `== 0.1` ne doit pas dépendre du dernier bit.

Les entités citées sont celles **nommées dans les scènes du jeu** ; la vérification les cherche dans la scène où le jeu se trouve alors (« n'existe plus » si elle n'y est pas), ce qui permet de suivre un changement de scène. `gectl validate` signale une entité, un composant ou un champ inconnu, avec « vouliez-vous dire … ? », avant même de lancer quoi que ce soit.

### L'empreinte : à manier avec précaution

`fingerprint == …` fige l'état complet du monde : il détecte tout changement de comportement, mais aussi les changements voulus. À réserver aux jeux de référence stables. Elle est calculée sur des flottants : le moteur compile sans multiplication-addition fusionnée pour que le résultat ne dépende pas du compilateur, mais une valeur écrite sur une machine n'est garantie que sur les mêmes outils.

## Le déterminisme est vérifié à chaque fois

Chaque scénario est joué **deux fois**, et les deux parties doivent finir dans le même état. Sinon il échoue avec `not deterministic`. C'est le garde-fou des futures parties en réseau : une simulation qui n'est pas rejouable à l'identique ne pourra pas être synchronisée.

## Rendu nul et enregistreur

Un jeu tourne sans écran parce que la logique ne dépend pas du rendu. `ge::app::RenderBackend` désigne le rendu de l'application : par défaut un `NullRenderer` qui ne dessine rien, ou un `RecordingRenderer` qui note chaque commande de dessin, pour vérifier « un vaisseau a été dessiné en 100, 300 » sans écran. L'état final est le même quel que soit le rendu attaché, et un test le vérifie. Le vrai rendu 2D (sprites, texte, caméra) arrive au milestone M7 et remplacera `DrawCommand`.

## Dans la CI

Les tests unitaires lancent `--test` sur tous les jeux de `games/` : un jeu de référence dont un scénario échoue fait échouer la CI sur toutes les plateformes où `gerun` est construit.

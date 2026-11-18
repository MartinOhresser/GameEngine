---
name: add-level
description: Ajouter une scène (niveau, écran, arène) à un jeu : préfabs, entités nommées, scénario qui la teste. À utiliser pour ajouter ou modifier un niveau d'un jeu existant.
---

# Ajouter un niveau

Un niveau est une **scène** : un fichier `games/<nom>/scenes/<scène>.json` qui liste les entités à créer. Les modèles réutilisables sont des **préfabs** (`games/<nom>/prefabs/`).

## Étapes

1. **Préfabs d'abord** : pour chaque type d'entité qui revient (joueur, ennemi, pièce, mur), un préfab. Une variante prend `"extends": "<préfab>"` et ne redit que ce qui change. Prends exemple sur `games/sparks/prefabs/`.
2. **La scène** :

```jsonc
{
  "schema_version": 1,
  "name": "level_2",
  "entities": [
    { "name": "ship", "prefab": "ship" },                                  // nommée : on peut la vérifier
    { "prefab": "coin", "components": { "transform2d.Position": { "x": 200, "y": 150 } } },
    { "components": { "lifetime.Lifetime": { "ticks": 600 } } }            // sans préfab
  ]
}
```

   - `name` (facultatif, unique dans la scène) sert à citer l'entité dans un scénario.
   - Une entité a un `prefab`, des `components`, ou les deux : ses propres valeurs l'emportent, champ par champ.
   - Les champs omis prennent le défaut du module.
2b. **Niveau étendu** : n'écris pas des centaines de tuiles à la main. Pars d'une carte en texte et génère la scène avec un script versionné dans le jeu (modèle : `games/treasure_run/tools/make_assets.py`, où une tuile est une entité avec `Position` et `Sprite`, et un mur y ajoute un `collision2d.Collider` avec `"solid": true`).
3. **L'enregistrer** dans `game.json` : ajoute le fichier à `"scenes"` ; `"start_scene"` désigne la scène du départ.
4. **La tester** : un scénario avec `"scene": "level_2"` (skill `playtest-headless`) qui prouve ce que le niveau doit offrir (entités présentes, comportement attendu).
5. **Valider** : `./build/dev/tools/gectl validate games/<nom>/game.json`, puis `./build/dev/runner/gerun games/<nom>/game.json --test`.

## Limites actuelles

- Une partie démarre sur `start_scene`. Pour passer d'une scène à une autre en cours de partie (« à la fin du niveau 1, charger le niveau 2 »), mets le module `scene_switch` : une entité `GoToScene` (déclencheur : action pressée, contact avec une couche, délai) nomme la scène d'arrivée, et ce qui doit rester (score, joueur) porte `scene_switch.Persistent`. Les entités persistantes restent dans la nouvelle scène : ne les recrée pas dans les autres scènes.
- Une entité ne peut pas désigner une autre entité dans les fichiers.
- Beaucoup d'entités identiques : un préfab et plusieurs lignes `{ "prefab": "…" }` plutôt que des copies de composants.

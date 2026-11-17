# ADR 0002 — Conception de l'ECS

- Statut : accepté

## Contexte

L'ECS doit rester simple à étendre (ajouter un composant ou un système sans toucher au cœur), générique, et compatible avec un futur état de monde copiable pour le réseau (rollback).

## Décision

- **Entités à génération** : `Entity { index, generation }`. Une référence périmée est détectée, jamais confondue avec la nouvelle occupante de la case.
- **Stockage sparse set par type de composant** : accès O(1), itération dense, retrait par échange avec le dernier. Un composant n'a besoin d'aucun enregistrement : le stockage est créé à la première utilisation.
- **Requêtes `each<Cs...>`** : on parcourt le stockage le plus petit et on cherche les autres par entité. `const T` donne un accès en lecture seule.
- **Ressources** : état global typé, stocké dans le `World`, plutôt que des globales.
- **Garde d'itération** : modifier la structure du monde pendant `each` déclenche une assertion. Les commandes différées sont prévues (feuille de route, étape 2).
- **Couche `app`** séparée de `ecs` : `App`, `Schedule` (étapes `Startup/PreUpdate/Update/PostUpdate`) et plugins. L'ECS n'en dépend pas.

## Conséquences

- Pas d'archétypes : l'itération sur plusieurs composants est un peu moins rapide que dans un ECS à archétypes, mais le modèle est plus simple. À réévaluer sur mesures réelles.
- Les systèmes s'exécutent séquentiellement dans l'ordre d'enregistrement ; le parallélisme et les dépendances entre systèmes viendront plus tard.
- Pour le rollback, il faudra une copie du `World` : les composants devront être copiables ou sérialisables. Ce point sera traité avec la sérialisation (étape 7).

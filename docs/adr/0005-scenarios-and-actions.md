# ADR 0005 : tests de jeux par scénarios et entrées par actions

Statut : accepté.

## Contexte

Les jeux sont produits par Claude à partir de données. Il faut un moyen de vérifier, sans fenêtre et sans humain, qu'un jeu fait ce qu'il doit, et que ce moyen reste valable le jour où il y aura une fenêtre, une manette ou du réseau.

## Décision

- La logique de jeu ne lit que des **actions** nommées (`Actions`), jamais un clavier ni une horloge. Un backend les remplit ; le premier est un script (`ScriptedInput`) appliqué au début de chaque tick, d'où la possibilité de rejouer une partie à l'identique.
- Un **scénario** est un fichier de données : ticks, entrées scriptées, vérifications écrites comme de courtes phrases (`hero.transform2d.Position.x > 100`). Elles sont validées contre le jeu avant d'être exécutées.
- Pour lire un champ par son nom, chaque composant généré a une fonction `read` à côté de `insert`.
- Chaque scénario est joué deux fois ; deux états différents sont un échec.
- Le rendu est une interface (`Renderer`) avec une implémentation nulle et une qui enregistre. Elle ne fait pas partie de l'empreinte du monde.
- Le moteur est compilé avec `-ffp-contract=off` pour que les flottants ne dépendent pas du compilateur.
- Le lanceur `gerun --test` exécute les scénarios ; les tests de la CI le lancent sur tous les jeux de référence.

## Conséquences

- Un jeu de référence cassé par un changement du moteur est détecté sur toutes les plateformes.
- Les noms d'actions sont une convention (`move_x`, `move_y` pour `action_control`) tant qu'un composant ne peut pas porter de texte ; un type texte pour les champs viendra avec l'interface (M8).
- L'empreinte écrite à la main reste fragile entre machines ; les jeux de référence s'appuient plutôt sur des valeurs de champs.
- Le vrai rendu (M7) remplacera `DrawCommand` ; l'interface actuelle ne sert qu'à garder la dépendance dans le bon sens.

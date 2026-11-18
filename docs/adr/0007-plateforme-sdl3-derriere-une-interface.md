# ADR 0007 : la plateforme est SDL3 derrière une interface

Statut : accepté.

## Contexte

M6 donne une fenêtre au moteur. Le moteur doit rester déterministe, tourner sans fenêtre (tests, CI, génération par Claude) et pouvoir accueillir plus tard d'autres entrées (manette, tactile), d'autres plateformes et un vrai rendu 2D puis 3D. La performance compte : le choix de l'API de dessin ne doit pas être figé par la couche plateforme.

## Décision

- **SDL3** gère fenêtre, boucle d'événements, clavier, souris et son. Il ne dessine pas : l'API de dessin (SDL_GPU, bgfx ou autre) se décidera en M7 sur un essai chiffré, derrière l'interface `Renderer` qui existe depuis M4.
- Tout passe par une interface, `ge::platform::Backend` (entrées brutes, temps, rendu, son). Un backend de script (`ScriptedBackend`) et le backend SDL la remplissent ; les tests prouvent qu'ils alimentent le même jeu.
- Le jeu ne lit que des **actions**. Un fichier `input.json`, déclaré dans le manifeste, associe des sources (`key:space`, `mouse:left`, plus tard `pad:south`) à des actions.
- Les entrées d'un backend en direct passent par une ressource `LiveInput` appliquée au début de chaque tick, après la mémorisation des valeurs précédentes : `pressed` et `released` se comportent comme avec un script.
- La couche plateforme est **optionnelle** (`GE_BUILD_PLATFORM`, préréglage `platform`) : sans elle, rien d'autre ne change. SDL est compilé depuis les sources (statique) s'il n'est pas installé.
- Aucun en-tête SDL hors de `platform/sdl/`, vérifié en CI. Les en-têtes du backend n'exposent aucun type SDL.
- Le backend SDL a un mode « sans tête » (pilotes dummy) qui dessine en mémoire : la CI joue un jeu et en capture l'image sans écran.

## Conséquences

- Ajouter une manette ou un écran tactile revient à écrire un backend et quelques lignes d'`input.json`.
- Les tests de la plateforme tournent sur Linux, macOS et Windows sans fenêtre réelle ; ce qui n'est pas couvert, ce sont les vrais pilotes d'affichage et de son (à essayer à la main avec `geplay`).
- Un jeu ne peut pas exiger un clavier : l'association est dans sa config, et un script peut toujours le piloter.
- La performance du dessin reste ouverte : elle se mesurera en M7, sans toucher aux jeux ni à la logique.

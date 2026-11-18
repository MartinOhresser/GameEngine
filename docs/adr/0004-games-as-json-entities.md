# ADR 0004 : les jeux sont des fichiers JSON d'entités

Statut : accepté.

## Contexte

Un jeu doit pouvoir être produit (par Claude ou à la main) sans écrire de C++. Les binaires des jeux doivent rester légers, donc n'embarquer que les modules utilisés. Les fichiers doivent rester lisibles et modifiables par un humain.

## Décision

- Un jeu est un manifeste `game.json`, des préfabs et des scènes. Tout est entité : interface, joueur, et plus tard cinématiques et histoire.
- Format JSON avec commentaires et virgules finales. Plusieurs petits fichiers, des préfabs qui s'étendent (`extends`) et se superposent champ par champ.
- Le jeu est validé contre le catalogue des modules (`gectl validate`) avec des erreurs localisées et des suggestions. Des JSON Schemas pour l'éditeur sont générés (`gectl schema`).
- Pendant le développement, un lanceur universel (`gerun`) lie tous les modules et lit n'importe quel jeu. L'export d'un jeu avec seulement ses modules et des données compactes est reporté.
- Si un jeu a besoin d'un composant ou d'un système qui n'existe pas, on écrit un module (branche, PR, CI), jamais de code propre au jeu.

## Conséquences

- Le catalogue des modules est le contrat entre ceux qui écrivent les modules et ceux qui écrivent les jeux.
- Le chargement à l'exécution passe par la fonction d'insertion générée de chaque composant, sans réflexion ni dépendance externe.
- Les références entre entités, un formateur qui conserve les commentaires et les modules pilotés par les données (cinématiques, dialogues, déclencheurs) restent à faire.

# ADR 0009 : texte, interface, inventaire et changement de scène, tout par données

Statut : accepté (conception de M8 ; les détails s'affinent au fil de l'implémentation, les écarts sont notés ici).

## Contexte

M8 ajoute ce qui s'affiche par-dessus le monde (texte, HUD, menus) et ce qui se range (inventaire). Exigence du projet : **rien n'est câblé en dur**. Il n'y a pas « un » inventaire ni « un » menu dans le moteur : chaque jeu décide de la taille, des emplacements, de l'affichage et des règles de chaque case, en JSON. Le moteur fournit des briques ; Claude (ou la personne) les assemble.

Contraintes qui restent : les composants ne contiennent que des nombres et des booléens (simulation déterministe, empreinte, réplication possible plus tard) ; tout dessin est de la présentation, qui ne change pas la simulation ; un jeu se teste sans fenêtre, donc l'inventaire et les menus aussi.

## Décisions

### 1. Des listes nommées déclarées par le jeu

Le manifeste peut déclarer des **listes de noms** : `"lists": { "item_kind": ["potion", "sword"], "category": ["weapon", "consumable"], "tag": ["player"] }`. Un module déclare qu'un champ `u32` pioche dans une liste avec `"ref": "category"` (déjà le cas de `"atlas"`), et les JSON y écrivent le nom. C'est la généralisation du mécanisme `ref` de M7 : les atlas restent une liste spéciale (ils ont un fichier), les autres sont de simples noms. Un jeu n'invente donc ses propres catégories d'objets, étiquettes ou actions sans toucher au moteur.

Un champ `"ref_set": "category"` (toujours `u32`) accepte **plusieurs noms** et les range dans un masque de bits (jusqu'à 32 noms par liste) : `"accepts": ["weapon", "tool"]`.

### 2. Le texte vient d'une table de chaînes, jamais d'un champ chaîne

Les composants gardent des nombres. Le texte vient de fichiers `strings` du manifeste (`{ "locale": "fr", "entries": { "menu.start": "Jouer", "hud.score": "Score : {value}" } }`). Un composant `Text` désigne une entrée par `ref` (`"string": "hud.score"`). Gagnés au passage : traduction (une table par langue, choisie par un réglage du jeu), pas de chaîne dans la simulation, texte vérifié au chargement (une clé inconnue est une erreur claire, avec « vouliez-vous dire »).

Un texte dynamique (le score) se remplit par des **liaisons** : `{value}` prend la valeur d'un champ lu par réflexion (`ComponentInfo.read`) sur l'entité repérée par une étiquette (`Tag`, liste `tag`). Pas d'entité référencée par identifiant : une étiquette se retrouve comme `CameraTarget` en M7, la plus ancienne gagne.

La police est un **atlas de glyphes en PNG** (même mécanisme que les sprites, et même chargement) avec la liste de caractères en JSON. Une police TrueType pourra s'ajouter derrière la même interface `Renderer` sans changer les jeux.

### 3. L'interface est faite d'entités, ancrées à l'écran

Un panneau, une étiquette, une barre de vie, un bouton, une case : des **entités** avec des composants du module `ui` (ancrage au bord ou au centre de l'écran, décalage, taille, marges, disposition en ligne, colonne ou grille). Ni la caméra ni le zoom ne les déplacent ; ils se dessinent par-dessus le monde, avec les mêmes `Sprite` et `Text`. Un HUD ou un inventaire n'a donc aucune forme imposée : c'est un assemblage.

### 4. Les menus et les boutons déclenchent des actions nommées

Un `Button` désigne une action de la liste `action` ; le focus se déplace au clavier (haut, bas, valider, actions déclarées dans `input.json`) et à la souris (la position de la souris devient une entrée, M6 n'avait que ses boutons). Ce qu'une action *fait* est aussi de la donnée : changer de scène, quitter, ou lever une action du jeu qu'un autre système lit. Pas de menu « principal » codé : un menu est une scène.

### 5. L'inventaire : des conteneurs, des cases et des objets, tous configurables

- Une **case** est une entité (`Slot`) : le conteneur auquel elle appartient (étiquette), son numéro, les **catégories qu'elle accepte** (`ref_set`), la taille maximale d'une pile, et si elle est verrouillée. Une case « épée seulement » ou « potions seulement », une case de taille 1 ou de 99, tout est un choix du jeu.
- Un **objet** est une entité (`Item`) : son genre (liste `item_kind`), ses catégories, sa quantité, la case où il se trouve. Le genre et les catégories viennent d'un préfab, donc d'un fichier de données.
- La **taille** d'un inventaire est le nombre de cases que la scène ou un préfab crée ; la grille, la liste, une ligne de barre rapide ou une silhouette d'équipement sont des dispositions `ui` différentes sur les mêmes cases. Un deuxième inventaire (coffre, boutique) est un autre conteneur, pas un autre module.
- Les règles (empilement, refus d'un objet qui ne convient pas, déplacement, échange) sont des systèmes du module `inventory` qui s'exécutent dans la simulation : ils se testent en scénario sans fenêtre.

### 6. Changer de scène en cours de partie

Une demande de changement (portée par une action de menu ou une règle) désigne une scène du manifeste ; la boucle l'applique entre deux ticks : les entités de la scène courante sont retirées, sauf celles marquées persistantes (le joueur, son inventaire), puis la nouvelle scène est créée. L'opération est déterministe et se rejoue dans les scénarios.

## Écarts constatés à l'implémentation

- **Liaison d'un nombre** : plutôt qu'un couple composant/champ écrit en chaînes, un champ `ref: "field"` désigne un élément de la liste intégrée `field` (« module.Composant.champ », faite par le moteur à partir des modules du jeu, triée). Le moteur (`FieldTable`) et le chargement du jeu construisent la même liste dans le même ordre. `ui.Bind` lit ce champ par réflexion (`ComponentInfo.read`) sur l'entité repérée par un `ui.Tag`.
- **Boutons** : un bouton lève une action avec `Actions::raise` : elle est pressée pendant un tick entier, le tick suivant, ce qui rend le résultat indépendant de l'ordre des systèmes (un `GoToScene` écoute l'action sans avoir à passer après le menu). Un champ d'action laissé de côté vaut « aucune » (4294967295), pas la première action de la liste.
- **Pointeur** : `mouse:x` et `mouse:y` sont des sources de `input.json` qui ne sont pas bornées à [-1, 1] ; la taille de la fenêtre est une donnée du jeu (`window`, ressource `Viewport`), ce qui garde les positions d'une interface ancrée déterministes.
- **Inventaire** : des cases (`Slot`) et des objets (`Item`) liés par une adresse (`container`, `index`), pas par une référence d'entité ; `Gatherer`, `Transfer` et `Hand` en sont les règles. Pas de division de pile ni de transfert piloté par une règle de données : limites notées dans `docs/UI.md`.
- **Scénarios** : une vérification `scene is NOM`, et les entités nommées de toutes les scènes sont admises dans les vérifications.

## Conséquences

- Aucun type de champ chaîne dans les composants : l'ADR 0008 prévoyait ce type ; la table de chaînes le remplace, et rend la traduction possible sans coût supplémentaire.
- Le catalogue, les schémas JSON et les messages d'erreur apprennent les listes et les ensembles ; les jeux existants ne changent pas.
- Les limites sont explicites : 32 noms par liste utilisée en ensemble, pas de mise en page automatique complexe (grilles, lignes et colonnes seulement), pas de texte enrichi, pas de TrueType, pas de défilement.

## Ce qui reste ouvert

- Les liaisons de texte lisent un champ par son nom de composant ; si un jeu a besoin de formules, ce sera un module à part.
- La persistance entre scènes (« persistant ») est d'abord un marqueur simple ; une sauvegarde sur disque est hors MVP.

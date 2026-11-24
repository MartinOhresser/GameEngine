# Dessiner un jeu 2D

Tout ce qui se voit vient de données : une **image découpée en cellules** (un atlas), des entités qui portent un `Sprite`, et éventuellement une entité `Camera`. Le dessin ne change jamais la simulation : un jeu se teste sans fenêtre, et les scénarios vérifient les mêmes valeurs qu'à l'écran.

![treasure_run](img/treasure_run.png)

*`games/treasure_run`, capturé sans fenêtre avec `geplay --headless --screenshot`.*

## Les atlas

Un atlas est un fichier JSON qui nomme une image PNG et la taille d'une cellule ; l'image est une grille de cellules identiques, numérotées ligne par ligne depuis le coin haut gauche, en commençant à 0.

```json
{ "schema_version": 1, "name": "tiles", "image": "tiles.png", "cell_width": 16, "cell_height": 16 }
```

Le manifeste liste les atlas (`"atlases": ["art/tiles.json"]`). L'ordre compte : le premier est l'atlas 0.

`"filter"` choisit comment l'image est agrandie, réduite ou tournée :
- `"nearest"`, le défaut : chaque pixel reste un carré net, pour le pixel art ;
- `"linear"` : l'image est lissée, pour les images peintes, les silhouettes et les dégradés.

Une cellule lissée emprunte un peu des pixels de ses voisines, à son bord. Laissez donc une marge transparente autour du dessin de chaque cellule, ou mettez une grande image seule dans son atlas : une cellule de la taille de l'image, jusqu'à 4096 pixels de côté.

## Les sprites

`sprite2d.Sprite` dessine une cellule, centrée sur `transform2d.Position`.

| Champ | Rôle |
|---|---|
| `atlas` | le **nom** de l'atlas (`"tiles"`) ou son numéro |
| `frame` | numéro de la cellule |
| `width`, `height` | taille dans le monde ; 0 garde la taille de la cellule (une cellule de 16 affichée en 32 reste nette) |
| `layer` | les couches hautes passent devant ; à égalité, l'entité la plus ancienne est dessinée en premier |
| `flip_x` | miroir horizontal |
| `red`, `green`, `blue`, `alpha` | teinte et transparence, 0 à 255 (255 ne change rien) |
| `additive` | ajoute sa lumière à ce qui est dessous : halos, étincelles, rayons ; le noir n'ajoute rien |
| `multiply` | assombrit ce qui est dessous de sa couleur : ombres, voiles colorés ; le blanc ne change rien |

`transform2d.Rotation` (`angle`, en degrés, dans le sens des aiguilles d'une montre à l'écran) tourne l'image autour de sa `Position` : le pivot reste en place, comme une main qui tient une épée. `transform2d.Scale` l'agrandit. Les collisions ne changent ni avec l'un ni avec l'autre.

`sprite2d.SpriteAnimation` fait défiler `frame_count` cellules à partir de `first_frame`, `ticks_per_frame` ticks chacune, en boucle ou non. Elle avance avec la **simulation** (pas avec l'horloge de l'écran) : une animation est identique à chaque exécution et testable dans un scénario.

## La caméra

Une entité avec `camera2d.Camera` et une `Position` : ce point du monde est au centre de l'écran, et `zoom` agrandit. Sans caméra, une unité du monde vaut un pixel. Pour suivre le héros : `camera2d.CameraTarget` sur lui, `camera2d.CameraFollow` sur la caméra (`stiffness` 1 colle au héros, 0.1 le suit en douceur). S'il y a plusieurs caméras, la plus ancienne est utilisée.

## La parallaxe

`sprite2d.Parallax` fait d'un sprite un plan lointain : `x` et `y` disent quelle part du mouvement de la caméra il suit.
- 1 bouge avec le monde (sans `Parallax`, c'est le cas) ;
- 0,5 bouge deux fois moins, comme des collines au loin ;
- 0 reste fixe à l'écran, comme le ciel ;
- plus de 1 bouge plus vite que le monde, comme un premier plan entre le joueur et la caméra.

Avec `repeat_x`, l'image est redessinée côte à côte, tous les `width`, sur toute la largeur de la vue : une seule image fait une bande sans fin (collines, nuages, sol). Le zoom s'applique à tous les plans de la même façon. Mettez les plans les plus lointains sur les couches (`layer`) les plus basses.

Un plan de parallaxe `p` montre, quand la caméra est en `C`, son point `C × p` au centre de l'écran. Pour qu'il soit centré au départ d'un niveau où la caméra est en `C0`, placez-le en `C0 × p`.

## Les tuiles

Il n'y a pas de type « carte de tuiles » : une tuile est une entité avec `Position` et `Sprite`, et un mur y ajoute un `Collider`. Pour un grand niveau, écrire la scène à la main est pénible ; on la génère depuis une carte en texte, comme `games/treasure_run/tools/make_assets.py` (le script produit aussi les images). Le fichier généré est versionné et validé comme les autres.

## Les collisions

Le module `collision2d` ne dépend pas du dessin : il travaille sur les positions et fonctionne à l'identique sans fenêtre.

- `Collider` : une boîte (`width`, `height`) centrée sur la `Position`. `layer` dit ce qu'est l'entité, `mask` à quelles couches elle réagit (ensembles de bits : 1, 2, 4, 8…). `solid` en fait un mur.
- `Body` : l'entité est repoussée hors des murs solides par le chemin le plus court et perd la vitesse qui allait dans le mur (elle glisse le long). Un corps qui avance de plus que sa propre taille en un tick peut traverser un mur mince : gardez des murs au moins aussi épais.
- `Contacts` : rempli à chaque tick pour une entité qui a aussi un `Collider` : `count` (combien de boîtes la touchent) et `layers` (leurs couches réunies). Deux boîtes qui ne partagent qu'un bord ne se touchent pas.

La détection trie les boîtes par bord gauche et balaie : le coût suit le nombre de contacts, pas le carré du nombre de boîtes.

## Le texte

Une entité avec `text2d.Text` affiche l'un des textes du jeu, lettre par lettre, avec une police (un atlas qui a `glyphs`) : voir [MANIFEST.md](MANIFEST.md) pour les fichiers de langue. Avec `screen: true` (aussi pour `Sprite`), la position et la taille sont en pixels de la fenêtre et la caméra n'agit pas : c'est ainsi qu'une interface reste en place pendant que le monde défile. `{value}` dans un texte écrit le nombre du champ `value`.

Un `Sprite` a un `pivot_x` et un `pivot_y` (0,5 par défaut : le milieu de l'image sur la `Position`) ; 0 met le bord gauche ou haut sur la `Position`, 1 le bord droit ou bas. Il faut une taille (`width`, `height`) pour que le pivot ait un effet. Les barres de `ui` s'en servent pour grandir depuis un bord. L'interface complète (ancrage, grilles, menus, inventaire) est décrite dans [UI.md](UI.md).

## Voir un jeu

```
geplay games/treasure_run/game.json                      # fenêtre, flèches ou WASD
geplay games/treasure_run/game.json --headless --frames 60 --screenshot image.bmp
```

Voir [PLATFORM.md](PLATFORM.md). Le choix de `SDL_Renderer` et sa mesure : [ADR 0008](adr/0008-rendu-2d-sdl-renderer-derriere-renderer.md), [ADR 0015](adr/0015-rendu-atmospherique.md).

## Un mouvement fluide sur tous les écrans

La simulation avance par ticks fixes (60 par seconde par défaut), alors qu'un écran se rafraîchit 60, 120 ou 144 fois par seconde. Dessiner le dernier tick tel quel ferait avancer les choses par petits sauts. Quand le jeu tourne en temps réel (`geplay`, le lanceur, un jeu exporté), chaque entité est donc dessinée **entre sa position du tick précédent et celle du dernier tick**, au prorata du temps écoulé (`Time::alpha`). La caméra aussi, ce qui supprime les saccades du défilement. Le dessin a ainsi un tick de retard (1/60 s), comme dans tous les moteurs qui font ainsi.

Un déplacement de plus de `snap_distance` unités en un tick (réglage de `transform2d`, 256 par défaut) est un saut : téléportation, passage d'un bord de l'écran à l'autre, nouveau niveau. Il est dessiné d'un coup, sans glisser à travers l'écran.

Rien de cela ne touche la simulation. Les tests et les scénarios voient exactement le dernier tick (`Time::interpolate` n'y est pas mis), et un module de dessin obtient la position à dessiner avec `transform2d::drawn_position` (`ge/mod/transform2d/drawn.hpp`).

## Pas encore

L'[ADR 0015](adr/0015-rendu-atmospherique.md) prévoit encore la lumière, les particules et les effets d'écran.

- **Polices TrueType** : le texte se dessine avec des polices en atlas PNG ; lire un `.ttf` directement viendra plus tard.
- **Cartes de tuiles en tableau** : une tuile reste une entité.
- Ordre de dessin par la position verticale, particules, lumière.

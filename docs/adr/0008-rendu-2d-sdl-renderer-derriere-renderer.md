# ADR 0008 : le rendu 2D utilise SDL_Renderer, derrière l'interface `Renderer`

Statut : accepté.

## Contexte

M7 ajoute les sprites, les atlas, la caméra et les collisions. La roadmap prévoyait de trancher l'API de dessin (SDL_Renderer, SDL_GPU, bgfx) « sur un essai chiffré » (ADR 0007). La performance est la priorité du projet ; la 3D viendra plus tard avec des modèles vectoriels, pas des sprites.

## Décision

- **La 2D est dessinée avec `SDL_Renderer`**, déjà présent avec SDL3 : aucune dépendance de plus, accéléré par le GPU quand le pilote en offre un, avec repli logiciel. Les atlas sont des textures (images PNG lues par `stb_image`, un seul fichier d'en-tête, ajouté tel quel), agrandies sans lissage pour que le pixel art reste net.
- **Tout le jeu passe par l'interface `Renderer`** (`draw`, `load_atlas`, `set_camera`, `end_frame`). Les modules (`sprite2d`, `camera2d`, `debug_draw`) ne connaissent ni SDL ni le GPU. La 3D (SDL_GPU ou bgfx, avec des modèles glTF) sera un autre `Renderer`, ajouté sans changer les jeux 2D.
- **`end_frame` est appelé une fois par image par la boucle**, plus par les systèmes de dessin : plusieurs modules dessinent dans la même image.
- **La caméra est une donnée du jeu** (un composant `Camera` sur une entité), transmise au `Renderer` avant le dessin ; sans caméra, un pixel vaut une unité du monde.
- Les images et les cellules d'un atlas sont **nommées dans le jeu** (`atlases` du manifeste) ; un champ `u32` de composant déclaré `"ref": "atlas"` accepte le nom d'un atlas et le jeu le convertit en numéro au chargement. Les composants ne contiennent toujours que des nombres.

## Ce qui n'a pas été mesuré

**Il n'y a pas de GPU dans l'environnement où M7 a été écrit** : la performance du dessin n'a donc pas pu être chiffrée sur un vrai GPU, et le choix ci-dessus n'est pas issu d'un banc d'essai. Il repose sur le fait que `SDL_Renderer` regroupe lui-même les dessins qui partagent une texture et suffit largement à un jeu 2D de plusieurs milliers de sprites. À refaire sur une machine avec GPU (nombre de sprites par image à 60 images/s) avant de s'engager sur un jeu très chargé : si le résultat est insuffisant, il suffit de remplacer le `Renderer` (SDL_GPU en lots), sans toucher aux jeux ni aux modules.

## Conséquences

- Rien à installer pour dessiner, et les tests de rendu (capture de pixels en mode sans tête, pilotes dummy) tournent partout, CI comprise.
- Un tri par couche (`layer`) est fait par `sprite2d` côté simulation ; le `Renderer` dessine dans l'ordre reçu.
- Limites connues et assumées en M7 : pas de rotation ni de mise à l'échelle par axe négative, pas de texte (il demande un type de champ chaîne, prévu en M8), pas de cartes de tuiles en tableau (les tuiles sont des entités avec un `Sprite` ; un format en tableau viendra avec les champs de données de M8).

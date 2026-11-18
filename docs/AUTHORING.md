# Faire écrire un jeu à Claude

Tu décris le jeu ; Claude écrit les fichiers JSON, crée un module seulement si le catalogue ne suffit pas, vérifie le résultat sans fenêtre et te le rend. Le savoir-faire est dans cinq **skills** versionnés avec le code, dans `.claude/skills/` :

| Skill | Rôle |
| --- | --- |
| `game-from-prompt` | Le déroulé complet, de l'idée à la PR. C'est le point d'entrée. |
| `engine-catalog` | Lire `gectl catalog` et choisir des modules, composants et réglages qui existent. |
| `add-module` | Créer ou étendre un module (composants, systèmes, tests) quand rien ne convient. |
| `playtest-headless` | Écrire des scénarios, les lancer sans fenêtre, lire leurs échecs. |
| `add-level` | Ajouter des scènes et des préfabs. |

## Le déroulé

1. **Cadrer** : au plus quelques questions ; valeurs par défaut 2D, solo, bureau.
2. **Lire le catalogue** (`gectl catalog --compact`) : Claude ne cite que ce qui y figure.
3. **Découper l'idée en comportements** et les associer aux modules ; lister ce qui manque.
4. **Combler les manques** : un module par besoin, créé avec `gectl new-module`, testé, relu en PR.
5. **Écrire le jeu** dans `games/<nom>/` : manifeste, préfabs, scènes, scénarios.
6. **Valider** (`gectl validate`) jusqu'à zéro erreur, puis **jouer sans fenêtre** (`gerun --test`).
7. **Contrôler** (`./scripts/check.sh`) et ouvrir la PR ; **rendre compte** de ce que le jeu fait, de ce qui le prouve, et de ce qui n'existe pas encore.
8. **Itérer** : tes retours modifient surtout les JSON, que tu peux aussi éditer toi-même (voir [MANIFEST.md](MANIFEST.md)).

La règle qui tient l'ensemble : **un jeu est des données, une règle est un module, et chaque règle annoncée a un scénario qui la prouve.**

## Un exemple fait avec ce déroulé

Idée : « un vaisseau que je pilote et qui ramasse des pièces ; chaque pièce rapporte des points ».

- Le catalogue avait le déplacement et le pilotage, mais ni collision ni score : le manque est « ramasser quelque chose ».
- Création du module `pickup` (composants `Collector` et `Pickup`, système `collect`) avec ses tests : `modules/pickup`.
- Le jeu `games/coin_dash` : trois préfabs (`ship`, `coin`, `gem` qui étend `coin`), une scène, trois scénarios. Les valeurs attendues sont calculées à la main : la pièce à 65 unités du vaisseau est ramassée au 21ᵉ tick, la gemme au 34ᵉ.

## Les skills sont vérifiés

`scripts/check_skills.py` (lancé par `check.sh` et la CI) refuse un skill qui cite une commande `gectl` inexistante, une option inconnue de `gectl` ou `gerun`, ou un fichier du dépôt qui n'existe pas. Un skill ne peut donc pas rester en retard sur les outils sans que ça se voie.

## Limites actuelles

- Pas de fenêtre ni de rendu (M6, M7) : on valide le comportement, pas l'apparence.
- Pas de passage d'une scène à l'autre en cours de partie, ni de référence d'une entité à une autre dans les fichiers : ce sont des modules à écrire.
- Un composant ne porte que des nombres et des booléens.
- `gectl build` du plan initial est remplacé par l'export (un binaire avec seulement les modules utilisés), à venir.

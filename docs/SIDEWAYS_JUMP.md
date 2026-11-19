# Sideways Jump : un Doodle Jump tourné d'un quart de tour

`games/sideways_jump` est le jeu de référence **horizontal** : le même genre de jeu que [Doodle Jump](DOODLE_JUMP.md), mais on avance vers la droite. Il n'a **aucune ligne de C++**, ni image (les formes sont dessinées par `debug_draw`) : il existe pour montrer que les modules ne connaissent pas « le haut » et « le bas ». Chaque sens est un réglage.

| Doodle Jump (vers le haut) | Sideways Jump (vers la droite) | Réglage |
| --- | --- | --- |
| Gravité vers le bas | Gravité vers la gauche | `gravity2d.Gravity` : `x` -1400, `y` 0 |
| Rebond sur le dessus des plateformes | Rebond sur le côté droit des blocs | `bounce2d.Bouncer` : `fall_x` -1, `fall_y` 0 |
| On dirige à gauche et à droite | On dirige en haut et en bas | `action_control.Controlled` : `horizontal` faux ; `screen_wrap.WrapArea` : `wrap_x` faux |
| La caméra ne fait que monter | La caméra ne va qu'à droite | `camera2d.CameraFollow` : `follow_y` faux, `only_right`, `offset_x` -120 |
| Le monde se construit vers le haut | Le monde se construit vers la droite | `spawner2d.Generator` : `toward_x` 1, `toward_y` 0 ; `across_min`/`across_max` bornent y |
| Ce qui est sous l'écran disparaît | Ce qui est à gauche disparaît | `spawner2d.Cull` : `side_x` -1, `side_y` 0 |
| Tomber sous l'écran tue | Sortir à gauche tue | `combat2d.Mortal` : `fall_x` -1, `fall_y` 0 |
| Le score est la hauteur | Le score est la distance vers la droite | `progress.Progress` : `toward_x` 1, `toward_y` 0 |

Pour un autre sens, on change ces nombres : un sens est lu par `app::Direction` (`(0, -1)` : haut, `(0, 1)` : bas, `(1, 0)` : droite, `(-1, 0)` : gauche ; l'axe du plus grand nombre compte, et `y` grandit vers le bas). La même génération sert à poser n'importe quelle entité (obstacles, pièces, ennemis) : un `Generator` par sorte d'entité, avec son préfab.

La fenêtre fait 720 × 480 et `"adapt": true` : la hauteur reste à 480 et la largeur suit la forme de l'écran (un téléphone tenu à l'horizontale).

```bash
geplay games/sideways_jump/game.json                     # flèches ou WS ; R pour rejouer
geplay games/sideways_jump/game.json --window 844x390    # essayer la forme d'un téléphone
gerun games/sideways_jump/game.json --test               # les scénarios, sans fenêtre
```

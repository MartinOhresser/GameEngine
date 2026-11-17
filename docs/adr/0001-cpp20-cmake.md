# ADR 0001 — C++20, CMake, pas de RTTI ni d'exceptions

- Statut : accepté

## Contexte

Le moteur doit tourner sur desktop, mobile (Android NDK, iOS) et web (WebAssembly), et pouvoir plus tard s'intégrer à des plateformes aux contraintes strictes. Le choix s'est porté sur C++ pour l'écosystème, l'accès natif aux SDK mobiles et la familiarité.

## Décision

- **C++20** (concepts, `std::span`, `requires`), avec CMake ≥ 3.25 et des presets.
- **`-fno-exceptions -fno-rtti`** (hors MSVC) : l'identité des types se fait via un compteur (`type_id<T>()`), les erreurs de programmation via `GE_ASSERT`, les erreurs attendues via des valeurs de retour.
- **Aucune dépendance** pour le cœur et les tests (framework de test interne minimal).
- Avertissements stricts, traités comme des erreurs en CI.

## Conséquences

- Les bibliothèques tierces qui exigent exceptions ou RTTI devront rester hors du cœur.
- Le cœur reste compilable partout où un compilateur C++20 existe.
- Les faux positifs de GCC 13 sur `-Wnull-dereference` et `-Wstringop-overflow` sont écartés ; Clang, clang-tidy et les sanitizers couvrent ces cas.

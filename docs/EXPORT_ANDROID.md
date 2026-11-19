# Exporter un jeu pour Android

`gectl export JEU --target android` fabrique un fichier `.apk` qu'on copie sur un téléphone et qu'on ouvre. Même idée que le zip Windows : le joueur n'installe rien d'autre. Cette page dit ce qu'il faut sur **la machine qui exporte**, comment faire, et ce que le joueur voit. Le cadre général (étapes, options, README) est dans [EXPORT.md](EXPORT.md) ; les décisions sont dans l'[ADR 0012](adr/0012-export-android.md).

La cible `android` est offerte comme Windows : `gectl export --targets` la montre (avec ce qui manque, si quelque chose manque) et le panneau « Export » du lanceur la propose.

## Ce qu'il faut sur la machine

| Outil | Où le trouver |
| --- | --- |
| SDK Android (une plateforme récente et les Build-Tools) | Android Studio l'installe ; sinon `ANDROID_HOME` désigne le dossier du SDK |
| NDK | Android Studio, Settings, SDK Manager, SDK Tools, « NDK (Side by side) » ; sinon `ANDROID_NDK_ROOT` |
| JDK 17 ou plus | celui d'Android Studio convient : `JAVA_HOME` désigne son dossier `jbr` |
| Ninja | l'élément « CMake » du SDK Manager l'apporte ; sinon dans le `PATH` |

`gectl export JEU --target android` cherche tout cela seul (`ANDROID_HOME`, `ANDROID_SDK_ROOT`, les dossiers habituels `~/Android/Sdk`, `~/Library/Android/sdk`, `%LOCALAPPDATA%\Android\Sdk`) et **dit ce qui manque et où le trouver** avant de commencer. Le **premier export est long** : Gradle et ses dépendances sont téléchargés (quelques minutes), puis SDL et les modules du jeu sont compilés ; les suivants réutilisent le dossier de travail (`build/export/android/<jeu>`).

## Exporter

```
gectl export games/doodle_jump/game.json --target android
gectl export games/doodle_jump/game.json --target android --abi arm64-v8a,x86_64 --out dist
```

`--abi` liste les processeurs visés : `arm64-v8a` (les téléphones, par défaut), `x86_64` (les émulateurs et certains Chromebooks), `armeabi-v7a` (très vieux téléphones 32 bits, non vérifié). Chaque processeur ajoute sa compilation et environ 1 Mo à l'APK. Le résultat est `dist/<Jeu> <version> android.apk`.

Ce qui vient du jeu : l'identifiant de l'application (`id` du manifeste, `ge.games.<nom>` par défaut), la version (`1.2.3` donne le code de version 10203), le nom affiché (`title`), l'icône (`icon`, un PNG) et l'orientation (portrait si la fenêtre est plus haute que large, paysage sinon).

## L'installer, la signer

- `--install` met l'APK sur le téléphone ou l'émulateur branché, avec `adb` (celui du dossier `platform-tools` du SDK, ou du `PATH`) : `gectl export games/doodle_jump/game.json --target android --install`. Il faut un téléphone avec le « débogage USB » activé (ou un émulateur démarré) : `adb devices` doit le lister. Sans `adb`, l'export le dit avant de commencer.
- `--keystore FICHIER --key-alias NOM` signe avec **votre** clé plutôt que celle de débogage (un APK signé ainsi se met à jour par-dessus lui-même, d'un export à l'autre, sans désinstaller). Les mots de passe ne passent jamais par la ligne de commande : `GE_KEYSTORE_PASSWORD` (et `GE_KEY_PASSWORD` si la clé en a un autre). On fabrique un fichier de clés avec `keytool` (livré avec le JDK) : `keytool -genkeypair -v -keystore ma.keystore -alias ma_cle -keyalg RSA -keysize 2048 -validity 10000`. Garder cette clé : la perdre empêche de mettre à jour une application déjà publiée. Le format pour Google Play (AAB) reste à faire.

## Ce que voit le joueur

- Il copie le `.apk` sur le téléphone (câble, courriel, nuage) et l'ouvre ; Android demande d'autoriser l'installation d'applications de cette source. Avec un câble et `adb` : `adb install "Doodle jump 0.1.0 android.apk"`.
- Sans `--keystore`, l'APK est **signé avec la clé de débogage** d'Android : il s'installe par copie mais ne peut pas être publié sur Google Play (le format AAB viendra plus tard, voir l'ADR).
- Le jeu s'ouvre plein écran. Les menus se touchent (un doigt est un clic de souris), les jeux qui ont des contrôles à l'écran les montrent (module `touch_controls`, voir [UI.md](UI.md)). Le bouton Retour quitte le jeu.
- La sauvegarde (meilleur score) est dans le dossier privé de l'application ; désinstaller l'application l'efface.

## Ce qui est vérifié, et par quoi

- **En test** (sans Android) : la découverte des outils avec un faux SDK, les valeurs tirées du manifeste, la suite exacte de commandes (CMake par processeur, puis Gradle) avec un exécuteur factice (`tests/tools/test_export_android.cpp`).
- **En CI** (job « Android APK of … », sur chaque pull request) : le vrai export de `doodle_jump` et `treasure_run` avec le NDK et Gradle du runner, `aapt2 dump badging` (identifiant, nom, processeurs), puis un **émulateur Android 14** installe l'APK, le lance, vérifie par capture d'écran qu'il dessine, touche le bouton Jouer, pose un doigt sur le joystick (les contrôles s'affichent) et appuie sur Retour (le jeu se ferme). Les captures et l'APK sont dans les artefacts du job.
- **À la publication** d'une version, les APK de `doodle_jump`, `treasure_run` et `coin_dash` (`arm64-v8a` et `x86_64`) sont ajoutés à la release, à côté des zips Windows.
- **Pas vérifié** : un vrai téléphone, un Android plus ancien que 14, `armeabi-v7a`. Les exécutions sont sur x86_64.

## Quand ça ne marche pas

| Message | Cause probable |
| --- | --- |
| « An Android package needs, on this machine: … » | un outil manque ; le message dit lequel et où l'obtenir |
| « CMake could not prepare the build for … » | NDK sans `build/cmake/android.toolchain.cmake` (NDK incomplet) ; réinstallez-le |
| « Gradle could not make the APK » | lisez la fin de la sortie de Gradle au-dessus ; souvent le réseau (premier téléchargement) ou un JDK trop vieux |
| l'application s'installe mais se ferme aussitôt | `adb logcat -s SDL` montre pourquoi ; vérifiez que l'APK contient le processeur du téléphone (`--abi`) |

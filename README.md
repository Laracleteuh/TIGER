# TIGER

Firmware TIGER du projet TID-70, un char à chenilles avec tourelle motorisée.

## Matériel

Carte principale : Arduino Mega 2560.

## Installation

1. Installer VS Code et l'extension PlatformIO.
2. Ouvrir le dossier du repo dans VS Code.
3. Compiler le projet, puis le téléverser sur la carte.

## Structure

- `src/` : code source (tâches FreeRTOS et pilotes)
- `include/` : fichiers de configuration, dont `config.h` qui contient le brochage
- `platformio.ini` : environnement de compilation

## Releases

Chaque release fournit une archive `.zip` du projet.

## Licence

Code distribué sous licence MIT.

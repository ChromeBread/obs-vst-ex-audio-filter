[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Bahasa Indonesia](README.id.md) | **Français** | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

Une fenêtre de filtres audio étendue pour OBS Studio avec hébergement **VST2 / VST3**, bascule ON/OFF instantanée
pour le chant en direct, outils de latence et vue de la chaîne. Elle travaille sur la même chaîne de filtres que la
fenêtre *Filtres* d’OBS : les deux restent compatibles.

- Windows x64, OBS Studio 31 ou plus récent (testé sur 33.0)
- Auteur : BlackDream feat. ChromeBread · Licence : GPL-2.0

## Installation

1. Téléchargez `obs-vst-ex-audio-filter-1.0.0-windows-x64.zip` depuis [Releases](../../releases).
2. Quittez OBS et extrayez le zip dans `C:\ProgramData\obs-studio\plugins\`
   (vous obtenez `C:\ProgramData\obs-studio\plugins\obs-vst-ex-audio-filter\bin\64bit\...`).
3. Lancez OBS. Au premier lancement, il demande où garder miniatures, captures et historique.

Désinstallation : quittez OBS et supprimez ce dossier `obs-vst-ex-audio-filter`
(Outils > VST Ex Audio Filter > Mode débogage liste tous les dossiers à supprimer).

## Fonctionnalités

**Fenêtre Filtres (VST Ex)** — Mixeur audio : clic droit sur une source > *Filtres (VST Ex)*
- Grands boutons ON/OFF, sélection multiple, glisser pour réordonner, dupliquer (Ctrl+D), tout supprimer,
  une note par filtre
- Annuler / Rétablir et un historique enregistré automatiquement qui survit aux redémarrages d’OBS
- Latence par filtre, latence totale, et un bouton qui règle le décalage de synchro pour compenser
- **Préréglages** : enregistrer / appliquer / exporter / importer toute une chaîne de filtres
- **Ensembles ON/OFF** avec décalage de synchro et volume : passer couplet → refrain en plein morceau, par raccourci
- **Mesurer sur 4 temps** : un morceau test BPM120 joue, vous tapez dans les mains (ou collez une oreillette au
  micro) et le décalage est calculé ; formes d’onde zoomables avec règle en millisecondes pour l’ajuster à la main

**Plugins VST**
- VST2 et VST3, analyse sûre dans un processus séparé (lancée par vous, avec estimation du temps)
- Navigateur en liste / miniatures / détails, tri, recherche, favoris, marques HID (masquer), notes, récemment utilisés
- Miniatures prises automatiquement à la première ouverture d’un éditeur, ou toutes d’un coup ; captures par filtre
- Exporter / importer les réglages d’un plugin depuis la fenêtre de l’éditeur

**Filtres intégrés**
- *VST Ex Transposition / Pan* : hauteur en demi-tons et panoramique, pratique pour distinguer les appareils
- *VST Ex Pseudo-stéréo* : élargit un micro mono sans DAW (effet Haas), un seul filtre à ajouter ou retirer

**Docks et outils**
- Dock *VST Ex* : boutons des ensembles ON/OFF et utilisation CPU (OBS total et part VST)
- Dock *VST Ex Mixer* : une colonne par source sous les vumètres du mixeur, un bouton par VST
- *VST Ex Chain View* (menu Outils) : chaque source de l’appareil à la sortie, latence, niveaux, sidechain,
  imageur en demi-cercle (optionnel), enregistrement en image
- Outils > VST Ex Audio Filter : langue (EN / JP / KR / ID / FR / TW), version, informations de débogage

## Aperçu

![Fenêtre Filtres (VST Ex), docks VST Ex Mixer et VST Ex](docs/images/preview-filters.jpg)
*Fenêtre Filtres (VST Ex), docks VST Ex Mixer et VST Ex*

![Choisir un plugin VST (vue miniatures)](docs/images/preview-browser.jpg)
*Choisir un plugin VST (vue miniatures)*

## Compilation

Visual Studio 2022 et CMake 3.28+ : `cmake --preset windows-x64` puis
`cmake --build build_x64 --config RelWithDebInfo`.

**English** | [日本語](README.ja.md) | [한국어](README.ko.md) | [Bahasa Indonesia](README.id.md) | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

An extended audio filter window for OBS Studio with built-in **VST2 / VST3** hosting, instant ON/OFF switching
for live singing, latency tools and a chain view. It works on the same filter chain as OBS's own *Filters*
window, so both stay compatible.

- Windows x64, OBS Studio 31 or newer (tested on 33.0)
- Author: BlackDream feat. ChromeBread · License: GPL-2.0

## Install

1. Download `obs-vst-ex-audio-filter-1.0.0-windows-x64.zip` from [Releases](../../releases).
2. Quit OBS and extract the zip into `C:\ProgramData\obs-studio\plugins\`
   (you get `C:\ProgramData\obs-studio\plugins\obs-vst-ex-audio-filter\bin\64bit\...`).
3. Start OBS. On first start it asks where to keep thumbnails, screenshots and history.

Uninstall: quit OBS and delete that `obs-vst-ex-audio-filter` folder (Tools > VST Ex Audio Filter > Debug mode
lists every folder to delete).

## Features

**Filters (VST Ex) window** — Audio Mixer: right-click a source > *Filters (VST Ex)*
- Big ON/OFF buttons, multi-select, drag to reorder, duplicate (Ctrl+D), delete all, a note per filter
- Undo / Redo and an autosaved history that survives OBS restarts
- Per-filter latency, total latency, and a button that sets Sync Offset to compensate
- **Presets**: save / apply / export / import a whole filter chain
- **ON/OFF sets** with Sync Offset and volume: switch verse → chorus mid-song instantly, by hotkey
- **Measure with 4 beats**: a BPM120 test song plays, you clap along (or hold an earphone to the mic) and the
  Sync Offset is computed; zoomable waveforms with a millisecond ruler for fine manual adjustment

**VST plugins**
- VST2 and VST3, crash-safe scanning in a separate process (started by you, with time estimate)
- Browser with list / thumbnails / details view, sort, search, favorites, HID (hide) marks, notes, recently used
- Thumbnails taken automatically the first time an editor opens, or all at once; per-filter screenshots
- Export / import plugin settings from the editor window

**Built-in filters**
- *VST Ex Pitch / Pan*: pitch in semitones and pan, handy to tell devices apart when aligning them
- *VST Ex Pseudo Stereo*: widens a mono mic without a DAW (Haas effect), one filter to add or remove

**Docks and tools**
- *VST Ex* dock: ON/OFF set buttons and CPU usage (OBS total and VST share)
- *VST Ex Mixer* dock: one column per source under the Audio Mixer's meters, a button per VST
- *VST Ex Chain View* (Tools menu): every source from device to output, latency, levels, sidechain links,
  optional half-circle imager, save as image
- Tools > VST Ex Audio Filter: language (EN / JP / KR / ID / FR / TW), version, debug information

## Preview

![Filters (VST Ex) window, VST Ex Mixer and VST Ex docks](docs/images/preview-filters.jpg)
*Filters (VST Ex) window, VST Ex Mixer and VST Ex docks*

![Choose a VST Plugin (thumbnail view)](docs/images/preview-browser.jpg)
*Choose a VST Plugin (thumbnail view)*

## Build

Visual Studio 2022 and CMake 3.28+: `cmake --preset windows-x64` then
`cmake --build build_x64 --config RelWithDebInfo`.

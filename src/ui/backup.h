#pragma once

class QWidget;

// Debug mode > Backup: every setting of VST Ex and VST Enigmax Loader, plus thumbnails, screenshots,
// history and presets, to a folder and back.
bool export_all_settings(QWidget *parent);
bool import_all_settings(QWidget *parent);

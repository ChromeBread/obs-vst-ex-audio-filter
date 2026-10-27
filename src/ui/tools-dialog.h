#pragma once

#include <QDialog>

// Tools > VST Ex Audio Filter: language, both Hidden buttons, version info and a debug section
// (install paths, uninstall steps, screenshot storage, VST folders).
class ToolsDialog : public QDialog {
	Q_OBJECT

public:
	static void open();

private:
	explicit ToolsDialog(QWidget *parent);
	QWidget *buildDebugSection();
};

void install_tools_menu();
// Asks for the data folder (thumbnails, screenshots, history). True if one was set.
bool choose_data_folder(QWidget *parent, bool firstRun);
// The language chosen in the Tools dialog, if any, replaces the one OBS picked at load.
void apply_saved_language();

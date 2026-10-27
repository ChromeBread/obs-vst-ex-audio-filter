#pragma once

#include "host/plugin-host.h"

#include <QImage>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

// Top-level window that hosts a plugin's native editor, with a small screenshot toolbar on top.
class EditorWindow : public QWidget {
	Q_OBJECT

public:
	EditorWindow(vstex::PluginInstance *plugin, const QString &title, QWidget *parent);
	~EditorWindow() override;

	bool openEditor();
	// Must be called before the plugin is destroyed.
	void closeEditor();

	QImage grabEditor() const;
	// Screenshot -> plugin thumbnail. False if there is nothing to grab.
	bool saveThumbnail();

	// Set by a filter so imported settings go through its OBS settings (autosave, undo).
	// Without it the state is put into the plugin directly.
	std::function<void(const std::vector<uint8_t> &state)> importState;

	// Loads the plugin just for this, opens its editor briefly and stores the screenshot as its thumbnail.
	// `done` gets whether a thumbnail was saved; with a callback, errors are not shown in a message box.
	static void captureThumbnail(const vstex::PluginDescription &desc, QWidget *parent,
				     std::function<void(bool)> done = nullptr);

	// How long an editor gets to draw its first frames before it is captured.
	static constexpr int kCaptureDelayMs = 1500;

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	void resizeToPlugin(int width, int height);
	void saveScreenshotAs();
	void exportSettings();
	void importSettings();

	vstex::PluginInstance *plugin;
	std::shared_ptr<vstex::PluginInstance> ownedPlugin; // set by captureThumbnail()
	QWidget *container;
	QTimer idleTimer;
	bool opened = false;
};

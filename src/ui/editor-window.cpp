#include "ui/editor-window.h"

#include "plugin-meta.h"
#include "ui/ui-text.h"

#include <obs.h>

#include <QCloseEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <windows.h>

EditorWindow::EditorWindow(vstex::PluginInstance *plugin_, const QString &title, QWidget *parent)
	: QWidget(parent, Qt::Window), plugin(plugin_)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(title);

	auto *thumb = new QPushButton(QStringLiteral("📷 ") + T("Editor.SetThumbnail"), this);
	auto *saveAs = new QPushButton(T("Editor.SaveScreenshot"), this);
	thumb->setToolTip(T("Editor.SetThumbnail.Tip"));
	connect(thumb, &QPushButton::clicked, this, &EditorWindow::saveThumbnail);
	connect(saveAs, &QPushButton::clicked, this, &EditorWindow::saveScreenshotAs);
	auto *exportButton = new QPushButton(T("Editor.Export"), this);
	auto *importButton = new QPushButton(T("Editor.Import"), this);
	connect(exportButton, &QPushButton::clicked, this, &EditorWindow::exportSettings);
	connect(importButton, &QPushButton::clicked, this, &EditorWindow::importSettings);

	auto *toolbar = new QHBoxLayout();
	toolbar->setContentsMargins(4, 2, 4, 2);
	toolbar->addWidget(thumb);
	toolbar->addWidget(saveAs);
	toolbar->addSpacing(12);
	toolbar->addWidget(exportButton);
	toolbar->addWidget(importButton);
	toolbar->addStretch(1);

	container = new QWidget(this);
	container->setAttribute(Qt::WA_NativeWindow);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->setSizeConstraint(QLayout::SetFixedSize);
	layout->addLayout(toolbar);
	layout->addWidget(container);

	connect(&idleTimer, &QTimer::timeout, this, [this] {
		if (plugin)
			plugin->editorIdle();
	});
}

EditorWindow::~EditorWindow()
{
	closeEditor();
}

bool EditorWindow::openEditor()
{
	if (!plugin || opened)
		return opened;
	plugin->onEditorResize = [this](int w, int h) {
		QMetaObject::invokeMethod(this, [this, w, h] { resizeToPlugin(w, h); });
	};
	int w = 0, h = 0;
	if (!plugin->openEditor((void *)container->winId(), w, h)) {
		plugin->onEditorResize = nullptr;
		return false;
	}
	opened = true;
	resizeToPlugin(w, h);
	idleTimer.start(30);
	// First time this plugin's editor is seen: take its thumbnail automatically.
	if (!ownedPlugin && !PluginMeta::instance().hasUserThumbnail(plugin->description()))
		QTimer::singleShot(kCaptureDelayMs, this, [this] {
			if (plugin && !PluginMeta::instance().hasUserThumbnail(plugin->description()))
				saveThumbnail();
		});
	return true;
}

void EditorWindow::closeEditor()
{
	idleTimer.stop();
	if (plugin && opened) {
		plugin->closeEditor();
		plugin->onEditorResize = nullptr;
	}
	opened = false;
	plugin = nullptr;
	ownedPlugin.reset();
}

void EditorWindow::closeEvent(QCloseEvent *event)
{
	closeEditor();
	event->accept();
}

void EditorWindow::resizeToPlugin(int width, int height)
{
	// Plugins report physical pixels; Qt sizes are logical.
	qreal ratio = devicePixelRatioF();
	container->setFixedSize(qMax(1, qRound(width / ratio)), qMax(1, qRound(height / ratio)));
}

// PrintWindow with PW_RENDERFULLCONTENT also captures GPU-drawn editors and works when the window is covered.
QImage EditorWindow::grabEditor() const
{
	HWND hwnd = (HWND)container->winId();
	RECT rc;
	GetClientRect(hwnd, &rc);
	int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0)
		return {};

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = SelectObject(mem, bmp);
	PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
	SelectObject(mem, old);
	QImage image = QImage::fromHBITMAP(bmp);
	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return image;
}

bool EditorWindow::saveThumbnail()
{
	if (!plugin)
		return false;
	QImage image = grabEditor();
	if (image.isNull())
		return false;
	PluginMeta::instance().setThumbnail(plugin->description(), image);
	return true;
}

void EditorWindow::saveScreenshotAs()
{
	if (!plugin)
		return;
	QImage image = grabEditor();
	if (image.isNull())
		return;
	QString name = QString("%1 %2.png")
			       .arg(QString::fromStdString(plugin->description().name),
				    QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss"));
	QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	QString path = QFileDialog::getSaveFileName(this, T("Editor.SaveScreenshot"), dir + "/" + name, "PNG (*.png)");
	if (!path.isEmpty())
		image.save(path, "PNG");
}

namespace {
const char *kPresetFilter = "VST Ex preset (*.vstexpreset)";
}

// JSON with the plugin identity, so a preset is never loaded into a different plugin.
void EditorWindow::exportSettings()
{
	if (!plugin)
		return;
	const auto &d = plugin->description();
	std::vector<uint8_t> state = plugin->getState();
	QJsonObject root{{"format", vstex::formatName(d.format)},
			 {"uid", QString::fromStdString(d.uid)},
			 {"name", QString::fromStdString(d.name)},
			 {"state", QString::fromLatin1(QByteArray((const char *)state.data(), (qsizetype)state.size()).toBase64())}};
	QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	QString name = QString("%1 %2.vstexpreset")
			       .arg(QString::fromStdString(d.name), QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss"));
	QString path = QFileDialog::getSaveFileName(this, T("Editor.Export"), dir + "/" + name, kPresetFilter);
	if (path.isEmpty())
		return;
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0)
		QMessageBox::warning(this, T("Editor.Export"), f.errorString());
}

void EditorWindow::importSettings()
{
	if (!plugin)
		return;
	QString path = QFileDialog::getOpenFileName(this, T("Editor.Import"),
						    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
						    kPresetFilter);
	if (path.isEmpty())
		return;
	QFile f(path);
	QJsonObject root = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
	if (!root.contains("state")) {
		QMessageBox::warning(this, T("Editor.Import"), T("Editor.Import.Invalid"));
		return;
	}
	const auto &d = plugin->description();
	if (root["uid"].toString() != QString::fromStdString(d.uid)) {
		QMessageBox::warning(this, T("Editor.Import"), T("Editor.Import.OtherPlugin").arg(root["name"].toString()));
		return;
	}
	QByteArray raw = QByteArray::fromBase64(root["state"].toString().toLatin1());
	std::vector<uint8_t> state(raw.begin(), raw.end());
	if (importState)
		importState(state);
	else
		plugin->setState(state);
}

void EditorWindow::captureThumbnail(const vstex::PluginDescription &desc, QWidget *parent,
				    std::function<void(bool)> done)
{
	std::string error;
	std::shared_ptr<vstex::PluginInstance> plugin = vstex::loadPlugin(desc.path, desc.uid, error);
	audio_t *audio = obs_get_audio();
	if (plugin && !plugin->prepare((double)audio_output_get_sample_rate(audio), AUDIO_OUTPUT_FRAMES,
				       (int)audio_output_get_channels(audio)))
		plugin.reset();
	if (!plugin) {
		if (done)
			done(false);
		else
			QMessageBox::warning(parent, T("Browser.Capture"),
					     T("LoadFailed") + ": " + QString::fromStdString(error));
		return;
	}

	auto *window = new EditorWindow(plugin.get(), QString::fromStdString(desc.name), parent);
	window->ownedPlugin = plugin;
	if (!window->openEditor()) {
		delete window;
		if (done)
			done(false);
		return;
	}
	window->show();
	// Give the editor time to draw its first frames, then capture and close.
	QTimer::singleShot(kCaptureDelayMs, window, [window, done] {
		bool ok = window->saveThumbnail();
		window->close();
		if (done)
			done(ok);
	});
}

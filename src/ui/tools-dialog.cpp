#include "ui/tools-dialog.h"

#include "plugin-meta.h"
#include "ui/backup.h"
#include "loader-api.h"
#include "ui/chain-view.h"
#include "ui/ui-text.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QDate>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct Language {
	const char *code;
	const char *label;
};
const Language kLanguages[] = {{"en-US", "EN  English"},          {"ja-JP", "JP  日本語"},
			       {"ko-KR", "KR  한국어"},             {"id-ID", "ID  Bahasa Indonesia"},
			       {"fr-FR", "FR  Français"},         {"zh-TW", "TW  繁體中文"}};

QPointer<ToolsDialog> openDialog;

// Saved choice, else OBS's language when it is one of ours, else English.
QString currentLanguage()
{
	QString saved = PluginMeta::instance().uiValue("language").toString();
	if (!saved.isEmpty())
		return saved;
	QString obs = QString::fromUtf8(obs_get_locale());
	for (const Language &l : kLanguages)
		if (obs == l.code)
			return obs;
	return "en-US";
}

QString native(const QString &path)
{
	return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QLabel *selectable(const QString &text)
{
	auto *label = new QLabel(text);
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	label->setWordWrap(true);
	return label;
}

QString binaryDir()
{
	return QFileInfo(QString::fromUtf8(obs_get_module_binary_path(obs_current_module()))).absolutePath();
}

QString configDir()
{
	char *p = obs_module_config_path("");
	QString path = QString::fromUtf8(p);
	bfree(p);
	return path;
}

} // namespace

void install_tools_menu()
{
	obs_frontend_add_tools_menu_item("VST Ex Audio Filter", [](void *) { ToolsDialog::open(); }, nullptr);
	if (PluginMeta::instance().uiValue("featureChainView").toBool(true))
		obs_frontend_add_tools_menu_item("VST Ex Chain View", [](void *) { open_chain_view(); }, nullptr);
	// First run: ask for the data folder once OBS is up, so it is not forgotten.
	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void *) {
			if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING && !PluginMeta::instance().dataDirChosen())
				QTimer::singleShot(0, [] {
					choose_data_folder(static_cast<QWidget *>(obs_frontend_get_main_window()), true);
				});
		},
		nullptr);
}

bool choose_data_folder(QWidget *parent, bool firstRun)
{
	auto &meta = PluginMeta::instance();
	QDialog dlg(parent);
	dlg.setWindowTitle(T("Tools.DataDir"));
	auto *help = new QLabel(firstRun ? T("Tools.DataDir.FirstRun") : T("Tools.DataDir.Help"), &dlg);
	help->setWordWrap(true);
	auto *path = new QLineEdit(native(meta.dataDir()), &dlg);
	auto *browse = new QPushButton(T("Browse"), &dlg);
	QObject::connect(browse, &QPushButton::clicked, &dlg, [&dlg, path] {
		QString dir = QFileDialog::getExistingDirectory(&dlg, T("Tools.DataDir"), path->text());
		if (!dir.isEmpty())
			path->setText(native(dir));
	});
	auto *row = new QHBoxLayout();
	row->addWidget(path, 1);
	row->addWidget(browse);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	auto *layout = new QVBoxLayout(&dlg);
	layout->addWidget(help);
	layout->addLayout(row);
	layout->addWidget(buttons);
	dlg.resize(560, dlg.sizeHint().height());

	if (dlg.exec() != QDialog::Accepted) {
		if (firstRun) { // keep the default and don't ask again; it can be changed in Tools
			meta.setDataDir(meta.dataDir());
			loader_set_data_dir(meta.dataDir()); // one data folder for both modules
		}
		return false;
	}
	QString dir = QDir::fromNativeSeparators(path->text().trimmed());
	if (dir.isEmpty() || !QDir().mkpath(dir)) {
		QMessageBox::warning(parent, T("Tools.DataDir"), T("Tools.DataDir.Failed").arg(path->text()));
		return false;
	}
	if (!meta.setDataDir(dir)) {
		QMessageBox::warning(parent, T("Tools.DataDir"), T("Tools.DataDir.Failed").arg(path->text()));
		return false;
	}
	loader_set_data_dir(dir);
	return true;
}

void apply_saved_language()
{
	QString lang = PluginMeta::instance().uiValue("language").toString();
	if (!lang.isEmpty())
		obs_module_set_locale(lang.toUtf8().constData());
}

void ToolsDialog::open()
{
	if (openDialog) {
		openDialog->raise();
		openDialog->activateWindow();
		return;
	}
	openDialog = new ToolsDialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	openDialog->show();
}

ToolsDialog::ToolsDialog(QWidget *parent) : QDialog(parent)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(T("Tools.Title"));
	resize(720, 420);
	setMaximumSize(1280, 720); // debug mode included, it must fit a 1280x720 screen
	auto &meta = PluginMeta::instance();

	auto *language = new QComboBox(this);
	for (const Language &l : kLanguages)
		language->addItem(QString::fromUtf8(l.label), QString::fromLatin1(l.code));
	language->setCurrentIndex(qMax(0, language->findData(currentLanguage())));
	connect(language, &QComboBox::activated, this, [this, language] {
		QString code = language->currentData().toString();
		PluginMeta::instance().setUiValue("language", code);
		loader_set_ui("language", code);
		obs_module_set_locale(code.toUtf8().constData());
		// Reopen so this window is in the new language too.
		close();
		QTimer::singleShot(0, [] { ToolsDialog::open(); });
	});
	auto *languageNote = new QLabel(T("Tools.Language.Note"), this);
	languageNote->setEnabled(false);

	auto *hiddenBoth = new QCheckBox(T("Tools.HiddenBoth"), this);
	hiddenBoth->setToolTip(T("Tools.HiddenBoth.Tip"));
	hiddenBoth->setChecked(meta.uiValue("hidePluginRows").toBool(false) && meta.uiValue("pickerHidden").toBool(true));
	connect(hiddenBoth, &QCheckBox::toggled, this, [](bool on) {
		PluginMeta::instance().setUiValue("hidePluginRows", on);
		PluginMeta::instance().setUiValue("pickerHidden", on);
		loader_set_ui("pickerHidden", on);
	});

	auto *features = new QGroupBox(T("Tools.Features"), this);
	auto *featureLayout = new QVBoxLayout(features);
	for (auto [key, text] : {std::pair{"featureChainView", "Tools.Feature.ChainView"},
				 {"featureMixer", "Tools.Feature.Mixer"},
				 {"featureSwitcher", "Tools.Feature.Switcher"}}) {
		auto *box = new QCheckBox(T(text), features);
		box->setChecked(meta.uiValue(key).toBool(true));
		connect(box, &QCheckBox::toggled, this, [key](bool on) { PluginMeta::instance().setUiValue(key, on); });
		featureLayout->addWidget(box);
	}
	auto *featureNote = new QLabel(T("Tools.Features.Note"), features);
	featureNote->setEnabled(false);
	featureLayout->addWidget(featureNote);

	auto *debug = new QCheckBox(T("Tools.Debug"), this);
	debug->setChecked(meta.uiValue("debugMode").toBool(false));

	auto *form = new QFormLayout();
	form->addRow(T("Tools.Language"), language);
	form->addRow(QString(), languageNote);
	form->addRow(hiddenBoth);
	form->addRow(T("Tools.Version"), selectable(QString::fromUtf8(PLUGIN_VERSION)));
	// __DATE__ is "Oct 27 2026" (English, ambiguous order for some readers); parse it, then format per language.
	QDate built = QLocale::c().toDate(QStringLiteral(__DATE__).simplified(), "MMM d yyyy");
	// Per-language pattern with year, month and day marked (e.g. "yyyy'年'-M'月'-d'日'"), so no order is misread.
	QString builtText = QLocale(currentLanguage()).toString(built, T("Tools.BuildDate.Format"));
	form->addRow(T("Tools.BuildDate"), selectable(builtText + "  " + QStringLiteral(__TIME__).left(5)));
	form->addRow(T("Tools.Author"), selectable(QStringLiteral("BlackDream feat. ChromeBread")));
	form->addRow(debug);

	QWidget *debugSection = buildDebugSection();
	debugSection->setVisible(debug->isChecked());
	connect(debug, &QCheckBox::toggled, this, [this, debugSection](bool on) {
		PluginMeta::instance().setUiValue("debugMode", on);
		debugSection->setVisible(on);
		resize(width(), qMin(720, sizeHint().height()));
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(features);
	layout->addWidget(debugSection, 1);
	layout->addWidget(buttons);
}

QWidget *ToolsDialog::buildDebugSection()
{
	// Tabs keep the dialog compact; stacked group boxes did not fit a 1280x720 screen.
	auto *tabs = new QTabWidget(this);
	QWidget *box = tabs;

	// Data folder (paths stay out of sight unless debug mode is on)
	auto &meta = PluginMeta::instance();
	auto *dataPage = new QWidget(box);
	auto *dataLayout = new QVBoxLayout(dataPage);
	auto *dataDir = selectable(native(meta.dataDir()));
	auto *changeDir = new QPushButton(T("Tools.DataDir.Change"), dataPage);
	connect(changeDir, &QPushButton::clicked, this, [this] {
		if (choose_data_folder(this, false)) {
			close(); // reopen with the new paths
			QTimer::singleShot(0, [] { ToolsDialog::open(); });
		}
	});
	auto *dataRow = new QHBoxLayout();
	dataRow->addWidget(dataDir, 1);
	dataRow->addWidget(changeDir);

	dataLayout->addLayout(dataRow);
	dataLayout->addStretch(1);
	tabs->addTab(dataPage, T("Tools.DataDir"));

	// Installed files
	QString bin = binaryDir();
	QString pluginRoot = QDir(bin + "/../..").absolutePath();
	auto *installed = new QWidget(box);
	auto *installedLayout = new QVBoxLayout(installed);
	installedLayout->addWidget(selectable(
		QStringList{native(QString::fromUtf8(obs_get_module_binary_path(obs_current_module()))),
			    native(QString::fromUtf8(obs_get_module_data_path(obs_current_module())))}
			.join('\n')));
	tabs->addTab(installed, T("Tools.Installed"));

	// Uninstall
	auto *uninstall = new QWidget(box);
	auto *uninstallLayout = new QVBoxLayout(uninstall);
	uninstallLayout->addWidget(selectable(T("Tools.Uninstall.Text") + "\n" +
					      T("Tools.Uninstall.Plugin").arg(native(pluginRoot)) + "\n" +
					      T("Tools.Uninstall.Config").arg(native(configDir())) + "\n" +
					      T("Tools.Uninstall.Data").arg(native(PluginMeta::instance().dataDir()))));
	tabs->addTab(uninstall, T("Tools.Uninstall"));

	// Backup: everything both modules keep, to a folder and back
	auto *backup = new QWidget(box);
	auto *backupLayout = new QVBoxLayout(backup);
	backupLayout->addWidget(selectable(T("Backup.Text")));
	auto *exportAll = new QPushButton(T("Backup.Export"), backup);
	auto *importAll = new QPushButton(T("Backup.Import"), backup);
	connect(exportAll, &QPushButton::clicked, this, [this] { export_all_settings(this); });
	connect(importAll, &QPushButton::clicked, this, [this] {
		if (import_all_settings(this)) {
			close(); // reopen with the imported settings
			QTimer::singleShot(0, [] { ToolsDialog::open(); });
		}
	});
	auto *backupButtons = new QHBoxLayout();
	backupButtons->addWidget(exportAll);
	backupButtons->addWidget(importAll);
	backupButtons->addStretch(1);
	backupLayout->addLayout(backupButtons);
	backupLayout->addStretch(1);
	tabs->addTab(backup, T("Backup"));

	return tabs;
}

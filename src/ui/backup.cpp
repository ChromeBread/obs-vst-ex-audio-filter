#include "ui/backup.h"

#include "loader-api.h"
#include "plugin-meta.h"
#include "ui/ui-text.h"

#include <obs-module.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>

// Backup layout (the uninstaller writes the same one):
//   vst-ex-backup.ini
//   config/OBS-VST-Ex-Audio-Filter/*   VST Ex settings (top-level files of its config folder)
//   config/vst-enigmax-loader/*        VST Enigmax Loader settings, plugin cache
//   data/thumbnails, data/screenshots  the loader's pictures
//   data/history, data/presets         VST Ex's filter history and chain presets
// ON/OFF sets are not here: they are saved in OBS's scene collections.

namespace {

const char *kMainName = "OBS-VST-Ex-Audio-Filter";
const char *kLoaderName = "vst-enigmax-loader";
const char *kLoaderSubs[] = {"thumbnails", "screenshots"};
const char *kMainSubs[] = {"history", "presets"};

QString configRoot()
{
	char *p = obs_module_config_path("");
	QString path = QDir::cleanPath(QString::fromUtf8(p) + "/..");
	bfree(p);
	return path;
}

QString loaderDataDir()
{
	QString dir = configRoot() + "/" + kLoaderName;
	QFile f(dir + "/plugin-meta.json");
	if (f.open(QIODevice::ReadOnly)) {
		QString saved = QJsonDocument::fromJson(f.readAll()).object()["ui"].toObject()["dataDir"].toString();
		if (!saved.isEmpty())
			return saved;
	}
	return dir;
}

// Copies every file under `from` into `to`, overwriting. False on the first file that fails.
bool copyTree(const QString &from, const QString &to, bool topLevelOnly = false)
{
	if (!QDir(from).exists())
		return true;
	QDirIterator it(from, QDir::Files, topLevelOnly ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString file = it.next();
		QString target = to + file.mid(from.size());
		QDir().mkpath(QFileInfo(target).path());
		QFile::remove(target);
		if (!QFile::copy(file, target))
			return false;
	}
	return true;
}

// Imported settings keep pointing at this machine's data folder, not the one they were exported from.
void keepDataDir(const QString &metaFile, const QString &dataDir)
{
	QFile in(metaFile);
	if (!in.open(QIODevice::ReadOnly))
		return;
	QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
	in.close();
	QJsonObject ui = root["ui"].toObject();
	ui["dataDir"] = dataDir;
	root["ui"] = ui;
	QSaveFile out(metaFile);
	if (out.open(QIODevice::WriteOnly)) {
		out.write(QJsonDocument(root).toJson());
		out.commit();
	}
}

} // namespace

bool export_all_settings(QWidget *parent)
{
	QString dir = QFileDialog::getExistingDirectory(parent, T("Backup.Export"));
	if (dir.isEmpty())
		return false;
	QString to = dir + "/VST-Ex-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
	QString root = configRoot();
	bool ok = true;
	for (const char *name : {kMainName, kLoaderName})
		ok = ok && copyTree(root + "/" + name, to + "/config/" + name, true);
	QFile::remove(to + "/config/" + kMainName + "/install-features.ini"); // one-shot installer hand-off
	for (const char *sub : kLoaderSubs)
		ok = ok && copyTree(loaderDataDir() + "/" + sub, to + "/data/" + sub);
	for (const char *sub : kMainSubs)
		ok = ok && copyTree(PluginMeta::instance().dataDir() + "/" + sub, to + "/data/" + sub);
	QSettings ini(to + "/vst-ex-backup.ini", QSettings::IniFormat);
	ini.setValue("backup/version", 1);
	ini.setValue("backup/created", QDateTime::currentDateTime().toString(Qt::ISODate));
	ini.sync();
	if (!ok) {
		QMessageBox::warning(parent, T("Backup.Export"), T("Backup.Failed").arg(QDir::toNativeSeparators(to)));
		return false;
	}
	QMessageBox::information(parent, T("Backup.Export"), T("Backup.Exported").arg(QDir::toNativeSeparators(to)));
	return true;
}

bool import_all_settings(QWidget *parent)
{
	QString from = QFileDialog::getExistingDirectory(parent, T("Backup.Import"));
	if (from.isEmpty())
		return false;
	if (!QFileInfo::exists(from + "/vst-ex-backup.ini")) { // never spray a random folder over the settings
		QMessageBox::warning(parent, T("Backup.Import"), T("Backup.NotBackup"));
		return false;
	}
	if (QMessageBox::question(parent, T("Backup.Import"), T("Backup.Import.Confirm")) != QMessageBox::Yes)
		return false;
	QString root = configRoot();
	QString mainData = PluginMeta::instance().dataDir(), loaderData = loaderDataDir();
	bool ok = true;
	for (const char *name : {kMainName, kLoaderName})
		ok = ok && copyTree(from + "/config/" + name, root + "/" + name, true);
	keepDataDir(root + "/" + kMainName + "/plugin-meta.json", mainData);
	if (QFileInfo::exists(root + "/" + kLoaderName + "/plugin-meta.json"))
		keepDataDir(root + "/" + kLoaderName + "/plugin-meta.json", loaderData);
	for (const char *sub : kLoaderSubs)
		ok = ok && copyTree(from + "/data/" + sub, loaderData + "/" + sub);
	for (const char *sub : kMainSubs)
		ok = ok && copyTree(from + "/data/" + sub, mainData + "/" + sub);
	// Both modules re-read their files now, so nothing in memory writes the old settings back.
	PluginMeta::instance().reload();
	loader_reload();
	if (!ok)
		QMessageBox::warning(parent, T("Backup.Import"), T("Backup.Failed").arg(QDir::toNativeSeparators(from)));
	else
		QMessageBox::information(parent, T("Backup.Import"), T("Backup.Imported"));
	return ok;
}

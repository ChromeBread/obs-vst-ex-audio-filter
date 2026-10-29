#include "plugin-scanner.h"

#include <obs-module.h>
#include <util/platform.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

namespace {

constexpr int kParallelScans = 4;
constexpr int kCacheVersion = 2; // 2: VST3 bundle snapshots
constexpr int kScanTimeoutMs = 120000; // big shells (WaveShell) take a while

QString configFile(const char *name)
{
	char *p = obs_module_config_path(name);
	QString path = QString::fromUtf8(p);
	bfree(p);
	return path;
}

QString scannerExe()
{
	QFileInfo module(QString::fromUtf8(obs_get_module_binary_path(obs_current_module())));
	return module.dir().filePath("vst-ex-scanner.exe");
}

QStringList defaultPaths()
{
	QString common = qEnvironmentVariable("CommonProgramFiles", "C:/Program Files/Common Files");
	QString programs = qEnvironmentVariable("ProgramFiles", "C:/Program Files");
	QStringList candidates = {common + "/VST3",           common + "/VST2",
				  common + "/Steinberg/VST2", programs + "/VSTPlugins",
				  programs + "/Steinberg/VSTPlugins"};
	QStringList existing;
	for (const QString &c : candidates)
		if (QDir(c).exists())
			existing << QDir::cleanPath(c);
	return existing;
}

QJsonObject toJson(const vstex::PluginDescription &d)
{
	return {{"format", vstex::formatName(d.format)},
		{"path", QString::fromStdString(d.path)},
		{"uid", QString::fromStdString(d.uid)},
		{"name", QString::fromStdString(d.name)},
		{"vendor", QString::fromStdString(d.vendor)},
		{"category", QString::fromStdString(d.category)},
		{"version", QString::fromStdString(d.version)},
		{"description", QString::fromStdString(d.description)},
		{"snapshot", QString::fromStdString(d.snapshot)},
		{"inputs", d.numInputs},
		{"outputs", d.numOutputs},
		{"editor", d.hasEditor}};
}

vstex::PluginDescription fromJson(const QJsonObject &o)
{
	vstex::PluginDescription d;
	d.format = o["format"].toString() == "VST2" ? vstex::PluginFormat::VST2 : vstex::PluginFormat::VST3;
	d.path = o["path"].toString().toStdString();
	d.uid = o["uid"].toString().toStdString();
	d.name = o["name"].toString().toStdString();
	d.vendor = o["vendor"].toString().toStdString();
	d.category = o["category"].toString().toStdString();
	d.version = o["version"].toString().toStdString();
	d.description = o["description"].toString().toStdString();
	d.snapshot = o["snapshot"].toString().toStdString();
	d.numInputs = o["inputs"].toInt();
	d.numOutputs = o["outputs"].toInt();
	d.hasEditor = o["editor"].toBool();
	return d;
}

// .vst3 bundles are folders; don't descend into them. Everything else is a candidate .vst3 or VST2 .dll.
QStringList findPluginFiles(const QStringList &roots)
{
	QStringList found;
	QSet<QString> seen; // a symlink or junction pointing back up would otherwise loop forever
	for (const QString &root : roots) {
		QStringList pending{root};
		while (!pending.isEmpty()) {
			QDir dir(pending.takeFirst());
			if (seen.contains(dir.canonicalPath()))
				continue;
			seen.insert(dir.canonicalPath());
			for (const QFileInfo &fi : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
				QString suffix = fi.suffix().toLower();
				if (suffix == "vst3")
					found << QDir::cleanPath(fi.absoluteFilePath());
				else if (fi.isDir())
					pending << fi.absoluteFilePath();
				else if (suffix == "dll")
					found << QDir::cleanPath(fi.absoluteFilePath());
			}
		}
	}
	return found;
}

} // namespace

PluginCatalog &PluginCatalog::instance()
{
	static PluginCatalog catalog;
	return catalog;
}

PluginCatalog::PluginCatalog()
{
	load();
}

QVector<vstex::PluginDescription> PluginCatalog::plugins() const
{
	QVector<vstex::PluginDescription> list;
	for (const FileEntry &e : files)
		list += e.plugins;
	return list;
}

void PluginCatalog::setSearchPaths(const QStringList &list)
{
	paths = list;
	save();
}

void PluginCatalog::scan(bool full)
{
	if (isScanning())
		return;
	if (full)
		files.clear();

	QStringList candidates = findPluginFiles(paths);
	// Drop cache entries for files that are gone.
	for (auto it = files.begin(); it != files.end();)
		it = candidates.contains(it.key()) ? std::next(it) : files.erase(it);

	queue.clear();
	for (const QString &file : candidates) {
		qint64 modified = QFileInfo(file).lastModified().toMSecsSinceEpoch();
		auto it = files.find(file);
		if (it == files.end() || it->modified != modified)
			queue << file;
	}
	done = 0;
	total = (int)queue.size();
	if (queue.isEmpty()) {
		save();
		emit finished();
		return;
	}
	for (int i = 0; i < kParallelScans && !queue.isEmpty(); i++)
		startNext();
}

void PluginCatalog::startNext()
{
	if (queue.isEmpty()) {
		if (running == 0) {
			save();
			emit finished();
		}
		return;
	}

	QString file = queue.takeFirst();
	running++;
	emit progress(done, total, file);

	auto *proc = new QProcess(this);
	auto *timeout = new QTimer(proc);
	timeout->setSingleShot(true);
	connect(timeout, &QTimer::timeout, proc, &QProcess::kill);
	connect(proc, &QProcess::finished, this, [this, proc, file](int exitCode, QProcess::ExitStatus status) {
		FileEntry entry;
		entry.modified = QFileInfo(file).lastModified().toMSecsSinceEpoch();
		if (status == QProcess::NormalExit && exitCode == 0) {
			QJsonArray arr = QJsonDocument::fromJson(proc->readAllStandardOutput()).array();
			for (const auto &v : arr)
				entry.plugins << fromJson(v.toObject());
		}
		if (entry.plugins.isEmpty()) {
			QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
			entry.error = status == QProcess::CrashExit ? QStringLiteral("crashed or timed out")
								    : (err.isEmpty() ? QStringLiteral("not a plugin") : err);
		}
		files[file] = entry;
		proc->deleteLater();
		running--;
		done++;
		emit progress(done, total, file);
		startNext();
	});
	timeout->start(kScanTimeoutMs);
	proc->start(scannerExe(), {QDir::toNativeSeparators(file)});
}

void PluginCatalog::load()
{
	QFile f(configFile("plugin-cache.json"));
	if (!f.open(QIODevice::ReadOnly)) {
		paths = defaultPaths();
		return;
	}
	QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
	for (const auto &p : root["paths"].toArray())
		paths << p.toString();
	if (root["version"].toInt() < kCacheVersion)
		return; // older cache lacks fields; rescan files but keep the folders
	for (const auto &v : root["files"].toArray()) {
		QJsonObject o = v.toObject();
		FileEntry e;
		e.modified = (qint64)o["modified"].toDouble();
		e.error = o["error"].toString();
		for (const auto &p : o["plugins"].toArray())
			e.plugins << fromJson(p.toObject());
		files.insert(o["file"].toString(), e);
	}
}

void PluginCatalog::save() const
{
	QJsonArray fileArr;
	for (auto it = files.begin(); it != files.end(); ++it) {
		QJsonArray plugins;
		for (const auto &d : it->plugins)
			plugins << toJson(d);
		fileArr << QJsonObject{{"file", it.key()},
				       {"modified", (double)it->modified},
				       {"error", it->error},
				       {"plugins", plugins}};
	}
	QJsonObject root{{"version", kCacheVersion}, {"paths", QJsonArray::fromStringList(paths)}, {"files", fileArr}};

	QString path = configFile("plugin-cache.json");
	QDir().mkpath(QFileInfo(path).path());
	QSaveFile f(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(root).toJson());
		f.commit();
	}
}

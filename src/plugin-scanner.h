#pragma once

#include "host/plugin-host.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

class QProcess;

// Known plugins, cached on disk. Scanning runs vst-ex-scanner.exe once per plugin file.
class PluginCatalog : public QObject {
	Q_OBJECT

public:
	static PluginCatalog &instance();

	QVector<vstex::PluginDescription> plugins() const;
	QStringList searchPaths() const { return paths; }
	void setSearchPaths(const QStringList &list);

	// full = forget the cache and rescan everything; otherwise only new or changed files.
	void scan(bool full);
	bool isScanning() const { return !queue.isEmpty() || running > 0; }

signals:
	void progress(int done, int total, const QString &current);
	void finished();

private:
	struct FileEntry {
		qint64 modified = 0;
		QString error; // non-empty if the file is not a usable plugin
		QVector<vstex::PluginDescription> plugins;
	};

	PluginCatalog();
	void load();
	void save() const;
	void startNext();

	QStringList paths;
	QHash<QString, FileEntry> files; // keyed by plugin file path
	QStringList queue;
	int running = 0;
	int done = 0;
	int total = 0;
};

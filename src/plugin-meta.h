#pragma once

#include "host/plugin-host.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

// The user's own information about plugins: favorites 1-8, hidden, memo, screenshot thumbnail.
// Stored in the module config folder (plugin-meta.json + thumbnails/).
class PluginMeta : public QObject {
	Q_OBJECT

public:
	static constexpr int kFavorites = 8;
	static constexpr int kMaxRecent = 50; // keeps the file and the "Used" tab small

	struct Entry {
		bool hidden = false;
		quint8 favorites = 0; // bit n = favorite n+1
		QString memo;
	};

	static PluginMeta &instance();
	static QString keyOf(const vstex::PluginDescription &d);

	Entry get(const QString &key) const { return entries.value(key); }
	void setHidden(const QString &key, bool hidden);
	void setFavorite(const QString &key, int index, bool on);
	void setMemo(const QString &key, const QString &memo);
	// Several plugins at once (multi-selection).
	void setHidden(const QStringList &keys, bool hidden);
	void setFavorite(const QStringList &keys, int index, bool on);

	// Name the user gave favorite `index`, empty if none.
	QString favoriteName(int index) const;
	void setFavoriteName(int index, const QString &name);
	// Favorites start as a single slot; the user adds more (up to kFavorites) and removes the last one.
	int favoriteCount() const { return favCount; }
	void addFavorite();
	void removeLastFavorite();

	// User screenshot if there is one, else the snapshot shipped in a VST3 bundle, else empty.
	QString thumbnailPath(const vstex::PluginDescription &d) const;
	bool hasUserThumbnail(const vstex::PluginDescription &d) const;
	// Full-size screenshot behind the thumbnail if one was captured, else the thumbnail itself.
	QString fullImagePath(const vstex::PluginDescription &d) const;
	void setThumbnail(const vstex::PluginDescription &d, const QImage &image);

	// Plugins picked recently, newest first.
	QStringList recent() const { return recentKeys; }
	void addRecent(const QString &key);
	void clearRecent();

	// Screenshot of one filter instance (its own settings), separate from the plugin thumbnail.
	QString filterShotPath(const QString &filterUuid, bool full) const;
	void setFilterShot(const QString &filterUuid, const QImage &image);

	// Where thumbnails, screenshots and filter history live. The user picks it on first run;
	// until then (and by default) it is the module config folder.
	QString dataDir() const;
	bool dataDirChosen() const { return ui.contains("dataDir"); }
	// Moves the existing files over. False (and nothing changed) if they could not be moved.
	bool setDataDir(const QString &dir);

	// Folder holding all thumbnails and screenshots.
	QString screenshotDir() const;
	void deleteAllScreenshots();

	// Small UI preferences (column order, toggles), saved with the rest.
	QJsonValue uiValue(const QString &key) const { return ui.value(key); }
	void setUiValue(const QString &key, const QJsonValue &value);

signals:
	void changed();
	void uiChanged(const QString &key);

private:
	PluginMeta();
	void save() const;
	static void saveShot(const QImage &image, const QString &smallPath, const QString &fullPath);
	QString userThumbnailPath(const QString &key, const char *suffix = "") const;

	QHash<QString, Entry> entries;
	QStringList favoriteNames;
	int favCount = 1;
	QJsonObject ui;
	QStringList recentKeys;
};

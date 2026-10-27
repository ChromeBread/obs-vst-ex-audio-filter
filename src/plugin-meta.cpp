#include "plugin-meta.h"

#include <obs-module.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

// Thumbnails are kept small on purpose ("low resolution" previews, fast to load).
constexpr int kThumbMaxWidth = 480;

QString configPath(const QString &name)
{
	char *p = obs_module_config_path(name.toUtf8().constData());
	QString path = QString::fromUtf8(p);
	bfree(p);
	return path;
}

// Moves a folder, falling back to copy + delete across drives.
bool moveTree(const QString &from, const QString &to)
{
	if (!QDir(from).exists())
		return true;
	QDir().mkpath(QFileInfo(to).path());
	if (QDir().rename(from, to))
		return true;
	QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString file = it.next();
		QString target = to + file.mid(from.size());
		QDir().mkpath(QFileInfo(target).path());
		QFile::remove(target);
		if (!QFile::copy(file, target))
			return false; // originals stay in place
	}
	QDir(from).removeRecursively();
	return true;
}

} // namespace

PluginMeta &PluginMeta::instance()
{
	static PluginMeta meta;
	return meta;
}

// Format + uid + name: stays the same when a plugin is reinstalled to another folder.
QString PluginMeta::keyOf(const vstex::PluginDescription &d)
{
	return QString("%1|%2|%3").arg(vstex::formatName(d.format), QString::fromStdString(d.uid),
				       QString::fromStdString(d.name));
}

PluginMeta::PluginMeta()
{
	QFile f(configPath("plugin-meta.json"));
	if (!f.open(QIODevice::ReadOnly))
		return;
	QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
	for (const auto &v : root["favoriteNames"].toArray())
		favoriteNames << v.toString();
	favCount = root["favoriteCount"].toInt(1);
	ui = root["ui"].toObject();
	for (const auto &v : root["recent"].toArray())
		recentKeys << v.toString();
	QJsonObject plugins = root["plugins"].toObject();
	for (auto it = plugins.begin(); it != plugins.end(); ++it) {
		QJsonObject o = it.value().toObject();
		quint8 favs = (quint8)o["favorites"].toInt();
		entries.insert(it.key(), {o["hidden"].toBool(), favs, o["memo"].toString()});
		for (int i = favCount; i < kFavorites; i++) // never hide a favorite that is in use
			if (favs & (1u << i))
				favCount = i + 1;
	}
	favCount = qBound(1, favCount, (int)kFavorites);
}

void PluginMeta::save() const
{
	QJsonObject plugins;
	for (auto it = entries.begin(); it != entries.end(); ++it) {
		const Entry &e = it.value();
		if (!e.hidden && !e.favorites && e.memo.isEmpty())
			continue;
		plugins.insert(it.key(), QJsonObject{{"hidden", e.hidden}, {"favorites", e.favorites}, {"memo", e.memo}});
	}
	QJsonObject root{{"version", 1},
			 {"favoriteNames", QJsonArray::fromStringList(favoriteNames)},
			 {"favoriteCount", favCount},
			 {"ui", ui},
			 {"recent", QJsonArray::fromStringList(recentKeys)},
			 {"plugins", plugins}};

	QString path = configPath("plugin-meta.json");
	QDir().mkpath(QFileInfo(path).path());
	QSaveFile f(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(root).toJson());
		f.commit();
	}
}

void PluginMeta::setHidden(const QString &key, bool hidden)
{
	entries[key].hidden = hidden;
	save();
	emit changed();
}

void PluginMeta::setFavorite(const QString &key, int index, bool on)
{
	quint8 bit = quint8(1u << index);
	Entry &e = entries[key];
	e.favorites = on ? (e.favorites | bit) : (e.favorites & ~bit);
	save();
	emit changed();
}

void PluginMeta::setHidden(const QStringList &keys, bool hidden)
{
	for (const QString &key : keys)
		entries[key].hidden = hidden;
	save();
	emit changed();
}

void PluginMeta::setFavorite(const QStringList &keys, int index, bool on)
{
	quint8 bit = quint8(1u << index);
	for (const QString &key : keys) {
		Entry &e = entries[key];
		e.favorites = on ? (e.favorites | bit) : (e.favorites & ~bit);
	}
	save();
	emit changed();
}

void PluginMeta::setMemo(const QString &key, const QString &memo)
{
	entries[key].memo = memo;
	save();
	emit changed();
}

QString PluginMeta::favoriteName(int index) const
{
	return favoriteNames.value(index);
}

void PluginMeta::setFavoriteName(int index, const QString &name)
{
	while (favoriteNames.size() < kFavorites)
		favoriteNames << QString();
	favoriteNames[index] = name;
	save();
	emit changed();
}

void PluginMeta::addFavorite()
{
	if (favCount >= kFavorites)
		return;
	favCount++;
	save();
	emit changed();
}

void PluginMeta::removeLastFavorite()
{
	if (favCount <= 1)
		return;
	favCount--;
	quint8 bit = quint8(1u << favCount);
	for (Entry &e : entries)
		e.favorites &= ~bit;
	if (favCount < favoriteNames.size())
		favoriteNames[favCount].clear();
	save();
	emit changed();
}

void PluginMeta::setUiValue(const QString &key, const QJsonValue &value)
{
	ui.insert(key, value);
	save();
	emit uiChanged(key);
}

void PluginMeta::addRecent(const QString &key)
{
	recentKeys.removeAll(key);
	recentKeys.prepend(key);
	while (recentKeys.size() > kMaxRecent)
		recentKeys.removeLast();
	save();
	emit changed();
}

void PluginMeta::clearRecent()
{
	recentKeys.clear();
	save();
	emit changed();
}

QString PluginMeta::dataDir() const
{
	QString dir = ui.value("dataDir").toString();
	return dir.isEmpty() ? QDir::cleanPath(configPath("")) : dir;
}

bool PluginMeta::setDataDir(const QString &dir)
{
	QString from = dataDir(), to = QDir::cleanPath(dir);
	if (QDir(from) != QDir(to))
		for (const char *sub : {"thumbnails", "history", "presets"})
			if (!moveTree(from + "/" + sub, to + "/" + sub))
				return false;
	setUiValue("dataDir", to);
	emit changed();
	return true;
}

QString PluginMeta::screenshotDir() const
{
	return dataDir() + "/thumbnails";
}

void PluginMeta::deleteAllScreenshots()
{
	QDir(screenshotDir()).removeRecursively();
	emit changed();
}

QString PluginMeta::filterShotPath(const QString &filterUuid, bool full) const
{
	return screenshotDir() + QString("/filter-%1%2.png").arg(filterUuid, full ? "-full" : "");
}

QString PluginMeta::userThumbnailPath(const QString &key, const char *suffix) const
{
	QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
	return screenshotDir() + QString("/%1%2.png").arg(QString::fromLatin1(hash), QString::fromLatin1(suffix));
}

QString PluginMeta::thumbnailPath(const vstex::PluginDescription &d) const
{
	QString user = userThumbnailPath(keyOf(d));
	if (QFileInfo::exists(user))
		return user;
	QString bundled = QString::fromStdString(d.snapshot);
	return !bundled.isEmpty() && QFileInfo::exists(bundled) ? bundled : QString();
}

bool PluginMeta::hasUserThumbnail(const vstex::PluginDescription &d) const
{
	return QFileInfo::exists(userThumbnailPath(keyOf(d)));
}

QString PluginMeta::fullImagePath(const vstex::PluginDescription &d) const
{
	QString full = userThumbnailPath(keyOf(d), "-full");
	return QFileInfo::exists(full) ? full : thumbnailPath(d);
}

void PluginMeta::setThumbnail(const vstex::PluginDescription &d, const QImage &image)
{
	saveShot(image, userThumbnailPath(keyOf(d)), userThumbnailPath(keyOf(d), "-full"));
	emit changed();
}

void PluginMeta::setFilterShot(const QString &filterUuid, const QImage &image)
{
	saveShot(image, filterShotPath(filterUuid, false), filterShotPath(filterUuid, true));
	emit changed();
}

// Small copy for lists; the full-size one only when it differs.
void PluginMeta::saveShot(const QImage &image, const QString &smallPath, const QString &fullPath)
{
	QDir().mkpath(QFileInfo(smallPath).path());
	QImage small = image.width() > kThumbMaxWidth ? image.scaledToWidth(kThumbMaxWidth, Qt::SmoothTransformation)
						      : image;
	small.save(smallPath, "PNG");
	if (small.size() != image.size())
		image.save(fullPath, "PNG");
	else
		QFile::remove(fullPath);
}

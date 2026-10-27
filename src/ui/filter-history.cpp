#include "ui/filter-history.h"

#include "plugin-meta.h"

#include "ui/ui-text.h"

#include <obs-module.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

constexpr int kMaxSnapshots = 100;

QString historyFile(obs_source_t *source)
{
	return PluginMeta::instance().dataDir() +
	       QString("/history/%1.json").arg(QString::fromUtf8(obs_source_get_uuid(source)));
}

QByteArray captureChain(obs_source_t *source)
{
	OBSDataArrayAutoRelease filters = obs_source_backup_filters(source);
	OBSDataAutoRelease wrapper = obs_data_create();
	obs_data_set_array(wrapper, "filters", filters);
	return QByteArray(obs_data_get_json(wrapper));
}

} // namespace

FilterHistory &FilterHistory::instance()
{
	static FilterHistory history;
	return history;
}

FilterHistory::Track &FilterHistory::track(obs_source_t *source)
{
	Track &t = tracks[QString::fromUtf8(obs_source_get_uuid(source))];
	if (t.loaded)
		return t;
	t.loaded = true;

	QFile f(historyFile(source));
	if (f.open(QIODevice::ReadOnly)) {
		QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
		for (const auto &v : root["snapshots"].toArray()) {
			QJsonObject o = v.toObject();
			t.items.push_back({QDateTime::fromString(o["time"].toString(), Qt::ISODate), o["label"].toString(),
					   o["chain"].toString().toUtf8()});
		}
		t.pos = qBound(-1, root["position"].toInt(t.items.size() - 1), (int)t.items.size() - 1);
	}
	return t;
}

void FilterHistory::record(obs_source_t *source, const QString &label)
{
	if (!source)
		return;
	Track &t = track(source);
	QByteArray json = captureChain(source);
	if (t.pos >= 0 && t.items[t.pos].json == json)
		return;

	t.items.resize(t.pos + 1); // a new change discards the redo branch
	t.items.push_back({QDateTime::currentDateTime(), label, json});
	if (t.items.size() > kMaxSnapshots)
		t.items.remove(0, t.items.size() - kMaxSnapshots);
	t.pos = (int)t.items.size() - 1;
	persist(source);
	emit changed(source);
}

bool FilterHistory::canUndo(obs_source_t *source)
{
	return track(source).pos > 0;
}

bool FilterHistory::canRedo(obs_source_t *source)
{
	Track &t = track(source);
	return t.pos >= 0 && t.pos < t.items.size() - 1;
}

void FilterHistory::undo(obs_source_t *source)
{
	// Unrecorded edits (e.g. made in OBS's own filter window) become a state first, so undo can come back to them.
	record(source, T("History.Unsaved"));
	Track &t = track(source);
	if (t.pos <= 0)
		return;
	t.pos--;
	apply(source, t.items[t.pos].json);
	// Re-capture: plugins may serialize the same state with different bytes, which would look like a new edit.
	t.items[t.pos].json = captureChain(source);
	persist(source);
	emit changed(source);
}

void FilterHistory::redo(obs_source_t *source)
{
	if (!canRedo(source))
		return;
	Track &t = track(source);
	t.pos++;
	apply(source, t.items[t.pos].json);
	// Re-capture: plugins may serialize the same state with different bytes, which would look like a new edit.
	t.items[t.pos].json = captureChain(source);
	persist(source);
	emit changed(source);
}

QVector<FilterHistory::Snapshot> FilterHistory::snapshots(obs_source_t *source)
{
	return track(source).items;
}

int FilterHistory::position(obs_source_t *source)
{
	return track(source).pos;
}

void FilterHistory::restore(obs_source_t *source, int index)
{
	record(source, T("History.Unsaved"));
	Track &t = track(source);
	if (index < 0 || index >= t.items.size())
		return;
	Snapshot snap = t.items[index];
	apply(source, snap.json);
	record(source, T("History.Restored").arg(snap.label));
}

QByteArray FilterHistory::capture(obs_source_t *source)
{
	return captureChain(source);
}

void FilterHistory::applyChain(obs_source_t *source, const QByteArray &json, const QString &label)
{
	record(source, T("History.Unsaved"));
	apply(source, json);
	record(source, label);
}

// obs_source_restore_filters() keeps existing filters untouched, so settings and on/off would not roll back.
// This reconciles the chain one filter at a time instead.
void FilterHistory::apply(obs_source_t *source, const QByteArray &json)
{
	OBSDataAutoRelease wrapper = obs_data_create_from_json(json.constData());
	OBSDataArrayAutoRelease wanted = obs_data_get_array(wrapper, "filters");
	size_t count = obs_data_array_count(wanted);

	std::vector<OBSSource> current;
	obs_source_enum_filters(
		source, [](obs_source_t *, obs_source_t *filter, void *param) {
			static_cast<std::vector<OBSSource> *>(param)->emplace_back(filter);
		},
		&current);

	auto findFilter = [&](obs_data_t *item) -> obs_source_t * {
		const char *uuid = obs_data_get_string(item, "uuid");
		const char *name = obs_data_get_string(item, "name");
		const char *id = obs_data_get_string(item, "id");
		for (auto &f : current)
			if (*uuid && strcmp(obs_source_get_uuid(f), uuid) == 0)
				return f;
		for (auto &f : current)
			if (strcmp(obs_source_get_name(f), name) == 0 && strcmp(obs_source_get_id(f), id) == 0)
				return f;
		return nullptr;
	};

	// Remove filters that are not part of the snapshot.
	std::vector<obs_source_t *> keep;
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(wanted, i);
		if (obs_source_t *f = findFilter(item))
			keep.push_back(f);
	}
	for (auto &f : current)
		if (std::find(keep.begin(), keep.end(), f.Get()) == keep.end())
			obs_source_filter_remove(source, f);

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(wanted, i);
		OBSSource filter = findFilter(item);
		if (filter) {
			OBSDataAutoRelease settings = obs_data_get_obj(item, "settings");
			obs_source_reset_settings(filter, settings);
			const char *name = obs_data_get_string(item, "name");
			if (strcmp(obs_source_get_name(filter), name) != 0)
				obs_source_set_name(filter, name);
		} else {
			// The old filter object is gone; recreate it without its old uuid to avoid a clash.
			OBSDataAutoRelease copy = obs_data_create();
			obs_data_apply(copy, item);
			obs_data_erase(copy, "uuid");
			OBSSourceAutoRelease created = obs_load_source(copy);
			if (!created)
				continue;
			obs_source_filter_add(source, created);
			filter = created.Get();
		}
		obs_source_set_enabled(filter, obs_data_get_bool(item, "enabled"));
		obs_source_filter_set_index(source, filter, i);
	}
}

void FilterHistory::persist(obs_source_t *source)
{
	Track &t = track(source);
	QJsonArray arr;
	for (const auto &s : t.items)
		arr << QJsonObject{{"time", s.time.toString(Qt::ISODate)},
				   {"label", s.label},
				   {"chain", QString::fromUtf8(s.json)}};
	QJsonObject root{{"source", QString::fromUtf8(obs_source_get_name(source))}, {"position", t.pos}, {"snapshots", arr}};

	QString path = historyFile(source);
	QDir().mkpath(QFileInfo(path).path());
	QSaveFile f(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
		f.commit();
	}
}

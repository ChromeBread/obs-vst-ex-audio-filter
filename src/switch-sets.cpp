#include "switch-sets.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <map>
#include <memory>
#include <vector>

namespace switchsets {
namespace {

constexpr const char *kKey = "vstex_switch_sets";

struct Hotkey {
	obs_hotkey_id id;
	OBSWeakSource source;
	QString setId;
};
// Registered hotkeys by source uuid. Owned here; the callback gets a Hotkey* as its data.
std::map<QString, std::vector<std::unique_ptr<Hotkey>>> hotkeys;

void onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *hk = static_cast<Hotkey *>(data);
	OBSSourceAutoRelease source = obs_weak_source_get_source(hk->source);
	if (!source)
		return;
	for (const Set &set : load(source))
		if (set.id == hk->setId)
			apply(source, set);
}

void unregisterSource(const QString &uuid)
{
	auto it = hotkeys.find(uuid);
	if (it == hotkeys.end())
		return;
	for (auto &hk : it->second)
		obs_hotkey_unregister(hk->id); // no-op if OBS already dropped it with the source
	hotkeys.erase(it);
}

// Re-registering under the same name picks the saved key bindings up again.
void registerSource(obs_source_t *source)
{
	QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	unregisterSource(uuid);
	auto &list = hotkeys[uuid];
	for (const Set &set : load(source)) {
		auto hk = std::make_unique<Hotkey>();
		hk->source = OBSGetWeakRef(source);
		hk->setId = set.id;
		QByteArray name = ("vstex_switch_" + set.id).toUtf8();
		QByteArray desc = QString("VST Ex: %1").arg(set.name).toUtf8();
		hk->id = obs_hotkey_register_source(source, name.constData(), desc.constData(), onHotkey, hk.get());
		list.push_back(std::move(hk));
	}
	if (list.empty())
		hotkeys.erase(uuid);
}

void registerAll()
{
	obs_enum_sources(
		[](void *, obs_source_t *source) {
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			if (*obs_data_get_string(priv, kKey))
				registerSource(source);
			return true;
		},
		nullptr);
}

void unregisterAll()
{
	while (!hotkeys.empty())
		unregisterSource(hotkeys.begin()->first);
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		registerAll();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
	case OBS_FRONTEND_EVENT_EXIT:
		unregisterAll();
		break;
	default:
		break;
	}
}

} // namespace

QVector<Set> load(obs_source_t *source)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	QJsonArray arr = QJsonDocument::fromJson(QByteArray(obs_data_get_string(priv, kKey))).array();
	QVector<Set> sets;
	for (const auto &v : arr) {
		QJsonObject o = v.toObject();
		Set set;
		set.id = o["id"].toString();
		set.name = o["name"].toString();
		set.hasAudio = o.contains("volume");
		set.syncOffsetNs = (int64_t)o["syncOffsetNs"].toDouble(); // ns fit a double exactly
		set.volume = (float)o["volume"].toDouble(1.0);
		QJsonObject on = o["enabled"].toObject();
		for (auto it = on.begin(); it != on.end(); ++it)
			set.enabled.insert(it.key(), it.value().toBool());
		if (!set.id.isEmpty())
			sets << set;
	}
	return sets;
}

void save(obs_source_t *source, const QVector<Set> &sets)
{
	QJsonArray arr;
	for (const Set &set : sets) {
		QJsonObject on;
		for (auto it = set.enabled.begin(); it != set.enabled.end(); ++it)
			on.insert(it.key(), it.value());
		QJsonObject o{{"id", set.id}, {"name", set.name}, {"enabled", on}};
		if (set.hasAudio) {
			o.insert("syncOffsetNs", (double)set.syncOffsetNs);
			o.insert("volume", set.volume);
		}
		arr << o;
	}
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	obs_data_set_string(priv, kKey, QJsonDocument(arr).toJson(QJsonDocument::Compact).constData());
	registerSource(source);
}

Set capture(obs_source_t *source, const QString &name)
{
	Set set;
	set.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	set.name = name;
	set.hasAudio = true;
	set.syncOffsetNs = obs_source_get_sync_offset(source);
	set.volume = obs_source_get_volume(source);
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			static_cast<Set *>(param)->enabled.insert(QString::fromUtf8(obs_source_get_uuid(filter)),
								  obs_source_enabled(filter));
		},
		&set);
	return set;
}

void apply(obs_source_t *source, const Set &set)
{
	if (set.hasAudio) {
		if (obs_source_get_sync_offset(source) != set.syncOffsetNs)
			obs_source_set_sync_offset(source, set.syncOffsetNs);
		obs_source_set_volume(source, set.volume);
	}
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			const Set &s = *static_cast<const Set *>(param);
			auto it = s.enabled.find(QString::fromUtf8(obs_source_get_uuid(filter)));
			if (it != s.enabled.end() && obs_source_enabled(filter) != it.value())
				obs_source_set_enabled(filter, it.value());
		},
		const_cast<Set *>(&set));
}

void install()
{
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
}

void uninstall()
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	unregisterAll();
}

} // namespace switchsets

#include "loader/handover.h"

#include "host/plugin-host.h"
#include "loader/juce-format.h"
#include "plugin-scanner.h"
#include "ui/ui-text.h"
#include "vst-filter.h"

#include "pluginterfaces/base/funknown.h"
#include "public.sdk/source/vst/utility/uid.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cstring>

namespace handover {

namespace {

const char *kObsVstId = "vst_filter";
const char *kAtkId = "atkaudio_plugin_host";

bool typeInstalled(const char *id)
{
	return obs_source_get_display_name(id) != nullptr;
}

std::vector<uint8_t> fromBase64(const char *text)
{
	QByteArray raw = QByteArray::fromBase64(QByteArray(text));
	return std::vector<uint8_t>(raw.begin(), raw.end());
}

std::string toBase64(const std::vector<uint8_t> &data)
{
	return QByteArray((const char *)data.data(), (qsizetype)data.size()).toBase64().toStdString();
}

// Compares plugin paths the way Windows does; a path inside a .vst3 bundle counts as the bundle.
QString pathKey(const QString &path)
{
	QString p = QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
	int bundle = p.indexOf(".vst3/");
	return bundle >= 0 ? p.left(bundle + 5) : p;
}

// JUCE's PluginDescription ids for a VST3 class id (see juce_VST3PluginFormatImpl.h).
bool juceIds(const std::string &uid, uint32_t &uniqueId, uint32_t &deprecatedUid)
{
	auto parsed = VST3::UID::fromString(uid);
	if (!parsed)
		return false;
	const Steinberg::TUID &tuid = parsed->data();
	Steinberg::FUID fuid(tuid);
	const uint32_t longs[4] = {fuid.getLong1(), fuid.getLong2(), fuid.getLong3(), fuid.getLong4()};
	uniqueId = (uint32_t)jucefmt::hashRange(longs);
	deprecatedUid = (uint32_t)jucefmt::hashRange(tuid);
	return true;
}

const vstex::PluginDescription *findInCatalog(const QVector<vstex::PluginDescription> &all, vstex::PluginFormat format,
					      const QString &path, const std::string &uid = {})
{
	for (const auto &d : all)
		if (d.format == format && pathKey(QString::fromStdString(d.path)) == pathKey(path) &&
		    (uid.empty() || d.uid == uid))
			return &d;
	return nullptr;
}

// atkAudio's settings: optional "KEY:value" header lines, then <state><PLUGIN .../><inner_state>..</inner_state></state>.
struct AtkState {
	QString format, file, name;
	uint32_t uniqueId = 0, deprecatedUid = 0;
	std::vector<uint8_t> component, controller;
};

bool parseAtk(const char *settingsText, AtkState &out)
{
	QByteArray text(settingsText);
	int start = text.indexOf('<');
	if (start < 0)
		return false;
	QXmlStreamReader xml(text.mid(start));
	QString inner;
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement() && xml.name() == QLatin1String("PLUGIN")) {
			auto a = xml.attributes();
			out.format = a.value("format").toString();
			out.file = a.value("file").toString();
			out.name = a.value("name").toString();
			out.uniqueId = a.value("uniqueId").toString().toUInt(nullptr, 16);
			out.deprecatedUid = a.value("uid").toString().toUInt(nullptr, 16);
		} else if (xml.isStartElement() && xml.name() == QLatin1String("inner_state")) {
			inner = xml.readElementText();
		}
	}
	if (xml.hasError())
		return false;
	std::vector<uint8_t> innerBinary;
	if (out.format != "VST3" || !jucefmt::base64Decode(inner.toStdString(), innerBinary))
		return false;
	QXmlStreamReader vst3(QByteArray::fromStdString(jucefmt::binaryToXml(innerBinary)));
	while (!vst3.atEnd()) {
		vst3.readNext();
		if (!vst3.isStartElement())
			continue;
		bool component = vst3.name() == QLatin1String("IComponent");
		if (component || vst3.name() == QLatin1String("IEditController"))
			jucefmt::base64Decode(vst3.readElementText().toStdString(), component ? out.component : out.controller);
	}
	return !out.component.empty();
}

std::string makeAtk(const vstex::PluginDescription &d, const std::vector<uint8_t> &component,
		    const std::vector<uint8_t> &controller)
{
	std::string vst3 = "<VST3PluginState><IComponent>" + jucefmt::base64Encode(component) + "</IComponent>";
	if (!controller.empty())
		vst3 += "<IEditController>" + jucefmt::base64Encode(controller) + "</IEditController>";
	vst3 += "</VST3PluginState>";

	uint32_t uniqueId = 0, deprecatedUid = 0;
	juceIds(d.uid, uniqueId, deprecatedUid);
	QByteArray out;
	QXmlStreamWriter xml(&out);
	xml.writeStartElement("state");
	xml.writeAttribute("editor_style", "0");
	xml.writeStartElement("PLUGIN");
	xml.writeAttribute("name", QString::fromStdString(d.name));
	xml.writeAttribute("format", "VST3");
	xml.writeAttribute("category", QString::fromStdString(d.category));
	xml.writeAttribute("manufacturer", QString::fromStdString(d.vendor));
	xml.writeAttribute("version", QString::fromStdString(d.version));
	xml.writeAttribute("file", QDir::toNativeSeparators(QString::fromStdString(d.path)));
	xml.writeAttribute("uniqueId", QString::number(uniqueId, 16));
	xml.writeAttribute("isInstrument", "0");
	xml.writeAttribute("fileTime", "0");
	xml.writeAttribute("infoUpdateTime", "0");
	xml.writeAttribute("numInputs", QString::number(d.numInputs));
	xml.writeAttribute("numOutputs", QString::number(d.numOutputs));
	xml.writeAttribute("isShell", "0");
	xml.writeAttribute("hasARAExtension", "0");
	xml.writeAttribute("uid", QString::number(deprecatedUid, 16));
	xml.writeEndElement();
	xml.writeTextElement("inner_state", QString::fromStdString(jucefmt::base64Encode(jucefmt::xmlToBinary(vst3))));
	xml.writeEndElement();
	return out.toStdString();
}

// Our VST3 state: [uint32 component size][component][controller] (see vst3-instance.cpp).
bool splitVst3State(const std::vector<uint8_t> &state, std::vector<uint8_t> &component, std::vector<uint8_t> &controller)
{
	uint32_t size = 0;
	if (state.size() < sizeof(size))
		return false;
	std::memcpy(&size, state.data(), sizeof(size));
	if (size > state.size() - sizeof(size))
		return false;
	component.assign(state.begin() + sizeof(size), state.begin() + sizeof(size) + size);
	controller.assign(state.begin() + sizeof(size) + size, state.end());
	return true;
}

std::vector<uint8_t> joinVst3State(const std::vector<uint8_t> &component, const std::vector<uint8_t> &controller)
{
	uint32_t size = (uint32_t)component.size();
	std::vector<uint8_t> state(sizeof(size));
	std::memcpy(state.data(), &size, sizeof(size));
	state.insert(state.end(), component.begin(), component.end());
	state.insert(state.end(), controller.begin(), controller.end());
	return state;
}

std::unique_ptr<vstex::PluginInstance> loadPrepared(const std::string &path, const std::string &uid, QString &error)
{
	std::string err;
	auto plugin = vstex::loadPlugin(path, uid, err);
	if (!plugin) {
		error = QString::fromStdString(err);
		return nullptr;
	}
	audio_t *audio = obs_get_audio();
	plugin->prepare((double)audio_output_get_sample_rate(audio), AUDIO_OUTPUT_FRAMES, (int)audio_output_get_channels(audio));
	return plugin;
}

// Swaps `old` for a new filter of type `id`, keeping its place, name, ON/OFF and memo.
bool replace(obs_source_t *parent, obs_source_t *old, const char *id, obs_data_t *settings, QString &error)
{
	OBSDataAutoRelease oldSettings = obs_source_get_settings(old);
	if (obs_data_has_user_value(oldSettings, S_VSTEX_MEMO))
		obs_data_set_string(settings, S_VSTEX_MEMO, obs_data_get_string(oldSettings, S_VSTEX_MEMO));
	OBSSourceAutoRelease created = obs_source_create(id, obs_source_get_name(old), settings, nullptr);
	if (!created) {
		error = T("Handover.CreateFailed");
		return false;
	}
	int index = obs_source_filter_get_index(parent, old);
	bool enabled = obs_source_enabled(old);
	obs_source_filter_remove(parent, old);
	obs_source_filter_add(parent, created);
	if (index >= 0)
		obs_source_filter_set_index(parent, created, (size_t)index);
	obs_source_set_enabled(created, enabled);
	return true;
}

} // namespace

QString targetName(Kind kind)
{
	switch (kind) {
	case Kind::LoaderVst2:
		return QStringLiteral("OBS VST 2.x");
	case Kind::LoaderVst3:
		return QStringLiteral("atkAudio PluginHost");
	default:
		return QStringLiteral("VST Enigmax Loader");
	}
}

QVector<Item> collect()
{
	QVector<Item> items;
	QVector<vstex::PluginDescription> catalog = PluginCatalog::instance().plugins();
	auto visitSource = [&](obs_source_t *parent) {
		struct Ctx {
			obs_source_t *parent;
			QVector<Item> *items;
			const QVector<vstex::PluginDescription> *catalog;
		} ctx{parent, &items, &catalog};
		obs_source_enum_filters(
			parent,
			[](obs_source_t *, obs_source_t *filter, void *param) {
				auto *c = static_cast<Ctx *>(param);
				const char *id = obs_source_get_unversioned_id(filter);
				OBSDataAutoRelease s = obs_source_get_settings(filter);
				Item item;
				if (strcmp(id, VST_EX_FILTER_ID) == 0) {
					bool vst2 = strcmp(obs_data_get_string(s, S_PLUGIN_FORMAT), "VST2") == 0;
					item.kind = vst2 ? Kind::LoaderVst2 : Kind::LoaderVst3;
					item.plugin = QString::fromUtf8(obs_data_get_string(s, S_PLUGIN_NAME));
					if (!typeInstalled(vst2 ? kObsVstId : kAtkId))
						item.problem = T("Handover.NotInstalled").arg(targetName(item.kind));
					else if (!*obs_data_get_string(s, S_PLUGIN_PATH))
						item.problem = T("Handover.NoPlugin");
				} else if (strcmp(id, kObsVstId) == 0) {
					item.kind = Kind::ObsVst2;
					item.plugin = QFileInfo(QString::fromUtf8(obs_data_get_string(s, "plugin_path"))).completeBaseName();
					if (!*obs_data_get_string(s, "plugin_path"))
						item.problem = T("Handover.NoPlugin");
				} else if (strcmp(id, kAtkId) == 0) {
					item.kind = Kind::Atk;
					AtkState atk;
					if (!parseAtk(obs_data_get_string(s, kAtkId), atk))
						item.problem = T("Handover.AtkUnreadable");
					item.plugin = atk.name;
					if (item.problem.isEmpty() && !findInCatalog(*c->catalog, vstex::PluginFormat::VST3, atk.file))
						item.problem = T("Handover.NotScanned");
				} else {
					return;
				}
				item.parent = OBSGetWeakRef(c->parent);
				item.filter = OBSGetWeakRef(filter);
				item.sourceName = QString::fromUtf8(obs_source_get_name(c->parent));
				item.filterName = QString::fromUtf8(obs_source_get_name(filter));
				c->items->push_back(item);
			},
			&ctx);
	};
	using Visit = decltype(visitSource);
	auto enumCb = [](void *param, obs_source_t *source) {
		(*static_cast<Visit *>(param))(source);
		return true;
	};
	obs_enum_sources(enumCb, &visitSource);
	obs_enum_scenes(enumCb, &visitSource);
	return items;
}

bool convert(const Item &item, QString &error)
{
	OBSSourceAutoRelease parent = obs_weak_source_get_source(item.parent);
	OBSSourceAutoRelease filter = obs_weak_source_get_source(item.filter);
	if (!parent || !filter) {
		error = T("Handover.Gone");
		return false;
	}
	OBSDataAutoRelease s = obs_source_get_settings(filter);
	OBSDataAutoRelease out = obs_data_create();
	std::vector<uint8_t> state = fromBase64(obs_data_get_string(s, S_PLUGIN_STATE));
	std::string path = obs_data_get_string(s, S_PLUGIN_PATH), uid = obs_data_get_string(s, S_PLUGIN_UID);

	switch (item.kind) {
	case Kind::LoaderVst2: {
		auto plugin = loadPrepared(path, uid, error);
		if (!plugin)
			return false;
		if (!state.empty())
			plugin->setState(state);
		obs_data_set_string(out, "plugin_path", path.c_str());
		obs_data_set_string(out, "chunk_data", toBase64(plugin->getObsVstData()).c_str());
		obs_data_set_string(out, "chunk_hash", ""); // empty: OBS applies the chunk without checking the dll's MD5
		return replace(parent, filter, kObsVstId, out, error);
	}
	case Kind::LoaderVst3: {
		const QVector<vstex::PluginDescription> catalog = PluginCatalog::instance().plugins();
		const vstex::PluginDescription *found =
			findInCatalog(catalog, vstex::PluginFormat::VST3, QString::fromStdString(path), uid);
		vstex::PluginDescription d = found ? *found : vstex::PluginDescription();
		d.path = path;
		d.uid = uid;
		d.name = obs_data_get_string(s, S_PLUGIN_NAME);
		std::vector<uint8_t> component, controller;
		if (!state.empty() && !splitVst3State(state, component, controller)) {
			error = T("Handover.BadState");
			return false;
		}
		obs_data_set_string(out, kAtkId, makeAtk(d, component, controller).c_str());
		return replace(parent, filter, kAtkId, out, error);
	}
	case Kind::ObsVst2: {
		std::string vstPath = obs_data_get_string(s, "plugin_path");
		auto plugin = loadPrepared(vstPath, "", error);
		if (!plugin)
			return false;
		plugin->setObsVstData(fromBase64(obs_data_get_string(s, "chunk_data")));
		const vstex::PluginDescription &d = plugin->description();
		obs_data_set_string(out, S_PLUGIN_PATH, vstPath.c_str());
		obs_data_set_string(out, S_PLUGIN_UID, d.uid.c_str());
		obs_data_set_string(out, S_PLUGIN_NAME, d.name.c_str());
		obs_data_set_string(out, S_PLUGIN_FORMAT, "VST2");
		obs_data_set_string(out, S_PLUGIN_STATE, toBase64(plugin->getState()).c_str());
		return replace(parent, filter, VST_EX_FILTER_ID, out, error);
	}
	case Kind::Atk: {
		AtkState atk;
		if (!parseAtk(obs_data_get_string(s, kAtkId), atk)) {
			error = T("Handover.AtkUnreadable");
			return false;
		}
		// The class whose JUCE ids match; JUCE accepts either id, so do we.
		const QVector<vstex::PluginDescription> catalog = PluginCatalog::instance().plugins();
		const vstex::PluginDescription *match = nullptr;
		for (const auto &d : catalog) {
			uint32_t uniqueId = 0, deprecatedUid = 0;
			if (d.format == vstex::PluginFormat::VST3 && pathKey(QString::fromStdString(d.path)) == pathKey(atk.file) &&
			    juceIds(d.uid, uniqueId, deprecatedUid) &&
			    (uniqueId == atk.uniqueId || deprecatedUid == atk.deprecatedUid)) {
				match = &d;
				break;
			}
		}
		if (!match) {
			error = T("Handover.NotScanned");
			return false;
		}
		obs_data_set_string(out, S_PLUGIN_PATH, match->path.c_str());
		obs_data_set_string(out, S_PLUGIN_UID, match->uid.c_str());
		obs_data_set_string(out, S_PLUGIN_NAME, match->name.c_str());
		obs_data_set_string(out, S_PLUGIN_FORMAT, "VST3");
		obs_data_set_string(out, S_PLUGIN_STATE, toBase64(joinVst3State(atk.component, atk.controller)).c_str());
		return replace(parent, filter, VST_EX_FILTER_ID, out, error);
	}
	}
	return false;
}

} // namespace handover

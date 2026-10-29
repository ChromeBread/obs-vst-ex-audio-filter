/*
VST Enigmax Loader
Copyright (C) 2026 BlackDream feat. ChromeBread

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

// VST2/VST3 hosting as its own OBS module: the "vst_ex_audio_filter" filter, scanner, picker, editor,
// thumbnails and screenshots. VST Ex (the filters window, docks) calls into it through procs, see vst-filter.h.

#include "loader/loader-dialog.h"
#include "plugin-meta.h"
#include "ui/plugin-picker.h"
#include "vst-filter.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QWidget>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

namespace {

// Before the split everything lived in VST Ex's config folder: take over its plugin data once, and keep
// using the same data folder (which defaulted to that config folder).
void migrateFromVstEx()
{
	char *p = obs_module_config_path("");
	QDir mine(QString::fromUtf8(p));
	bfree(p);
	QDir old(mine.absoluteFilePath("../obs-vst-ex-audio-filter"));
	if (QFileInfo::exists(mine.filePath("plugin-meta.json")) || !QFileInfo::exists(old.filePath("plugin-meta.json")))
		return;
	mine.mkpath(".");
	QFile::copy(old.filePath("plugin-cache.json"), mine.filePath("plugin-cache.json"));
	QFile in(old.filePath("plugin-meta.json"));
	if (!in.open(QIODevice::ReadOnly))
		return;
	QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
	QJsonObject ui = root["ui"].toObject();
	if (ui["dataDir"].toString().isEmpty())
		ui["dataDir"] = QDir::cleanPath(old.absolutePath());
	root["ui"] = ui;
	QSaveFile out(mine.filePath("plugin-meta.json"));
	if (out.open(QIODevice::WriteOnly)) {
		out.write(QJsonDocument(root).toJson());
		out.commit();
	}
	obs_log(LOG_INFO, "took over plugin data from VST Ex");
}

void registerProcs()
{
	proc_handler_t *ph = obs_get_proc_handler();
	proc_handler_add(
		ph, LOADER_PROC_PICK,
		[](void *, calldata_t *cd) {
			QVector<vstex::PluginDescription> chosen;
			if (!PluginPicker::pickMany(static_cast<QWidget *>(calldata_ptr(cd, "parent")), chosen))
				return;
			obs_data_array_t *list = obs_data_array_create();
			for (const auto &d : chosen) {
				OBSDataAutoRelease item = obs_data_create();
				obs_data_set_string(item, S_PLUGIN_PATH, d.path.c_str());
				obs_data_set_string(item, S_PLUGIN_UID, d.uid.c_str());
				obs_data_set_string(item, S_PLUGIN_NAME, d.name.c_str());
				obs_data_set_string(item, S_PLUGIN_FORMAT, vstex::formatName(d.format));
				obs_data_array_push_back(list, item);
			}
			calldata_set_ptr(cd, "list", list);
		},
		nullptr);
	proc_handler_add(
		ph, LOADER_PROC_PROCESS_NS,
		[](void *, calldata_t *cd) { calldata_set_int(cd, "ns", (long long)vst_ex_total_process_ns()); }, nullptr);
	proc_handler_add(
		ph, LOADER_PROC_SET_DATA_DIR,
		[](void *, calldata_t *cd) { PluginMeta::instance().setDataDir(QString::fromUtf8(calldata_string(cd, "dir"))); },
		nullptr);
	proc_handler_add(
		ph, LOADER_PROC_SET_UI_STRING,
		[](void *, calldata_t *cd) {
			QString key = QString::fromUtf8(calldata_string(cd, "key"));
			QString value = QString::fromUtf8(calldata_string(cd, "value"));
			PluginMeta::instance().setUiValue(key, value);
			if (key == "language")
				obs_module_set_locale(value.toUtf8().constData());
		},
		nullptr);
	proc_handler_add(
		ph, LOADER_PROC_SET_UI_BOOL,
		[](void *, calldata_t *cd) {
			PluginMeta::instance().setUiValue(QString::fromUtf8(calldata_string(cd, "key")), calldata_bool(cd, "value"));
		},
		nullptr);

	proc_handler_add(ph, LOADER_PROC_RELOAD, [](void *, calldata_t *) { PluginMeta::instance().reload(); }, nullptr);

	signal_handler_add(obs_get_signal_handler(), LOADER_SIGNAL_EDITED);
	signal_handler_add(obs_get_signal_handler(), LOADER_SIGNAL_PICTURES);
	QObject::connect(&PluginMeta::instance(), &PluginMeta::changed, [] {
		signal_handler_signal(obs_get_signal_handler(), "vst_enigmax_pictures_changed", nullptr);
	});
}

} // namespace

bool obs_module_load(void)
{
	migrateFromVstEx();
	QString lang = PluginMeta::instance().uiValue("language").toString();
	if (!lang.isEmpty())
		obs_module_set_locale(lang.toUtf8().constData());
	register_vst_ex_filter();
	registerProcs();
	obs_frontend_add_tools_menu_item("VST Enigmax Loader", [](void *) { LoaderDialog::open(); }, nullptr);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

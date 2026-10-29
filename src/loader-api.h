#pragma once

// VST Ex side of the calls into VST Enigmax Loader (see vst-filter.h). Each one does nothing when it is not installed.

#include "vst-filter.h"

#include <callback/calldata.h>
#include <callback/proc.h>
#include <callback/signal.h>

#include <QString>

class QWidget;

inline bool loader_installed()
{
	return obs_source_get_display_name(VST_EX_FILTER_ID) != nullptr;
}

// Proc name from its declaration, e.g. "void vst_enigmax_pick(...)" -> "vst_enigmax_pick".
inline QByteArray loader_proc_name(const char *decl)
{
	QByteArray d(decl);
	int start = d.indexOf(' ') + 1;
	return d.mid(start, d.indexOf('(') - start);
}

inline bool loader_call(proc_handler_t *ph, const char *decl, calldata_t *cd)
{
	return ph && proc_handler_call(ph, loader_proc_name(decl).constData(), cd);
}

inline void loader_call_filter(obs_source_t *filter, const char *decl)
{
	calldata_t cd = {};
	loader_call(obs_source_get_proc_handler(filter), decl, &cd);
	calldata_free(&cd);
}

inline int loader_latency_samples(obs_source_t *filter)
{
	calldata_t cd = {};
	long long samples = 0;
	if (loader_call(obs_source_get_proc_handler(filter), LOADER_PROC_LATENCY, &cd))
		samples = calldata_int(&cd, "samples");
	calldata_free(&cd);
	return int(samples);
}

inline uint64_t loader_process_ns()
{
	calldata_t cd = {};
	long long ns = 0;
	if (loader_call(obs_get_proc_handler(), LOADER_PROC_PROCESS_NS, &cd))
		ns = calldata_int(&cd, "ns");
	calldata_free(&cd);
	return uint64_t(ns);
}

// Opens the plugin picker. Returns the chosen plugins as filter settings (caller releases), or null.
inline obs_data_array_t *loader_pick(QWidget *parent)
{
	calldata_t cd = {};
	calldata_set_ptr(&cd, "parent", parent);
	obs_data_array_t *list = nullptr;
	if (loader_call(obs_get_proc_handler(), LOADER_PROC_PICK, &cd))
		list = static_cast<obs_data_array_t *>(calldata_ptr(&cd, "list"));
	calldata_free(&cd);
	return list;
}

inline void loader_pictures(obs_source_t *filter, QString &thumb, QString &full)
{
	calldata_t cd = {};
	if (loader_call(obs_source_get_proc_handler(filter), LOADER_PROC_PICTURES, &cd)) {
		thumb = QString::fromUtf8(calldata_string(&cd, "thumb"));
		full = QString::fromUtf8(calldata_string(&cd, "full"));
	}
	calldata_free(&cd);
}

inline void loader_set_data_dir(const QString &dir)
{
	calldata_t cd = {};
	calldata_set_string(&cd, "dir", dir.toUtf8().constData());
	loader_call(obs_get_proc_handler(), LOADER_PROC_SET_DATA_DIR, &cd);
	calldata_free(&cd);
}

inline void loader_set_ui(const char *key, const QString &value)
{
	calldata_t cd = {};
	calldata_set_string(&cd, "key", key);
	calldata_set_string(&cd, "value", value.toUtf8().constData());
	loader_call(obs_get_proc_handler(), LOADER_PROC_SET_UI_STRING, &cd);
	calldata_free(&cd);
}

inline void loader_reload()
{
	calldata_t cd = {};
	loader_call(obs_get_proc_handler(), LOADER_PROC_RELOAD, &cd);
	calldata_free(&cd);
}

inline void loader_set_ui(const char *key, bool value)
{
	calldata_t cd = {};
	calldata_set_string(&cd, "key", key);
	calldata_set_bool(&cd, "value", value);
	loader_call(obs_get_proc_handler(), LOADER_PROC_SET_UI_BOOL, &cd);
	calldata_free(&cd);
}

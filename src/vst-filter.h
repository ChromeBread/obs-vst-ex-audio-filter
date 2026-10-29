#pragma once

#include <obs.h>

#include <cstdint>

// The filter type stays "vst_ex_audio_filter" so filters saved before VST Enigmax Loader was split out still load.
#define VST_EX_FILTER_ID "vst_ex_audio_filter"

#define S_PLUGIN_PATH "plugin_path"
#define S_PLUGIN_UID "plugin_uid"
#define S_PLUGIN_NAME "plugin_name"
#define S_PLUGIN_FORMAT "plugin_format"
#define S_PLUGIN_STATE "plugin_state"
#define S_PLUGIN_INFO "plugin_info"
#define S_CHOOSE_PLUGIN "choose_plugin"
// Our own note on any filter (not only VST Ex ones); other filter types just keep the extra key.
#define S_VSTEX_MEMO "vstex_memo"

// VST Enigmax Loader and VST Ex are separate OBS modules. They talk only through OBS proc handlers and signals,
// so either one works without the other.
// Global procs (obs_get_proc_handler()):
#define LOADER_PROC_PICK "void vst_enigmax_pick(in ptr parent, out ptr list)" // list: obs_data_array_t*, caller releases
#define LOADER_PROC_PROCESS_NS "void vst_enigmax_process_ns(out int ns)"
#define LOADER_PROC_SET_DATA_DIR "void vst_enigmax_set_data_dir(in string dir)"
#define LOADER_PROC_SET_UI_STRING "void vst_enigmax_set_ui_string(in string key, in string value)"
#define LOADER_PROC_SET_UI_BOOL "void vst_enigmax_set_ui_bool(in string key, in bool value)"
#define LOADER_PROC_RELOAD "void vst_enigmax_reload()" // settings files were replaced (backup import)
// Per filter (obs_source_get_proc_handler(filter)):
#define LOADER_PROC_OPEN_EDITOR "void vst_enigmax_open_editor()"
#define LOADER_PROC_SCREENSHOT "void vst_enigmax_screenshot()"
#define LOADER_PROC_LATENCY "void vst_enigmax_latency(out int samples)"
#define LOADER_PROC_PICTURES "void vst_enigmax_pictures(out string thumb, out string full)"
// Global signals (obs_get_signal_handler()), declared by the loader; connect once OBS has finished loading.
#define LOADER_SIGNAL_EDITED "void vst_enigmax_edited(ptr source)" // the user edited a plugin in its editor
#define LOADER_SIGNAL_PICTURES "void vst_enigmax_pictures_changed()" // a thumbnail or screenshot was saved

#ifdef VST_ENIGMAX_LOADER
void register_vst_ex_filter();
void vst_ex_open_editor(obs_source_t *filter);
// Screenshot of this filter's editor (opened briefly if it is closed), stored for this filter only.
void vst_ex_capture_screenshot(obs_source_t *filter);
int vst_ex_latency_samples(obs_source_t *filter);
// Total time spent in plugin audio processing since load (audio thread only, not plugins' own threads).
uint64_t vst_ex_total_process_ns();
#endif

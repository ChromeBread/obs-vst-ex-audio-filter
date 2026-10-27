#pragma once

#include <obs.h>

#include <cstdint>
#include <functional>

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

void register_vst_ex_filter();

// Called on the UI thread (debounced by the caller) when the user edits a plugin inside its editor.
void vst_ex_set_edited_callback(std::function<void(obs_source_t *filter)> cb);

void vst_ex_open_editor(obs_source_t *filter);
// Screenshot of this filter's editor (opened briefly if it is closed), stored for this filter only.
void vst_ex_capture_screenshot(obs_source_t *filter);
int vst_ex_latency_samples(obs_source_t *filter);
// Total time spent in plugin audio processing since load (audio thread only, not plugins' own threads).
uint64_t vst_ex_total_process_ns();

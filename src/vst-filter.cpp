#include "vst-filter.h"

#include "host/plugin-host.h"
#include "plugin-meta.h"
#include "ui/editor-window.h"
#include "ui/plugin-picker.h"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QApplication>
#include <QByteArray>
#include <QMainWindow>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace {

std::function<void(obs_source_t *)> editedCallback;
std::atomic<uint64_t> processNs{0}; // time spent inside plugins' process(), all filters

struct VstFilter {
	obs_source_t *source = nullptr;

	// Guards `plugin` between the UI thread (load/state) and the audio thread (process).
	std::mutex lock;
	std::shared_ptr<vstex::PluginInstance> plugin;

	std::string path;
	std::string uid;
	std::string appliedState; // base64 state last applied or saved, to detect external changes
	std::string loadError;
	QPointer<EditorWindow> editor;

	double sampleRate = 48000.0;
	int channels = 2;
};

bool onUiThread()
{
	return qApp && QThread::currentThread() == qApp->thread();
}

// Runs fn on the UI thread; plugins expect creation, state and teardown calls there.
template<typename Fn> void runOnUi(Fn fn)
{
	if (onUiThread() || !qApp)
		fn();
	else
		QMetaObject::invokeMethod(qApp, fn, Qt::BlockingQueuedConnection);
}

std::string encodeState(const std::vector<uint8_t> &state)
{
	return QByteArray((const char *)state.data(), (qsizetype)state.size()).toBase64().toStdString();
}

std::vector<uint8_t> decodeState(const std::string &b64)
{
	QByteArray raw = QByteArray::fromBase64(QByteArray::fromStdString(b64));
	return std::vector<uint8_t>(raw.begin(), raw.end());
}

void closeEditor(VstFilter *f)
{
	if (f->editor) {
		f->editor->closeEditor();
		f->editor->close();
	}
}

void loadPlugin(VstFilter *f, const std::string &path, const std::string &uid, const std::string &state)
{
	closeEditor(f);
	std::shared_ptr<vstex::PluginInstance> next;
	f->loadError.clear();
	if (!path.empty()) {
		std::string error;
		next = vstex::loadPlugin(path, uid, error);
		if (next && !next->prepare(f->sampleRate, AUDIO_OUTPUT_FRAMES, f->channels)) {
			error = "plugin rejected the OBS audio format";
			next.reset();
		}
		if (!next) {
			f->loadError = error;
			obs_log(LOG_WARNING, "failed to load '%s': %s", path.c_str(), error.c_str());
		}
	}
	if (next) {
		if (!state.empty())
			next->setState(decodeState(state));
		obs_source_t *source = f->source;
		next->onEdited = [source] {
			// VST2 automation can arrive on the audio thread; hop to the UI thread.
			QMetaObject::invokeMethod(qApp, [source] {
				if (editedCallback)
					editedCallback(source);
			});
		};
	}

	std::shared_ptr<vstex::PluginInstance> old;
	{
		std::lock_guard<std::mutex> guard(f->lock);
		old = std::move(f->plugin);
		f->plugin = std::move(next);
	}
	old.reset(); // destroyed outside the audio lock
	f->path = path;
	f->uid = uid;
	f->appliedState = state;
}

const char *filter_get_name(void *)
{
	return obs_module_text("VstExFilter");
}

void filter_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<VstFilter *>(data);
	std::string path = obs_data_get_string(settings, S_PLUGIN_PATH);
	std::string uid = obs_data_get_string(settings, S_PLUGIN_UID);
	std::string state = obs_data_get_string(settings, S_PLUGIN_STATE);

	runOnUi([&] {
		if (path != f->path || uid != f->uid || !f->plugin) {
			if (path != f->path || uid != f->uid || f->loadError.empty())
				loadPlugin(f, path, uid, state);
		} else if (state != f->appliedState && !state.empty()) {
			// Settings were replaced from outside (undo/restore): push them into the plugin.
			std::lock_guard<std::mutex> guard(f->lock);
			f->plugin->setState(decodeState(state));
			f->appliedState = state;
		}
	});
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new VstFilter();
	f->source = source;
	audio_t *audio = obs_get_audio();
	f->sampleRate = (double)audio_output_get_sample_rate(audio);
	f->channels = (int)audio_output_get_channels(audio);
	filter_update(f, settings);
	return f;
}

void filter_destroy(void *data)
{
	auto *f = static_cast<VstFilter *>(data);
	std::shared_ptr<vstex::PluginInstance> plugin;
	{
		std::lock_guard<std::mutex> guard(f->lock);
		plugin = std::move(f->plugin);
	}
	QPointer<EditorWindow> editor = f->editor;
	delete f;

	// Sources may be destroyed off the UI thread; tear the plugin down there without blocking.
	auto teardown = [plugin, editor]() mutable {
		if (editor) {
			editor->closeEditor();
			editor->close();
		}
		plugin.reset();
	};
	if (onUiThread() || !qApp)
		teardown();
	else
		QMetaObject::invokeMethod(qApp, teardown, Qt::QueuedConnection);
}

void filter_save(void *data, obs_data_t *settings)
{
	auto *f = static_cast<VstFilter *>(data);
	runOnUi([&] {
		if (!f->plugin)
			return;
		f->appliedState = encodeState(f->plugin->getState());
	});
	if (!f->appliedState.empty())
		obs_data_set_string(settings, S_PLUGIN_STATE, f->appliedState.c_str());
}

obs_audio_data *filter_audio(void *data, obs_audio_data *audio)
{
	auto *f = static_cast<VstFilter *>(data);
	std::unique_lock<std::mutex> guard(f->lock, std::try_to_lock);
	if (guard.owns_lock() && f->plugin) {
		uint64_t start = os_gettime_ns();
		f->plugin->process(reinterpret_cast<float *const *>(audio->data), f->channels, (int)audio->frames);
		processNs += os_gettime_ns() - start;
	}
	return audio;
}

bool open_editor_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *f = static_cast<VstFilter *>(data);
	vst_ex_open_editor(f->source);
	return false;
}

bool choose_plugin_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *f = static_cast<VstFilter *>(data);
	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	vstex::PluginDescription chosen;
	if (!PluginPicker::pick(QApplication::activeWindow() ? QApplication::activeWindow() : parent, chosen))
		return false;

	OBSDataAutoRelease settings = obs_source_get_settings(f->source);
	obs_data_set_string(settings, S_PLUGIN_PATH, chosen.path.c_str());
	obs_data_set_string(settings, S_PLUGIN_UID, chosen.uid.c_str());
	obs_data_set_string(settings, S_PLUGIN_NAME, chosen.name.c_str());
	obs_data_set_string(settings, S_PLUGIN_FORMAT, vstex::formatName(chosen.format));
	obs_data_erase(settings, S_PLUGIN_STATE);
	obs_source_update(f->source, settings);
	return true;
}

obs_properties_t *filter_properties(void *data)
{
	auto *f = static_cast<VstFilter *>(data);
	obs_properties_t *props = obs_properties_create();

	OBSDataAutoRelease settings = obs_source_get_settings(f->source);
	std::string name = obs_data_get_string(settings, S_PLUGIN_NAME);
	std::string format = obs_data_get_string(settings, S_PLUGIN_FORMAT);
	std::string info = name.empty() ? obs_module_text("NoPluginSelected") : name + "  [" + format + "]";
	if (!f->loadError.empty())
		info += "\n" + std::string(obs_module_text("LoadFailed")) + ": " + f->loadError;

	obs_property_t *p = obs_properties_add_text(props, S_PLUGIN_INFO, obs_module_text("Plugin"), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(p, true);
	obs_data_set_string(settings, S_PLUGIN_INFO, info.c_str());

	obs_properties_add_button(props, "open_editor", obs_module_text("OpenEditor"), open_editor_clicked);
	obs_properties_add_button(props, S_CHOOSE_PLUGIN, obs_module_text("ChoosePlugin"), choose_plugin_clicked);
	obs_properties_add_text(props, S_PLUGIN_PATH, obs_module_text("PluginPath"), OBS_TEXT_INFO);
	return props;
}

} // namespace

void vst_ex_set_edited_callback(std::function<void(obs_source_t *)> cb)
{
	editedCallback = std::move(cb);
}

void vst_ex_open_editor(obs_source_t *filter)
{
	if (!filter || strcmp(obs_source_get_unversioned_id(filter), VST_EX_FILTER_ID) != 0)
		return;
	auto *f = static_cast<VstFilter *>(obs_obj_get_data(filter));
	if (!f || !f->plugin)
		return;
	if (f->editor) {
		f->editor->raise();
		f->editor->activateWindow();
		return;
	}

	obs_source_t *parent = obs_filter_get_parent(filter);
	QString title = QString::fromUtf8(obs_source_get_name(filter));
	if (parent)
		title += " - " + QString::fromUtf8(obs_source_get_name(parent));

	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *editor = new EditorWindow(f->plugin.get(), title, main);
	OBSWeakSource weak = OBSGetWeakRef(filter);
	editor->importState = [weak](const std::vector<uint8_t> &state) {
		OBSSourceAutoRelease source = obs_weak_source_get_source(weak);
		if (!source)
			return;
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		obs_data_set_string(settings, S_PLUGIN_STATE, encodeState(state).c_str());
		obs_source_update(source, settings); // filter_update pushes the new state into the plugin
		if (editedCallback)
			editedCallback(source);
	};
	if (!editor->openEditor()) {
		delete editor;
		return;
	}
	f->editor = editor;
	editor->show();
}

void vst_ex_capture_screenshot(obs_source_t *filter)
{
	auto *f = static_cast<VstFilter *>(obs_obj_get_data(filter));
	if (!f || !f->plugin)
		return;
	bool wasOpen = f->editor;
	vst_ex_open_editor(filter);
	QPointer<EditorWindow> editor = f->editor;
	if (!editor)
		return;
	QString uuid = QString::fromUtf8(obs_source_get_uuid(filter));
	auto save = [editor, uuid] {
		QImage image = editor->grabEditor();
		if (!image.isNull())
			PluginMeta::instance().setFilterShot(uuid, image);
	};
	if (wasOpen) {
		save();
		return;
	}
	QTimer::singleShot(EditorWindow::kCaptureDelayMs, editor, [editor, save] {
		save();
		editor->close();
	});
}

uint64_t vst_ex_total_process_ns()
{
	return processNs;
}

int vst_ex_latency_samples(obs_source_t *filter)
{
	if (strcmp(obs_source_get_unversioned_id(filter), VST_EX_FILTER_ID) != 0)
		return 0; // another filter type: its data is not a VstFilter
	auto *f = static_cast<VstFilter *>(obs_obj_get_data(filter));
	return f && f->plugin ? f->plugin->latencySamples() : 0;
}

void register_vst_ex_filter()
{
	obs_source_info info = {};
	info.id = VST_EX_FILTER_ID;
	info.type = OBS_SOURCE_TYPE_FILTER;
	// DEPRECATED hides the type from OBS's own "+" filter menu; existing instances keep working there.
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DEPRECATED;
	info.get_name = filter_get_name;
	info.create = filter_create;
	info.destroy = filter_destroy;
	info.update = filter_update;
	info.save = filter_save;
	info.filter_audio = filter_audio;
	info.get_properties = filter_properties;
	obs_register_source(&info);
}

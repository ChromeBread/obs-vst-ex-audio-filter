#include "stereo-filter.h"

#include "dsp/pseudo-stereo.h"

#include <obs-module.h>

#include <atomic>

namespace {

constexpr const char *kDelay = "delay_ms";
constexpr const char *kWidth = "width";      // 0..100 %
constexpr const char *kRight = "delay_right"; // the delayed copy goes right (else left)

struct StereoFilter {
	vstex::PseudoStereo dsp;
	std::atomic<int> delayMs{15}, width{70};
	std::atomic<bool> right{true};
};

const char *get_name(void *)
{
	return obs_module_text("Stereo.Name");
}

void update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<StereoFilter *>(data);
	f->delayMs = (int)obs_data_get_int(settings, kDelay);
	f->width = (int)obs_data_get_int(settings, kWidth);
	f->right = obs_data_get_bool(settings, kRight);
}

void *create(obs_data_t *settings, obs_source_t *)
{
	auto *f = new StereoFilter();
	f->dsp.prepare((double)audio_output_get_sample_rate(obs_get_audio()));
	update(f, settings);
	return f;
}

void destroy(void *data)
{
	delete static_cast<StereoFilter *>(data);
}

obs_audio_data *filter_audio(void *data, obs_audio_data *audio)
{
	auto *f = static_cast<StereoFilter *>(data);
	if (audio_output_get_channels(obs_get_audio()) < 2 || !audio->data[0] || !audio->data[1])
		return audio; // needs a stereo output to place sound left/right
	f->dsp.set(f->delayMs, f->width / 100.0, f->right);
	f->dsp.process(reinterpret_cast<float *>(audio->data[0]), reinterpret_cast<float *>(audio->data[1]),
		       (int)audio->frames);
	return audio;
}

obs_properties_t *properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *info = obs_properties_add_text(props, "usage", obs_module_text("Stereo.Usage"), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(info, true);
	obs_property_t *p = obs_properties_add_int_slider(props, kDelay, obs_module_text("Stereo.Delay"), 5, 40, 1);
	obs_property_int_set_suffix(p, " ms");
	p = obs_properties_add_int_slider(props, kWidth, obs_module_text("Stereo.Width"), 0, 100, 1);
	obs_property_int_set_suffix(p, " %");
	p = obs_properties_add_list(props, kRight, obs_module_text("Stereo.Side"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_BOOL);
	obs_property_list_add_bool(p, obs_module_text("Stereo.Side.Right"), true);
	obs_property_list_add_bool(p, obs_module_text("Stereo.Side.Left"), false);
	return props;
}

void defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kDelay, 15);
	obs_data_set_default_int(settings, kWidth, 70);
	obs_data_set_default_bool(settings, kRight, true);
}

} // namespace

void register_stereo_filter()
{
	obs_source_info info = {};
	info.id = "vst_ex_pseudo_stereo_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = get_name;
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.filter_audio = filter_audio;
	info.get_properties = properties;
	info.get_defaults = defaults;
	obs_register_source(&info);
}

#include "pitch-filter.h"

#include "dsp/pitch-shifter.h"

#include <obs-module.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace {

constexpr const char *kId = "vst_ex_pitch_filter";
constexpr const char *kSemitones = "semitones";
constexpr const char *kPan = "pan"; // -100 (left) .. 100 (right)

struct PitchFilter {
	std::vector<vstex::PitchShifter> channels;
	std::atomic<int> semitones{0};
	std::atomic<int> pan{0};
	int applied = 0;
};

const char *get_name(void *)
{
	return obs_module_text("Pitch.Name");
}

void update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<PitchFilter *>(data);
	f->semitones = (int)obs_data_get_int(settings, kSemitones);
	f->pan = (int)obs_data_get_int(settings, kPan);
}

void *create(obs_data_t *settings, obs_source_t *)
{
	auto *f = new PitchFilter();
	audio_t *audio = obs_get_audio();
	f->channels.resize(audio_output_get_channels(audio));
	for (auto &ch : f->channels)
		ch.prepare((double)audio_output_get_sample_rate(audio));
	update(f, settings);
	return f;
}

void destroy(void *data)
{
	delete static_cast<PitchFilter *>(data);
}

obs_audio_data *filter_audio(void *data, obs_audio_data *audio)
{
	auto *f = static_cast<PitchFilter *>(data);
	int semis = f->semitones;
	if (semis != f->applied) {
		for (auto &ch : f->channels)
			ch.setSemitones(semis);
		f->applied = semis;
	}
	if (semis != 0) // 0: untouched, no delay, no coloring
		for (size_t c = 0; c < f->channels.size(); c++)
			if (audio->data[c])
				f->channels[c].process(reinterpret_cast<float *>(audio->data[c]), (int)audio->frames);

	// Balance: the far side fades out, the near side stays at full level (center = unchanged).
	int pan = f->pan;
	if (pan != 0 && f->channels.size() >= 2 && audio->data[0] && audio->data[1]) {
		float left = pan > 0 ? 1.0f - pan / 100.0f : 1.0f, right = pan < 0 ? 1.0f + pan / 100.0f : 1.0f;
		auto *l = reinterpret_cast<float *>(audio->data[0]), *r = reinterpret_cast<float *>(audio->data[1]);
		for (uint32_t i = 0; i < audio->frames; i++) {
			l[i] *= left;
			r[i] *= right;
		}
	}
	return audio;
}

obs_properties_t *properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *info = obs_properties_add_text(props, "usage", obs_module_text("Pitch.Usage"), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(info, true);
	obs_property_t *p = obs_properties_add_int_slider(props, kSemitones, obs_module_text("Pitch.Semitones"), -12, 12, 1);
	obs_property_set_long_description(p, obs_module_text("Pitch.Semitones.Tip"));
	p = obs_properties_add_int_slider(props, kPan, obs_module_text("Pitch.Pan"), -100, 100, 1);
	obs_property_set_long_description(p, obs_module_text("Pitch.Pan.Tip"));
	return props;
}

void defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kSemitones, 0);
	obs_data_set_default_int(settings, kPan, 0);
}

} // namespace

int vst_ex_pitch_latency_samples(obs_source_t *filter)
{
	if (strcmp(obs_source_get_unversioned_id(filter), kId) != 0)
		return -1;
	auto *f = static_cast<PitchFilter *>(obs_obj_get_data(filter));
	return f && f->semitones != 0 && !f->channels.empty() ? f->channels[0].latencySamples() : 0;
}

void register_pitch_filter()
{
	obs_source_info info = {};
	info.id = kId;
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

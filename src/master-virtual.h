#pragma once

#include <obs.h>
#include <media-io/audio-io.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

// A seventh audio track that exists only in memory: sound pushed here comes out of an audio_t of our own, which an
// encoder can read instead of OBS's six mixes. Nothing here is saved anywhere, so a crash, a power cut or removing the
// plugin leaves OBS's own settings as they were.

inline uint64_t master_block_ns()
{
	return uint64_t(AUDIO_OUTPUT_FRAMES) * 1000000000ULL / audio_output_get_sample_rate(obs_get_audio());
}

// One track's mix goes into the bus source. OBS treats a source running behind the mix as a reason to raise its audio
// buffering for everything, so the bus gets it one block ahead; VirtualTrack::push takes that back off.
inline void master_forward(obs_source_t *bus, const struct audio_data *mix)
{
	const struct audio_output_info *info = audio_output_get_info(obs_get_audio());
	struct obs_source_audio out = {};
	for (size_t i = 0; i < MAX_AV_PLANES; i++)
		out.data[i] = mix->data[i];
	out.frames = mix->frames;
	out.speakers = info->speakers;
	out.format = AUDIO_FORMAT_FLOAT_PLANAR;
	out.samples_per_sec = info->samples_per_sec;
	out.timestamp = mix->timestamp + master_block_ns();
	obs_source_output_audio(bus, &out);
}

class VirtualTrack {
public:
	bool open()
	{
		const struct audio_output_info *main = audio_output_get_info(obs_get_audio());
		planes = audio_output_get_channels(obs_get_audio());
		rate = main->samples_per_sec;
		slots.resize(kBlocks);
		for (Slot &slot : slots)
			slot.samples.resize(planes * AUDIO_OUTPUT_FRAMES);
		struct audio_output_info info = {};
		info.name = "VST Ex Master";
		info.samples_per_sec = main->samples_per_sec;
		info.format = AUDIO_FORMAT_FLOAT_PLANAR;
		info.speakers = main->speakers;
		info.input_callback = onTick;
		info.input_param = this;
		return audio_output_open(&output, &info) == AUDIO_OUTPUT_SUCCESS;
	}

	void close()
	{
		if (output)
			audio_output_close(output);
		output = nullptr;
	}

	audio_t *audio() const { return output; }

	// Sound from the bus, any length (some filters hand out other sizes than they get) cut back into mix blocks.
	// Each block keeps the time it was mixed for, so the stream stays in sync with its video.
	void push(const struct audio_data *in, float gain)
	{
		std::lock_guard<std::mutex> guard(lock);
		uint64_t mixedAt = in->timestamp - master_block_ns();
		for (uint32_t done = 0; done < in->frames;) {
			if (count == kBlocks) { // nobody is reading: keep the newest
				head = (head + 1) % kBlocks;
				count--;
			}
			Slot &slot = slots[(head + count) % kBlocks];
			if (filled == 0)
				slot.timestamp = mixedAt + uint64_t(done) * 1000000000ULL / rate;
			uint32_t take = std::min<uint32_t>(in->frames - done, AUDIO_OUTPUT_FRAMES - filled);
			for (size_t c = 0; c < planes; c++) {
				float *to = slot.samples.data() + c * AUDIO_OUTPUT_FRAMES + filled;
				auto *from = reinterpret_cast<const float *>(in->data[c]);
				for (uint32_t i = 0; i < take; i++)
					to[i] = from ? from[done + i] * gain : 0.0f;
			}
			done += take;
			filled += take;
			if (filled == AUDIO_OUTPUT_FRAMES) {
				filled = 0;
				count++;
			}
		}
	}

private:
	static constexpr size_t kBlocks = 32; // about 0.7 s at 48 kHz

	struct Slot {
		uint64_t timestamp = 0;
		std::vector<float> samples;
	};

	static bool onTick(void *param, uint64_t, uint64_t, uint64_t *newTs, uint32_t mixers, struct audio_output_data *mixes)
	{
		auto *self = static_cast<VirtualTrack *>(param);
		std::lock_guard<std::mutex> guard(self->lock);
		if (!self->count)
			return false; // this tick has nothing; the next one sends the block that arrives
		const Slot &slot = self->slots[self->head];
		for (size_t m = 0; m < MAX_AUDIO_MIXES; m++)
			if (mixers & (1u << m))
				for (size_t c = 0; c < self->planes; c++)
					memcpy(mixes[m].data[c], slot.samples.data() + c * AUDIO_OUTPUT_FRAMES,
					       AUDIO_OUTPUT_FRAMES * sizeof(float));
		*newTs = slot.timestamp;
		self->head = (self->head + 1) % kBlocks;
		self->count--;
		return true;
	}

	audio_t *output = nullptr;
	size_t planes = 2;
	uint32_t rate = 48000;
	std::mutex lock;
	std::vector<Slot> slots;
	size_t head = 0, count = 0;
	uint32_t filled = 0; // frames already in the block being written
};

// End-to-end check of the virtual track on a real libobs, with two outputs side by side for 30 s: "direct" encodes
// track 1 the normal way, "virtual" encodes the same sound after it went through the bus and the virtual track.
// Clicks must all arrive, at the bus volume, at the same place relative to the video as on the direct output.
// Run from OBS's bin/64bit folder. Exits non-zero on failure.
#include "master-virtual.h"

#include <obs.h>
#include <util/platform.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static constexpr uint32_t kBlock = 480;
static constexpr int kClickEveryBlocks = 50; // 500 ms
static constexpr int kSeconds = 30;

static obs_source_t *bus;
static VirtualTrack track;
static std::atomic<bool> running{true};
static std::mutex lock;
static std::vector<int64_t> sentUs; // when each click was played

struct Result {
	std::vector<int64_t> clickPts; // samples from the start of the audio
	float peak = 0.0f;
	int64_t videoSysStartUs = -1;
};
static Result direct, virt;

static Result &resultOf(const char *name)
{
	return strcmp(name, "direct") == 0 ? direct : virt;
}

// Encoders that only record: enough for libobs to run its real start, pairing and timing logic.
static const char *encName(void *)
{
	return "test";
}
static void *encCreate(obs_data_t *, obs_encoder_t *encoder)
{
	return encoder;
}
static void encDestroy(void *) {}

static uint8_t byte;

static bool audioEncode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received)
{
	auto *samples = reinterpret_cast<const float *>(frame->data[0]);
	Result &r = resultOf(obs_encoder_get_name(static_cast<obs_encoder_t *>(data)));
	{
		std::lock_guard<std::mutex> guard(lock);
		for (uint32_t i = 0; samples && i < frame->frames; i++)
			if (std::fabs(samples[i]) > 0.05f) {
				r.clickPts.push_back(frame->pts + i);
				r.peak = std::fabs(samples[i]);
			}
	}
	packet->data = &byte;
	packet->size = 1;
	packet->pts = packet->dts = frame->pts;
	packet->timebase_num = 1;
	packet->timebase_den = 48000;
	packet->type = OBS_ENCODER_AUDIO;
	*received = true;
	return true;
}

static size_t audioFrameSize(void *)
{
	return 1024;
}

static bool videoEncode(void *, struct encoder_frame *frame, struct encoder_packet *packet, bool *received)
{
	packet->data = &byte;
	packet->size = 1;
	packet->pts = packet->dts = frame->pts;
	packet->timebase_num = 1;
	packet->timebase_den = 30;
	packet->type = OBS_ENCODER_VIDEO;
	packet->keyframe = true;
	*received = true;
	return true;
}

static const char *outName(void *)
{
	return "test";
}
static void *outCreate(obs_data_t *, obs_output_t *output)
{
	return output;
}
static void outDestroy(void *) {}
static bool outStart(void *data)
{
	auto *output = static_cast<obs_output_t *>(data);
	if (!obs_output_can_begin_data_capture(output, 0) || !obs_output_initialize_encoders(output, 0))
		return false;
	return obs_output_begin_data_capture(output, 0);
}
static void outStop(void *data, uint64_t)
{
	obs_output_end_data_capture(static_cast<obs_output_t *>(data));
}
static void outPacket(void *data, struct encoder_packet *packet)
{
	if (!packet || packet->type != OBS_ENCODER_VIDEO)
		return;
	Result &r = resultOf(obs_output_get_name(static_cast<obs_output_t *>(data)));
	std::lock_guard<std::mutex> guard(lock);
	if (r.videoSysStartUs < 0)
		r.videoSysStartUs = packet->sys_dts_usec - packet->dts_usec;
}

static void onTrack1(void *, size_t, struct audio_data *data)
{
	master_forward(bus, data);
}

static void onBus(void *, obs_source_t *source, const struct audio_data *data, bool muted)
{
	// Cut in two, like a filter that hands out other sizes than it gets.
	float gain = muted ? 0.0f : obs_source_get_volume(source);
	struct audio_data first = *data, second = *data;
	first.frames = 300;
	second.frames = data->frames - 300;
	for (size_t c = 0; c < 2; c++)
		second.data[c] = data->data[c] + 300 * sizeof(float);
	track.push(&first, gain);
	track.push(&second, gain);
}

static const char *sourceName(void *)
{
	return "test";
}
static void *sourceCreate(obs_data_t *, obs_source_t *s)
{
	return s;
}
static void sourceDestroy(void *) {}

static void feed(obs_source_t *source)
{
	std::vector<float> samples(kBlock);
	uint64_t next = os_gettime_ns();
	for (int block = 0; running; block++) {
		std::fill(samples.begin(), samples.end(), 0.0f);
		if (block % kClickEveryBlocks == 0) {
			samples[0] = 0.8f;
			std::lock_guard<std::mutex> guard(lock);
			sentUs.push_back(int64_t(next / 1000));
		}
		struct obs_source_audio a = {};
		a.data[0] = a.data[1] = reinterpret_cast<const uint8_t *>(samples.data());
		a.frames = kBlock;
		a.speakers = SPEAKERS_STEREO;
		a.format = AUDIO_FORMAT_FLOAT_PLANAR;
		a.samples_per_sec = 48000;
		a.timestamp = next;
		obs_source_output_audio(source, &a);
		next += 10000000;
		os_sleepto_ns(next);
	}
}

// How long after being played each click sits on the output's timeline.
static void report(const char *name, const Result &r, double &meanMs)
{
	std::vector<double> delays;
	for (int64_t pts : r.clickPts) {
		int64_t at = r.videoSysStartUs + pts * 1000000 / 48000;
		auto nearest = std::min_element(sentUs.begin(), sentUs.end(),
						[at](int64_t a, int64_t b) { return std::llabs(a - at) < std::llabs(b - at); });
		delays.push_back(double(at - *nearest) / 1000.0);
	}
	double sum = 0.0, low = 1e9, high = -1e9;
	for (double d : delays) {
		sum += d;
		low = std::min(low, d);
		high = std::max(high, d);
	}
	meanMs = delays.empty() ? 1e9 : sum / double(delays.size());
	printf("%-8s clicks %zu, peak %.3f, delay mean %.2f ms (min %.2f, max %.2f)\n", name, delays.size(), r.peak, meanMs, low,
	       high);
}

int main()
{
	if (!obs_startup("en-US", nullptr, nullptr))
		return 2;
	struct obs_audio_info audio = {48000, SPEAKERS_STEREO};
	obs_reset_audio(&audio);
	struct obs_video_info video = {};
	char dir[512];
	os_getcwd(dir, sizeof(dir));
	std::string module = std::string(dir) + "/libobs-d3d11"; // libobs only searches next to this exe otherwise
	video.graphics_module = module.c_str();
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = video.output_width = 320;
	video.base_height = video.output_height = 180;
	video.output_format = VIDEO_FORMAT_NV12;
	video.gpu_conversion = true;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS)
		return 3;

	struct obs_source_info si = {};
	si.id = "virtual_test_source";
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.output_flags = OBS_SOURCE_AUDIO;
	si.get_name = sourceName;
	si.create = sourceCreate;
	si.destroy = sourceDestroy;
	obs_register_source(&si);

	struct obs_encoder_info ae = {};
	ae.id = "virtual_test_audio";
	ae.type = OBS_ENCODER_AUDIO;
	ae.codec = "aac";
	ae.get_name = encName;
	ae.create = encCreate;
	ae.destroy = encDestroy;
	ae.encode = audioEncode;
	ae.get_frame_size = audioFrameSize;
	obs_register_encoder(&ae);
	struct obs_encoder_info ve = {};
	ve.id = "virtual_test_video";
	ve.type = OBS_ENCODER_VIDEO;
	ve.codec = "h264";
	ve.get_name = encName;
	ve.create = encCreate;
	ve.destroy = encDestroy;
	ve.encode = videoEncode;
	obs_register_encoder(&ve);

	struct obs_output_info oi = {};
	oi.id = "virtual_test_output";
	oi.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED;
	oi.get_name = outName;
	oi.create = outCreate;
	oi.destroy = outDestroy;
	oi.start = outStart;
	oi.stop = outStop;
	oi.encoded_packet = outPacket;
	obs_register_output(&oi);

	// The sound: track 1 only. The bus sits on no output channel, so OBS never mixes it into any track.
	obs_source_t *source = obs_source_create_private("virtual_test_source", "source", nullptr);
	bus = obs_source_create_private("virtual_test_source", "bus", nullptr);
	obs_source_set_audio_mixers(source, 1 << 0);
	obs_source_set_volume(bus, 0.5f);
	obs_set_output_source(1, source);
	if (!track.open())
		return 4;
	obs_source_add_audio_capture_callback(bus, onBus, nullptr);
	struct audio_convert_info conv = {};
	conv.samples_per_sec = 48000;
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;
	conv.allow_clipping = true;
	obs_add_raw_audio_callback(0, &conv, onTrack1, nullptr);

	std::thread feeder(feed, source);
	os_sleep_ms(1000);

	obs_output_t *outputs[2];
	const char *names[2] = {"direct", "virtual"};
	for (int i = 0; i < 2; i++) {
		obs_encoder_t *v = obs_video_encoder_create("virtual_test_video", names[i], nullptr, nullptr);
		obs_encoder_t *a = obs_audio_encoder_create("virtual_test_audio", names[i], nullptr, 0, nullptr);
		obs_encoder_set_video(v, obs_get_video());
		obs_encoder_set_audio(a, obs_get_audio());
		if (i == 1)
			obs_encoder_set_audio(a, track.audio()); // the swap, as the plugin would do before the stream starts
		outputs[i] = obs_output_create("virtual_test_output", names[i], nullptr, nullptr);
		obs_output_set_video_encoder(outputs[i], v);
		obs_output_set_audio_encoder(outputs[i], a, 0);
		if (!obs_output_start(outputs[i])) {
			printf("output %s did not start: %s\n", names[i], obs_output_get_last_error(outputs[i]));
			std::_Exit(5);
		}
	}
	os_sleep_ms(kSeconds * 1000);
	for (obs_output_t *o : outputs)
		obs_output_stop(o);
	os_sleep_ms(500);
	running = false;
	feeder.join();

	double directMs = 0.0, virtualMs = 0.0;
	std::lock_guard<std::mutex> guard(lock);
	printf("libobs %s\n", obs_get_version_string());
	report("direct", direct, directMs);
	report("virtual", virt, virtualMs);
	bool ok = direct.clickPts.size() >= 50 && virt.clickPts.size() + 1 >= direct.clickPts.size() &&
		  std::fabs(virt.peak - 0.4f) < 0.05f && std::fabs(virtualMs - directMs) < 5.0;
	printf(ok ? "master-virtual-test passed\n" : "master-virtual-test FAILED\n");
	fflush(stdout);
	std::_Exit(ok ? 0 : 1); // shortcut: skips libobs shutdown, this process only measures
}

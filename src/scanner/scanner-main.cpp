// vst-ex-scanner: loads one plugin file and prints its effects as JSON on stdout.
// Runs as a child process of OBS so that a plugin crashing during scan cannot crash OBS.
//
// Usage: vst-ex-scanner <plugin path>
//        vst-ex-scanner --process-test <plugin path> [uid]   (loads, processes a sine, round-trips the state)
// Exit code 0 and a JSON array on success, non-zero and a message on stderr otherwise.

#include "host/plugin-host.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

static std::string narrow(const wchar_t *w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n - 1 : 0, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
	return s;
}

static std::string jsonString(const std::string &s)
{
	std::string out = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += (char)c;
			}
		}
	}
	return out + "\"";
}

static int processTest(const std::string &path, const std::string &uid)
{
	std::string error;
	auto plugin = vstex::loadPlugin(path, uid, error);
	if (!plugin || !plugin->prepare(48000.0, 1024, 2)) {
		fprintf(stderr, "load/prepare failed: %s\n", error.c_str());
		return 1;
	}

	std::vector<float> left(1024), right(1024);
	float *channels[2] = {left.data(), right.data()};
	double inSum = 0, outSum = 0;
	size_t t = 0;
	for (int block = 0; block < 200; block++) {
		for (size_t i = 0; i < left.size(); i++, t++)
			left[i] = right[i] = 0.25f * (float)std::sin(2.0 * 3.14159265358979 * 440.0 * t / 48000.0);
		for (float s : left)
			inSum += s * s;
		plugin->process(channels, 2, 1024);
		for (int c = 0; c < 2; c++)
			for (float s : (c ? right : left)) {
				if (!std::isfinite(s)) {
					fprintf(stderr, "non-finite output\n");
					return 1;
				}
				outSum += c ? 0 : s * s;
			}
	}

	auto state = plugin->getState();
	bool restored = plugin->setState(state);
	printf("name=%s latency=%d in_rms=%.4f out_rms=%.4f state_bytes=%zu state_restore=%s\n",
	       plugin->description().name.c_str(), plugin->latencySamples(), std::sqrt(inSum / (200 * 1024)),
	       std::sqrt(outSum / (200 * 1024)), state.size(), restored ? "ok" : "FAILED");
	fflush(stdout);
	TerminateProcess(GetCurrentProcess(), restored ? 0 : 1);
	return 0;
}

int wmain(int argc, wchar_t **argv)
{
	// No "program has stopped working" dialogs: the parent treats a crash as a failed scan.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	if (argc >= 3 && std::wstring(argv[1]) == L"--process-test")
		return processTest(narrow(argv[2]), argc >= 4 ? narrow(argv[3]) : std::string());

	if (argc < 2) {
		fprintf(stderr, "usage: vst-ex-scanner <plugin path>\n");
		return 2;
	}

	std::string path = narrow(argv[1]);
	std::string error;
	auto list = vstex::describePlugins(path, error);
	if (list.empty()) {
		fprintf(stderr, "%s\n", error.empty() ? "no plugins found" : error.c_str());
		return 1;
	}

	std::string out = "[";
	for (size_t i = 0; i < list.size(); i++) {
		const auto &d = list[i];
		if (i)
			out += ",";
		out += "{\"format\":" + jsonString(vstex::formatName(d.format)) + ",\"path\":" + jsonString(d.path) +
		       ",\"uid\":" + jsonString(d.uid) + ",\"name\":" + jsonString(d.name) +
		       ",\"vendor\":" + jsonString(d.vendor) + ",\"category\":" + jsonString(d.category) +
		       ",\"version\":" + jsonString(d.version) + ",\"description\":" + jsonString(d.description) +
		       ",\"snapshot\":" + jsonString(d.snapshot) +
		       ",\"inputs\":" + std::to_string(d.numInputs) + ",\"outputs\":" + std::to_string(d.numOutputs) +
		       ",\"editor\":" + (d.hasEditor ? "true" : "false") + "}";
	}
	out += "]";
	fwrite(out.data(), 1, out.size(), stdout);
	fflush(stdout);
	// Some plugins hang or crash in their unload code; we have what we need, so skip it.
	TerminateProcess(GetCurrentProcess(), 0);
	return 0;
}

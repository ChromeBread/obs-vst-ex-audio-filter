// Self-check for PitchShifter: a 440 Hz sine shifted by N semitones must come out at 440 * 2^(N/12) Hz.
// Built as its own small executable; exits non-zero on failure.
#include "dsp/pitch-shifter.h"
#include "dsp/polar.h"
#include "dsp/pseudo-stereo.h"

#include <cmath>
#include <cstdio>
#include <vector>

static double measureHz(const std::vector<float> &x, double sampleRate)
{
	// Count upward zero crossings over the second half (past the start-up transient).
	size_t start = x.size() / 2, crossings = 0;
	size_t first = 0, last = 0;
	for (size_t i = start + 1; i < x.size(); i++)
		if (x[i - 1] < 0 && x[i] >= 0) {
			if (!crossings)
				first = i;
			last = i;
			crossings++;
		}
	return crossings > 1 ? (crossings - 1) * sampleRate / double(last - first) : 0.0;
}

int main()
{
	const double rate = 48000, inHz = 440;
	int failures = 0;
	for (int semis : {-12, -5, 0, 3, 7, 12}) {
		vstex::PitchShifter ps;
		ps.prepare(rate);
		ps.setSemitones(semis);
		std::vector<float> x(48000);
		for (size_t i = 0; i < x.size(); i++)
			x[i] = float(std::sin(2 * 3.14159265358979323846 * inHz * double(i) / rate));
		for (size_t off = 0; off < x.size(); off += 480) // OBS-sized blocks
			ps.process(x.data() + off, 480);
		double want = inHz * std::pow(2.0, semis / 12.0), got = measureHz(x, rate);
		bool ok = std::fabs(got - want) / want < 0.03; // within 3%
		std::printf("%+3d semitones: want %7.1f Hz, got %7.1f Hz  %s\n", semis, want, got, ok ? "ok" : "FAIL");
		failures += !ok;
	}
	// Pseudo stereo: an impulse must come out dry on the left at once, and on the right only
	// (width 1) after the delay.
	{
		vstex::PseudoStereo ps;
		ps.prepare(48000);
		ps.set(15.0, 1.0, true);
		std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
		l[0] = r[0] = 1.0f;
		ps.process(l.data(), r.data(), 2000);
		bool ok = l[0] == 1.0f && r[0] == 0.0f && r[720] == 1.0f && l[720] == 0.0f;
		std::printf("pseudo stereo: left %.1f now, right %.1f after 720 samples  %s\n", l[0], r[720], ok ? "ok" : "FAIL");
		failures += !ok;
	}
	// Imager: mono straight up, left-only up-left, right-only up-right, out of phase flat, never below.
	{
		float x, y;
		bool ok = true;
		vstex::polarPoint(0.5f, 0.5f, x, y);
		ok &= std::fabs(x) < 1e-6f && y > 0;
		vstex::polarPoint(0.5f, 0.0f, x, y);
		ok &= x < 0 && std::fabs(x + y) < 1e-6f;
		vstex::polarPoint(0.0f, 0.5f, x, y);
		ok &= x > 0 && std::fabs(x - y) < 1e-6f;
		vstex::polarPoint(0.5f, -0.5f, x, y);
		ok &= std::fabs(y) < 1e-6f && std::fabs(x) > 0.5f;
		vstex::polarPoint(-0.5f, -0.4f, x, y);
		ok &= y >= 0;
		std::printf("polar imager mapping  %s\n", ok ? "ok" : "FAIL");
		failures += !ok;
	}
	return failures;
}

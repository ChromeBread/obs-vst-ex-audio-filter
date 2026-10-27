#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace vstex {

// Light pitch shifter for one channel: two read taps sweep through a short delay line at the pitch ratio,
// crossfaded with sin^2 windows half a window apart (their gains always sum to 1).
// Cheap and click-free; large shifts sound a little "phasey", which is fine for streaming.
class PitchShifter {
public:
	void prepare(double sampleRate, double windowSeconds = 0.04)
	{
		window = std::max(64.0, sampleRate * windowSeconds);
		buffer.assign((size_t)window * 2 + 8, 0.0f);
		writePos = 0;
		delay = 0.0;
	}

	void setSemitones(double semitones) { ratio = std::pow(2.0, semitones / 12.0); }

	void process(float *samples, int frames)
	{
		const size_t size = buffer.size();
		const double pi = 3.14159265358979323846;
		for (int i = 0; i < frames; i++) {
			buffer[writePos] = samples[i];
			double d1 = delay, d2 = std::fmod(delay + window / 2, window);
			double g1 = std::sin(pi * d1 / window), g2 = std::sin(pi * d2 / window);
			samples[i] = float(g1 * g1 * read(d1 + kMinDelay, size) + g2 * g2 * read(d2 + kMinDelay, size));
			// Reading faster than writing (ratio > 1) means the delay shrinks, and vice versa.
			delay = std::fmod(delay + (1.0 - ratio) + window, window);
			writePos = (writePos + 1) % size;
		}
	}

	// Samples the output lags behind the input on average.
	int latencySamples() const { return int(window / 2 + kMinDelay); }

private:
	static constexpr double kMinDelay = 2.0; // keeps the interpolating read behind the write head

	float read(double d, size_t size) const
	{
		double pos = double(writePos) - d;
		while (pos < 0)
			pos += double(size);
		size_t i0 = size_t(pos) % size, i1 = (i0 + 1) % size;
		double frac = pos - std::floor(pos);
		return float(buffer[i0] * (1.0 - frac) + buffer[i1] * frac);
	}

	std::vector<float> buffer;
	size_t writePos = 0;
	double window = 1920.0;
	double delay = 0.0;
	double ratio = 1.0;
};

} // namespace vstex

#pragma once

#include <algorithm>
#include <vector>

namespace vstex {

// Pseudo stereo from one mic (Haas effect): mix to mono, keep the dry sound on one side and put a short
// delayed copy on the other. `width` 0 = plain mono, 1 = the far side hears only the delayed copy.
class PseudoStereo {
public:
	void prepare(double sampleRate, double maxDelayMs = 50.0)
	{
		buffer.assign(size_t(sampleRate * maxDelayMs / 1000.0) + 2, 0.0f);
		pos = 0;
		rate = sampleRate;
	}

	void set(double delayMs, double widthAmount, bool delayRight)
	{
		delay = std::min(buffer.size() - 1, size_t(rate * delayMs / 1000.0));
		width = float(widthAmount);
		right = delayRight;
	}

	void process(float *left, float *rightCh, int frames)
	{
		const size_t size = buffer.size();
		for (int i = 0; i < frames; i++) {
			float mono = 0.5f * (left[i] + rightCh[i]);
			buffer[pos] = mono;
			float delayed = buffer[(pos + size - delay) % size];
			float wet = (1.0f - width) * mono + width * delayed;
			left[i] = right ? mono : wet;
			rightCh[i] = right ? wet : mono;
			pos = (pos + 1) % size;
		}
	}

private:
	std::vector<float> buffer;
	size_t pos = 0, delay = 0;
	double rate = 48000.0;
	float width = 0.0f;
	bool right = true;
};

} // namespace vstex

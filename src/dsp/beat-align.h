#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace vstex {

// Beat alignment for the sync-offset helper. Works on envelopes with one value per millisecond
// (peak of |sample| in that ms).

// Onsets: where the envelope (smoothed with a 5 ms running max) rises through 40% of its maximum.
// After an onset, nothing within `refractoryMs` counts (one clap, one kick).
inline std::vector<int> findOnsets(const std::vector<float> &env, int refractoryMs = 150)
{
	std::vector<float> smooth(env.size(), 0.0f);
	for (size_t i = 0; i < env.size(); i++)
		for (size_t j = (i >= 4 ? i - 4 : 0); j <= i; j++)
			smooth[i] = std::max(smooth[i], env[j]);
	float peak = smooth.empty() ? 0.0f : *std::max_element(smooth.begin(), smooth.end());
	std::vector<int> onsets;
	if (peak <= 1e-4f) // silence
		return onsets;
	float threshold = 0.4f * peak;
	int last = -refractoryMs;
	for (size_t i = 1; i < smooth.size(); i++)
		if (smooth[i] >= threshold && smooth[i - 1] < threshold && int(i) - last >= refractoryMs) {
			onsets.push_back(int(i));
			last = int(i);
		}
	return onsets;
}

struct AlignResult {
	bool ok = false;
	int lagMs = 0;    // target onsets come this much after the reference onsets (negative: before)
	int matched = 0;  // target onsets that found a reference onset
	int spreadMs = 0; // max - min of the individual lags: how steady the taps were
};

// Median lag between each target onset and its nearest reference onset within +-maxLagMs.
// maxLagMs must stay under half a beat, or a beat and its neighbour look the same.
inline AlignResult alignOnsets(const std::vector<int> &reference, const std::vector<int> &target, int maxLagMs = 240,
			       int minMatches = 3)
{
	std::vector<int> lags;
	for (int t : target) {
		int best = maxLagMs + 1;
		for (int r : reference)
			if (std::abs(t - r) < std::abs(best))
				best = t - r;
		if (std::abs(best) <= maxLagMs)
			lags.push_back(best);
	}
	AlignResult res;
	res.matched = int(lags.size());
	if (res.matched < minMatches)
		return res;
	std::sort(lags.begin(), lags.end());
	res.ok = true;
	res.lagMs = lags[lags.size() / 2];
	res.spreadMs = lags.back() - lags.front();
	return res;
}

} // namespace vstex

// Self-check for PeakRing and the beat grid. Exits non-zero on failure.
#include "dsp/beat-grid.h"
#include "dsp/peak-ring.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

int main()
{
	PeakRing ring;
	check(ring.get(0) == 0.0f && ring.get(5) == 0.0f, "untouched bins are silent");
	ring.put(5, 0.5f);
	ring.put(5, 0.25f);
	check(std::fabs(ring.get(5) - 0.5f) < 0.001f, "a bin keeps its highest peak");
	ring.put(5, 0.75f);
	check(std::fabs(ring.get(5) - 0.75f) < 0.001f, "a higher peak replaces it");
	ring.put(5 + PeakRing::kBins, 0.1f);
	check(ring.get(5) == 0.0f, "a bin overwritten by a newer one reads as silence");
	check(std::fabs(ring.get(5 + PeakRing::kBins) - 0.1f) < 0.001f, "the newer bin is there");
	ring.put(7, 3.0f);
	check(ring.get(7) == 1.0f, "peaks clip at full scale");
	check(beat_at_or_before(1000, 1000, 500) == 0, "the anchor is beat 0");
	check(beat_at_or_before(1499, 1000, 500) == 0, "still beat 0 just before the next");
	check(beat_at_or_before(999, 1000, 500) == -1, "just before the anchor is beat -1");
	check(beat_at_or_before(500, 1000, 500) == -1, "exactly on beat -1");
	check(beat_starts_bar(0) && beat_starts_bar(-4) && !beat_starts_bar(-1) && !beat_starts_bar(5), "every 4th beat");
	printf(failures ? "peak-ring-test FAILED\n" : "peak-ring-test passed\n");
	return failures ? 1 : 0;
}

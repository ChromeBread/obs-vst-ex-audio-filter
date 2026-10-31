#pragma once

#include <cstdint>

// Beats counted from an anchor time; beats before the anchor have negative numbers.
inline int64_t beat_at_or_before(int64_t ns, int64_t anchorNs, int64_t beatNs)
{
	int64_t since = ns - anchorNs;
	return since / beatNs - (since % beatNs < 0 ? 1 : 0);
}

inline bool beat_starts_bar(int64_t beat)
{
	return ((beat % 4) + 4) % 4 == 0;
}

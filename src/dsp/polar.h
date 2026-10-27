#pragma once

#include <cmath>

namespace vstex {

// One stereo sample pair as a point of a half-circle "polar sample" imager.
// Mono (L = R) is straight up, left-only leans 45 degrees left, right-only 45 degrees right, and
// out-of-phase sound (L = -R) lies flat on the edge. y is always >= 0: opposite points are folded up.
inline void polarPoint(float left, float right, float &x, float &y)
{
	const float k = 0.70710678f;
	float mid = (left + right) * k, side = (left - right) * k;
	if (mid < 0) { // fold the lower half up
		mid = -mid;
		side = -side;
	}
	x = -side; // more left than right -> negative x (left)
	y = mid;
}

} // namespace vstex

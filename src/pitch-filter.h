#pragma once

// "VST Ex Pitch / Pan": moves a source up or down by whole semitones (-12..+12) and pans it left/right.
// No external plugin needed.
#include <obs.h>

void register_pitch_filter();
// Delay this filter adds right now (0 when not shifting); -1 if `filter` is not a pitch filter.
int vst_ex_pitch_latency_samples(obs_source_t *filter);

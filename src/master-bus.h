#pragma once

#include <obs.h>

// OBS has no master bus: filters and volume exist only per source, and its six tracks are all there is. VST Ex Master
// builds one out of public API without touching any of them: a private source takes the mix of one track, runs it
// through its own filters and volume, and the result goes to a virtual seventh track that only the stream reads, from
// the moment it starts until it stops. Nothing of that is saved in OBS's settings.
namespace masterbus {

struct Route {
	bool enabled = false;
	int in = 1; // track 1..6
};

void install();
void uninstall();

// Experimental: until the user accepts it in Tools (debug mode), the master does nothing and source() is null, which
// hides it from every dock.
bool unlocked();
void setUnlocked(bool on);

// Replaced when the scene collection changes. Null when locked. Not a new reference.
obs_source_t *source();
Route route();
void setEnabled(bool on);
void setInput(int in);

// The stream is reading the master right now. While it is, route changes would cut its sound.
bool onStream();
// The last stream start could not go through the master, so it runs on OBS's normal sound.
bool streamFailed();

// The track whose sources reach the stream.
int onAirTrack();

} // namespace masterbus

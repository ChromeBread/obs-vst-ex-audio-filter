#pragma once

// "VST Ex Mixer" dock, REAPER-style FX buttons: every audio source that has VST filters, with one button per
// VST (green = ON). Click = ON/OFF, right-click = open the plugin (VST Ex) or its Filters (VST Ex) window.
// Its own dock on public OBS API only, so OBS updates to the Audio Mixer cannot break it.
void install_fx_dock();
void remove_fx_dock();

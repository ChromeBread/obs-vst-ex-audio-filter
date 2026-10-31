#pragma once

#include <QRect>

void install_master_dock();
void remove_master_dock();

// Where the master strip is on screen, for the VST Ex Mixer to line up with. False when the dock is not showing.
bool master_strip_rect(QRect &globalRect);

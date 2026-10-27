#pragma once

// "VST Ex Pseudo Stereo": widens one mic without a DAW. Mixes to mono, keeps the dry voice on one side and
// a short delayed copy on the other (Haas effect). One filter, so the whole recipe is removed in one go.
void register_stereo_filter();

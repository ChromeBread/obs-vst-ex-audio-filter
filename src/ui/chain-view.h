#pragma once

// "VST Ex Chain View": every audio source as one row, left to right:
// device -> filters (ON/OFF, latency) -> Sync Offset / volume -> output (tracks, monitoring, level, total delay),
// with sidechain/ducking links drawn between rows. Double-click a row to open its Filters (VST Ex).
void open_chain_view();

// Live filter over whole monitors: a plugin decides what the overlay shows.
#pragma once
#include <string>

struct dp_plugin;

namespace dp {
// monitor < 0: every monitor. marker 1/2: test mark (see run() in filter_windows.cpp).
void start(const dp_plugin* plugin, int monitor = -1, int marker = 0);
// Stops every monitor and returns one report line per monitor.
std::string stop();
}  // namespace dp

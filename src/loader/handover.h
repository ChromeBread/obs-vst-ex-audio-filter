#pragma once

#include <obs.hpp>

#include <QString>
#include <QVector>

// Moves VST filters between VST Enigmax Loader and the other hosts, replacing each filter in place
// (same source, position, name, ON/OFF):
//   VST Enigmax Loader VST2  <->  OBS's own "VST 2.x Plug-in" (vst_filter)
//   VST Enigmax Loader VST3  <->  atkAudio PluginHost (atkaudio_plugin_host)
namespace handover {

enum class Kind { LoaderVst2, LoaderVst3, ObsVst2, Atk };

struct Item {
	OBSWeakSource parent, filter;
	Kind kind;
	QString sourceName, filterName, plugin;
	QString problem; // why it cannot be converted; empty when it can
};

// Every convertible filter on every source, in both directions.
QVector<Item> collect();
// Replaces the filter. False and `error` set if nothing was changed.
bool convert(const Item &item, QString &error);
// Display name of where the item goes ("OBS VST 2.x", "atkAudio", "VST Enigmax Loader").
QString targetName(Kind kind);

} // namespace handover

#pragma once

#include <obs.h>

#include <QHash>
#include <QString>
#include <QVector>

// ON/OFF sets: which filters of a source are on, saved under a name. Applying one only flips ON/OFF,
// nothing is reloaded, so it is instant and glitch-free (e.g. verse -> chorus mid-song).
// Stored in the source's private settings (travels with the scene collection); each set gets an OBS
// hotkey on its source (Settings > Hotkeys).
namespace switchsets {

struct Set {
	QString id; // stable, so hotkey bindings survive a rename
	QString name;
	QHash<QString, bool> enabled; // filter uuid -> on
	// Source-level audio, so a chain with more latency can bring its own offset and level.
	// Sets saved before these existed have hasAudio = false and leave both alone.
	bool hasAudio = false;
	int64_t syncOffsetNs = 0;
	float volume = 1.0f; // multiplier, as obs_source_get_volume()
};

QVector<Set> load(obs_source_t *source);
// Saves and refreshes the source's hotkeys.
void save(obs_source_t *source, const QVector<Set> &sets);
// The current ON/OFF state of every filter, plus the source's sync offset and volume.
Set capture(obs_source_t *source, const QString &name);
// Filters added after the set was saved are left as they are.
void apply(obs_source_t *source, const Set &set);

void install();
void uninstall();

} // namespace switchsets

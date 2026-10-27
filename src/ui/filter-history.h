#pragma once

#include <obs.hpp>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVector>

// Autosaved snapshots of a source's whole filter chain (settings, order, on/off, names, VST state).
// Persisted per source in the module config folder so they survive an OBS restart.
class FilterHistory : public QObject {
	Q_OBJECT

public:
	struct Snapshot {
		QDateTime time;
		QString label;
		QByteArray json;
	};

	static FilterHistory &instance();

	// Captures the current chain as the newest state. Identical consecutive states are skipped.
	void record(obs_source_t *source, const QString &label);
	bool canUndo(obs_source_t *source);
	bool canRedo(obs_source_t *source);
	void undo(obs_source_t *source);
	void redo(obs_source_t *source);

	QVector<Snapshot> snapshots(obs_source_t *source);
	int position(obs_source_t *source);
	// Puts the chain back to snapshot `index` and records that as a new state, so nothing is lost.
	void restore(obs_source_t *source, int index);

	// Whole filter chain as JSON, and putting one onto a source (recorded, so Undo brings the old one back).
	static QByteArray capture(obs_source_t *source);
	void applyChain(obs_source_t *source, const QByteArray &json, const QString &label);

signals:
	void changed(obs_source_t *source);

private:
	struct Track {
		bool loaded = false;
		QVector<Snapshot> items;
		int pos = -1;
	};

	Track &track(obs_source_t *source);
	void apply(obs_source_t *source, const QByteArray &json);
	void persist(obs_source_t *source);

	QHash<QString, Track> tracks; // keyed by source uuid
};

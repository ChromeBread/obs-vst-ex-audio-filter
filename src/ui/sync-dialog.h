#pragma once

#include <obs.hpp>

#include <QByteArray>
#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>

#include <memory>
#include <mutex>
#include <vector>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class WaveView;
class BeatBar;
class SongPlayer;

// Measures how late a source (e.g. the mic) is against another (e.g. desktop audio) from 4 beats:
// a count-in and a BPM120 chiptune test song play, the user claps on every kick, and the beat
// positions in both sources give the Sync Offset. Both waveforms scroll by while it records.
class SyncDialog : public QDialog {
	Q_OBJECT

public:
	// `target` is preselected as the source to adjust. Returns after the dialog closes.
	static void run(QWidget *parent, obs_source_t *target);
	~SyncDialog() override;

private:
	SyncDialog(QWidget *parent, obs_source_t *target);
	static void onAudio(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);
	void start();
	void tick();
	void finish();
	void stopCapture();
	OBSSource sourceAt(QComboBox *box) const;

	struct Chunk {
		int lane;
		uint64_t ts;
		std::vector<float> samples;
	};

	QComboBox *referenceBox;
	QComboBox *targetBox;
	QCheckBox *playSong;
	QComboBox *deviceBox;
	QSlider *songSlider;
	QSpinBox *songDb;
	QCheckBox *earphoneMode; // earphone held on the mic: every kick counts, no clapping
	QPushButton *startButton;
	QPushButton *applyButton;
	QSpinBox *manualLag;
	QLabel *status;
	WaveView *view;
	BeatBar *beats;
	QTimer timer;
	QElapsedTimer clock;
	std::unique_ptr<SongPlayer> player;

	OBSSource capturing[2];
	std::mutex lock;
	std::vector<Chunk> pending; // filled on the audio thread
	uint64_t t0 = 0;
	std::vector<float> env[2]; // 1 ms peaks, lane 0 = reference, 1 = target
	std::vector<int> onsets[2];
	int lagMs = 0;
};

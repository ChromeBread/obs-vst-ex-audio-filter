#include "ui/sync-dialog.h"

#include "dsp/beat-align.h"
#include "dsp/test-song.h"
#include "plugin-meta.h"
#include "ui/ui-text.h"

#include <util/platform.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSpinBox>
#include <QWheelEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <windows.h>
#include <mmsystem.h>

namespace {

constexpr int kBpm = 120;
constexpr int kBeatMs = 60000 / kBpm;
constexpr int kLoopBeats = 8;      // 4 to listen, 4 to clap
constexpr int kLoops = 2;          // the second loop doubles the claps (and shows off)
constexpr int kSongBeats = kLoopBeats * kLoops;
constexpr int kCaptureMs = kSongBeats * kBeatMs + 1200; // song + margin for late claps
constexpr int kVisibleMs = 3000;   // width of the scrolling view
constexpr int kStartLeadMs = 300;  // record a little before the song starts
constexpr int kSampleRate = 48000; // of the generated test song

// 16-bit mono WAV of the generated test song (see dsp/test-song.h).
QByteArray makeTestSong()
{
	std::vector<float> mix = vstex::makeTestSong(kSampleRate, kBeatMs, kLoopBeats, kLoops);
	const int total = int(mix.size());
	// 16-bit mono WAV
	QByteArray wav;
	auto put32 = [&](uint32_t v) { wav.append(reinterpret_cast<const char *>(&v), 4); };
	auto put16 = [&](uint16_t v) { wav.append(reinterpret_cast<const char *>(&v), 2); };
	wav.append("RIFF");
	put32(36 + total * 2);
	wav.append("WAVEfmt ");
	put32(16);
	put16(1);
	put16(1);
	put32(kSampleRate);
	put32(kSampleRate * 2);
	put16(2);
	put16(16);
	wav.append("data");
	put32(total * 2);
	for (float s : mix)
		put16(uint16_t(int16_t(std::lround(std::clamp(s, -1.0f, 1.0f) * 32000))));
	return wav;
}

bool hasAudio(obs_source_t *source)
{
	return (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

} // namespace

// Two lanes like a DJ deck: reference on top, target below. While recording, the newest audio enters
// on the right. Afterwards: mouse wheel zooms around the cursor, dragging pans, a millisecond ruler runs
// along the top, every beat/clap is marked with its time, and the target lane is drawn shifted by the
// manual lag so you can line the claps up with the kicks by eye.
class WaveView : public QWidget {
public:
	explicit WaveView(QWidget *parent) : QWidget(parent)
	{
		setMinimumSize(560, 200);
		setMouseTracking(true);
	}

	void set(const std::vector<float> *lanes, int nowMs, bool live, const std::vector<int> *onsets)
	{
		this->lanes = lanes;
		this->live = live;
		this->onsets = onsets;
		totalMs = qMax(1, nowMs);
		if (live) {
			viewStart = nowMs - kVisibleMs;
			viewSpan = kVisibleMs;
		} else if (!wasDone) { // first time done: show everything
			viewStart = 0;
			viewSpan = totalMs;
		}
		wasDone = !live;
		update();
	}

	// Target lane drawn this many ms earlier, to preview a manual offset.
	void setShift(int ms)
	{
		shiftMs = ms;
		update();
	}

protected:
	void wheelEvent(QWheelEvent *e) override
	{
		if (live || !lanes)
			return;
		double at = viewStart + viewSpan * e->position().x() / qMax(1, width());
		double factor = e->angleDelta().y() > 0 ? 0.8 : 1.25;
		viewSpan = std::clamp(viewSpan * factor, 20.0, double(totalMs) * 1.2);
		viewStart = at - viewSpan * e->position().x() / qMax(1, width());
		update();
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		dragX = e->position().x();
		dragStart = viewStart;
	}

	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!live && (e->buttons() & Qt::LeftButton)) {
			viewStart = dragStart - (e->position().x() - dragX) * viewSpan / qMax(1, width());
			update();
		}
	}

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(0x16, 0x16, 0x1c));
		if (!lanes)
			return;
		const int ruler = 18;
		auto xOf = [&](double ms) { return int((ms - viewStart) * width() / viewSpan); };

		// Ruler: a tick step that keeps labels at least ~70 px apart.
		static const int steps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000};
		int step = 2000;
		for (int st : steps)
			if (st * width() / viewSpan >= 70) {
				step = st;
				break;
			}
		p.setPen(QColor(150, 150, 160));
		QFont f = font();
		f.setPixelSize(11);
		p.setFont(f);
		for (int ms = int(std::floor(viewStart / step)) * step; ms <= viewStart + viewSpan; ms += step) {
			int x = xOf(ms);
			p.drawLine(x, ruler - 5, x, ruler);
			p.drawText(x + 3, ruler - 6, QString("%1 ms").arg(ms));
		}

		const QColor colors[2] = {QColor(80, 170, 255), QColor(255, 150, 60)};
		int laneH = (height() - ruler) / 2;
		for (int lane = 0; lane < 2; lane++) {
			int top = ruler + lane * laneH, mid = top + laneH / 2;
			double shift = lane == 1 && !live ? shiftMs : 0;
			p.setPen(QColor(60, 60, 70));
			p.drawLine(0, top, width(), top);
			p.setPen(colors[lane]);
			const auto &env = lanes[lane];
			for (int x = 0; x < width(); x++) {
				int a = int(viewStart + shift + viewSpan * x / width());
				int b = int(viewStart + shift + viewSpan * (x + 1) / width());
				float peak = 0;
				for (int ms = qMax(0, a); ms < qMin(qMax(b, a + 1), int(env.size())); ms++)
					peak = qMax(peak, env[ms]);
				int h = int(qMin(1.0f, peak) * (laneH / 2 - 4));
				p.drawLine(x, mid - h, x, mid + h);
			}
			if (!onsets)
				continue;
			for (int ms : onsets[lane]) {
				int x = xOf(ms - shift);
				p.setPen(QPen(QColor(255, 255, 255, 170), 1, Qt::DashLine));
				p.drawLine(x, top + 2, x, top + laneH - 2);
				p.setPen(Qt::white);
				p.drawText(x + 3, top + 13, QString("%1").arg(ms));
			}
		}
		// Claps: how far each one (after the shift) is from the nearest kick.
		if (onsets && !live)
			for (int clap : onsets[1]) {
				int best = 100000;
				for (int kick : onsets[0])
					if (std::abs(clap - shiftMs - kick) < std::abs(best))
						best = clap - shiftMs - kick;
				if (std::abs(best) > 250)
					continue;
				p.setPen(std::abs(best) <= 5 ? QColor(120, 230, 120) : QColor(255, 210, 90));
				p.drawText(xOf(clap - shiftMs) + 3, height() - 4, QString("%1%2 ms").arg(best > 0 ? "+" : "").arg(best));
			}
		if (!live) {
			p.setPen(QColor(150, 150, 160));
			p.drawText(rect().adjusted(0, 0, -6, -4), Qt::AlignRight | Qt::AlignBottom, T("Sync.ViewHint"));
		}
	}

private:
	const std::vector<float> *lanes = nullptr;
	const std::vector<int> *onsets = nullptr;
	bool live = false, wasDone = false;
	int totalMs = 1, shiftMs = 0;
	double viewStart = 0, viewSpan = kVisibleMs, dragX = 0, dragStart = 0;
};

// 8 boxes, one per beat: 1-4 "listen", 5-8 "clap". A box lights when its kick is *heard* in the reference,
// never on a timer, so it can't lead the sound and pull the claps off the music.
class BeatBar : public QWidget {
public:
	explicit BeatBar(QWidget *parent) : QWidget(parent) { setMinimumHeight(86); }

	void setClapMode(bool on)
	{
		clapMode = on;
		update();
	}

	void set(int heard, int claps, bool running)
	{
		loop = heard > 0 ? qMin((heard - 1) / kLoopBeats, kLoops - 1) : 0;
		heard -= loop * kLoopBeats; // boxes show the current loop
		this->heard = heard;
		this->claps = claps;
		this->running = running;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		int gap = 6, cellW = (width() - gap * (kLoopBeats + 1)) / kLoopBeats, cellH = height() - 30;
		QFont big = font();
		big.setPixelSize(cellH / 2);
		big.setBold(true);
		for (int i = 0; i < kLoopBeats; i++) {
			bool clapBeat = clapMode && i >= kLoopBeats / 2;
			bool lit = i < heard;
			bool current = i == heard - 1;
			QRect cell(gap + i * (cellW + gap), 0, cellW, cellH);
			QColor base = !clapMode ? QColor(90, 200, 120) : clapBeat ? QColor(255, 140, 40) : QColor(80, 160, 255);
			p.setPen(Qt::NoPen);
			p.setBrush(lit ? (current ? base.lighter(130) : base.darker(160)) : QColor(50, 50, 58));
			p.drawRoundedRect(cell, 6, 6);
			p.setPen(lit ? Qt::white : QColor(140, 140, 150));
			p.setFont(big);
			p.drawText(cell, Qt::AlignCenter, QString::number(i + 1));
		}
		// The word under each half: what to do in it.
		QFont labelFont = font();
		labelFont.setBold(true);
		p.setFont(labelFont);
		QString loopText = QString("   [%1/%2]").arg(loop + 1).arg(kLoops);
		if (!clapMode) { // earphone on the mic: one label, and how many kicks the mic heard
			p.setPen(QColor(130, 220, 150));
			QString text = T("Sync.Kicks") + loopText;
			if (running)
				text += "   " + T("Sync.MicHeard").arg(claps);
			p.drawText(QRect(0, cellH, width(), 30), Qt::AlignCenter, text);
			return;
		}
		int half = (width() - gap) / 2;
		p.setPen(QColor(120, 180, 255));
		p.drawText(QRect(0, cellH, half, 30), Qt::AlignCenter,
			   T("Sync.Listen") + loopText);
		p.setPen(QColor(255, 160, 70));
		QString clapText = T("Sync.Clap");
		if (running && heard >= kLoopBeats / 2)
			clapText += QString("  (%1)").arg(claps);
		p.drawText(QRect(half, cellH, half, 30), Qt::AlignCenter, clapText);
	}

private:
	int heard = 0, claps = 0, loop = 0;
	bool running = false, clapMode = true;
};

void SyncDialog::run(QWidget *parent, obs_source_t *target)
{
	SyncDialog dlg(parent, target);
	dlg.exec();
}

SyncDialog::SyncDialog(QWidget *parent, obs_source_t *target) : QDialog(parent)
{
	setWindowTitle(T("Sync.Title"));
	referenceBox = new QComboBox(this);
	targetBox = new QComboBox(this);
	OBSSourceAutoRelease desktop = obs_get_output_source(1); // OBS's "Desktop Audio" channel
	struct Ctx {
		SyncDialog *self;
		obs_source_t *target, *desktop;
	} ctx{this, target, desktop.Get()};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *c = static_cast<Ctx *>(param);
			if (!hasAudio(source))
				return true;
			QString name = QString::fromUtf8(obs_source_get_name(source));
			QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
			c->self->referenceBox->addItem(name, uuid);
			c->self->targetBox->addItem(name, uuid);
			if (source == c->desktop)
				c->self->referenceBox->setCurrentIndex(c->self->referenceBox->count() - 1);
			if (source == c->target)
				c->self->targetBox->setCurrentIndex(c->self->targetBox->count() - 1);
			return true;
		},
		&ctx);

	earphoneMode = new QCheckBox(T("Sync.EarphoneMode"), this);
	earphoneMode->setToolTip(T("Sync.EarphoneMode.Tip"));
	earphoneMode->setChecked(PluginMeta::instance().uiValue("syncEarphone").toBool(false));
	playSong = new QCheckBox(T("Sync.PlaySong"), this);
	playSong->setChecked(true);
	auto *help = new QLabel(T("Sync.Help"), this);
	help->setWordWrap(true);
	view = new WaveView(this);
	beats = new BeatBar(this);
	beats->setClapMode(!earphoneMode->isChecked());
	connect(earphoneMode, &QCheckBox::toggled, this, [this](bool on) {
		PluginMeta::instance().setUiValue("syncEarphone", on);
		beats->setClapMode(!on);
	});
	status = new QLabel(this);
	status->setWordWrap(true);
	startButton = new QPushButton(T("Sync.Start"), this);
	manualLag = new QSpinBox(this);
	manualLag->setRange(-1000, 1000);
	manualLag->setSuffix(" ms");
	manualLag->setPrefix(T("Sync.Manual") + " ");
	manualLag->setToolTip(T("Sync.Manual.Tip"));
	manualLag->setVisible(false);
	connect(manualLag, &QSpinBox::valueChanged, this, [this](int ms) {
		lagMs = ms;
		view->setShift(ms);
		OBSSource ref = sourceAt(referenceBox);
		applyButton->setText(T("Sync.Apply").arg((ref ? obs_source_get_sync_offset(ref) : 0) / 1000000 - ms));
		applyButton->setEnabled(true);
	});
	applyButton = new QPushButton(this);
	applyButton->setVisible(false);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

	auto *form = new QFormLayout();
	form->addRow(T("Sync.Reference"), referenceBox);
	form->addRow(T("Sync.Target"), targetBox);
	form->addRow(playSong);
	form->addRow(earphoneMode);
	auto *row = new QHBoxLayout();
	row->addWidget(startButton);
	row->addWidget(manualLag);
	row->addWidget(applyButton);
	row->addStretch(1);
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(help);
	layout->addLayout(form);
	layout->addWidget(beats);
	layout->addWidget(view, 1);
	layout->addWidget(status);
	layout->addLayout(row);
	layout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(startButton, &QPushButton::clicked, this, &SyncDialog::start);
	connect(&timer, &QTimer::timeout, this, &SyncDialog::tick);
	connect(applyButton, &QPushButton::clicked, this, [this] {
		OBSSource ref = sourceAt(referenceBox), tgt = sourceAt(targetBox);
		if (!ref || !tgt)
			return;
		// Same beat must end up at the same time: target_offset = reference_offset - lag.
		obs_source_set_sync_offset(tgt, obs_source_get_sync_offset(ref) - int64_t(lagMs) * 1000000);
		status->setText(T("Sync.Applied").arg(obs_source_get_sync_offset(tgt) / 1000000));
		applyButton->setEnabled(false);
	});
}

SyncDialog::~SyncDialog()
{
	stopCapture();
}

OBSSource SyncDialog::sourceAt(QComboBox *box) const
{
	OBSSourceAutoRelease s = obs_get_source_by_uuid(box->currentData().toString().toUtf8().constData());
	return s.Get();
}

void SyncDialog::onAudio(void *param, obs_source_t *source, const struct audio_data *audio, bool)
{
	auto *self = static_cast<SyncDialog *>(param);
	if (!audio->data[0] || !audio->frames)
		return;
	Chunk c;
	c.lane = source == self->capturing[0].Get() ? 0 : 1;
	c.ts = audio->timestamp;
	// Some sources stamp audio with their own clock; fall back to arrival time if it is far off.
	uint64_t now = os_gettime_ns();
	if ((c.ts > now ? c.ts - now : now - c.ts) > 10000000000ULL)
		c.ts = now;
	auto *s = reinterpret_cast<const float *>(audio->data[0]);
	c.samples.assign(s, s + audio->frames);
	std::lock_guard<std::mutex> guard(self->lock);
	self->pending.push_back(std::move(c));
}

void SyncDialog::start()
{
	OBSSource ref = sourceAt(referenceBox), tgt = sourceAt(targetBox);
	if (!ref || !tgt || ref.Get() == tgt.Get()) {
		status->setText(T("Sync.PickTwo"));
		return;
	}
	stopCapture();
	beats->set(0, 0, true);
	env[0].clear();
	env[1].clear();
	pending.clear();
	applyButton->setVisible(false);
	manualLag->setVisible(false);
	view->setShift(0);
	capturing[0] = ref;
	capturing[1] = tgt;
	t0 = os_gettime_ns();
	obs_source_add_audio_capture_callback(ref, onAudio, this);
	obs_source_add_audio_capture_callback(tgt, onAudio, this);
	startButton->setEnabled(false);
	status->setText(T("Sync.Recording"));
	clock.start();
	timer.start(30);
	if (playSong->isChecked())
		QTimer::singleShot(kStartLeadMs, this, [this] {
			song = makeTestSong();
			PlaySoundW(reinterpret_cast<LPCWSTR>(song.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
		});
}

void SyncDialog::stopCapture()
{
	timer.stop();
	for (int lane = 0; lane < 2; lane++)
		if (capturing[lane]) {
			obs_source_remove_audio_capture_callback(capturing[lane], onAudio, this);
			capturing[lane] = nullptr;
		}
	PlaySoundW(nullptr, nullptr, 0);
}

// Moves captured audio into 1 ms peak envelopes, placed by timestamp so both lanes share one time axis.
void SyncDialog::tick()
{
	std::vector<Chunk> chunks;
	{
		std::lock_guard<std::mutex> guard(lock);
		chunks.swap(pending);
	}
	const double rate = audio_output_get_sample_rate(obs_get_audio()); // capture callbacks get OBS's rate
	for (const Chunk &c : chunks) {
		if (c.ts < t0)
			continue;
		auto &e = env[c.lane];
		double startMs = double(c.ts - t0) / 1e6;
		for (size_t i = 0; i < c.samples.size(); i++) {
			size_t ms = size_t(startMs + 1000.0 * double(i) / rate);
			if (ms >= e.size())
				e.resize(ms + 1, 0.0f);
			e[ms] = std::max(e[ms], std::fabs(c.samples[i]));
		}
	}
	view->set(env, int(clock.elapsed()), true, nullptr);

	// Live cue from what the reference actually played: kicks heard so far, claps since beat 4.
	std::vector<int> heardKicks = vstex::findOnsets(env[0]);
	int heard = qMin(int(heardKicks.size()), kSongBeats);
	int loop = heard > 0 ? qMin((heard - 1) / kLoopBeats, kLoops - 1) : 0;
	int inLoop = heard - loop * kLoopBeats;
	int claps = 0;
	if (earphoneMode->isChecked()) { // every kick counts: show how many the mic heard
		claps = int(vstex::findOnsets(env[1]).size());
		beats->set(heard, claps, true);
		status->setText(heard == 0 ? T("Sync.GetReady") : T("Sync.Earphone.Running"));
		if (clock.elapsed() >= kCaptureMs + kStartLeadMs)
			finish();
		return;
	}
	if (inLoop > kLoopBeats / 2) { // count claps after this loop's 4th kick
		int from = heardKicks[loop * kLoopBeats + kLoopBeats / 2 - 1] + kBeatMs / 2;
		for (int ms : vstex::findOnsets(env[1]))
			claps += ms > from;
	}
	beats->set(heard, claps, true);
	if (heard == 0)
		status->setText(T("Sync.GetReady"));
	else if (inLoop <= kLoopBeats / 2) // claps start on kick 5 of the loop
		status->setText(T("Sync.CountIn").arg(kLoopBeats / 2 + 1 - inLoop));
	else
		status->setText(T("Sync.ClapNow"));
	if (clock.elapsed() >= kCaptureMs + kStartLeadMs)
		finish();
}

void SyncDialog::finish()
{
	stopCapture();
	startButton->setEnabled(true);
	onsets[0] = vstex::findOnsets(env[0]);
	onsets[1] = vstex::findOnsets(env[1]);
	view->set(env, int(qMax(env[0].size(), env[1].size())), false, onsets);

	vstex::AlignResult res = vstex::alignOnsets(onsets[0], onsets[1]);
	if (onsets[0].empty()) {
		status->setText(T("Sync.NoReference"));
		return;
	}
	// Even without a match, the lag can be set by hand while looking at the waveforms.
	status->setText(res.ok ? T("Sync.Result").arg(res.lagMs).arg(res.matched).arg(res.spreadMs)
			       : T("Sync.NoMatch").arg(onsets[1].size()));
	manualLag->setVisible(true);
	applyButton->setVisible(true);
	manualLag->setValue(res.ok ? res.lagMs : 0);
	emit manualLag->valueChanged(manualLag->value()); // refresh the button even if the value did not change
}

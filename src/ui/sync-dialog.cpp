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
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

#include <windows.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <propsys.h>

#ifndef DRV_QUERYFUNCTIONINSTANCEID // from mmddk.h, which the SDK does not always ship
#define DRV_QUERYFUNCTIONINSTANCEID (DRV_RESERVED + 17)
#define DRV_QUERYFUNCTIONINSTANCEIDSIZE (DRV_RESERVED + 18)
#endif

namespace {

constexpr int kBpm = 120;
constexpr int kBeatMs = 60000 / kBpm;
constexpr int kLoopBeats = 8;      // the test song: 2 loops of 8 beats, clapped all the way
constexpr int kLoops = 2;
constexpr int kCountInBeats = 4;   // "tan tan tan (rest)" before the song
constexpr int kBarBeats = 4;
constexpr int kTotalBeats = kCountInBeats + kLoopBeats * kLoops;
constexpr int kCaptureMs = kTotalBeats * kBeatMs + 1200; // count-in + song + margin for late claps
constexpr int kVisibleMs = 3000;   // width of the scrolling view
constexpr int kStartLeadMs = 300;  // record a little before the song starts
constexpr int kSampleRate = 48000; // of the generated test song
constexpr int kSongMuteDb = -61;    // song volume at or below this is silent
constexpr int kSongSliderMaxDb = 0; // the slider stops here; typing goes up to kSongMaxDb
constexpr int kSongMaxDb = 12;

float dbToGain(int db)
{
	return db <= kSongMuteDb ? 0.0f : float(std::pow(10.0, db / 20.0));
}

// Count-in (three wood clicks and a rest), then the test song (see dsp/test-song.h).
std::vector<float> makeSongWithCountIn()
{
	const int beatSamples = kSampleRate * kBeatMs / 1000;
	std::vector<float> mix(size_t(beatSamples) * kCountInBeats, 0.0f);
	for (int beat = 0; beat < kCountInBeats - 1; beat++)
		for (int i = 0; i < kSampleRate / 15; i++) {
			double t = double(i) / kSampleRate;
			mix[size_t(beat) * beatSamples + i] = float(0.7 * std::exp(-t * 60.0) * std::sin(2 * 3.14159265358979 * 1600.0 * t));
		}
	std::vector<float> song = vstex::makeTestSong(kSampleRate, kBeatMs, kLoopBeats, kLoops);
	mix.insert(mix.end(), song.begin(), song.end());
	return mix;
}

} // namespace

// Streams the song to one output through waveOut, four 20 ms buffers ahead, applying the volume as it goes,
// so the slider works while it plays (PlaySound took the volume once, at the start, and only the default output).
class SongPlayer {
public:
	~SongPlayer() { stop(); }

	void setGain(float g) { gain = g; }

	bool start(UINT device, std::vector<float> samples)
	{
		stop();
		song = std::move(samples);
		pos = 0;
		WAVEFORMATEX fmt = {};
		fmt.wFormatTag = WAVE_FORMAT_PCM;
		fmt.nChannels = 1;
		fmt.nSamplesPerSec = kSampleRate;
		fmt.wBitsPerSample = 16;
		fmt.nBlockAlign = 2;
		fmt.nAvgBytesPerSec = kSampleRate * 2;
		event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (waveOutOpen(&out, device, &fmt, (DWORD_PTR)event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
			CloseHandle(event);
			event = nullptr;
			out = nullptr;
			return false;
		}
		stopping = false;
		worker = std::thread([this] { run(); });
		return true;
	}

	void stop()
	{
		if (!out)
			return;
		stopping = true;
		SetEvent(event);
		worker.join();
		waveOutReset(out);
		for (Buffer &b : buffers)
			if (b.header.dwFlags & WHDR_PREPARED)
				waveOutUnprepareHeader(out, &b.header, sizeof(WAVEHDR));
		waveOutClose(out);
		out = nullptr;
		CloseHandle(event);
		event = nullptr;
	}

private:
	static constexpr size_t kFrames = kSampleRate / 50;
	struct Buffer {
		WAVEHDR header = {};
		std::array<int16_t, kFrames> data = {};
	};

	void run()
	{
		for (Buffer &b : buffers)
			b.header = {};
		while (!stopping) {
			bool playing = false;
			for (Buffer &b : buffers) {
				if ((b.header.dwFlags & WHDR_PREPARED) && !(b.header.dwFlags & WHDR_DONE)) {
					playing = true;
					continue;
				}
				if (b.header.dwFlags & WHDR_PREPARED)
					waveOutUnprepareHeader(out, &b.header, sizeof(WAVEHDR));
				b.header = {};
				if (pos >= song.size())
					continue;
				size_t n = std::min(kFrames, song.size() - pos);
				float g = gain;
				for (size_t i = 0; i < n; i++)
					b.data[i] = int16_t(std::lround(std::clamp(song[pos + i] * g, -1.0f, 1.0f) * 32000));
				pos += n;
				b.header.lpData = reinterpret_cast<LPSTR>(b.data.data());
				b.header.dwBufferLength = DWORD(n * sizeof(int16_t));
				waveOutPrepareHeader(out, &b.header, sizeof(WAVEHDR));
				waveOutWrite(out, &b.header, sizeof(WAVEHDR));
				playing = true;
			}
			if (!playing)
				break; // all of it played
			WaitForSingleObject(event, 100);
		}
	}

	HWAVEOUT out = nullptr;
	HANDLE event = nullptr;
	std::thread worker;
	std::atomic<bool> stopping{false};
	std::atomic<float> gain{1.0f};
	std::array<Buffer, 4> buffers;
	std::vector<float> song;
	size_t pos = 0;
};

namespace {

struct OutputDevice {
	UINT id;
	QString name, endpoint; // endpoint: Windows' device id, kept to find the same device next time
};

// waveOut devices with their full names (waveOut itself cuts names at 31 characters).
QVector<OutputDevice> outputDevices()
{
	static const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
	QVector<OutputDevice> list;
	IMMDeviceEnumerator *devices = nullptr;
	CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices));
	for (UINT i = 0; i < waveOutGetNumDevs(); i++) {
		WAVEOUTCAPSW caps = {};
		waveOutGetDevCapsW(i, &caps, sizeof(caps));
		OutputDevice d{i, QString::fromWCharArray(caps.szPname), QString()};
		auto handle = reinterpret_cast<HWAVEOUT>(UINT_PTR(i));
		ULONG size = 0;
		if (waveOutMessage(handle, DRV_QUERYFUNCTIONINSTANCEIDSIZE, DWORD_PTR(&size), 0) == MMSYSERR_NOERROR && size) {
			std::wstring id(size / sizeof(wchar_t), L'\0');
			if (waveOutMessage(handle, DRV_QUERYFUNCTIONINSTANCEID, DWORD_PTR(id.data()), size) == MMSYSERR_NOERROR) {
				d.endpoint = QString::fromWCharArray(id.c_str());
				IMMDevice *dev = nullptr;
				IPropertyStore *props = nullptr;
				if (devices && SUCCEEDED(devices->GetDevice(id.c_str(), &dev)) &&
				    SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
					PROPVARIANT v;
					PropVariantInit(&v);
					if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR)
						d.name = QString::fromWCharArray(v.pwszVal);
					PropVariantClear(&v);
				}
				if (props)
					props->Release();
				if (dev)
					dev->Release();
			}
		}
		list << d;
	}
	if (devices)
		devices->Release();
	return list;
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

// VirtualDJ-style beat counter: four blocks for the beats of the current bar (the first one accented), and a
// row below for where the bar is: count-in, then the song's bars. Driven by what the reference actually played
// (from the first click it heard), never by a timer of its own, so it cannot run ahead of the sound.
class BeatBar : public QWidget {
public:
	explicit BeatBar(QWidget *parent) : QWidget(parent) { setMinimumHeight(96); }

	// beat: 0-based beat heard so far (count-in included), -1 before the first click.
	void set(int beat)
	{
		this->beat = beat;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const int gap = 6, rowH = 16, cellH = height() - rowH - gap;
		const int cellW = (width() - gap * (kBarBeats + 1)) / kBarBeats;
		const bool countIn = beat < kCountInBeats;
		const int bar = beat < 0 ? -1 : beat / kBarBeats, inBar = beat < 0 ? -1 : beat % kBarBeats;
		QFont big = font();
		big.setPixelSize(cellH / 2);
		big.setBold(true);
		p.setFont(big);
		for (int i = 0; i < kBarBeats; i++) {
			QRect cell(gap + i * (cellW + gap), 0, cellW, cellH);
			bool rest = countIn && i == kBarBeats - 1; // "tan tan tan (rest)"
			QColor on = countIn ? QColor(90, 160, 255) : i == 0 ? QColor(255, 90, 60) : QColor(240, 240, 245);
			p.setPen(Qt::NoPen);
			p.setBrush(i == inBar ? (rest ? on.darker(200) : on) : i < inBar ? on.darker(300) : QColor(50, 50, 58));
			p.drawRoundedRect(cell, 6, 6);
			p.setPen(i == inBar && !countIn && i != 0 ? QColor(30, 30, 36) : QColor(200, 200, 210));
			p.drawText(cell, Qt::AlignCenter, rest ? QStringLiteral("-") : QString::number(i + 1));
		}
		// Bar position: count-in, then each bar of the song.
		const int bars = kTotalBeats / kBarBeats;
		const int segW = (width() - gap * (bars + 1)) / bars;
		for (int b = 0; b < bars; b++) {
			QRect seg(gap + b * (segW + gap), height() - rowH, segW, rowH);
			QColor on = b == 0 ? QColor(90, 160, 255) : QColor(255, 170, 60);
			p.setPen(Qt::NoPen);
			p.setBrush(b == bar ? on : b < bar ? on.darker(250) : QColor(50, 50, 58));
			p.drawRoundedRect(seg, 3, 3);
		}
	}

private:
	int beat = -1;
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
	player = std::make_unique<SongPlayer>();
	deviceBox = new QComboBox(this);
	deviceBox->setToolTip(T("Sync.Device.Tip"));
	deviceBox->addItem(T("Sync.Device.Default"), QString());
	deviceBox->setItemData(0, uint(WAVE_MAPPER), Qt::UserRole + 1);
	QString savedDevice = PluginMeta::instance().uiValue("syncSongDevice").toString();
	for (const OutputDevice &d : outputDevices()) {
		deviceBox->addItem(d.name, d.endpoint);
		deviceBox->setItemData(deviceBox->count() - 1, d.id, Qt::UserRole + 1);
		if (!savedDevice.isEmpty() && d.endpoint == savedDevice)
			deviceBox->setCurrentIndex(deviceBox->count() - 1);
	}
	connect(deviceBox, &QComboBox::activated, this, [this] {
		PluginMeta::instance().setUiValue("syncSongDevice", deviceBox->currentData().toString());
	});
	// Song volume: the slider covers -60..0 dB (its left end is silent), the box also takes up to +12 dB.
	songSlider = new QSlider(Qt::Horizontal, this);
	songSlider->setRange(kSongMuteDb, kSongSliderMaxDb);
	songDb = new QSpinBox(this);
	songDb->setRange(kSongMuteDb, kSongMaxDb);
	songDb->setSuffix(" dB");
	songDb->setSpecialValueText(T("Sync.SongMute"));
	songDb->setToolTip(T("Sync.SongVolume.Tip"));
	songSlider->setToolTip(T("Sync.SongVolume.Tip"));
	connect(songSlider, &QSlider::valueChanged, songDb, &QSpinBox::setValue);
	connect(songDb, &QSpinBox::valueChanged, this, [this](int db) {
		QSignalBlocker block(songSlider); // a typed +dB value parks the slider at 0 without lowering the box
		songSlider->setValue(db);
		player->setGain(dbToGain(db)); // also while the song plays
		PluginMeta::instance().setUiValue("syncSongDb", db);
	});
	// A boost typed last time is not brought back: a forgotten +12 dB would blast the next session.
	songDb->setValue(qMin(PluginMeta::instance().uiValue("syncSongDb").toInt(-6), kSongSliderMaxDb));
	songSlider->setValue(songDb->value());
	connect(playSong, &QCheckBox::toggled, songSlider, &QWidget::setEnabled);
	connect(playSong, &QCheckBox::toggled, songDb, &QWidget::setEnabled);
	connect(playSong, &QCheckBox::toggled, deviceBox, &QWidget::setEnabled);
	auto *help = new QLabel(T("Sync.Help"), this);
	help->setWordWrap(true);
	view = new WaveView(this);
	beats = new BeatBar(this);
	connect(earphoneMode, &QCheckBox::toggled, this,
		[](bool on) { PluginMeta::instance().setUiValue("syncEarphone", on); });
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
	form->addRow(T("Sync.Device"), deviceBox);
	auto *volumeRow = new QHBoxLayout();
	volumeRow->addWidget(songSlider, 1);
	volumeRow->addWidget(songDb);
	form->addRow(T("Sync.SongVolume"), volumeRow);
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
	beats->set(-1);
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
	status->setText(T("Sync.GetReady"));
	clock.start();
	timer.start(30);
	if (playSong->isChecked())
		QTimer::singleShot(kStartLeadMs, this, [this] {
			player->setGain(dbToGain(songDb->value()));
			if (!player->start(deviceBox->currentData(Qt::UserRole + 1).toUInt(), makeSongWithCountIn()))
				status->setText(T("Sync.DeviceFailed"));
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
	player->stop();
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

	// Live cue from what the reference actually played: the beat since the first click it heard.
	std::vector<int> heard = vstex::findOnsets(env[0]);
	int beat = heard.empty() ? -1 : (int(env[0].size()) - heard.front() + kBeatMs / 4) / kBeatMs;
	beats->set(qMin(beat, kTotalBeats - 1));
	if (beat < 0)
		status->setText(T("Sync.GetReady"));
	else
		status->setText(earphoneMode->isChecked() ? T("Sync.Earphone.Running") : T("Sync.ClapNow"));
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

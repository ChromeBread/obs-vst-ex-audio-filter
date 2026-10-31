#include "ui/waveline-dock.h"

#include "dsp/beat-grid.h"
#include "dsp/peak-ring.h"
#include "master-bus.h"
#include "plugin-meta.h"
#include "ui/ui-text.h"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <vector>

namespace {

constexpr const char *kDockId = "vst-ex-waveline-dock";
constexpr const char *kMasterId = "master"; // stands for the master bus in the saved list
constexpr uint64_t kBinNs = 5000000;        // 5 ms; PeakRing::kBins of these is about 80 s of history
constexpr int kFrameMs = 33;
constexpr int kSyncFrames = 15; // sources and routing are checked every half second
constexpr uint64_t kHeadNs = 60000000; // audio arrives a little after "now"; keeps the right edge from flickering
constexpr double kWidestSeconds = 60.0, kNarrowestSeconds = 2.0;
constexpr float kFloorDb = -60.0f;
constexpr int kFaintAlpha = 70;
constexpr int kControlHeight = 14; // two rows of these set how thin the dock can get
constexpr double kDefaultBpm = 120.0;
constexpr int kMinOpacity = 10; // a source at 0% could not be found again on screen

const QColor kPalette[] = {{255, 76, 76},  {0, 188, 212},  {255, 193, 7},  {156, 204, 101},
			   {186, 104, 200}, {255, 138, 101}, {100, 181, 246}, {240, 98, 146}};

double spanSeconds(int zoomPercent)
{
	return kWidestSeconds * std::pow(kNarrowestSeconds / kWidestSeconds, zoomPercent / 100.0);
}

struct Track {
	QString id;
	OBSWeakSource weak;
	PeakRing ring;
	QString name;
	bool hooked = false;
	bool onAir = false;
	int color = 0;
	int opacity = 100; // percent
};

void onAudio(void *param, obs_source_t *source, const struct audio_data *audio, bool)
{
	auto *track = static_cast<Track *>(param);
	const struct audio_output_info *info = audio_output_get_info(obs_get_audio());
	size_t planes = audio_output_get_channels(obs_get_audio());
	uint32_t binFrames = std::max<uint32_t>(1, uint32_t(uint64_t(info->samples_per_sec) * kBinNs / 1000000000ULL));
	float volume = obs_source_get_volume(source);
	for (uint32_t start = 0; start < audio->frames; start += binFrames) {
		uint32_t end = std::min(audio->frames, start + binFrames);
		float peak = 0.0f;
		for (size_t c = 0; c < planes; c++) {
			auto *data = reinterpret_cast<const float *>(audio->data[c]);
			if (!data)
				continue;
			for (uint32_t i = start; i < end; i++)
				peak = std::max(peak, std::fabs(data[i]));
		}
		uint64_t at = audio->timestamp + uint64_t(start) * 1000000000ULL / info->samples_per_sec;
		track->ring.put(at / kBinNs, peak * volume);
	}
}

class WaveView : public QWidget {
public:
	WaveView(QWidget *parent, const std::vector<Track *> &list) : QWidget(parent), tracks(list)
	{
		setMinimumSize(40, 16);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	int zoom = 25;
	double bpm = 0.0; // 0: no beat dots
	int64_t beatAnchor = 0;

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.fillRect(rect(), Qt::black);
		int w = width(), base = height() - 6, top = 2;
		p.setPen(QColor(70, 70, 70));
		p.drawLine(0, base, w, base);
		double binsPerPixel = spanSeconds(zoom) * 1e9 / double(kBinNs) / double(w);
		auto nowNs = int64_t(os_gettime_ns() - kHeadNs);
		// Columns are fixed to absolute time, so old peaks keep their shape while they scroll.
		int64_t newest = int64_t(std::floor(double(nowNs / int64_t(kBinNs)) / binsPerPixel));
		if (bpm > 0.0)
			drawBeats(p, nowNs, newest, binsPerPixel, base);
		if (tracks.empty()) {
			p.setPen(QColor(150, 150, 150));
			p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, T("Waveline.Empty"));
			return;
		}
		QVector<QLine> lines;
		lines.reserve(w);
		for (Track *track : tracks) { // the list is the layer order, bottom first
			lines.clear();
			for (int x = w - 1; x >= 0; x--) {
				int64_t column = newest - (w - 1 - x);
				auto from = uint64_t(std::ceil(double(column) * binsPerPixel));
				auto to = std::max(from + 1, uint64_t(std::ceil(double(column + 1) * binsPerPixel)));
				float peak = 0.0f;
				for (uint64_t b = from; b < to; b++)
					peak = std::max(peak, track->ring.get(b));
				if (peak <= 0.0f)
					continue;
				float db = 20.0f * std::log10(peak);
				float height01 = std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
				if (height01 > 0.0f)
					lines.push_back(QLine(x, base, x, base - int(height01 * float(base - top))));
			}
			QColor color = kPalette[track->color % std::size(kPalette)];
			color.setAlpha((track->onAir ? 255 : kFaintAlpha) * track->opacity / 100);
			p.setPen(color);
			p.drawLines(lines);
		}
		// Source names in their colours.
		QFont f = font();
		f.setPixelSize(10);
		p.setFont(f);
		int x = 4;
		for (Track *track : tracks) {
			if (x >= w)
				break;
			QColor color = kPalette[track->color % std::size(kPalette)];
			color.setAlpha(track->onAir ? 255 : 120);
			p.setPen(color);
			p.drawText(x, 11, track->name);
			x += QFontMetrics(f).horizontalAdvance(track->name) + 12;
		}
	}

private:
	// Dots in the strip under the baseline, where no waveform can cover them; a bigger one every 4 beats.
	void drawBeats(QPainter &p, int64_t nowNs, int64_t newest, double binsPerPixel, int base)
	{
		auto beatNs = int64_t(60e9 / bpm);
		for (int64_t beat = beat_at_or_before(nowNs, beatAnchor, beatNs);; beat--) {
			double bin = double(beatAnchor + beat * beatNs) / double(kBinNs);
			int x = width() - 1 - int(newest - int64_t(std::floor(bin / binsPerPixel)));
			if (x < 0)
				break;
			if (beat_starts_bar(beat))
				p.fillRect(x - 1, base + 1, 4, 4, QColor(255, 255, 255));
			else
				p.fillRect(x, base + 2, 2, 2, QColor(170, 170, 170));
		}
	}

	const std::vector<Track *> &tracks;
};

// Sound of several sources side by side in time. Clicking Tr buttons changes what is shown strong, never the routing.
class WavelineDock : public QWidget {
public:
	WavelineDock() : QWidget(nullptr)
	{
		auto &meta = PluginMeta::instance();
		for (const auto &v : meta.uiValue("wavelineSources").toArray())
			wanted << v.toString();
		styles = meta.uiValue("wavelineStyles").toObject();

		auto *zoom = new QSlider(Qt::Vertical, this);
		zoom->setMinimumHeight(10);
		zoom->setRange(0, 100);
		zoom->setValue(std::clamp(meta.uiValue("wavelineZoom").toInt(25), 0, 100));
		zoom->setToolTip(T("Waveline.Zoom.Tip"));
		zoomLabel = new QLabel(this);
		zoomLabel->setAlignment(Qt::AlignCenter);
		QFont small = zoomLabel->font();
		small.setPixelSize(10);
		zoomLabel->setFont(small);
		zoomLabel->setFixedWidth(30);
		view = new WaveView(this, tracks);
		view->zoom = zoom->value();
		connect(zoom, &QSlider::valueChanged, this, [this](int v) {
			PluginMeta::instance().setUiValue("wavelineZoom", v);
			view->zoom = v;
			showZoom(v);
		});
		showZoom(zoom->value());

		auto *pick = new QToolButton(this);
		pick->setText(QStringLiteral("+"));
		pick->setToolTip(T("Waveline.Sources"));
		pick->setPopupMode(QToolButton::InstantPopup);
		auto *menu = new QMenu(pick);
		pick->setMenu(menu);
		connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillMenu(menu); });

		auto compact = [](QToolButton *b, int width) {
			b->setFixedSize(width, kControlHeight);
			b->setStyleSheet("QToolButton{padding:0;font-size:10px;}QToolButton::menu-indicator{image:none;}"
					 "QToolButton:checked{background:#2e7d32;color:white;}");
		};
		compact(pick, 18);
		auto *layersButton = new QToolButton(this);
		layersButton->setText(QStringLiteral("≡"));
		layersButton->setToolTip(T("Waveline.Layers"));
		compact(layersButton, 18);
		connect(layersButton, &QToolButton::clicked, this, [this] { showLayers(); });

		QJsonValue saved = meta.uiValue("wavelineTracks");
		followStream = saved.isUndefined();
		mask = followStream ? 0 : uint32_t(saved.toInt());
		auto *controls = new QGridLayout();
		controls->setSpacing(1);
		controls->addWidget(pick, 0, 0);
		controls->addWidget(layersButton, 1, 0);
		for (int t = 0; t < 6; t++) {
			auto *b = new QToolButton(this);
			b->setText(QStringLiteral("Tr%1").arg(t + 1));
			b->setToolTip(T("Waveline.Track.Tip").arg(t + 1));
			b->setCheckable(true);
			compact(b, 24);
			connect(b, &QToolButton::clicked, this, [this] { readButtons(); });
			trackButtons[t] = b;
			controls->addWidget(b, t / 3, 1 + t % 3);
		}

		auto *beats = new QToolButton(this);
		beats->setText(QStringLiteral("●"));
		beats->setToolTip(T("Waveline.Beat.Tip"));
		beats->setCheckable(true);
		compact(beats, 40);
		auto *bpm = new QDoubleSpinBox(this);
		bpm->setRange(20.0, 300.0);
		bpm->setDecimals(1);
		bpm->setValue(meta.uiValue("wavelineBpm").toDouble(kDefaultBpm));
		bpm->setToolTip(T("Waveline.Bpm.Tip"));
		bpm->setButtonSymbols(QAbstractSpinBox::NoButtons);
		bpm->setKeyboardTracking(false); // typing "128" must not jump through 1 and 12
		bpm->setFixedSize(40, kControlHeight);
		bpm->setStyleSheet("QDoubleSpinBox{padding:0;margin:0;border:0;font-size:10px;min-height:0;}");
		controls->addWidget(beats, 0, 4);
		controls->addWidget(bpm, 1, 4);
		connect(bpm, &QDoubleSpinBox::valueChanged, this, [this](double v) {
			PluginMeta::instance().setUiValue("wavelineBpm", v);
			if (view->bpm > 0.0)
				view->bpm = v;
		});
		// The click sets the first beat, so pressing it on a beat lines the dots up with the music.
		connect(beats, &QToolButton::toggled, this, [this, bpm](bool on) {
			PluginMeta::instance().setUiValue("wavelineBeats", on);
			view->bpm = on ? bpm->value() : 0.0;
			view->beatAnchor = int64_t(os_gettime_ns());
		});
		beats->setChecked(meta.uiValue("wavelineBeats").toBool(false));

		auto *left = new QHBoxLayout();
		left->setSpacing(1);
		left->addWidget(zoom);
		left->addWidget(zoomLabel);
		auto *right = new QVBoxLayout();
		right->setSpacing(0);
		right->addLayout(controls);
		right->addStretch(1);
		auto *layout = new QHBoxLayout(this);
		layout->setContentsMargins(1, 1, 1, 1);
		layout->setSpacing(3);
		layout->addLayout(left);
		layout->addWidget(view, 1);
		layout->addLayout(right);

		sync();
		connect(&timer, &QTimer::timeout, this, &WavelineDock::frame);
		timer.start(kFrameMs);
	}

	~WavelineDock() override
	{
		for (auto &track : owned)
			unhook(*track);
	}

private:
	void showZoom(int v)
	{
		zoomLabel->setText(QString::number(v) + "%");
		zoomLabel->setToolTip(T("Waveline.Span").arg(spanSeconds(v), 0, 'f', 1));
	}

	void readButtons()
	{
		mask = 0;
		for (int t = 0; t < 6; t++)
			if (trackButtons[t]->isChecked())
				mask |= 1u << t;
		followStream = false; // from now on the user's choice stays
		PluginMeta::instance().setUiValue("wavelineTracks", int(mask));
		sync();
	}

	void frame()
	{
		if (!isVisible())
			return;
		if (++frames >= kSyncFrames) {
			frames = 0;
			sync();
		}
		view->update();
	}

	OBSSource resolve(const QString &id)
	{
		if (id == kMasterId)
			return masterbus::source();
		OBSSourceAutoRelease s = obs_get_source_by_uuid(id.toUtf8().constData());
		return s.Get();
	}

	void unhook(Track &track)
	{
		if (!track.hooked)
			return;
		track.hooked = false;
		OBSSourceAutoRelease s = obs_weak_source_get_source(track.weak);
		if (s)
			obs_source_remove_audio_capture_callback(s, onAudio, &track);
	}

	Track *trackFor(const QString &id)
	{
		for (auto &t : owned)
			if (t->id == id)
				return t.get();
		auto track = std::make_unique<Track>();
		track->id = id;
		QJsonObject style = styles[id].toObject();
		track->color = style["color"].toInt(freeColor());
		track->opacity = std::clamp(style["opacity"].toInt(100), kMinOpacity, 100);
		owned.push_back(std::move(track));
		saveStyle(*owned.back());
		return owned.back().get();
	}

	// The colour stays with the source when it changes layer.
	int freeColor() const
	{
		for (int c = 0; c < int(std::size(kPalette)); c++)
			if (std::none_of(owned.begin(), owned.end(), [&](const auto &t) { return t->color == c && wanted.contains(t->id); }))
				return c;
		return int(owned.size());
	}

	void saveStyle(const Track &track)
	{
		styles[track.id] = QJsonObject{{"color", track.color}, {"opacity", track.opacity}};
		PluginMeta::instance().setUiValue("wavelineStyles", styles);
	}

	void saveSources()
	{
		PluginMeta::instance().setUiValue("wavelineSources", QJsonArray::fromStringList(wanted));
		sync();
		fillLayers();
	}

	void showLayers()
	{
		if (!layers) {
			layers = new QDialog(this);
			layers->setWindowTitle(T("Waveline.Layers"));
			layers->resize(360, 10);
		}
		fillLayers();
		layers->show();
		layers->raise();
	}

	// Top layer first, the way it looks on screen.
	void fillLayers()
	{
		if (!layers)
			return;
		delete layers->layout();
		qDeleteAll(layers->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
		auto *grid = new QGridLayout(layers);
		grid->setColumnStretch(1, 1);
		grid->setColumnStretch(2, 2);
		if (tracks.empty())
			grid->addWidget(new QLabel(T("Waveline.Empty"), layers), 0, 0);
		for (int row = 0; row < int(tracks.size()); row++) {
			int index = int(tracks.size()) - 1 - row;
			Track *track = tracks[index];
			auto *swatch = new QLabel(layers);
			swatch->setFixedSize(12, 12);
			swatch->setStyleSheet("background:" + kPalette[track->color % std::size(kPalette)].name() + ";");
			auto *opacity = new QSlider(Qt::Horizontal, layers);
			opacity->setRange(kMinOpacity, 100);
			opacity->setValue(track->opacity);
			opacity->setToolTip(T("Waveline.Opacity"));
			auto *percent = new QLabel(QString::number(track->opacity) + "%", layers);
			percent->setFixedWidth(36);
			connect(opacity, &QSlider::valueChanged, this, [this, track, percent](int v) {
				track->opacity = v;
				percent->setText(QString::number(v) + "%");
				saveStyle(*track);
			});
			auto *up = new QToolButton(layers);
			up->setText(QStringLiteral("▲"));
			up->setEnabled(row > 0);
			auto *down = new QToolButton(layers);
			down->setText(QStringLiteral("▼"));
			down->setEnabled(index > 0);
			connect(up, &QToolButton::clicked, this, [this, index] { moveLayer(index, index + 1); });
			connect(down, &QToolButton::clicked, this, [this, index] { moveLayer(index, index - 1); });
			grid->addWidget(swatch, row, 0);
			grid->addWidget(new QLabel(track->name, layers), row, 1);
			grid->addWidget(opacity, row, 2);
			grid->addWidget(percent, row, 3);
			grid->addWidget(up, row, 4);
			grid->addWidget(down, row, 5);
		}
	}

	// Deferred: the clicked button is deleted when the rows are rebuilt.
	void moveLayer(int from, int to)
	{
		QTimer::singleShot(0, this, [this, from, to] {
			if (from < 0 || to < 0 || from >= int(tracks.size()) || to >= int(tracks.size()))
				return;
			wanted.swapItemsAt(wanted.indexOf(tracks[from]->id), wanted.indexOf(tracks[to]->id));
			saveSources();
		});
	}

	// Hooks what is wanted and exists. Track objects live as long as the dock: a capture callback may still be
	// running on an audio thread at the moment its source goes away.
	void sync()
	{
		if (followStream)
			mask = 1u << (masterbus::onAirTrack() - 1);
		for (int t = 0; t < 6; t++)
			trackButtons[t]->setChecked(mask & (1u << t));

		tracks.clear();
		for (auto &t : owned)
			if (!wanted.contains(t->id))
				unhook(*t);
		for (const QString &id : wanted) {
			OBSSource source = resolve(id);
			if (!source)
				continue;
			Track *track = trackFor(id);
			OBSSourceAutoRelease hookedTo = obs_weak_source_get_source(track->weak);
			if (track->hooked && hookedTo.Get() != source.Get())
				unhook(*track);
			if (!track->hooked) {
				track->weak = OBSGetWeakRef(source);
				obs_source_add_audio_capture_callback(source, onAudio, track);
				track->hooked = true;
			}
			track->name = QString::fromUtf8(obs_source_get_name(source));
			bool routed = obs_source_get_audio_mixers(source) & mask;
			if (id == kMasterId)
				track->onAir = masterbus::onStream() && !obs_source_muted(source);
			else
				track->onAir = routed && !obs_source_muted(source) && obs_source_active(source);
			tracks.push_back(track);
		}
	}

	void fillMenu(QMenu *menu)
	{
		menu->clear();
		auto add = [this, menu](const QString &id, const QString &name) {
			QAction *a = menu->addAction(name);
			a->setCheckable(true);
			a->setChecked(wanted.contains(id));
			connect(a, &QAction::toggled, this, [this, id](bool on) {
				if (on)
					wanted << id;
				else
					wanted.removeAll(id);
				saveSources();
			});
		};
		if (masterbus::source()) {
			add(kMasterId, QStringLiteral("VST Ex Master"));
			menu->addSeparator();
		}
		std::vector<OBSSource> sources;
		obs_enum_sources(
			[](void *p, obs_source_t *s) {
				if (obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO)
					static_cast<std::vector<OBSSource> *>(p)->emplace_back(s);
				return true;
			},
			&sources);
		for (const OBSSource &s : sources)
			add(QString::fromUtf8(obs_source_get_uuid(s)), QString::fromUtf8(obs_source_get_name(s)));
	}

	WaveView *view;
	QLabel *zoomLabel;
	QToolButton *trackButtons[6];
	QStringList wanted; // shown sources, by uuid; the order is the layer order, bottom first
	QJsonObject styles; // colour and opacity by source, kept when a source is hidden
	QPointer<QDialog> layers;
	std::deque<std::unique_ptr<Track>> owned;
	std::vector<Track *> tracks; // those showing now
	uint32_t mask = 0;
	bool followStream = true; // Tr buttons follow the stream until the user clicks one
	int frames = 0;
	QTimer timer;
};

} // namespace

void install_waveline_dock()
{
	obs_frontend_add_dock_by_id(kDockId, "VST Ex WaveLine", new WavelineDock());
}

void remove_waveline_dock()
{
	obs_frontend_remove_dock(kDockId);
}

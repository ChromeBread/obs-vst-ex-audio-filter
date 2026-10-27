#include "ui/chain-view.h"

#include "dsp/polar.h"

#include "ui/ex-filters-dialog.h"
#include "ui/ui-text.h"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QHash>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kBoxW = 160, kBoxH = 52, kGap = 26, kCurveRoom = 20, kMargin = 12; // box size at 100%

enum class Kind { Device, Filter, Audio, Output };

constexpr size_t kImagerPoints = 600; // recent points drawn per source
constexpr uint32_t kImagerStep = 16;  // use every 16th sample: plenty for the eye, cheap

struct Node {
	Kind kind;
	QString title, subtitle, extra;
	bool enabled = true;
	QString sidechain; // name of the source this filter listens to, if any
};

struct Row {
	OBSWeakSource source;
	QString uuid, name;
	std::vector<Node> nodes;
};

// Level of one source, written on the audio thread by its volmeter.
struct Meter {
	obs_volmeter_t *volmeter = nullptr;
	std::atomic<float> peakDb{-100.0f};

	static void onLevels(void *param, const float *, const float peak[MAX_AUDIO_CHANNELS], const float *)
	{
		float p = -100.0f;
		for (int c = 0; c < MAX_AUDIO_CHANNELS; c++)
			p = std::max(p, peak[c]);
		static_cast<Meter *>(param)->peakDb = p;
	}

	// Imager points (x -1..1, y 0..1), written by the capture callback only while the imager is on.
	std::mutex pointsLock;
	std::vector<QPointF> points = std::vector<QPointF>(kImagerPoints);
	size_t head = 0;
	OBSWeakSource weak;
	bool imaging = false;

	static void onAudio(void *param, obs_source_t *, const struct audio_data *audio, bool)
	{
		auto *m = static_cast<Meter *>(param);
		auto *l = reinterpret_cast<const float *>(audio->data[0]);
		auto *r = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : l; // mono: centre
		if (!l)
			return;
		std::lock_guard<std::mutex> guard(m->pointsLock);
		for (uint32_t i = 0; i < audio->frames; i += kImagerStep) {
			float x, y;
			vstex::polarPoint(l[i], r[i], x, y);
			m->points[m->head] = QPointF(x, y);
			m->head = (m->head + 1) % m->points.size();
		}
	}

	void setImager(bool on)
	{
		OBSSourceAutoRelease source = obs_weak_source_get_source(weak);
		if (on == imaging || !source)
			return;
		if (on)
			obs_source_add_audio_capture_callback(source, onAudio, this);
		else
			obs_source_remove_audio_capture_callback(source, onAudio, this);
		imaging = on;
	}

	explicit Meter(obs_source_t *source)
	{
		weak = OBSGetWeakRef(source);
		volmeter = obs_volmeter_create(OBS_FADER_LOG);
		obs_volmeter_attach_source(volmeter, source);
		obs_volmeter_add_callback(volmeter, onLevels, this);
	}
	~Meter()
	{
		setImager(false);
		obs_volmeter_remove_callback(volmeter, onLevels, this);
		obs_volmeter_detach_source(volmeter);
		obs_volmeter_destroy(volmeter);
	}
};

double msOf(int samples)
{
	return 1000.0 * samples / audio_output_get_sample_rate(obs_get_audio());
}

class ChainCanvas : public QWidget {
public:
	// Text size, box width and box height (1.0 = 100%). With fullTitles the boxes widen so every bold
	// name shows in full; without, bold names are shortened to the box width like the other lines.
	void setLook(double text, bool full, double width, double height)
	{
		textScale = text;
		fullTitles = full;
		widthScale = width;
		heightScale = height;
		relayout();
	}

	// Imager on the left of every row (so rows line up for comparing), or after the output box.
	void setImagerRight(bool on)
	{
		imagerRight = on;
		relayout();
	}

	// Half-circle imager per row; reads audio only while on.
	void setImager(bool on)
	{
		imager = on;
		for (auto &[uuid, meter] : meters)
			meter->setImager(on);
		relayout();
	}

	ChainCanvas()
	{
		connect(&modelTimer, &QTimer::timeout, this, &ChainCanvas::rebuild);
		connect(&paintTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
		modelTimer.start(1000);
		paintTimer.start(50); // level meters
		rebuild();
	}

protected:
	void mouseDoubleClickEvent(QMouseEvent *e) override
	{
		int row = (int(e->position().y()) - kMargin) / rowH;
		if (row < 0 || row >= int(rows.size()))
			return;
		OBSSourceAutoRelease source = obs_weak_source_get_source(rows[row].source);
		if (source)
			ExFiltersDialog::open(source);
	}

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(rect(), QColor(0x1b, 0x1b, 0x22));
		if (rows.empty()) {
			p.setPen(QColor(150, 150, 160));
			p.drawText(rect(), Qt::AlignCenter, T("Chain.Empty"));
			return;
		}
		QHash<QString, QPoint> outputAnchor; // source name -> bottom of its output box
		for (size_t r = 0; r < rows.size(); r++)
			outputAnchor[rows[r].name] = QPoint(boxX(int(rows[r].nodes.size()) - 1) + boxW / 2, rowY(int(r)) + header + boxH);

		for (size_t r = 0; r < rows.size(); r++) {
			const Row &row = rows[r];
			int y = rowY(int(r));
			p.setFont(titleFont());
			p.setPen(Qt::white);
			p.drawText(kMargin, y + QFontMetrics(titleFont()).ascent() + 2, row.name);
			for (size_t i = 0; i < row.nodes.size(); i++) {
				const Node &n = row.nodes[i];
				QRect box(boxX(int(i)), y + header, boxW, boxH);
				if (i > 0) { // arrow from the previous box
					int ay = box.center().y();
					p.setPen(QPen(QColor(120, 120, 135), 2));
					p.drawLine(box.left() - gap + 2, ay, box.left() - 4, ay);
					p.drawLine(box.left() - 9, ay - 4, box.left() - 4, ay);
					p.drawLine(box.left() - 9, ay + 4, box.left() - 4, ay);
				}
				drawNode(p, box, n, row);
			}
			if (imager)
				drawImager(p, QRect(imagerRight ? boxX(int(row.nodes.size())) : kMargin, y + header, 2 * boxH, boxH), row);
		}
		// Sidechain / ducking: a dashed curve from the controlling source's output to the filter that listens.
		for (size_t r = 0; r < rows.size(); r++)
			for (size_t i = 0; i < rows[r].nodes.size(); i++) {
				const QString &from = rows[r].nodes[i].sidechain;
				if (from.isEmpty() || !outputAnchor.contains(from))
					continue;
				QPoint a = outputAnchor[from], b(boxX(int(i)) + boxW / 2, rowY(int(r)) + header);
				QPainterPath path(a);
				path.cubicTo(a + QPoint(0, 40), b - QPoint(0, 40), b);
				p.setPen(QPen(QColor(255, 170, 60), 2, Qt::DashLine));
				p.setBrush(Qt::NoBrush);
				p.drawPath(path);
				p.drawText(b + QPoint(6, -6), T("Chain.Sidechain"));
			}
	}

private:
	int imagerWidth() const { return imager ? 2 * boxH + gap : 0; }
	int boxX(int i) const { return kMargin + (imagerRight ? 0 : imagerWidth()) + i * (boxW + gap); }
	int rowY(int r) const { return kMargin + r * rowH; }

	QFont titleFont() const
	{
		QFont f = font();
		f.setPointSizeF(f.pointSizeF() * textScale);
		f.setBold(true);
		return f;
	}
	QFont smallFont() const
	{
		QFont f = font();
		f.setPointSizeF(f.pointSizeF() * textScale * 0.9);
		return f;
	}

	// One box size for all, so columns line up: the scaled base size, or wider/taller if a name needs it.
	void relayout()
	{
		QFontMetrics tm(titleFont()), sm(smallFont());
		int titleW = 0;
		if (fullTitles)
			for (const Row &row : rows)
				for (const Node &n : row.nodes)
					titleW = std::max(titleW, tm.horizontalAdvance(n.title));
		boxW = std::max(int(kBoxW * widthScale), titleW + 14);
		// Never shorter than the three lines of text, or they would spill out of the box.
		boxH = std::max(int(kBoxH * heightScale), tm.height() + 2 * sm.height() + 10);
		header = tm.height() + 4;
		rowH = header + boxH + kCurveRoom + tm.height();
		size_t widest = 0;
		for (const Row &row : rows)
			widest = std::max(widest, row.nodes.size());
		int rowNameW = 0;
		for (const Row &row : rows)
			rowNameW = std::max(rowNameW, tm.horizontalAdvance(row.name));
		setMinimumSize(std::max(boxX(int(widest)) + (imagerRight ? imagerWidth() : 0), rowNameW + 2 * kMargin) + kMargin,
			       std::max(120, rowY(int(rows.size())) + kMargin));
		update();
	}

	void drawNode(QPainter &p, const QRect &box, const Node &n, const Row &row)
	{
		static const QColor fills[] = {QColor(40, 70, 120), QColor(45, 80, 55), QColor(95, 80, 35), QColor(80, 50, 110)};
		QColor fill = fills[int(n.kind)];
		if (!n.enabled)
			fill = QColor(55, 55, 60);
		p.setPen(QPen(n.enabled ? fill.lighter(160) : QColor(90, 90, 95), 1, n.enabled ? Qt::SolidLine : Qt::DashLine));
		p.setBrush(fill);
		p.drawRoundedRect(box, 6, 6);

		QRect text = box.adjusted(7, 4, -7, -4);
		QFontMetrics fm(titleFont()), sm(smallFont());
		p.setFont(titleFont());
		p.setPen(n.enabled ? Qt::white : QColor(150, 150, 155));
		p.drawText(text.left(), text.top() + fm.ascent(),
			   fullTitles ? n.title : fm.elidedText(n.title, Qt::ElideRight, text.width()));
		p.setFont(smallFont());
		p.setPen(n.enabled ? QColor(210, 210, 220) : QColor(130, 130, 135));
		p.drawText(text.left(), text.top() + fm.height() + sm.ascent(), sm.elidedText(n.subtitle, Qt::ElideRight, text.width()));
		if (!n.extra.isEmpty()) {
			p.setPen(n.kind == Kind::Filter ? QColor(240, 200, 90) : QColor(230, 230, 240));
			p.drawText(text.left(), text.top() + fm.height() + sm.height() + sm.ascent(),
				   sm.elidedText(n.extra, Qt::ElideRight, text.width()));
		}
		if (n.kind == Kind::Output) { // level meter along the bottom edge
			auto it = meters.find(row.uuid);
			float db = it != meters.end() ? it->second->peakDb.load() : -100.0f;
			float level = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
			QRect bar(box.left() + 4, box.bottom() - 5, int((box.width() - 8) * level), 3);
			p.fillRect(bar, db > -3 ? QColor(230, 70, 60) : db > -12 ? QColor(230, 200, 60) : QColor(80, 210, 110));
		}
	}

	// Half circle: centre line = mono, left/right edges of the arc = hard left/right, flat edge = out of phase.
	void drawImager(QPainter &p, const QRect &box, const Row &row)
	{
		QPointF base(box.center().x(), box.bottom());
		double radius = box.height() - 4;
		p.setPen(QPen(QColor(90, 90, 105), 1));
		p.setBrush(QColor(30, 30, 38));
		p.drawPie(QRectF(base.x() - radius, base.y() - radius, 2 * radius, 2 * radius), 0, 180 * 16);
		p.drawLine(base, base - QPointF(0, radius));
		p.setPen(QColor(150, 150, 160));
		p.drawText(QPointF(box.left(), box.bottom() - 2), "L");
		p.drawText(QPointF(box.right() - 8, box.bottom() - 2), "R");

		auto it = meters.find(row.uuid);
		if (it == meters.end())
			return;
		std::vector<QPointF> pts;
		{
			std::lock_guard<std::mutex> guard(it->second->pointsLock);
			pts = it->second->points;
		}
		// Normalise to the loudest recent point so quiet sources still fill the fan.
		double peak = 0.05;
		for (const QPointF &pt : pts)
			peak = std::max(peak, std::hypot(pt.x(), pt.y()));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(120, 220, 255, 140));
		for (const QPointF &pt : pts)
			p.drawEllipse(base + QPointF(pt.x(), -pt.y()) * (radius / peak), 1.2, 1.2);
	}

	// Reads the chain of every audio source. Cheap except device names, which are cached.
	void rebuild()
	{
		std::vector<Row> next;
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				if (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)
					static_cast<ChainCanvas *>(param)->addRow(source);
				return true;
			},
			this);
		next.swap(building);

		// Meters follow the rows: keep the ones still needed, add new ones, drop the rest.
		std::unordered_map<QString, std::unique_ptr<Meter>> keep;
		for (const Row &row : next) {
			auto it = meters.find(row.uuid);
			if (it != meters.end())
				keep[row.uuid] = std::move(it->second);
			else if (OBSSourceAutoRelease s = obs_weak_source_get_source(row.source)) {
				keep[row.uuid] = std::make_unique<Meter>(s);
				keep[row.uuid]->setImager(imager);
			}
		}
		meters.swap(keep);

		rows.swap(next);
		relayout();
	}

	void addRow(obs_source_t *source)
	{
		Row row;
		row.source = OBSGetWeakRef(source);
		row.uuid = QString::fromUtf8(obs_source_get_uuid(source));
		row.name = QString::fromUtf8(obs_source_get_name(source));

		row.nodes.push_back({Kind::Device, deviceName(source),
				     QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(source))), QString(),
				     obs_source_active(source) || obs_source_showing(source), QString()});

		double totalMs = 0;
		struct Ctx {
			Row *row;
			double *totalMs;
		} ctx{&row, &totalMs};
		obs_source_enum_filters(
			source,
			[](obs_source_t *, obs_source_t *filter, void *param) {
				auto *c = static_cast<Ctx *>(param);
				uint32_t flags = obs_source_get_output_flags(filter);
				if (!(flags & OBS_SOURCE_AUDIO) || (flags & OBS_SOURCE_VIDEO))
					return;
				Node n{Kind::Filter, QString::fromUtf8(obs_source_get_name(filter)), filter_type_label(filter),
				       QString(), obs_source_enabled(filter), QString()};
				int samples = filter_latency_samples(filter);
				if (samples > 0) {
					n.extra = QString("+%1 ms").arg(msOf(samples), 0, 'f', 1);
					if (n.enabled)
						*c->totalMs += msOf(samples);
				}
				if (!n.enabled)
					n.extra = QStringLiteral("OFF");
				OBSDataAutoRelease s = obs_source_get_settings(filter);
				QString sc = QString::fromUtf8(obs_data_get_string(s, "sidechain_source"));
				if (!sc.isEmpty() && sc != "none") {
					n.sidechain = sc;
					if (n.extra.isEmpty())
						n.extra = QStringLiteral("← ") + sc;
				}
				c->row->nodes.push_back(n);
			},
			&ctx);

		int64_t offsetMs = obs_source_get_sync_offset(source) / 1000000;
		float vol = obs_source_get_volume(source);
		QString db = vol > 0 ? QString::number(20.0 * std::log10(vol), 'f', 1) + " dB" : QStringLiteral("-inf dB");
		row.nodes.push_back({Kind::Audio, T("Chain.Audio"), QString("Sync %1 ms").arg(offsetMs), db,
				     !obs_source_muted(source), QString()});

		QStringList tracks;
		uint32_t mixers = obs_source_get_audio_mixers(source);
		for (int t = 0; t < MAX_AUDIO_MIXES; t++)
			if (mixers & (1u << t))
				tracks << QString::number(t + 1);
		static const char *monitorKeys[] = {"Chain.Monitor.Off", "Chain.Monitor.Only", "Chain.Monitor.Both"};
		QString monitor = T(monitorKeys[std::clamp(int(obs_source_get_monitoring_type(source)), 0, 2)]);
		row.nodes.push_back({Kind::Output, T("Chain.Output"), "Tr " + (tracks.isEmpty() ? "-" : tracks.join(' ')) + " · " + monitor,
				     T("Chain.Total").arg(totalMs + double(offsetMs), 0, 'f', 1), true, QString()});
		building.push_back(std::move(row));
	}

	// The friendly device name from the source's own "device_id" list (e.g. "Mic (iD14 MkII)").
	QString deviceName(obs_source_t *source)
	{
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		QString id = QString::fromUtf8(obs_data_get_string(settings, "device_id"));
		if (id.isEmpty())
			return QString::fromUtf8(obs_source_get_name(source));
		QString key = QString::fromUtf8(obs_source_get_uuid(source)) + "|" + id;
		if (auto it = deviceNames.find(key); it != deviceNames.end())
			return *it;
		QString name = id == "default" ? T("Chain.DefaultDevice") : id;
		obs_properties_t *props = obs_source_properties(source);
		if (obs_property_t *p = obs_properties_get(props, "device_id"))
			for (size_t i = 0; i < obs_property_list_item_count(p); i++)
				if (id == QString::fromUtf8(obs_property_list_item_string(p, i))) {
					name = QString::fromUtf8(obs_property_list_item_name(p, i));
					break;
				}
		obs_properties_destroy(props);
		deviceNames.insert(key, name);
		return name;
	}

	std::vector<Row> rows, building;
	std::unordered_map<QString, std::unique_ptr<Meter>> meters; // by source uuid
	QHash<QString, QString> deviceNames;
	double textScale = 1.0, widthScale = 1.0, heightScale = 1.0;
	bool fullTitles = true, imager = false, imagerRight = false;
	int boxW = kBoxW, boxH = kBoxH, gap = kGap, header = 20, rowH = 100;
	QTimer modelTimer, paintTimer;
};

QPointer<QDialog> openView;

} // namespace

void open_chain_view()
{
	if (openView) {
		openView->raise();
		openView->activateWindow();
		return;
	}
	auto *dlg = new QDialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle(T("Chain.Title"));
	dlg->resize(1100, 560);
	auto *scroll = new QScrollArea(dlg);
	auto *canvas = new ChainCanvas();
	scroll->setWidget(canvas);
	scroll->setWidgetResizable(true);

	auto *textSize = new QSlider(Qt::Horizontal, dlg);
	auto *boxWidth = new QSlider(Qt::Horizontal, dlg);
	auto *boxHeight = new QSlider(Qt::Horizontal, dlg);
	for (QSlider *sl : {textSize, boxWidth, boxHeight}) {
		sl->setRange(50, 250); // percent
		sl->setValue(100);
		sl->setMaximumWidth(150);
	}
	auto *fullTitles = new QCheckBox(T("Chain.FullTitles"), dlg);
	fullTitles->setToolTip(T("Chain.FullTitles.Tip"));
	fullTitles->setChecked(true);
	auto *textLabel = new QLabel(dlg), *widthLabel = new QLabel(dlg), *heightLabel = new QLabel(dlg);
	auto apply = [=] {
		textLabel->setText(T("Chain.TextSize").arg(textSize->value()));
		widthLabel->setText(T("Chain.BoxWidth").arg(boxWidth->value()));
		heightLabel->setText(T("Chain.BoxHeight").arg(boxHeight->value()));
		canvas->setLook(textSize->value() / 100.0, fullTitles->isChecked(), boxWidth->value() / 100.0,
				boxHeight->value() / 100.0);
	};
	for (QSlider *sl : {textSize, boxWidth, boxHeight})
		QObject::connect(sl, &QSlider::valueChanged, dlg, apply);
	QObject::connect(fullTitles, &QCheckBox::toggled, dlg, apply);
	auto *imager = new QCheckBox(T("Chain.Imager"), dlg);
	imager->setToolTip(T("Chain.Imager.Tip"));
	QObject::connect(imager, &QCheckBox::toggled, canvas, &ChainCanvas::setImager);
	auto *imagerRight = new QCheckBox(T("Chain.ImagerRight"), dlg);
	imagerRight->setEnabled(false); // only means something while the imager is shown
	QObject::connect(imager, &QCheckBox::toggled, imagerRight, &QWidget::setEnabled);
	QObject::connect(imagerRight, &QCheckBox::toggled, canvas, &ChainCanvas::setImagerRight);
	apply();

	auto *save = new QPushButton(T("Chain.SaveImage"), dlg);
	QObject::connect(save, &QPushButton::clicked, dlg, [dlg, canvas] {
		QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
		QString name = QString("VST Ex chain %1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss"));
		QString path = QFileDialog::getSaveFileName(dlg, T("Chain.SaveImage"), dir + "/" + name, "PNG (*.png)");
		if (!path.isEmpty() && !canvas->grab().save(path, "PNG")) // grab() renders the whole chart, scrolled or not
			QMessageBox::warning(dlg, T("Chain.SaveImage"), path);
	});

	auto *controls = new QHBoxLayout();
	controls->addWidget(textLabel);
	controls->addWidget(textSize);
	controls->addWidget(fullTitles);
	controls->addSpacing(16);
	controls->addWidget(widthLabel);
	controls->addWidget(boxWidth);
	controls->addSpacing(8);
	controls->addWidget(heightLabel);
	controls->addWidget(boxHeight);
	controls->addSpacing(16);
	controls->addWidget(imager);
	controls->addWidget(imagerRight);
	controls->addStretch(1);
	controls->addWidget(save);
	auto *hint = new QLabel(T("Chain.Hint"), dlg);
	hint->setEnabled(false);
	auto *layout = new QVBoxLayout(dlg);
	layout->addLayout(controls);
	layout->addWidget(scroll, 1);
	layout->addWidget(hint);
	openView = dlg;
	dlg->show();
}

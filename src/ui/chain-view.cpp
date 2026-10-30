#include "ui/chain-view.h"

#include "dsp/polar.h"

#include "ui/ex-filters-dialog.h"
#include "ui/ui-text.h"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
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
#include <climits>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kBoxW = 160, kBoxH = 52, kGap = 26, kCurveRoom = 20, kMargin = 12; // box size at 100%
constexpr int kWrapGap = 18, kMemoGap = 12;
constexpr uint32_t kAllTracks = (1u << MAX_AUDIO_MIXES) - 1;

// Saved in each source's private settings, so they travel with the scene collection.
const char *kMemoKey = "vstex_chain_memo";
const char *kOrderKey = "vstex_chain_order";

// What the view (and so the saved image) shows.
struct ViewOptions {
	bool activeOnly = false;
	uint32_t trackMask = kAllTracks;
	bool volume = true, mono = false, balance = false, sync = true, monitoring = true;
	int wrapEvery = 0; // filters per line, 0 = never wrap
};

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
	QString uuid, name, memo;
	std::vector<Node> nodes;
	int order = INT_MAX; // sources never moved go last
	bool active = false;
	uint32_t mixers = 0;
	std::vector<QPoint> cells; // column and line of each node
	int y = 0;
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

	void setOptions(const ViewOptions &o)
	{
		options = o;
		rebuild();
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
		const Row *row = rowAt(int(e->position().y()));
		if (!row)
			return;
		OBSSourceAutoRelease source = obs_weak_source_get_source(row->source);
		if (source)
			ExFiltersDialog::open(source);
	}

	// Drag a row onto another to move it there.
	void mousePressEvent(QMouseEvent *e) override
	{
		const Row *row = e->button() == Qt::LeftButton ? rowAt(int(e->position().y())) : nullptr;
		dragUuid = row ? row->uuid : QString();
		dropUuid.clear();
	}

	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (dragUuid.isEmpty())
			return;
		const Row *row = rowAt(int(e->position().y()));
		dropUuid = row && row->uuid != dragUuid ? row->uuid : QString();
		update();
	}

	void mouseReleaseEvent(QMouseEvent *) override
	{
		if (!dropUuid.isEmpty())
			moveRow(dragUuid, dropUuid);
		dragUuid.clear();
		dropUuid.clear();
		update();
	}

	void contextMenuEvent(QContextMenuEvent *e) override
	{
		const Row *row = rowAt(e->y());
		OBSSourceAutoRelease source = row ? obs_weak_source_get_source(row->source) : nullptr;
		if (!source)
			return;
		const QString name = row->name, current = row->memo; // the dialog's event loop may rebuild the rows
		bool ok = false;
		QString memo = QInputDialog::getText(this, T("Chain.Memo"), name, QLineEdit::Normal, current, &ok);
		if (!ok)
			return;
		OBSDataAutoRelease priv = obs_source_get_private_settings(source);
		obs_data_set_string(priv, kMemoKey, memo.trimmed().toUtf8().constData());
		rebuild();
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
			outputAnchor[rows[r].name] = boxOf(rows[r], rows[r].nodes.size() - 1).bottomLeft() + QPoint(boxW / 2, 1);

		for (size_t r = 0; r < rows.size(); r++) {
			const Row &row = rows[r];
			int y = row.y;
			if (row.uuid == dropUuid)
				p.fillRect(0, y, width(), rowHeight(row), QColor(90, 140, 220, 70));
			QFontMetrics tm(titleFont());
			p.setFont(titleFont());
			p.setPen(Qt::white);
			p.drawText(kMargin, y + tm.ascent() + 2, row.name);
			if (!row.memo.isEmpty()) {
				p.setFont(smallFont());
				p.setPen(QColor(240, 200, 90));
				p.drawText(kMargin + tm.horizontalAdvance(row.name) + kMemoGap, y + tm.ascent() + 2, row.memo);
			}
			for (size_t i = 0; i < row.nodes.size(); i++) {
				const Node &n = row.nodes[i];
				QRect box = boxOf(row, i);
				if (i > 0 && row.cells[i].y() == row.cells[i - 1].y()) { // arrow from the previous box
					int ay = box.center().y();
					p.setPen(QPen(QColor(120, 120, 135), 2));
					p.drawLine(box.left() - gap + 2, ay, box.left() - 4, ay);
					p.drawLine(box.left() - 9, ay - 4, box.left() - 4, ay);
					p.drawLine(box.left() - 9, ay + 4, box.left() - 4, ay);
				} else if (i > 0) { // wrapped: down from the previous box, back to the start of this line
					QRect prev = boxOf(row, i - 1);
					int x = box.center().x(), mid = box.top() - kWrapGap / 2;
					p.setPen(QPen(QColor(120, 120, 135), 2));
					p.drawLine(prev.center().x(), prev.bottom(), prev.center().x(), mid);
					p.drawLine(prev.center().x(), mid, x, mid);
					p.drawLine(x, mid, x, box.top() - 3);
					p.drawLine(x - 4, box.top() - 8, x, box.top() - 3);
					p.drawLine(x + 4, box.top() - 8, x, box.top() - 3);
				}
				drawNode(p, box, n, row);
			}
			if (imager) {
				QRect last = boxOf(row, row.nodes.size() - 1);
				QPoint at = imagerRight ? last.topRight() + QPoint(gap + 1, 0) : QPoint(kMargin, y + header);
				drawImager(p, QRect(at, QSize(2 * boxH, boxH)), row);
			}
		}
		// Sidechain / ducking: a dashed curve from the controlling source's output to the filter that listens.
		for (size_t r = 0; r < rows.size(); r++)
			for (size_t i = 0; i < rows[r].nodes.size(); i++) {
				const QString &from = rows[r].nodes[i].sidechain;
				if (from.isEmpty() || !outputAnchor.contains(from))
					continue;
				QPoint a = outputAnchor[from], b = boxOf(rows[r], i).topLeft() + QPoint(boxW / 2, 0);
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
	QRect boxOf(const Row &row, size_t i) const
	{
		const QPoint &cell = row.cells[i];
		return QRect(boxX(cell.x()), row.y + header + cell.y() * (boxH + kWrapGap), boxW, boxH);
	}
	int rowHeight(const Row &row) const
	{
		int lines = row.cells.empty() ? 1 : row.cells.back().y() + 1;
		return header + lines * boxH + (lines - 1) * kWrapGap + kCurveRoom + QFontMetrics(titleFont()).height();
	}
	const Row *rowAt(int y) const
	{
		for (const Row &row : rows)
			if (y >= row.y && y < row.y + rowHeight(row))
				return &row;
		return nullptr;
	}

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
		int columns = 0, rowNameW = 0, y = kMargin;
		for (Row &row : rows) {
			int column = 0, line = 0, filters = 0;
			row.cells.clear();
			for (const Node &n : row.nodes) {
				if (n.kind == Kind::Filter && options.wrapEvery > 0 && filters > 0 && filters % options.wrapEvery == 0) {
					column = 1; // under the first filter
					line++;
				}
				filters += n.kind == Kind::Filter;
				row.cells.push_back({column++, line});
			}
			columns = std::max(columns, column);
			row.y = y;
			y += rowHeight(row);
			int nameW = tm.horizontalAdvance(row.name);
			if (!row.memo.isEmpty())
				nameW += kMemoGap + sm.horizontalAdvance(row.memo);
			rowNameW = std::max(rowNameW, nameW);
		}
		setMinimumSize(std::max(boxX(columns) + (imagerRight ? imagerWidth() : 0), rowNameW + 2 * kMargin) + kMargin,
			       std::max(120, y + kMargin));
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

		std::stable_sort(next.begin(), next.end(), [](const Row &a, const Row &b) { return a.order < b.order; });
		order.clear();
		for (const Row &row : next)
			order << row.uuid;
		next.erase(std::remove_if(next.begin(), next.end(),
					  [this](const Row &row) {
						  return (options.activeOnly && !row.active) ||
							 (options.trackMask != kAllTracks && !(row.mixers & options.trackMask));
					  }),
			   next.end());

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

	// Moves `from` to the place of `to` among all sources, including the ones filtered out right now.
	void moveRow(const QString &from, const QString &to)
	{
		int target = (int)order.indexOf(to);
		order.removeOne(from);
		order.insert(target, from);
		for (int i = 0; i < order.size(); i++) {
			OBSSourceAutoRelease source = obs_get_source_by_uuid(order[i].toUtf8().constData());
			if (!source)
				continue;
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			obs_data_set_int(priv, kOrderKey, i);
		}
		rebuild();
	}

	void addRow(obs_source_t *source)
	{
		Row row;
		row.source = OBSGetWeakRef(source);
		row.uuid = QString::fromUtf8(obs_source_get_uuid(source));
		row.name = QString::fromUtf8(obs_source_get_name(source));
		row.active = obs_source_active(source);
		row.mixers = obs_source_get_audio_mixers(source);
		OBSDataAutoRelease priv = obs_source_get_private_settings(source);
		row.memo = QString::fromUtf8(obs_data_get_string(priv, kMemoKey));
		if (obs_data_has_user_value(priv, kOrderKey))
			row.order = (int)obs_data_get_int(priv, kOrderKey);

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
		QStringList first, second;
		if (options.sync)
			first << QString("Sync %1 ms").arg(offsetMs);
		if (options.volume)
			first << db;
		if (options.mono)
			second << T((obs_source_get_flags(source) & OBS_SOURCE_FLAG_FORCE_MONO) ? "Chain.Mono" : "Chain.Stereo");
		if (options.balance)
			second << T("Chain.BalanceValue").arg(qRound(obs_source_get_balance_value(source) * 100.0f));
		if (first.isEmpty())
			first.swap(second);
		if (!first.isEmpty())
			row.nodes.push_back({Kind::Audio, T("Chain.Audio"), first.join(" · "), second.join(" · "),
					     !obs_source_muted(source), QString()});

		QStringList tracks;
		for (int t = 0; t < MAX_AUDIO_MIXES; t++)
			if (row.mixers & (1u << t))
				tracks << QString::number(t + 1);
		static const char *monitorKeys[] = {"Chain.Monitor.Off", "Chain.Monitor.Only", "Chain.Monitor.Both"};
		QString monitor = T(monitorKeys[std::clamp(int(obs_source_get_monitoring_type(source)), 0, 2)]);
		QString output = "Tr " + (tracks.isEmpty() ? "-" : tracks.join(' '));
		if (options.monitoring)
			output += " · " + monitor;
		row.nodes.push_back({Kind::Output, T("Chain.Output"), output,
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
	ViewOptions options;
	QStringList order;          // uuids of all audio sources, shown or not
	QString dragUuid, dropUuid; // row being dragged, row under it
	std::unordered_map<QString, std::unique_ptr<Meter>> meters; // by source uuid
	QHash<QString, QString> deviceNames;
	double textScale = 1.0, widthScale = 1.0, heightScale = 1.0;
	bool fullTitles = true, imager = false, imagerRight = false;
	int boxW = kBoxW, boxH = kBoxH, gap = kGap, header = 20;
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

	auto *activeOnly = new QCheckBox(T("Chain.ActiveOnly"), dlg);
	std::vector<QCheckBox *> tracks;
	for (int t = 0; t < MAX_AUDIO_MIXES; t++) {
		tracks.push_back(new QCheckBox(QString::number(t + 1), dlg));
		tracks.back()->setChecked(true);
	}
	const ViewOptions defaults;
	auto *showVolume = new QCheckBox(T("Chain.Volume"), dlg);
	auto *showMono = new QCheckBox(T("Chain.Mono"), dlg);
	auto *showBalance = new QCheckBox(T("Chain.Balance"), dlg);
	auto *showSync = new QCheckBox(T("Chain.Sync"), dlg);
	auto *showMonitoring = new QCheckBox(T("Chain.Monitoring"), dlg);
	showVolume->setChecked(defaults.volume);
	showMono->setChecked(defaults.mono);
	showBalance->setChecked(defaults.balance);
	showSync->setChecked(defaults.sync);
	showMonitoring->setChecked(defaults.monitoring);
	auto *wrapEvery = new QSpinBox(dlg);
	wrapEvery->setRange(0, 32);
	wrapEvery->setSpecialValueText(T("Chain.WrapOff"));
	auto applyOptions = [=] {
		ViewOptions o;
		o.activeOnly = activeOnly->isChecked();
		o.trackMask = 0;
		for (int t = 0; t < MAX_AUDIO_MIXES; t++)
			o.trackMask |= tracks[t]->isChecked() ? 1u << t : 0;
		o.volume = showVolume->isChecked();
		o.mono = showMono->isChecked();
		o.balance = showBalance->isChecked();
		o.sync = showSync->isChecked();
		o.monitoring = showMonitoring->isChecked();
		o.wrapEvery = wrapEvery->value();
		canvas->setOptions(o);
	};
	std::vector<QCheckBox *> optionBoxes = {activeOnly, showVolume, showMono, showBalance, showSync, showMonitoring};
	optionBoxes.insert(optionBoxes.end(), tracks.begin(), tracks.end());
	for (QCheckBox *box : optionBoxes)
		QObject::connect(box, &QCheckBox::toggled, dlg, applyOptions);
	QObject::connect(wrapEvery, &QSpinBox::valueChanged, dlg, applyOptions);

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
	auto *filters = new QHBoxLayout();
	filters->addWidget(activeOnly);
	filters->addSpacing(16);
	filters->addWidget(new QLabel(T("Chain.Tracks"), dlg));
	for (QCheckBox *track : tracks)
		filters->addWidget(track);
	filters->addSpacing(16);
	filters->addWidget(new QLabel(T("Chain.Show"), dlg));
	for (QCheckBox *box : {showVolume, showMono, showBalance, showSync, showMonitoring})
		filters->addWidget(box);
	filters->addSpacing(16);
	filters->addWidget(new QLabel(T("Chain.WrapEvery"), dlg));
	filters->addWidget(wrapEvery);
	filters->addStretch(1);
	auto *hint = new QLabel(T("Chain.Hint"), dlg);
	hint->setEnabled(false);
	auto *layout = new QVBoxLayout(dlg);
	layout->addLayout(controls);
	layout->addLayout(filters);
	layout->addWidget(scroll, 1);
	layout->addWidget(hint);
	openView = dlg;
	dlg->show();
}

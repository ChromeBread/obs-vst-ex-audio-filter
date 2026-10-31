#include "ui/master-dock.h"

#include "master-bus.h"
#include "plugin-meta.h"
#include "ui/ex-filters-dialog.h"
#include "ui/ui-text.h"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QActionGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace {

constexpr const char *kDockId = "vst-ex-master-dock";
constexpr int kTickMs = 33;
constexpr int kMinSide = 72; // same in both layouts: a layout change must never resize the dock back
constexpr float kFloorDb = -60.0f;
constexpr float kDecayDbPerTick = 0.8f;
enum Shape { ShapeAuto, ShapeVertical, ShapeHorizontal };

// The colours and zones of OBS's level meter.
const QColor kNominal(76, 255, 76), kWarning(255, 255, 76), kError(255, 76, 76);
const QColor kNominalDim(38, 127, 38), kWarningDim(127, 127, 38), kErrorDim(127, 38, 38);
constexpr float kWarningDb = -20.0f, kErrorDb = -9.0f;

class Meter : public QWidget {
public:
	explicit Meter(QWidget *parent) : QWidget(parent)
	{
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumSize(8, 8);
	}

	void setLevels(const std::array<float, MAX_AUDIO_CHANNELS> &db, int count)
	{
		channels = std::max(1, count);
		for (int i = 0; i < MAX_AUDIO_CHANNELS; i++)
			shown[i] = std::max(db[i], shown[i] - kDecayDbPerTick);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		bool across = width() > height();
		int length = across ? width() : height();
		int depth = across ? height() : width();
		int labels = depth >= 40 ? 22 : 0; // scale numbers, when there is room beside the bars
		int barDepth = std::max(2, (depth - labels) / channels);
		auto pos = [&](float db) { return int((db - kFloorDb) / -kFloorDb * float(length)); };
		auto bar = [&](QPainter &painter, int from, int to, int line, int thickness, const QColor &color) {
			if (across)
				painter.fillRect(from, line, to - from, thickness, color);
			else
				painter.fillRect(line, length - to, thickness, to - from, color);
		};
		for (int c = 0; c < channels; c++) {
			int line = c * barDepth;
			int thickness = std::max(1, barDepth - 1);
			int level = pos(std::clamp(shown[c], kFloorDb, 0.0f));
			int warn = pos(kWarningDb), err = pos(kErrorDb);
			bar(p, 0, warn, line, thickness, kNominalDim);
			bar(p, warn, err, line, thickness, kWarningDim);
			bar(p, err, length, line, thickness, kErrorDim);
			bar(p, 0, std::min(level, warn), line, thickness, kNominal);
			if (level > warn)
				bar(p, warn, std::min(level, err), line, thickness, kWarning);
			if (level > err)
				bar(p, err, level, line, thickness, kError);
		}
		if (!labels)
			return;
		QFont f = font();
		f.setPixelSize(9);
		p.setFont(f);
		p.setPen(palette().color(QPalette::WindowText));
		int spacing = length >= 200 ? 10 : 20;
		for (int db = 0; db >= -60; db -= spacing) {
			int at = pos(float(db));
			QString text = QString::number(db);
			if (across)
				p.drawText(QRect(std::clamp(at - 12, 0, length - 24), depth - labels, 24, labels), Qt::AlignCenter, text);
			else
				p.drawText(QRect(depth - labels, std::clamp(length - at - 6, 0, length - 12), labels, 12),
					   Qt::AlignRight | Qt::AlignVCenter, text);
		}
	}

private:
	int channels = 2;
	std::array<float, MAX_AUDIO_CHANNELS> shown{};
};

class MasterStrip;
QPointer<QWidget> stripWidget;

// Looks like one Audio Mixer strip and is laid out by its own shape, never by where the dock or the mixer are, so
// every position works: beside, above, below, floating, tabbed with others, any size.
class MasterStrip : public QWidget {
public:
	MasterStrip() : QWidget(nullptr)
	{
		stripWidget = this;
		shape = std::clamp(PluginMeta::instance().uiValue("masterShape").toInt(ShapeAuto), int(ShapeAuto), int(ShapeHorizontal));
		across = shape == ShapeHorizontal;
		setMinimumSize(kMinSide, kMinSide);
		name = new QLabel(QStringLiteral("VST Ex Master"), this);
		name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		QFont bold = name->font();
		bold.setBold(true);
		name->setFont(bold);
		volume = new QLabel(this);
		volume->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		status = new QLabel(this);
		status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		status->setWordWrap(true); // the warning must stay readable in a narrow strip
		meter = new Meter(this);
		slider = new QSlider(Qt::Vertical, this);
		slider->setRange(0, 10000);
		slider->setMinimumSize(16, 16);
		mute = new QPushButton(this);
		mute->setCheckable(true);
		mute->setAutoDefault(false);
		mute->setProperty("class", "btn-mute");
		mute->setAccessibleName(T("Master.Mute"));
		mute->setToolTip(T("Master.Mute"));
		config = new QPushButton(this);
		config->setProperty("class", "icon-dots-vert");
		config->setAutoDefault(false);
		config->setToolTip(T("Master.Menu"));

		fader = obs_fader_create(OBS_FADER_LOG);
		volmeter = obs_volmeter_create(OBS_FADER_LOG);
		obs_volmeter_add_callback(volmeter, onLevels, this);

		connect(slider, &QSlider::valueChanged, this, [this](int v) {
			if (!syncing)
				obs_fader_set_deflection(fader, float(v) / 10000.0f);
		});
		connect(mute, &QPushButton::clicked, this, [this](bool on) {
			if (attached)
				obs_source_set_muted(attached, on);
		});
		connect(config, &QPushButton::clicked, this, [this] { showMenu(QCursor::pos()); });
		setContextMenuPolicy(Qt::CustomContextMenu);
		connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &at) { showMenu(mapToGlobal(at)); });

		arrange();
		connect(&timer, &QTimer::timeout, this, &MasterStrip::tick);
		timer.start(kTickMs);
	}

	~MasterStrip() override
	{
		obs_volmeter_remove_callback(volmeter, onLevels, this);
		obs_volmeter_destroy(volmeter);
		obs_fader_destroy(fader);
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		updateShape();
	}

private:
	static void onLevels(void *param, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			     const float[MAX_AUDIO_CHANNELS])
	{
		(void)magnitude;
		auto *self = static_cast<MasterStrip *>(param);
		for (int i = 0; i < MAX_AUDIO_CHANNELS; i++)
			self->levels[i].store(peak[i], std::memory_order_relaxed);
	}

	void updateShape()
	{
		// Margin on purpose: a dock near square must not flip on every pixel.
		bool automatic = across ? width() * 10 >= height() * 9 : width() * 9 > height() * 10;
		bool wide = shape == ShapeAuto ? automatic : shape == ShapeHorizontal;
		if (wide != across) {
			across = wide;
			QTimer::singleShot(0, this, [this] { arrange(); }); // not while Qt is laying out this very resize
		}
	}

	void arrange()
	{
		delete layout();
		auto *grid = new QGridLayout(this);
		grid->setContentsMargins(4, 2, 4, 2);
		grid->setSpacing(2);
		grid->setSizeConstraint(QLayout::SetNoConstraint);
		auto *buttons = new QHBoxLayout();
		buttons->setSpacing(0);
		if (across) {
			slider->setOrientation(Qt::Horizontal);
			grid->addWidget(name, 0, 0);
			grid->addWidget(volume, 0, 1);
			grid->addWidget(status, 1, 0, 1, 2);
			grid->addWidget(meter, 2, 0, 1, 3);
			grid->addWidget(slider, 3, 0, 1, 2);
			buttons->addWidget(mute);
			buttons->addWidget(config);
			grid->addLayout(buttons, 3, 2);
			grid->setColumnStretch(0, 1);
			grid->setColumnStretch(1, 1);
			grid->setRowStretch(2, 1);
		} else {
			slider->setOrientation(Qt::Vertical);
			grid->addWidget(name, 0, 0, 1, 2);
			grid->addWidget(volume, 1, 0, 1, 2);
			grid->addWidget(slider, 2, 0);
			grid->addWidget(meter, 2, 1);
			buttons->addStretch(1);
			buttons->addWidget(mute);
			buttons->addWidget(config);
			buttons->addStretch(1);
			grid->addLayout(buttons, 3, 0, 1, 2);
			grid->addWidget(status, 4, 0, 1, 2);
			grid->setRowStretch(2, 1);
			grid->setColumnStretch(1, 1);
		}
	}

	void attachSource(obs_source_t *source)
	{
		attached = source;
		obs_fader_attach_source(fader, source);
		obs_volmeter_attach_source(volmeter, source);
	}

	void tick()
	{
		if (masterbus::source() != attached.Get())
			attachSource(masterbus::source());
		if (!attached || !isVisible())
			return;
		std::array<float, MAX_AUDIO_CHANNELS> db;
		bool on = masterbus::route().enabled;
		for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			float v = levels[i].load(std::memory_order_relaxed);
			db[i] = !on || std::isnan(v) ? -1000.0f : v;
		}
		meter->setLevels(db, obs_volmeter_get_nr_channels(volmeter));
		if (!slider->isSliderDown()) {
			syncing = true;
			slider->setValue(int(obs_fader_get_deflection(fader) * 10000.0f));
			syncing = false;
		}
		float gain = obs_fader_get_db(fader);
		volume->setText(std::isinf(gain) ? QStringLiteral("-inf dB") : QString::number(gain, 'f', 1) + " dB");
		bool muted = obs_source_muted(attached);
		if (mute->isChecked() != muted || !painted) {
			painted = true;
			mute->setChecked(muted);
			mute->setProperty("class", muted ? "btn-mute checked" : "btn-mute");
			mute->style()->unpolish(mute);
			mute->style()->polish(mute);
		}
		if (++slowTicks >= 15) {
			slowTicks = 0;
			updateStatus();
		}
	}

	void updateStatus()
	{
		auto r = masterbus::route();
		QString text, tip;
		if (masterbus::onStream()) {
			text = T("Master.Status.Ok").arg(r.in);
			tip = T("Master.Status.Ok.Tip");
		} else if (masterbus::streamFailed()) {
			text = T("Master.Status.Failed");
			tip = T("Master.Status.Failed.Tip");
		} else if (r.enabled) {
			text = T("Master.Status.Ready").arg(r.in);
			tip = T("Master.Status.Ready.Tip");
		} else {
			text = T("Master.Status.Off");
			tip = T("Master.Status.Off.Tip");
		}
		status->setText(text);
		status->setToolTip(tip);
		status->setStyleSheet(masterbus::streamFailed() ? "color:#ffb74d;" : "");
	}

	void showMenu(const QPoint &at)
	{
		auto r = masterbus::route();
		QMenu menu(this);
		QAction *enable = menu.addAction(T("Master.Enable"));
		enable->setCheckable(true);
		enable->setChecked(r.enabled);
		// Changing these while the stream reads the master would cut its sound.
		bool busy = masterbus::onStream();
		enable->setEnabled(!busy);
		connect(enable, &QAction::toggled, this, [](bool on) { masterbus::setEnabled(on); });

		QMenu *input = menu.addMenu(T("Master.In"));
		auto *inputGroup = new QActionGroup(input);
		for (int t = 1; t <= 6; t++) {
			QAction *a = input->addAction(QStringLiteral("Tr%1").arg(t));
			a->setCheckable(true);
			a->setChecked(t == r.in);
			inputGroup->addAction(a);
			connect(a, &QAction::triggered, this, [t] { masterbus::setInput(t); });
		}
		input->setEnabled(!busy);

		QMenu *layoutMenu = menu.addMenu(T("Master.Layout"));
		auto *layoutGroup = new QActionGroup(layoutMenu);
		for (auto [value, text] : {std::pair{ShapeAuto, "Master.Layout.Auto"},
					   {ShapeVertical, "Master.Layout.Vertical"},
					   {ShapeHorizontal, "Master.Layout.Horizontal"}}) {
			QAction *a = layoutMenu->addAction(T(text));
			a->setCheckable(true);
			a->setChecked(shape == value);
			layoutGroup->addAction(a);
			connect(a, &QAction::triggered, this, [this, value = value] {
				shape = value;
				PluginMeta::instance().setUiValue("masterShape", shape);
				updateShape();
			});
		}
		menu.addSeparator();
		connect(menu.addAction(T("MixerMenu.ExFilters")), &QAction::triggered, this, [] {
			if (obs_source_t *s = masterbus::source())
				ExFiltersDialog::open(s);
		});
		connect(menu.addAction(T("Master.HowTo")), &QAction::triggered, this, [this] {
			QMessageBox box(this);
			box.setWindowTitle(QStringLiteral("VST Ex Master"));
			box.setTextFormat(Qt::RichText);
			box.setText(T("Master.HowTo.Text"));
			box.exec();
		});
		menu.exec(at);
	}

	QLabel *name, *volume, *status;
	Meter *meter;
	QSlider *slider;
	QPushButton *mute, *config;
	obs_fader_t *fader;
	obs_volmeter_t *volmeter;
	OBSSource attached;
	std::array<std::atomic<float>, MAX_AUDIO_CHANNELS> levels{};
	QTimer timer;
	bool across = false, syncing = false, painted = false;
	int shape = ShapeAuto;
	int slowTicks = 15;
};

} // namespace

void install_master_dock()
{
	if (!stripWidget)
		obs_frontend_add_dock_by_id(kDockId, "VST Ex Master", new MasterStrip());
}

void remove_master_dock()
{
	if (stripWidget)
		obs_frontend_remove_dock(kDockId);
}

bool master_strip_rect(QRect &globalRect)
{
	if (!stripWidget || !stripWidget->isVisible())
		return false;
	globalRect = QRect(stripWidget->mapToGlobal(QPoint(0, 0)), stripWidget->size());
	return true;
}

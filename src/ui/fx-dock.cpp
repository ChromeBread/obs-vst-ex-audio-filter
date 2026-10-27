#include "ui/fx-dock.h"

#include "plugin-meta.h"
#include "ui/ex-filters-dialog.h"
#include "ui/ui-text.h"
#include "vst-filter.h"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace {

constexpr const char *kDockId = "vst-ex-fx-dock";
constexpr int kPlainWidth = 100; // column width when not following the mixer (or its strip isn't found)
constexpr int kTickMs = 500;

// "VST" filters: ours (VST Ex, Pitch / Pan, Pseudo Stereo), OBS's VST 2.x filter and atkAudio's host.
bool isVstFilter(obs_source_t *filter)
{
	const char *id = obs_source_get_unversioned_id(filter);
	return strncmp(id, "vst", 3) == 0 || strncmp(id, "atkaudio", 8) == 0;
}

void paintOnOff(QPushButton *b, bool on)
{
	if (b->property("on").isValid() && b->property("on").toBool() == on)
		return; // restyling relayouts the dock; only when it changed
	b->setProperty("on", on);
	b->setStyleSheet(on ? "QPushButton{background:#2e7d32;color:white;font-weight:bold;padding:1px;}"
			    : "QPushButton{background:#4a4a4a;color:#aaa;padding:1px;}");
}

// Where OBS draws each vertical mixer strip, by source name. Read only: nothing in the mixer is touched.
// Class names: VolControl (OBS 31), VolumeControl (OBS 32/33). Empty if not found or the mixer is horizontal.
QHash<QString, QRect> mixerStrips()
{
	QHash<QString, QRect> out;
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!main)
		return out;
	for (QWidget *strip : main->findChildren<QWidget *>()) {
		const char *cls = strip->metaObject()->className();
		if ((strcmp(cls, "VolControl") != 0 && strcmp(cls, "VolumeControl") != 0) || !strip->isVisible())
			continue;
		if (strip->width() > 300)
			return {}; // horizontal mixer: strips are full-width rows, nothing to line up under
		QRect geo(strip->mapToGlobal(QPoint(0, 0)), strip->size());
		// The strip's objectName is the source name; the label may show a shortened one, so keep both.
		if (!strip->objectName().isEmpty())
			out.insert(strip->objectName(), geo);
		for (QWidget *w : strip->findChildren<QWidget *>()) {
			const char *c = w->metaObject()->className();
			if (strcmp(c, "OBSSourceLabel") == 0 || strcmp(c, "VolumeName") == 0) {
				out.insert(w->property("text").toString(), geo);
				break;
			}
		}
	}
	return out;
}

QToolButton *toggle(QWidget *parent, const char *text, const char *tip, bool on)
{
	auto *b = new QToolButton(parent);
	b->setText(T(text));
	b->setToolTip(T(tip));
	b->setCheckable(true);
	b->setChecked(on);
	return b;
}

class FxDock : public QWidget {
public:
	FxDock() : QWidget(nullptr)
	{
		auto &meta = PluginMeta::instance();
		follow = toggle(this, "FxDock.Follow", "FxDock.Follow.Tip", meta.uiValue("fxFollow").toBool(true));
		reserve = toggle(this, "FxDock.Reserve", "FxDock.Reserve.Tip", meta.uiValue("fxReserve").toBool(true));
		scrollbar = toggle(this, "FxDock.Scrollbar", "FxDock.Scrollbar.Tip", meta.uiValue("fxScrollbar").toBool(false));
		perRow = new QComboBox(this);
		for (int n = 1; n <= 3; n++)
			perRow->addItem(T("FxDock.PerRow").arg(n), n);
		perRow->setCurrentIndex(std::clamp(meta.uiValue("fxPerRow").toInt(2), 1, 3) - 1);
		perRow->setToolTip(T("FxDock.PerRow.Tip"));
		auto *bar = new QHBoxLayout();
		bar->setSpacing(2);
		bar->addWidget(follow);
		bar->addWidget(reserve);
		bar->addWidget(scrollbar);
		bar->addWidget(perRow);
		bar->addStretch(1);

		scroll = new QScrollArea(this);
		scroll->setWidgetResizable(false);
		scroll->setFrameShape(QFrame::NoFrame);
		content = new QWidget();
		scroll->setWidget(content);
		empty = new QLabel(T("FxDock.Empty"), this);
		empty->setWordWrap(true);
		empty->setEnabled(false);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(2, 2, 2, 2);
		layout->addLayout(bar);
		layout->addWidget(empty);
		layout->addWidget(scroll, 1);

		auto save = [this] {
			auto &m = PluginMeta::instance();
			m.setUiValue("fxFollow", follow->isChecked());
			m.setUiValue("fxReserve", reserve->isChecked());
			m.setUiValue("fxScrollbar", scrollbar->isChecked());
			m.setUiValue("fxPerRow", perRow->currentData().toInt());
			applyScrollbar();
			place();
		};
		for (QToolButton *b : {follow, reserve, scrollbar})
			connect(b, &QToolButton::toggled, this, save);
		connect(perRow, &QComboBox::activated, this, [this, save] {
			save();
			lastSignature = "-"; // buttons per row changes the grids: rebuild
			tick();
		});
		applyScrollbar();
		connect(&timer, &QTimer::timeout, this, &FxDock::tick);
		timer.start(kTickMs);
		tick();
	}

private:
	struct Button {
		OBSWeakSource filter;
		QPushButton *button;
		QString name;
	};

	struct Column {
		QString source;
		QWidget *box;
		QLabel *title;
		std::vector<Button> buttons; // empty: an audio source without VST (shown only to keep its space)
		int width = 0;               // last width the button texts were fitted to
	};

	struct SourceFx {
		OBSSource source;
		std::vector<OBSSource> filters;
	};

	void applyScrollbar()
	{
		scroll->setHorizontalScrollBarPolicy(scrollbar->isChecked() ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
	}

	// Rebuild when sources or their VST filters change; recolour and re-place every tick
	// (ON/OFF changed elsewhere, mixer scrolled or resized).
	void tick()
	{
		std::vector<SourceFx> found; // every audio source, with or without VST
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
					return true;
				SourceFx fx{source, {}};
				obs_source_enum_filters(
					source,
					[](obs_source_t *, obs_source_t *filter, void *p) {
						if (isVstFilter(filter))
							static_cast<SourceFx *>(p)->filters.emplace_back(filter);
					},
					&fx);
				static_cast<std::vector<SourceFx> *>(param)->push_back(std::move(fx));
				return true;
			},
			&found);

		QString signature;
		for (const auto &fx : found) {
			signature += QString::fromUtf8(obs_source_get_uuid(fx.source)) + QString::fromUtf8(obs_source_get_name(fx.source));
			for (const auto &f : fx.filters)
				signature += QString::fromUtf8(obs_source_get_uuid(f)) + QString::fromUtf8(obs_source_get_name(f));
		}
		if (signature != lastSignature) {
			lastSignature = signature;
			rebuild(found);
		}
		for (const Column &col : columns)
			for (const Button &b : col.buttons) {
				OBSSourceAutoRelease f = obs_weak_source_get_source(b.filter);
				if (f)
					paintOnOff(b.button, obs_source_enabled(f));
			}
		place();
	}

	void rebuild(const std::vector<SourceFx> &found)
	{
		for (Column &col : columns)
			delete col.box;
		columns.clear();
		bool anyVst = false;
		int buttonsPerRow = perRow->currentData().toInt();
		for (const auto &fx : found) {
			Column col;
			col.source = QString::fromUtf8(obs_source_get_name(fx.source));
			col.box = new QWidget(content);
			auto *layout = new QVBoxLayout(col.box);
			layout->setContentsMargins(1, 0, 1, 0);
			layout->setSpacing(2);
			col.title = new QLabel(col.box);
			col.title->setToolTip(col.source);
			col.title->setEnabled(!fx.filters.empty());
			layout->addWidget(col.title);
			auto *grid = new QGridLayout();
			grid->setSpacing(2);
			for (size_t i = 0; i < fx.filters.size(); i++) {
				obs_source_t *f = fx.filters[i];
				QString name = QString::fromUtf8(obs_source_get_name(f));
				auto *b = new QPushButton(col.box);
				b->setToolTip(name + "\n" + filter_type_label(f) + "\n" + T("FxDock.Hint"));
				b->setMinimumHeight(24);
				b->setMinimumWidth(10); // let the column width decide, texts are shortened to fit
				b->setContextMenuPolicy(Qt::CustomContextMenu);
				OBSWeakSource weak = OBSGetWeakRef(f);
				connect(b, &QPushButton::clicked, this, [this, weak] {
					OBSSourceAutoRelease filter = obs_weak_source_get_source(weak);
					if (filter)
						obs_source_set_enabled(filter, !obs_source_enabled(filter));
					// Recolour right after the click returns: a rebuild must never delete the button mid-click.
					QTimer::singleShot(0, this, [this] { tick(); });
				});
				connect(b, &QPushButton::customContextMenuRequested, this, [weak] {
					OBSSourceAutoRelease filter = obs_weak_source_get_source(weak);
					if (!filter)
						return;
					if (strcmp(obs_source_get_unversioned_id(filter), VST_EX_FILTER_ID) == 0)
						vst_ex_open_editor(filter);
					else if (obs_source_t *parent = obs_filter_get_parent(filter))
						ExFiltersDialog::open(parent);
				});
				grid->addWidget(b, int(i) / buttonsPerRow, int(i) % buttonsPerRow);
				col.buttons.push_back({weak, b, name});
			}
			anyVst |= !fx.filters.empty();
			layout->addLayout(grid);
			layout->addStretch(1);
			columns.push_back(std::move(col));
		}
		empty->setVisible(!anyVst);
	}

	// Follow ON: columns take x/width from their mixer strip; with "keep space" off they are packed left
	// in mixer order instead. Follow OFF: plain columns in source order. "Keep space" also shows (follow OFF)
	// or leaves room for (follow ON) audio sources without VST.
	void place()
	{
		QHash<QString, QRect> strips = follow->isChecked() ? mixerStrips() : QHash<QString, QRect>();
		bool keepSpace = reserve->isChecked();
		QPoint origin = content->mapToGlobal(QPoint(0, 0));

		std::vector<Column *> order;
		for (Column &col : columns) {
			bool show = !col.buttons.empty() || (keepSpace && strips.isEmpty());
			col.box->setVisible(show);
			if (show)
				order.push_back(&col);
		}
		// Mixer order (left to right) for the columns that have a strip; the rest keep source order after them.
		std::stable_sort(order.begin(), order.end(), [&](Column *a, Column *b) {
			auto ia = strips.find(a->source), ib = strips.find(b->source);
			if ((ia == strips.end()) != (ib == strips.end()))
				return ia != strips.end();
			return ia != strips.end() && ia->left() < ib->left();
		});

		int nextFree = 0, right = 0, bottom = 0;
		if (keepSpace)
			for (Column *col : order)
				if (auto it = strips.find(col->source); it != strips.end())
					nextFree = std::max(nextFree, it->right() - origin.x() + 1);
		for (Column *col : order) {
			QRect geo;
			auto it = strips.find(col->source);
			if (it != strips.end() && keepSpace) {
				geo = QRect(it->left() - origin.x(), 0, it->width(), 0); // right under its strip
			} else {
				int w = it != strips.end() ? it->width() : kPlainWidth;
				geo = QRect(nextFree, 0, w, 0);
				nextFree += w;
			}
			if (geo.width() != col->width) // fit the texts to the new width
				fitTexts(*col, geo.width());
			geo.setHeight(col->box->sizeHint().height());
			col->box->setGeometry(geo);
			right = std::max(right, geo.right() + 1);
			bottom = std::max(bottom, geo.bottom() + 1);
		}
		content->resize(std::max(right, scroll->viewport()->width()), bottom);
	}

	void fitTexts(Column &col, int width)
	{
		col.width = width;
		QFontMetrics fm(col.title->font());
		col.title->setText(fm.elidedText(col.source, Qt::ElideRight, width - 4));
		int buttonsPerRow = perRow->currentData().toInt();
		int buttonW = (width - 4) / buttonsPerRow - 6;
		for (Button &b : col.buttons)
			b.button->setText(b.button->fontMetrics().elidedText(b.name, Qt::ElideRight, std::max(10, buttonW)));
	}

	QToolButton *follow, *reserve, *scrollbar;
	QComboBox *perRow;
	QScrollArea *scroll;
	QWidget *content;
	QLabel *empty;
	QTimer timer;
	QString lastSignature = "-"; // never matches, so the first tick builds the columns
	std::vector<Column> columns;
};

} // namespace

void install_fx_dock()
{
	obs_frontend_add_dock_by_id(kDockId, "VST Ex Mixer", new FxDock());
}

void remove_fx_dock()
{
	obs_frontend_remove_dock(kDockId);
}

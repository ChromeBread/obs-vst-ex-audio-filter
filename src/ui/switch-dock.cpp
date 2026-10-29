#include "ui/switch-dock.h"

#include "plugin-meta.h"
#include "switch-sets.h"
#include "ui/chain-view.h"
#include "ui/ui-text.h"
#include "loader-api.h"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <vector>

namespace {

constexpr const char *kDockId = "vst-ex-switch-dock";
constexpr int kColumns = 2;
constexpr int kTickMs = 500;
constexpr const char *kMemoKey = "vstex_switch_memo"; // per source, in its private settings (saved with the scene collection)

QString memoOf(obs_source_t *source)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	return QString::fromUtf8(obs_data_get_string(priv, kMemoKey));
}

void setMemo(obs_source_t *source, const QString &memo)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	obs_data_set_string(priv, kMemoKey, memo.toUtf8().constData());
}

QToolButton *small(QWidget *parent, const QString &text, const QString &tip)
{
	auto *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	b->setAutoRaise(true);
	return b;
}

// A set is active when every filter it knows is in its state (and its offset/volume, if it has them).
bool isActive(obs_source_t *source, const switchsets::Set &set)
{
	if (set.hasAudio && (obs_source_get_sync_offset(source) != set.syncOffsetNs ||
			     qAbs(obs_source_get_volume(source) - set.volume) > 0.0001f))
		return false;
	struct Ctx {
		const switchsets::Set *set;
		bool match = true;
	} ctx{&set};
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			auto *c = static_cast<Ctx *>(param);
			auto it = c->set->enabled.find(QString::fromUtf8(obs_source_get_uuid(filter)));
			if (it != c->set->enabled.end() && it.value() != obs_source_enabled(filter))
				c->match = false;
		},
		&ctx);
	return ctx.match;
}

class SwitchDock : public QWidget {
public:
	SwitchDock() : QWidget(nullptr)
	{
		cpu = new QLabel(this);
		cpu->setToolTip(T("Dock.Cpu.Tip"));
		// Display options, layout editing and the how-to, out of the way in one menu.
		auto &meta = PluginMeta::instance();
		auto *options = new QToolButton(this);
		options->setText(QStringLiteral("⋯"));
		options->setToolTip(T("Dock.Options"));
		options->setPopupMode(QToolButton::InstantPopup);
		auto *menu = new QMenu(options);
		showNames = menu->addAction(T("Dock.ShowNames"));
		showNames->setCheckable(true);
		showNames->setChecked(meta.uiValue("switcherNames").toBool(true));
		showMemo = menu->addAction(T("Dock.ShowMemo"));
		showMemo->setCheckable(true);
		showMemo->setChecked(meta.uiValue("switcherMemo").toBool(false));
		editMode = menu->addAction(T("Dock.Edit"));
		editMode->setCheckable(true);
		menu->addSeparator();
		connect(menu->addAction(T("Dock.HowTo")), &QAction::triggered, this, &SwitchDock::showHowTo);
		options->setMenu(menu);
		auto relayout = [this] {
			PluginMeta::instance().setUiValue("switcherNames", showNames->isChecked());
			PluginMeta::instance().setUiValue("switcherMemo", showMemo->isChecked());
			lastSignature = "-";
			tick();
		};
		for (QAction *a : {showNames, showMemo, editMode})
			connect(a, &QAction::toggled, this, relayout);
		auto *top = new QHBoxLayout();
		top->addWidget(cpu, 1);
		top->addWidget(options);
		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		content = new QWidget();
		scroll->setWidget(content);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);
		layout->addLayout(top);
		layout->addWidget(scroll, 1);
		if (PluginMeta::instance().uiValue("featureChainView").toBool(true)) {
			auto *chain = new QPushButton(T("Chain.Open"), this);
			connect(chain, &QPushButton::clicked, this, [] { open_chain_view(); });
			layout->addWidget(chain);
		}

		usage = os_cpu_usage_info_start();
		lastWall = os_gettime_ns();
		lastVst = loader_process_ns();
		connect(&timer, &QTimer::timeout, this, &SwitchDock::tick);
		timer.start(kTickMs);
		tick();
	}
	~SwitchDock() override { os_cpu_usage_info_destroy(usage); }

private:
	struct Entry {
		OBSWeakSource source;
		switchsets::Set set;
		QPushButton *button;
	};

	void tick()
	{
		updateCpu();
		// Rebuild only when sources or sets changed; otherwise just light the active buttons.
		QString signature;
		std::vector<std::pair<OBSSource, QVector<switchsets::Set>>> found;
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				auto sets = switchsets::load(source);
				if (!sets.isEmpty())
					static_cast<decltype(found) *>(param)->emplace_back(source, sets);
				return true;
			},
			&found);
		// Sources in the order the user arranged them; ones not arranged yet keep OBS's order, after those.
		QStringList order;
		for (const auto &v : PluginMeta::instance().uiValue("switcherOrder").toArray())
			order << v.toString();
		auto rank = [&order](const OBSSource &s) {
			int i = (int)order.indexOf(QString::fromUtf8(obs_source_get_uuid(s)));
			return i < 0 ? INT_MAX : i;
		};
		std::stable_sort(found.begin(), found.end(), [&](const auto &a, const auto &b) { return rank(a.first) < rank(b.first); });
		for (auto &[source, sets] : found) {
			signature += QString::fromUtf8(obs_source_get_uuid(source)) + QString::fromUtf8(obs_source_get_name(source)) +
				     // while editing, a typed memo must not rebuild the dock under the next memo field
				     (editMode->isChecked() ? QString() : memoOf(source));
			for (const auto &set : sets)
				signature += set.id + set.name + QString::number(set.syncOffsetNs) + QString::number(set.volume) +
					     QString::number(set.enabled.size());
		}
		if (signature != lastSignature) {
			lastSignature = signature;
			rebuild(found);
		}
		for (Entry &e : entries) {
			OBSSourceAutoRelease source = obs_weak_source_get_source(e.source);
			bool on = source && isActive(source, e.set);
			e.button->setStyleSheet(on ? "QPushButton{background:#2e7d32;color:white;font-weight:bold;}" : "");
		}
	}

	void rebuild(const std::vector<std::pair<OBSSource, QVector<switchsets::Set>>> &found)
	{
		entries.clear();
		delete content->layout();
		qDeleteAll(content->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
		auto *layout = new QVBoxLayout(content);
		layout->setContentsMargins(0, 0, 0, 0);
		if (found.empty()) {
			auto *hint = new QLabel(T("Dock.Empty"), content);
			hint->setWordWrap(true);
			hint->setEnabled(false);
			layout->addWidget(hint);
		}
		const bool editing = editMode->isChecked();
		QStringList uuids;
		for (const auto &[source, sets] : found)
			uuids << QString::fromUtf8(obs_source_get_uuid(source));
		for (int g = 0; g < (int)found.size(); g++) {
			const OBSSource &source = found[g].first;
			const QVector<switchsets::Set> &sets = found[g].second;
			OBSWeakSource weak = OBSGetWeakRef(source);
			QString name = QString::fromUtf8(obs_source_get_name(source));
			if (editing) // memo field and the source's place among the others
				layout->addLayout(editHeader(weak, name, uuids, g));
			else if (showNames->isChecked()) {
				QString memo = memoOf(source);
				QString text = showMemo->isChecked() && !memo.isEmpty() ? memo : name;
				layout->addWidget(new QLabel("<b>" + text.toHtmlEscaped() + "</b>", content));
			}
			auto *grid = new QGridLayout();
			for (int i = 0; i < sets.size(); i++) {
				auto *b = new QPushButton(sets[i].name, content);
				b->setMinimumHeight(32);
				switchsets::Set set = sets[i];
				connect(b, &QPushButton::clicked, this, [weak, set] {
					OBSSourceAutoRelease s = obs_weak_source_get_source(weak);
					if (s)
						switchsets::apply(s, set);
				});
				entries.push_back({weak, set, b});
				if (!editing) {
					grid->addWidget(b, i / kColumns, i % kColumns);
					continue;
				}
				// Editing: arrows move the button one place earlier or later.
				auto *cell = new QHBoxLayout();
				cell->setSpacing(0);
				QToolButton *left = small(content, QStringLiteral("◀"), T("Dock.MoveEarlier"));
				QToolButton *right = small(content, QStringLiteral("▶"), T("Dock.MoveLater"));
				left->setEnabled(i > 0);
				right->setEnabled(i < sets.size() - 1);
				connect(left, &QToolButton::clicked, this, [this, weak, i] { moveSet(weak, i, i - 1); });
				connect(right, &QToolButton::clicked, this, [this, weak, i] { moveSet(weak, i, i + 1); });
				cell->addWidget(left);
				cell->addWidget(b, 1);
				cell->addWidget(right);
				grid->addLayout(cell, i / kColumns, i % kColumns);
			}
			layout->addLayout(grid);
		}
		layout->addStretch(1);
	}

	// Edit mode header: memo (shown instead of the name when that option is on) and up / down for the source.
	QHBoxLayout *editHeader(const OBSWeakSource &weak, const QString &name, const QStringList &uuids, int g)
	{
		auto *row = new QHBoxLayout();
		OBSSourceAutoRelease source = obs_weak_source_get_source(weak);
		auto *memo = new QLineEdit(source ? memoOf(source) : QString(), content);
		memo->setPlaceholderText(name);
		memo->setToolTip(T("Dock.Memo.Tip"));
		connect(memo, &QLineEdit::editingFinished, this, [weak, memo] {
			OBSSourceAutoRelease s = obs_weak_source_get_source(weak);
			if (s)
				setMemo(s, memo->text().trimmed());
		});
		QToolButton *up = small(content, QStringLiteral("▲"), T("Dock.MoveUp"));
		QToolButton *down = small(content, QStringLiteral("▼"), T("Dock.MoveDown"));
		up->setEnabled(g > 0);
		down->setEnabled(g < uuids.size() - 1);
		connect(up, &QToolButton::clicked, this, [this, uuids, g] { moveSource(uuids, g, g - 1); });
		connect(down, &QToolButton::clicked, this, [this, uuids, g] { moveSource(uuids, g, g + 1); });
		row->addWidget(memo, 1);
		row->addWidget(up);
		row->addWidget(down);
		return row;
	}

	// Deferred: the clicked button is deleted by the rebuild.
	void moveSet(const OBSWeakSource &weak, int from, int to)
	{
		QTimer::singleShot(0, this, [this, weak, from, to] {
			OBSSourceAutoRelease s = obs_weak_source_get_source(weak);
			if (!s)
				return;
			QVector<switchsets::Set> sets = switchsets::load(s);
			if (from < 0 || to < 0 || from >= sets.size() || to >= sets.size())
				return;
			sets.move(from, to);
			switchsets::save(s, sets); // hotkeys follow the set id, not its place
			lastSignature = "-";
			tick();
		});
	}

	void moveSource(QStringList uuids, int from, int to)
	{
		QTimer::singleShot(0, this, [this, uuids, from, to]() mutable {
			uuids.move(from, to);
			PluginMeta::instance().setUiValue("switcherOrder", QJsonArray::fromStringList(uuids));
			lastSignature = "-";
			tick();
		});
	}

	void showHowTo()
	{
		QMessageBox box(this);
		box.setWindowTitle(T("Dock.HowTo.Title"));
		box.setTextFormat(Qt::RichText);
		box.setText(T("Dock.HowTo.Text"));
		box.exec();
	}

	// OBS's process CPU (as in its stats), and VST processing time over the same period, both as % of all cores.
	void updateCpu()
	{
		uint64_t wall = os_gettime_ns(), vst = loader_process_ns();
		double vstPct = wall > lastWall ? 100.0 * double(vst - lastVst) / double(wall - lastWall) /
							 qMax(1, os_get_logical_cores())
						: 0.0;
		lastWall = wall;
		lastVst = vst;
		cpu->setText(T("Dock.Cpu").arg(os_cpu_usage_info_query(usage), 0, 'f', 1).arg(vstPct, 0, 'f', 1));
	}

	QLabel *cpu;
	QAction *showNames, *showMemo, *editMode;
	QWidget *content;
	QTimer timer;
	os_cpu_usage_info_t *usage;
	uint64_t lastWall, lastVst;
	QString lastSignature = "-"; // never matches, so the first tick builds the buttons
	std::vector<Entry> entries;
};

} // namespace

void install_switch_dock()
{
	obs_frontend_add_dock_by_id(kDockId, "VST Ex Switcher", new SwitchDock());
}

void remove_switch_dock()
{
	obs_frontend_remove_dock(kDockId);
}

#include "ui/switch-dock.h"

#include "switch-sets.h"
#include "ui/chain-view.h"
#include "ui/ui-text.h"
#include "vst-filter.h"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <vector>

namespace {

constexpr const char *kDockId = "vst-ex-switch-dock";
constexpr int kColumns = 2;
constexpr int kTickMs = 500;

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
		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		content = new QWidget();
		scroll->setWidget(content);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);
		layout->addWidget(cpu);
		layout->addWidget(scroll, 1);
		auto *chain = new QPushButton(T("Chain.Open"), this);
		connect(chain, &QPushButton::clicked, this, [] { open_chain_view(); });
		layout->addWidget(chain);

		usage = os_cpu_usage_info_start();
		lastWall = os_gettime_ns();
		lastVst = vst_ex_total_process_ns();
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
		for (auto &[source, sets] : found) {
			signature += QString::fromUtf8(obs_source_get_uuid(source)) + QString::fromUtf8(obs_source_get_name(source));
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
		for (const auto &[source, sets] : found) {
			layout->addWidget(new QLabel("<b>" + QString::fromUtf8(obs_source_get_name(source)).toHtmlEscaped() + "</b>",
						     content));
			auto *grid = new QGridLayout();
			for (int i = 0; i < sets.size(); i++) {
				auto *b = new QPushButton(sets[i].name, content);
				b->setMinimumHeight(32);
				OBSWeakSource weak = OBSGetWeakRef(source);
				switchsets::Set set = sets[i];
				connect(b, &QPushButton::clicked, this, [weak, set] {
					OBSSourceAutoRelease s = obs_weak_source_get_source(weak);
					if (s)
						switchsets::apply(s, set);
				});
				grid->addWidget(b, i / kColumns, i % kColumns);
				entries.push_back({weak, set, b});
			}
			layout->addLayout(grid);
		}
		layout->addStretch(1);
	}

	// OBS's process CPU (as in its stats), and VST processing time over the same period, both as % of all cores.
	void updateCpu()
	{
		uint64_t wall = os_gettime_ns(), vst = vst_ex_total_process_ns();
		double vstPct = wall > lastWall ? 100.0 * double(vst - lastVst) / double(wall - lastWall) /
							 qMax(1, os_get_logical_cores())
						: 0.0;
		lastWall = wall;
		lastVst = vst;
		cpu->setText(T("Dock.Cpu").arg(os_cpu_usage_info_query(usage), 0, 'f', 1).arg(vstPct, 0, 'f', 1));
	}

	QLabel *cpu;
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
	obs_frontend_add_dock_by_id(kDockId, "VST Ex", new SwitchDock());
}

void remove_switch_dock()
{
	obs_frontend_remove_dock(kDockId);
}

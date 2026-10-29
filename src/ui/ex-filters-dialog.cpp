#include "ui/ex-filters-dialog.h"

#include "pitch-filter.h"
#include "plugin-meta.h"
#include "switch-sets.h"
#include "ui/filter-history.h"
#include "ui/properties-panel.h"
#include "ui/sync-dialog.h"
#include "ui/ui-text.h"
#include "loader-api.h"

#include <obs-frontend-api.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QHash<QString, QPointer<ExFiltersDialog>> openDialogs; // by source uuid

// Chain presets: {"name": "<chain json>"} in <data folder>/presets/chain-presets.json, shared by all sources.
QString presetFile()
{
	return PluginMeta::instance().dataDir() + "/presets/chain-presets.json";
}

QJsonObject loadPresets()
{
	QFile f(presetFile());
	return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

bool savePresets(const QJsonObject &presets)
{
	QDir().mkpath(QFileInfo(presetFile()).path());
	QSaveFile f(presetFile());
	return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(presets).toJson()) >= 0 && f.commit();
}

const char *kChainFilter = "VST Ex chain (*.vstexchain)";

bool isAudioFilter(obs_source_t *filter)
{
	uint32_t flags = obs_source_get_output_flags(filter);
	return (flags & OBS_SOURCE_AUDIO) && !(flags & OBS_SOURCE_VIDEO);
}

// "Name", "Name 2", "Name 3", ...
QString uniqueFilterName(obs_source_t *source, const QString &base)
{
	QString name = base;
	for (int n = 2;; n++) {
		OBSSourceAutoRelease existing = obs_source_get_filter_by_name(source, name.toUtf8().constData());
		if (!existing)
			return name;
		name = QString("%1 %2").arg(base).arg(n);
	}
}

// Short type label that shows which plugin a VST-hosting filter runs, instead of just "VST 2.x Plug-in".
QString typeLabel(obs_source_t *filter)
{
	const char *id = obs_source_get_unversioned_id(filter);
	OBSDataAutoRelease s = obs_source_get_settings(filter);
	if (strcmp(id, VST_EX_FILTER_ID) == 0) {
		QString name = QString::fromUtf8(obs_data_get_string(s, S_PLUGIN_NAME));
		return QString("%1: %2").arg(QString::fromUtf8(obs_data_get_string(s, S_PLUGIN_FORMAT)), name);
	}
	if (strcmp(id, "vst_filter") == 0) {
		QString path = QString::fromUtf8(obs_data_get_string(s, "plugin_path"));
		if (!path.isEmpty())
			return QStringLiteral("VST2: ") + QFileInfo(path).completeBaseName();
	}
	if (strcmp(id, "atkaudio_plugin_host") == 0) {
		// atkAudio keeps its plugin list as XML inside one string setting: <PLUGIN name="..." format="..." .../>
		static const QRegularExpression re(R"re(<PLUGIN name="([^"]*)" format="([^"]*)")re");
		QStringList names;
		auto it = re.globalMatch(QString::fromUtf8(obs_data_get_string(s, "atkaudio_plugin_host")));
		while (it.hasNext()) {
			auto m = it.next();
			names << QString("%1 [%2]").arg(m.captured(1), m.captured(2));
		}
		if (!names.isEmpty())
			return QStringLiteral("atk: ") + names.join(", ");
	}
	return QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(filter)));
}

// Latency in samples for filters we can ask (VST Ex, our pitch filter); -1 for any other filter.
int filterLatencySamples(obs_source_t *filter)
{
	if (strcmp(obs_source_get_unversioned_id(filter), VST_EX_FILTER_ID) == 0)
		return loader_latency_samples(filter);
	return vst_ex_pitch_latency_samples(filter);
}

void styleToggle(QPushButton *b, bool on)
{
	b->setText(on ? QStringLiteral("ON") : QStringLiteral("OFF"));
	b->setStyleSheet(on ? "QPushButton{background:#2e7d32;color:white;font-weight:bold;border-radius:3px;padding:2px 6px;}"
			    : "QPushButton{background:#5a5a5a;color:#ddd;border-radius:3px;padding:2px 6px;}");
	b->setAccessibleName(on ? QStringLiteral("Filter enabled") : QStringLiteral("Filter disabled"));
}

} // namespace

QString filter_type_label(obs_source_t *filter)
{
	return typeLabel(filter);
}

int filter_latency_samples(obs_source_t *filter)
{
	return filterLatencySamples(filter);
}

void ExFiltersDialog::onChainSignal(void *data, calldata_t *)
{
	auto *self = static_cast<ExFiltersDialog *>(data);
	QMetaObject::invokeMethod(self, [self] {
		if (self->refreshQueued)
			return;
		self->refreshQueued = true;
		QTimer::singleShot(0, self, [self] {
			self->refreshQueued = false;
			self->refreshList();
			self->scheduleRecord(T("History.Changed"));
		});
	});
}

void ExFiltersDialog::notifyPluginEdited(obs_source_t *filter)
{
	obs_source_t *parent = obs_filter_get_parent(filter);
	if (!parent)
		return;
	if (auto dlg = openDialogs.value(QString::fromUtf8(obs_source_get_uuid(parent))))
		dlg->scheduleRecord(T("History.Settings").arg(QString::fromUtf8(obs_source_get_name(filter))));
}

void ExFiltersDialog::open(obs_source_t *source)
{
	QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	if (auto dlg = openDialogs.value(uuid)) {
		dlg->raise();
		dlg->activateWindow();
		return;
	}
	auto *dlg = new ExFiltersDialog(source, static_cast<QWidget *>(obs_frontend_get_main_window()));
	openDialogs.insert(uuid, dlg);
	dlg->show();
}

ExFiltersDialog::ExFiltersDialog(obs_source_t *src, QWidget *parent) : QDialog(parent), source(src)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(T("ExFilters.Title").arg(QString::fromUtf8(obs_source_get_name(source))));
	resize(900, 560);

	auto *addButton = new QPushButton(T("Add"), this);
	removeButton = new QPushButton(T("Delete"), this);
	auto *removeAllButton = new QPushButton(T("DeleteAll"), this);
	removeAllButton->setToolTip(T("DeleteAll.Tip"));
	removeAllButton->setStyleSheet("QPushButton{color:#ff8a80;}");
	auto *upButton = new QPushButton(QStringLiteral("▲"), this);
	auto *downButton = new QPushButton(QStringLiteral("▼"), this);
	undoButton = new QPushButton(T("Undo"), this);
	redoButton = new QPushButton(T("Redo"), this);
	auto *historyButton = new QPushButton(T("History"), this);
	switchButton = new QPushButton(T("Switch") + QStringLiteral(" ▼"), this);
	switchButton->setToolTip(T("Switch.Tip"));
	presetButton = new QPushButton(T("Preset") + QStringLiteral(" ▼"), this);
	presetButton->setToolTip(T("Preset.Tip"));
	auto *hiddenButton = new QPushButton(T("HidePluginRows"), this);
	hiddenButton->setCheckable(true);
	hiddenButton->setToolTip(T("HidePluginRows.Tip"));
	upButton->setToolTip(T("MoveUp"));
	downButton->setToolTip(T("MoveDown"));
	upButton->setAccessibleName(T("MoveUp"));
	downButton->setAccessibleName(T("MoveDown"));
	undoButton->setToolTip("Ctrl+Z");
	redoButton->setToolTip("Ctrl+Y");
	removeButton->setToolTip("Delete");

	auto *toolbar = new QHBoxLayout();
	toolbar->addWidget(addButton);
	toolbar->addWidget(removeButton);
	toolbar->addWidget(removeAllButton);
	toolbar->addWidget(upButton);
	toolbar->addWidget(downButton);
	toolbar->addStretch(1);
	toolbar->addWidget(undoButton);
	toolbar->addWidget(redoButton);
	toolbar->addWidget(switchButton);
	toolbar->addWidget(presetButton);
	toolbar->addWidget(historyButton);
	toolbar->addWidget(hiddenButton);

	list = new QListWidget(this);
	list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	list->setContextMenuPolicy(Qt::CustomContextMenu);
	list->setUniformItemSizes(true);
	list->setDragDropMode(QAbstractItemView::InternalMove);
	list->setDefaultDropAction(Qt::MoveAction);
	// Qt moved the rows; apply the new order to OBS once the drop is finished.
	connect(list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
		QTimer::singleShot(0, this, &ExFiltersDialog::applyListOrder);
	});

	auto *left = new QWidget(this);
	auto *leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->addLayout(toolbar);
	leftLayout->addWidget(list, 1);
	latencyLabel = new QLabel(left);
	latencyLabel->setToolTip(T("Latency.Tip"));
	latencyButton = new QPushButton(left);
	latencyButton->setToolTip(T("Latency.Apply.Tip"));
	connect(latencyButton, &QPushButton::clicked, this, [this] {
		obs_source_set_sync_offset(source, -latencyNs);
		refreshList();
		record(T("Latency.Applied").arg(-latencyNs / 1000000));
	});
	auto *measureButton = new QPushButton(T("Sync.Open"), left);
	measureButton->setToolTip(T("Sync.Open.Tip"));
	connect(measureButton, &QPushButton::clicked, this, [this] {
		int64_t before = obs_source_get_sync_offset(source);
		SyncDialog::run(this, source);
		refreshList();
		if (obs_source_get_sync_offset(source) != before)
			record(T("Latency.Applied").arg(obs_source_get_sync_offset(source) / 1000000));
	});
	auto *latencyRow = new QHBoxLayout();
	latencyRow->addWidget(latencyLabel, 1);
	latencyRow->addWidget(latencyButton);
	latencyRow->addWidget(measureButton);
	leftLayout->addLayout(latencyRow);

	auto *hint = new QLabel(T("ExFilters.Hint"), left);
	hint->setWordWrap(true);
	hint->setEnabled(false);
	leftLayout->addWidget(hint);

	props = new PropertiesPanel(this);
	bool hide = PluginMeta::instance().uiValue("hidePluginRows").toBool(false);
	hiddenButton->setChecked(hide);
	applyHiddenRows(hide);
	connect(hiddenButton, &QPushButton::toggled, this, [this](bool on) {
		PluginMeta::instance().setUiValue("hidePluginRows", on);
		applyHiddenRows(on);
	});
	connect(&PluginMeta::instance(), &PluginMeta::uiChanged, this, [hiddenButton](const QString &key) {
		if (key == "hidePluginRows") // e.g. switched from the Tools dialog
			hiddenButton->setChecked(PluginMeta::instance().uiValue(key).toBool());
	});
	auto *splitter = new QSplitter(this);
	splitter->addWidget(left);
	splitter->addWidget(props);
	splitter->setStretchFactor(0, 2);
	splitter->setStretchFactor(1, 3);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(splitter, 1);
	layout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(addButton, &QPushButton::clicked, this, &ExFiltersDialog::showAddMenu);
	connect(removeButton, &QPushButton::clicked, this, &ExFiltersDialog::removeSelected);
	connect(removeAllButton, &QPushButton::clicked, this, [this] {
		list->selectAll(); // same path as deleting a selection, so Undo brings everything back
		removeSelected();
	});
	connect(upButton, &QPushButton::clicked, this, [this] { moveSelected(-1); });
	connect(downButton, &QPushButton::clicked, this, [this] { moveSelected(1); });
	connect(undoButton, &QPushButton::clicked, this, [this] { FilterHistory::instance().undo(source); });
	connect(redoButton, &QPushButton::clicked, this, [this] { FilterHistory::instance().redo(source); });
	connect(historyButton, &QPushButton::clicked, this, &ExFiltersDialog::showHistory);
	connect(presetButton, &QPushButton::clicked, this, &ExFiltersDialog::showPresetMenu);
	connect(switchButton, &QPushButton::clicked, this, &ExFiltersDialog::showSwitchMenu);
	connect(list, &QListWidget::itemSelectionChanged, this, &ExFiltersDialog::selectionChanged);
	connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		if (OBSSource f = filterForItem(item))
			openFilterUi(f);
	});
	connect(list, &QListWidget::customContextMenuRequested, this, &ExFiltersDialog::showContextMenu);
	connect(props, &PropertiesPanel::memoChanged, this, [this] {
		refreshList();
		if (props->source())
			record(T("History.Memo").arg(QString::fromUtf8(obs_source_get_name(props->source()))));
	});
	connect(props, &PropertiesPanel::edited, this, [this] {
		if (props->source())
			scheduleRecord(T("History.Settings").arg(QString::fromUtf8(obs_source_get_name(props->source()))));
	});
	connect(&FilterHistory::instance(), &FilterHistory::changed, this, [this](obs_source_t *s) {
		if (s == source)
			updateHistoryButtons();
	});

	recordTimer.setSingleShot(true);
	recordTimer.setInterval(1500);
	connect(&recordTimer, &QTimer::timeout, this, [this] { record(pendingLabel); });

	// Keep the list in sync with changes from anywhere (OBS's own Filters window, scripts, undo).
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	for (const char *sig : {"filter_add", "filter_remove", "reorder_filters"})
		signals_.emplace_back(sh, sig, onChainSignal, this);
	signals_.emplace_back(sh, "remove", [](void *data, calldata_t *) {
		auto *self = static_cast<ExFiltersDialog *>(data);
		QMetaObject::invokeMethod(self, &QDialog::close);
	}, this);

	refreshList();
	record(T("History.Opened")); // baseline, so the first change can be undone
	updateHistoryButtons();
	if (list->count() > 0)
		list->setCurrentRow(0);
}

ExFiltersDialog::~ExFiltersDialog()
{
	filterSignals.clear();
	signals_.clear();
	if (recordTimer.isActive())
		record(pendingLabel);
	openDialogs.remove(QString::fromUtf8(obs_source_get_uuid(source)));
}

OBSSource ExFiltersDialog::filterForItem(QListWidgetItem *item) const
{
	if (!item)
		return nullptr;
	QByteArray uuid = item->data(Qt::UserRole).toString().toUtf8();
	OBSSourceAutoRelease f = obs_get_source_by_uuid(uuid.constData());
	return f.Get();
}

std::vector<OBSSource> ExFiltersDialog::selectedFilters() const
{
	std::vector<OBSSource> out;
	for (int i = 0; i < list->count(); i++)
		if (list->item(i)->isSelected())
			if (OBSSource f = filterForItem(list->item(i)))
				out.push_back(f);
	return out;
}

void ExFiltersDialog::refreshList()
{
	QStringList selected;
	for (auto *item : list->selectedItems())
		selected << item->data(Qt::UserRole).toString();
	QString current = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
	int scroll = list->verticalScrollBar()->value(); // rebuilding must not move the view

	std::vector<OBSSource> filters;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			if (isAudioFilter(filter))
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(filter);
		},
		&filters);

	QSignalBlocker block(list);
	list->clear();
	latencyNs = 0;
	filterSignals.clear();

	for (OBSSource &f : filters) {
		QString uuid = QString::fromUtf8(obs_source_get_uuid(f));
		auto *item = new QListWidgetItem();
		item->setData(Qt::UserRole, uuid);
		list->addItem(item);

		auto *row = new QWidget();
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(4, 2, 4, 2);
		auto *toggle = new QPushButton(row);
		toggle->setFixedWidth(48);
		toggle->setFocusPolicy(Qt::NoFocus);
		styleToggle(toggle, obs_source_enabled(f));
		auto *name = new QLabel(QString::fromUtf8(obs_source_get_name(f)), row);
		OBSDataAutoRelease fs = obs_source_get_settings(f);
		auto *memo = new QLabel(QString::fromUtf8(obs_data_get_string(fs, S_VSTEX_MEMO)), row);
		memo->setStyleSheet("color:#9ab;font-style:italic;");
		memo->setVisible(!memo->text().isEmpty());
		auto *type = new QLabel(typeLabel(f), row);
		type->setToolTip(QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(f))));
		type->setEnabled(false);
		h->addWidget(toggle);
		h->addWidget(name);
		h->addWidget(memo, 1);
		h->addWidget(type);
		int samples = filterLatencySamples(f);
		if (samples > 0) {
			double ms = 1000.0 * samples / audio_output_get_sample_rate(obs_get_audio());
			auto *lat = new QLabel(QString("%1 ms").arg(ms, 0, 'f', 1), row);
			lat->setStyleSheet("color:#e0b050;");
			lat->setToolTip(T("Latency.Tip"));
			h->addWidget(lat);
			if (obs_source_enabled(f))
				latencyNs += (int64_t)(ms * 1000000.0);
		}
		item->setSizeHint(row->sizeHint());
		list->setItemWidget(item, row);

		OBSWeakSource weak = OBSGetWeakRef(f);
		connect(toggle, &QPushButton::clicked, this, [this, weak, toggle] {
			OBSSourceAutoRelease filter = obs_weak_source_get_source(weak);
			if (!filter)
				return;
			bool on = !obs_source_enabled(filter);
			obs_source_set_enabled(filter, on);
			styleToggle(toggle, on);
			record(QString("%1: %2").arg(on ? "ON" : "OFF", QString::fromUtf8(obs_source_get_name(filter))));
		});

		signal_handler_t *fsh = obs_source_get_signal_handler(f);
		filterSignals.emplace_back(fsh, "enable", onChainSignal, this);
		filterSignals.emplace_back(fsh, "rename", onChainSignal, this);

		if (selected.contains(uuid))
			item->setSelected(true);
		if (uuid == current)
			list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
	}
	list->doItemsLayout();
	list->verticalScrollBar()->setValue(scroll);

	int64_t offsetNs = obs_source_get_sync_offset(source);
	latencyLabel->setText(T("Latency.Total").arg(latencyNs / 1e6, 0, 'f', 1).arg(offsetNs / 1000000));
	latencyButton->setText(T("Latency.Apply").arg(-latencyNs / 1000000));
	latencyButton->setEnabled(offsetNs != -latencyNs);
	selectionChanged();
}

void ExFiltersDialog::selectionChanged()
{
	auto selected = selectedFilters();
	removeButton->setEnabled(!selected.empty());
	OBSSource shown = list->currentItem() && list->currentItem()->isSelected() ? filterForItem(list->currentItem())
										  : (selected.empty() ? nullptr : selected.front());
	if (shown.Get() != props->source())
		props->setSource(shown);
}

void ExFiltersDialog::showAddMenu()
{
	QMenu menu(this);
	QAction *vst = menu.addAction(T("AddVst"));
	QFont bold = vst->font();
	bold.setBold(true);
	vst->setFont(bold);
	connect(vst, &QAction::triggered, this, &ExFiltersDialog::addVstFilter);
	if (!loader_installed()) { // VST hosting is the separate VST Enigmax Loader module
		vst->setText(T("AddVst.NoLoader"));
		vst->setEnabled(false);
	}
	menu.addSeparator();

	struct Type {
		QString name;
		std::string id;
	};
	std::vector<Type> types;
	const char *id;
	for (size_t i = 0; obs_enum_filter_types(i, &id); i++) {
		uint32_t flags = obs_get_source_output_flags(id);
		if ((flags & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED)) || !(flags & OBS_SOURCE_AUDIO) ||
		    (flags & OBS_SOURCE_VIDEO))
			continue;
		types.push_back({QString::fromUtf8(obs_source_get_display_name(id)), id});
	}
	std::sort(types.begin(), types.end(), [](const Type &a, const Type &b) { return a.name < b.name; });
	for (const Type &t : types) {
		std::string typeId = t.id;
		QString name = t.name;
		connect(menu.addAction(name), &QAction::triggered, this,
			[this, typeId, name] { addFilter(typeId.c_str(), name, nullptr); });
	}
	menu.exec(QCursor::pos());
}

void ExFiltersDialog::addFilter(const char *type, const QString &baseName, obs_data_t *settings)
{
	QString name = uniqueFilterName(source, baseName);
	OBSSourceAutoRelease filter = obs_source_create(type, name.toUtf8().constData(), settings, nullptr);
	if (!filter)
		return;
	obs_source_filter_add(source, filter);
	refreshList();
	for (int i = 0; i < list->count(); i++)
		if (list->item(i)->data(Qt::UserRole).toString() == QString::fromUtf8(obs_source_get_uuid(filter)))
			list->setCurrentRow(i);
	record(T("History.Added").arg(name));
}

void ExFiltersDialog::addVstFilter()
{
	OBSDataArrayAutoRelease list = loader_pick(this);
	for (size_t i = 0; i < obs_data_array_count(list); i++) {
		OBSDataAutoRelease settings = obs_data_array_item(list, i);
		addFilter(VST_EX_FILTER_ID, QString::fromUtf8(obs_data_get_string(settings, S_PLUGIN_NAME)), settings);
	}
}

void ExFiltersDialog::removeSelected()
{
	auto selected = selectedFilters();
	if (selected.empty())
		return; // no confirmation: Undo brings it back
	for (OBSSource &f : selected)
		obs_source_filter_remove(source, f);
	refreshList();
	record(selected.size() == 1 ? T("History.Removed").arg(QString::fromUtf8(obs_source_get_name(selected.front())))
				    : T("History.RemovedMany").arg(selected.size()));
}

void ExFiltersDialog::duplicateSelected()
{
	auto selected = selectedFilters();
	if (selected.empty())
		return;
	for (OBSSource &f : selected) {
		obs_source_save(f); // flushes the VST state into the settings that get copied
		QString name = uniqueFilterName(source, QString::fromUtf8(obs_source_get_name(f)));
		OBSSourceAutoRelease copy = obs_source_duplicate(f, name.toUtf8().constData(), false);
		if (!copy || copy.Get() == f.Get()) // OBS_SOURCE_DO_NOT_DUPLICATE types hand back the original
			continue;
		obs_source_set_enabled(copy, obs_source_enabled(f));
		obs_source_filter_add(source, copy);
		// OBS lists filters in reverse array order and add() put the copy at index 0 (the bottom);
		// one index below the original shows it right under the original.
		obs_source_filter_set_index(source, copy, (size_t)obs_source_filter_get_index(source, f) - 1);
	}
	refreshList();
	record(T("History.Duplicated").arg(selected.size() == 1 ? QString::fromUtf8(obs_source_get_name(selected.front()))
								 : QString::number(selected.size())));
}

// Double-click: the plugin's own window. VST Ex opens its editor; other filters get their "Open ..." button
// (e.g. OBS's VST 2.x filter), else OBS's properties window.
void ExFiltersDialog::openFilterUi(OBSSource filter)
{
	if (strcmp(obs_source_get_unversioned_id(filter), VST_EX_FILTER_ID) == 0) {
		loader_call_filter(filter, LOADER_PROC_OPEN_EDITOR);
		return;
	}
	obs_properties_t *p = obs_source_properties(filter);
	bool clicked = false;
	for (obs_property_t *prop = obs_properties_first(p); prop && !clicked; obs_property_next(&prop))
		if (obs_property_get_type(prop) == OBS_PROPERTY_BUTTON && obs_property_visible(prop) &&
		    QString::fromUtf8(obs_property_name(prop)).contains("open", Qt::CaseInsensitive)) {
			obs_property_button_clicked(prop, filter);
			clicked = true;
		}
	obs_properties_destroy(p);
	if (!clicked)
		obs_frontend_open_source_properties(filter);
}

void ExFiltersDialog::applyHiddenRows(bool hide)
{
	props->setHiddenProperties(hide ? QStringList{S_PLUGIN_INFO, S_CHOOSE_PLUGIN, S_PLUGIN_PATH} : QStringList());
}

void ExFiltersDialog::applyListOrder()
{
	// The list holds only the audio filters; give them back the chain positions they occupied, in list order.
	// OBS shows filters in reverse array order, so the bottom row gets the lowest index. Assigning from the
	// lowest index up keeps every already-placed filter where it is.
	std::vector<OBSSource> ordered;
	std::vector<size_t> positions;
	for (int i = list->count() - 1; i >= 0; i--)
		if (OBSSource f = filterForItem(list->item(i))) {
			ordered.push_back(f);
			positions.push_back((size_t)obs_source_filter_get_index(source, f));
		}
	std::sort(positions.begin(), positions.end());
	for (size_t i = 0; i < ordered.size(); i++)
		obs_source_filter_set_index(source, ordered[i], positions[i]);
	refreshList(); // dragged rows lose their ON/OFF widgets; rebuild them
	record(T("History.Moved"));
}

void ExFiltersDialog::moveSelected(int direction)
{
	auto selected = selectedFilters();
	if (selected.empty())
		return;
	// Move the block in the order that keeps relative positions when several rows are selected.
	if (direction > 0)
		std::reverse(selected.begin(), selected.end());
	for (OBSSource &f : selected)
		obs_source_filter_set_order(source, f, direction < 0 ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
	refreshList();
	record(T("History.Moved"));
}

void ExFiltersDialog::setSelectedEnabled(bool enabled)
{
	for (OBSSource &f : selectedFilters())
		obs_source_set_enabled(f, enabled);
	refreshList();
	record(enabled ? T("History.EnabledSelected") : T("History.DisabledSelected"));
}

void ExFiltersDialog::renameFilter(OBSSource filter)
{
	QString old = QString::fromUtf8(obs_source_get_name(filter));
	bool ok = false;
	QString name = QInputDialog::getText(this, T("Rename"), T("Rename.Prompt"), QLineEdit::Normal, old, &ok).trimmed();
	if (!ok || name.isEmpty() || name == old)
		return;
	OBSSourceAutoRelease clash = obs_source_get_filter_by_name(source, name.toUtf8().constData());
	if (clash) {
		QMessageBox::warning(this, T("Rename"), T("Rename.Exists").arg(name));
		return;
	}
	obs_source_set_name(filter, name.toUtf8().constData());
	refreshList();
	record(T("History.Renamed").arg(old, name));
}

void ExFiltersDialog::showContextMenu(const QPoint &pos)
{
	auto selected = selectedFilters();
	if (selected.empty())
		return;
	QMenu menu(this);
	connect(menu.addAction(T("EnableSelected")), &QAction::triggered, this, [this] { setSelectedEnabled(true); });
	connect(menu.addAction(T("DisableSelected")), &QAction::triggered, this, [this] { setSelectedEnabled(false); });
	menu.addSeparator();
	if (selected.size() == 1) {
		OBSSource f = selected.front();
		connect(menu.addAction(T("OpenEditor")), &QAction::triggered, this, [this, f] { openFilterUi(f); });
		connect(menu.addAction(T("Rename")), &QAction::triggered, this, [this, f] { renameFilter(f); });
	}
	QAction *dup = menu.addAction(T("Duplicate"));
	dup->setShortcut(QKeySequence("Ctrl+D"));
	connect(dup, &QAction::triggered, this, &ExFiltersDialog::duplicateSelected);
	connect(menu.addAction(T("Delete")), &QAction::triggered, this, &ExFiltersDialog::removeSelected);
	menu.exec(list->viewport()->mapToGlobal(pos));
}

// ON/OFF sets: only flip filters on or off, so switching mid-song is instant. Each set has an OBS hotkey.
void ExFiltersDialog::showSwitchMenu()
{
	QVector<switchsets::Set> sets = switchsets::load(source);
	QMenu menu(this);
	for (const auto &set : sets) {
		// e.g. "Chorus   (Sync 120 ms, -3.0 dB)"
		QString label = set.name;
		if (set.hasAudio)
			label += QString("   (Sync %1 ms, %2 dB)")
					 .arg(set.syncOffsetNs / 1000000)
					 .arg(set.volume > 0 ? QString::number(20.0 * std::log10(set.volume), 'f', 1)
							     : QStringLiteral("-inf"));
		connect(menu.addAction(label), &QAction::triggered, this, [this, set] {
			switchsets::apply(source, set);
			record(T("History.Switch").arg(set.name));
		});
	}
	if (!sets.isEmpty())
		menu.addSeparator();

	connect(menu.addAction(T("Switch.Save")), &QAction::triggered, this, [this] {
		bool ok = false;
		QString name = QInputDialog::getText(this, T("Switch.Save"), T("Switch.Name"), QLineEdit::Normal, QString(), &ok)
				       .trimmed();
		if (!ok || name.isEmpty())
			return;
		auto all = switchsets::load(source);
		all << switchsets::capture(source, name);
		switchsets::save(source, all);
	});
	QMenu *overwrite = menu.addMenu(T("Switch.Overwrite"));
	QMenu *remove = menu.addMenu(T("Switch.Delete"));
	overwrite->setEnabled(!sets.isEmpty());
	remove->setEnabled(!sets.isEmpty());
	for (int i = 0; i < sets.size(); i++) {
		QString name = sets[i].name;
		connect(overwrite->addAction(name), &QAction::triggered, this, [this, i] {
			auto all = switchsets::load(source);
			if (i >= all.size())
				return;
			auto fresh = switchsets::capture(source, all[i].name);
			fresh.id = all[i].id; // keep the id: its hotkey stays bound
			all[i] = fresh;
			switchsets::save(source, all);
		});
		connect(remove->addAction(name), &QAction::triggered, this, [this, i, name] {
			if (QMessageBox::question(this, T("Switch.Delete"), T("Switch.Delete.Confirm").arg(name)) !=
			    QMessageBox::Yes)
				return;
			auto all = switchsets::load(source);
			if (i < all.size())
				all.removeAt(i);
			switchsets::save(source, all);
		});
	}
	menu.addSeparator();
	menu.addAction(T("Switch.HotkeyHint"))->setEnabled(false);
	menu.exec(switchButton->mapToGlobal(QPoint(0, switchButton->height())));
}

// Presets: click one to put its whole chain on this source (Undo brings the old chain back).
void ExFiltersDialog::showPresetMenu()
{
	QJsonObject presets = loadPresets();
	QMenu menu(this);
	for (auto it = presets.begin(); it != presets.end(); ++it) {
		QString name = it.key();
		QByteArray json = it.value().toString().toUtf8();
		connect(menu.addAction(name), &QAction::triggered, this, [this, name, json] {
			FilterHistory::instance().applyChain(source, json, T("History.Preset").arg(name));
			refreshList();
		});
	}
	if (!presets.isEmpty())
		menu.addSeparator();

	connect(menu.addAction(T("Preset.Save")), &QAction::triggered, this, [this] {
		bool ok = false;
		QString name = QInputDialog::getText(this, T("Preset.Save"), T("Preset.Name"), QLineEdit::Normal,
						     QString::fromUtf8(obs_source_get_name(source)), &ok)
				       .trimmed();
		if (!ok || name.isEmpty())
			return;
		QJsonObject all = loadPresets();
		if (all.contains(name) &&
		    QMessageBox::question(this, T("Preset.Save"), T("Preset.Overwrite").arg(name)) != QMessageBox::Yes)
			return;
		all.insert(name, QString::fromUtf8(FilterHistory::capture(source)));
		if (!savePresets(all))
			QMessageBox::warning(this, T("Preset.Save"), presetFile());
	});

	QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	QMenu *exportMenu = menu.addMenu(T("Preset.Export"));
	QMenu *deleteMenu = menu.addMenu(T("Preset.Delete"));
	exportMenu->setEnabled(!presets.isEmpty());
	deleteMenu->setEnabled(!presets.isEmpty());
	for (auto it = presets.begin(); it != presets.end(); ++it) {
		QString name = it.key();
		QByteArray json = it.value().toString().toUtf8();
		connect(exportMenu->addAction(name), &QAction::triggered, this, [this, name, json, docs] {
			QString path = QFileDialog::getSaveFileName(this, T("Preset.Export"), docs + "/" + name + ".vstexchain",
								    kChainFilter);
			if (path.isEmpty())
				return;
			QSaveFile f(path);
			QJsonObject root{{"name", name}, {"chain", QString::fromUtf8(json)}};
			if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0 || !f.commit())
				QMessageBox::warning(this, T("Preset.Export"), f.errorString());
		});
		connect(deleteMenu->addAction(name), &QAction::triggered, this, [this, name] {
			if (QMessageBox::question(this, T("Preset.Delete"), T("Preset.Delete.Confirm").arg(name)) !=
			    QMessageBox::Yes)
				return;
			QJsonObject all = loadPresets();
			all.remove(name);
			savePresets(all);
		});
	}

	connect(menu.addAction(T("Preset.Import")), &QAction::triggered, this, [this, docs] {
		QString path = QFileDialog::getOpenFileName(this, T("Preset.Import"), docs, kChainFilter);
		if (path.isEmpty())
			return;
		QFile f(path);
		QJsonObject root = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
		QJsonObject chain = QJsonDocument::fromJson(root["chain"].toString().toUtf8()).object();
		if (!chain["filters"].isArray()) { // not a chain file; never apply guesses to the user's filters
			QMessageBox::warning(this, T("Preset.Import"), T("Preset.Import.Invalid"));
			return;
		}
		QString name = root["name"].toString(QFileInfo(path).completeBaseName());
		QJsonObject all = loadPresets();
		QString base = name;
		for (int n = 2; all.contains(name); n++)
			name = QString("%1 (%2)").arg(base).arg(n);
		all.insert(name, root["chain"].toString());
		savePresets(all);
	});
	menu.exec(presetButton->mapToGlobal(QPoint(0, presetButton->height())));
}

void ExFiltersDialog::showHistory()
{
	auto &history = FilterHistory::instance();
	auto snaps = history.snapshots(source);
	int pos = history.position(source);

	QDialog dlg(this);
	dlg.setWindowTitle(T("History"));
	dlg.resize(460, 420);
	auto *view = new QListWidget(&dlg);
	for (int i = (int)snaps.size() - 1; i >= 0; i--) {
		QString text = QString("%1   %2").arg(snaps[i].time.toString("yyyy-MM-dd HH:mm:ss"), snaps[i].label);
		if (i == pos)
			text += "   ◀ " + T("History.Current");
		auto *item = new QListWidgetItem(text, view);
		item->setData(Qt::UserRole, i);
	}
	auto *help = new QLabel(T("History.Help"), &dlg);
	help->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(&dlg);
	QPushButton *restore = buttons->addButton(T("History.Restore"), QDialogButtonBox::AcceptRole);
	buttons->addButton(QDialogButtonBox::Close);
	restore->setEnabled(false);
	connect(view, &QListWidget::currentRowChanged, restore, [restore](int row) { restore->setEnabled(row >= 0); });
	connect(view, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	auto *layout = new QVBoxLayout(&dlg);
	layout->addWidget(help);
	layout->addWidget(view, 1);
	layout->addWidget(buttons);

	if (dlg.exec() == QDialog::Accepted && view->currentItem())
		history.restore(source, view->currentItem()->data(Qt::UserRole).toInt());
}

void ExFiltersDialog::updateHistoryButtons()
{
	undoButton->setEnabled(FilterHistory::instance().canUndo(source));
	redoButton->setEnabled(FilterHistory::instance().canRedo(source));
}

void ExFiltersDialog::record(const QString &label)
{
	recordTimer.stop();
	FilterHistory::instance().record(source, label);
	updateHistoryButtons();
}

void ExFiltersDialog::scheduleRecord(const QString &label)
{
	pendingLabel = label;
	recordTimer.start();
}

void ExFiltersDialog::keyPressEvent(QKeyEvent *event)
{
	if (event->matches(QKeySequence::Undo)) {
		FilterHistory::instance().undo(source);
	} else if (event->matches(QKeySequence::Redo)) {
		FilterHistory::instance().redo(source);
	} else if (event->key() == Qt::Key_Delete && list->hasFocus()) {
		removeSelected();
	} else if (event->key() == Qt::Key_D && event->modifiers() == Qt::ControlModifier && list->hasFocus()) {
		duplicateSelected();
	} else if (event->key() == Qt::Key_F2 && list->hasFocus()) {
		if (OBSSource f = filterForItem(list->currentItem()))
			renameFilter(f);
	} else if (event->key() == Qt::Key_Space && list->hasFocus()) {
		auto selected = selectedFilters();
		if (!selected.empty())
			setSelectedEnabled(!obs_source_enabled(selected.front()));
	} else {
		QDialog::keyPressEvent(event);
	}
}

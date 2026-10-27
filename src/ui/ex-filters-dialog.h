#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QTimer>

#include <vector>

class PropertiesPanel;

// Short label for a filter, naming the plugin a VST-hosting filter runs (e.g. "VST3: ReaEQ").
QString filter_type_label(obs_source_t *filter);
// Latency in samples for filters that report it (VST Ex, VST Ex Pitch / Pan); -1 for any other filter.
int filter_latency_samples(obs_source_t *filter);
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Extended audio filter window: ON/OFF buttons, multi-select, undo/autosave history, VST2/VST3 plugins.
// Works on the same filter chain as OBS's own Filters window.
class ExFiltersDialog : public QDialog {
	Q_OBJECT

public:
	static void open(obs_source_t *source);
	// A plugin editor changed a VST filter; autosave it if its window is open.
	static void notifyPluginEdited(obs_source_t *filter);

	~ExFiltersDialog() override;

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	explicit ExFiltersDialog(obs_source_t *source, QWidget *parent);

	static void onChainSignal(void *data, calldata_t *cd);
	void refreshList();
	void selectionChanged();
	std::vector<OBSSource> selectedFilters() const;
	OBSSource filterForItem(QListWidgetItem *item) const;

	void showAddMenu();
	void addFilter(const char *type, const QString &baseName, obs_data_t *settings);
	void addVstFilter();
	void removeSelected();
	void duplicateSelected();
	void openFilterUi(OBSSource filter);
	void applyHiddenRows(bool hide);
	void moveSelected(int direction);
	void applyListOrder();
	void setSelectedEnabled(bool enabled);
	void renameFilter(OBSSource filter);
	void showHistory();
	void showPresetMenu();
	void showSwitchMenu();
	void showContextMenu(const QPoint &pos);
	void updateHistoryButtons();

	void record(const QString &label);
	void scheduleRecord(const QString &label);

	OBSSource source;
	std::vector<OBSSignal> signals_;
	std::vector<OBSSignal> filterSignals;
	QListWidget *list;
	PropertiesPanel *props;
	QPushButton *undoButton;
	QPushButton *redoButton;
	QPushButton *removeButton;
	QPushButton *presetButton;
	QPushButton *switchButton;
	class QLabel *latencyLabel;
	QPushButton *latencyButton;
	int64_t latencyNs = 0; // total of the filters that are ON
	QTimer recordTimer;
	QString pendingLabel;
	bool refreshQueued = false;
};

#pragma once

#include <obs.hpp>

#include <QPointer>
#include <QScrollArea>
#include <QStringList>

class QFormLayout;
class QLabel;
class QVBoxLayout;

// Renders a source's obs_properties as Qt widgets, like OBS's own properties view
// (which is not exported to plugins). Uncommon property types fall back to OBS's properties window.
class PropertiesPanel : public QScrollArea {
	Q_OBJECT

public:
	explicit PropertiesPanel(QWidget *parent = nullptr);
	~PropertiesPanel() override;

	void setSource(obs_source_t *source);
	obs_source_t *source() const { return src; }
	// Property names not to show (e.g. plugin path rows the user chose to hide).
	void setHiddenProperties(const QStringList &names);

signals:
	// The user changed a setting (emitted after the source was updated).
	void edited();
	// The user changed the filter's memo (stored in its settings as S_VSTEX_MEMO).
	void memoChanged();

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
	void rebuild();

private:
	void addProperties(obs_properties_t *props, QFormLayout *form);
	void addProperty(obs_property_t *prop, QFormLayout *form);
	void changed(obs_property_t *prop);
	void scheduleRebuild();
	void addPluginPicture(QVBoxLayout *layout);

	OBSSource src;
	OBSDataAutoRelease settings;
	obs_properties_t *props = nullptr;
	bool rebuildPending = false;
	QStringList hidden;
	QPointer<QLabel> picture; // low-res thumbnail; hovering shows the full-size screenshot
	QString fullImage;
	QLabel *fullPreview;
};

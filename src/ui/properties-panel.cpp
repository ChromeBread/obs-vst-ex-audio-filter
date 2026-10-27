#include "ui/properties-panel.h"

#include "plugin-meta.h"
#include "ui/ui-text.h"
#include "vst-filter.h"

#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPictureWidth = 200;
}

PropertiesPanel::PropertiesPanel(QWidget *parent) : QScrollArea(parent)
{
	setWidgetResizable(true);
	setFrameShape(QFrame::NoFrame);
	fullPreview = new QLabel(this, Qt::ToolTip);
	fullPreview->setStyleSheet("border:1px solid #888;background:#222;");
	connect(&PluginMeta::instance(), &PluginMeta::changed, this, [this] {
		if (picture)
			scheduleRebuild(); // a new screenshot was taken
	});
}

void PropertiesPanel::setHiddenProperties(const QStringList &names)
{
	hidden = names;
	rebuild();
}

bool PropertiesPanel::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == picture && event->type() == QEvent::Enter && !fullImage.isEmpty()) {
		// Actual size: one image pixel per screen pixel, clipped to the screen.
		QPixmap pm(fullImage);
		pm.setDevicePixelRatio(devicePixelRatioF());
		QRect screen = this->screen()->availableGeometry();
		QSize size = (pm.deviceIndependentSize().toSize()).boundedTo(screen.size());
		fullPreview->setPixmap(pm);
		fullPreview->resize(size);
		QPoint at = QCursor::pos() + QPoint(16, 16);
		at.setX(qBound(screen.left(), at.x(), screen.right() - size.width()));
		at.setY(qBound(screen.top(), at.y(), screen.bottom() - size.height()));
		fullPreview->move(at);
		fullPreview->show();
	} else if (obj == picture && (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress)) {
		fullPreview->hide();
	}
	return QScrollArea::eventFilter(obj, event);
}

void PropertiesPanel::addPluginPicture(QVBoxLayout *layout)
{
	vstex::PluginDescription d;
	d.format = QString::fromUtf8(obs_data_get_string(settings, S_PLUGIN_FORMAT)) == "VST2" ? vstex::PluginFormat::VST2
												 : vstex::PluginFormat::VST3;
	d.uid = obs_data_get_string(settings, S_PLUGIN_UID);
	d.name = obs_data_get_string(settings, S_PLUGIN_NAME);
	// This filter's own screenshot first; the plugin's thumbnail until one is taken.
	auto &meta = PluginMeta::instance();
	QString uuid = QString::fromUtf8(obs_source_get_uuid(src));
	QString thumb = meta.filterShotPath(uuid, false);
	if (QFileInfo::exists(thumb)) {
		QString full = meta.filterShotPath(uuid, true);
		fullImage = QFileInfo::exists(full) ? full : thumb;
	} else {
		thumb = meta.thumbnailPath(d);
		fullImage = meta.fullImagePath(d);
	}

	picture = new QLabel();
	if (thumb.isEmpty()) {
		picture->setText(T("Panel.NoScreenshot"));
		picture->setEnabled(false);
	} else {
		picture->setPixmap(QPixmap(thumb).scaledToWidth(kPictureWidth, Qt::SmoothTransformation));
		picture->installEventFilter(this);
	}
	auto *shot = new QPushButton(QStringLiteral("📷 ") + T("Panel.Screenshot"));
	shot->setToolTip(T("Panel.Screenshot.Tip"));
	OBSSource keep = src;
	connect(shot, &QPushButton::clicked, this, [keep] { vst_ex_capture_screenshot(keep); });

	// Stacked, not side by side, so a narrow panel never pushes the button out of view.
	layout->addWidget(picture, 0, Qt::AlignLeft);
	layout->addWidget(shot, 0, Qt::AlignLeft);
}

PropertiesPanel::~PropertiesPanel()
{
	if (props)
		obs_properties_destroy(props);
}

void PropertiesPanel::setSource(obs_source_t *source)
{
	src = source;
	rebuild();
}

void PropertiesPanel::scheduleRebuild()
{
	// Deferred: the widget that triggered the change must not be deleted inside its own signal.
	if (rebuildPending)
		return;
	rebuildPending = true;
	QTimer::singleShot(0, this, &PropertiesPanel::rebuild);
}

void PropertiesPanel::rebuild()
{
	rebuildPending = false;
	int scroll = verticalScrollBar()->value();
	if (props) {
		obs_properties_destroy(props);
		props = nullptr;
	}

	fullPreview->hide();
	auto *content = new QWidget();
	auto *outer = new QVBoxLayout(content);
	if (src) {
		settings = obs_source_get_settings(src);

		auto *memo = new QLineEdit(QString::fromUtf8(obs_data_get_string(settings, S_VSTEX_MEMO)), content);
		memo->setPlaceholderText(T("Panel.Memo.Placeholder"));
		connect(memo, &QLineEdit::editingFinished, this, [this, memo] {
			QByteArray text = memo->text().trimmed().toUtf8();
			if (text == obs_data_get_string(settings, S_VSTEX_MEMO))
				return;
			obs_data_set_string(settings, S_VSTEX_MEMO, text.constData()); // shared settings object: saved with the filter
			emit memoChanged();
		});
		auto *memoRow = new QFormLayout();
		memoRow->addRow(T("Col.Memo"), memo);
		outer->addLayout(memoRow);

		if (strcmp(obs_source_get_unversioned_id(src), VST_EX_FILTER_ID) == 0)
			addPluginPicture(outer);

		props = obs_source_properties(src);
		auto *form = new QFormLayout();
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		if (props)
			addProperties(props, form);
		outer->addLayout(form);

		auto *obsProps = new QPushButton(T("OpenObsProperties"), content);
		obsProps->setToolTip(T("OpenObsProperties.Tip"));
		OBSSource keep = src;
		connect(obsProps, &QPushButton::clicked, this, [keep] { obs_frontend_open_source_properties(keep); });
		outer->addSpacing(8);
		outer->addWidget(obsProps, 0, Qt::AlignLeft);
	}
	outer->addStretch(1);
	setWidget(content);
	verticalScrollBar()->setValue(scroll);
}

void PropertiesPanel::addProperties(obs_properties_t *list, QFormLayout *form)
{
	for (obs_property_t *p = obs_properties_first(list); p; obs_property_next(&p))
		addProperty(p, form);
}

void PropertiesPanel::changed(obs_property_t *prop)
{
	obs_source_update(src, settings);
	if (obs_property_modified(prop, settings))
		scheduleRebuild();
	emit edited();
}

void PropertiesPanel::addProperty(obs_property_t *prop, QFormLayout *form)
{
	if (!obs_property_visible(prop) || hidden.contains(QString::fromUtf8(obs_property_name(prop))))
		return;

	const char *name = obs_property_name(prop);
	QString desc = QString::fromUtf8(obs_property_description(prop));
	QString tip = QString::fromUtf8(obs_property_long_description(prop));
	QWidget *field = nullptr;
	bool withLabel = true;

	switch (obs_property_get_type(prop)) {
	case OBS_PROPERTY_BOOL: {
		auto *cb = new QCheckBox(desc);
		cb->setChecked(obs_data_get_bool(settings, name));
		connect(cb, &QCheckBox::toggled, this, [this, prop, name](bool on) {
			obs_data_set_bool(settings, name, on);
			changed(prop);
		});
		field = cb;
		withLabel = false;
		break;
	}
	case OBS_PROPERTY_INT: {
		auto *spin = new QSpinBox();
		spin->setRange(obs_property_int_min(prop), obs_property_int_max(prop));
		spin->setSingleStep(obs_property_int_step(prop));
		spin->setSuffix(QString::fromUtf8(obs_property_int_suffix(prop)));
		spin->setValue((int)obs_data_get_int(settings, name));
		connect(spin, &QSpinBox::valueChanged, this, [this, prop, name](int v) {
			obs_data_set_int(settings, name, v);
			changed(prop);
		});
		field = spin;
		if (obs_property_int_type(prop) == OBS_NUMBER_SLIDER) {
			auto *slider = new QSlider(Qt::Horizontal);
			slider->setRange(spin->minimum(), spin->maximum());
			slider->setSingleStep(spin->singleStep());
			slider->setValue(spin->value());
			connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
			connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
			auto *row = new QWidget();
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(slider, 1);
			h->addWidget(spin);
			field = row;
		}
		break;
	}
	case OBS_PROPERTY_FLOAT: {
		double min = obs_property_float_min(prop), max = obs_property_float_max(prop);
		double step = obs_property_float_step(prop);
		auto *spin = new QDoubleSpinBox();
		spin->setRange(min, max);
		spin->setSingleStep(step);
		spin->setDecimals(step > 0 ? qBound(0, (int)std::ceil(-std::log10(step)), 6) : 2);
		spin->setSuffix(QString::fromUtf8(obs_property_float_suffix(prop)));
		spin->setValue(obs_data_get_double(settings, name));
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop, name](double v) {
			obs_data_set_double(settings, name, v);
			changed(prop);
		});
		field = spin;
		if (obs_property_float_type(prop) == OBS_NUMBER_SLIDER && step > 0) {
			auto *slider = new QSlider(Qt::Horizontal);
			slider->setRange(0, (int)std::lround((max - min) / step));
			slider->setValue((int)std::lround((spin->value() - min) / step));
			connect(slider, &QSlider::valueChanged, spin, [spin, min, step](int v) { spin->setValue(min + v * step); });
			connect(spin, &QDoubleSpinBox::valueChanged, slider, [slider, min, step](double v) {
				QSignalBlocker block(slider);
				slider->setValue((int)std::lround((v - min) / step));
			});
			auto *row = new QWidget();
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(slider, 1);
			h->addWidget(spin);
			field = row;
		}
		break;
	}
	case OBS_PROPERTY_TEXT: {
		QString value = QString::fromUtf8(obs_data_get_string(settings, name));
		switch (obs_property_text_type(prop)) {
		case OBS_TEXT_INFO: {
			auto *label = new QLabel(value);
			label->setWordWrap(obs_property_text_info_word_wrap(prop));
			label->setTextInteractionFlags(Qt::TextSelectableByMouse);
			field = label;
			break;
		}
		case OBS_TEXT_MULTILINE: {
			auto *edit = new QPlainTextEdit(value);
			edit->setMaximumHeight(120);
			connect(edit, &QPlainTextEdit::textChanged, this, [this, prop, name, edit] {
				obs_data_set_string(settings, name, edit->toPlainText().toUtf8().constData());
				changed(prop);
			});
			field = edit;
			break;
		}
		default: {
			auto *edit = new QLineEdit(value);
			if (obs_property_text_type(prop) == OBS_TEXT_PASSWORD)
				edit->setEchoMode(QLineEdit::Password);
			connect(edit, &QLineEdit::textEdited, this, [this, prop, name](const QString &t) {
				obs_data_set_string(settings, name, t.toUtf8().constData());
				changed(prop);
			});
			field = edit;
		}
		}
		break;
	}
	case OBS_PROPERTY_PATH: {
		auto *edit = new QLineEdit(QString::fromUtf8(obs_data_get_string(settings, name)));
		edit->setReadOnly(true);
		auto *browse = new QPushButton(T("Browse"));
		connect(browse, &QPushButton::clicked, this, [this, prop, name, edit] {
			QString start = edit->text().isEmpty() ? QString::fromUtf8(obs_property_path_default_path(prop))
							       : edit->text();
			QString filter = QString::fromUtf8(obs_property_path_filter(prop));
			QString path;
			switch (obs_property_path_type(prop)) {
			case OBS_PATH_DIRECTORY:
				path = QFileDialog::getExistingDirectory(this, QString(), start);
				break;
			case OBS_PATH_FILE_SAVE:
				path = QFileDialog::getSaveFileName(this, QString(), start, filter);
				break;
			default:
				path = QFileDialog::getOpenFileName(this, QString(), start, filter);
			}
			if (path.isEmpty())
				return;
			edit->setText(path);
			obs_data_set_string(settings, name, path.toUtf8().constData());
			changed(prop);
		});
		auto *row = new QWidget();
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(edit, 1);
		h->addWidget(browse);
		field = row;
		break;
	}
	case OBS_PROPERTY_LIST: {
		auto *combo = new QComboBox();
		obs_combo_format format = obs_property_list_format(prop);
		size_t count = obs_property_list_item_count(prop);
		int current = -1;
		for (size_t i = 0; i < count; i++) {
			QVariant v;
			bool match = false;
			switch (format) {
			case OBS_COMBO_FORMAT_INT:
				v = (qlonglong)obs_property_list_item_int(prop, i);
				match = v.toLongLong() == obs_data_get_int(settings, name);
				break;
			case OBS_COMBO_FORMAT_FLOAT:
				v = obs_property_list_item_float(prop, i);
				match = v.toDouble() == obs_data_get_double(settings, name);
				break;
			case OBS_COMBO_FORMAT_BOOL:
				v = obs_property_list_item_bool(prop, i);
				match = v.toBool() == obs_data_get_bool(settings, name);
				break;
			default:
				v = QString::fromUtf8(obs_property_list_item_string(prop, i));
				match = v.toString() == QString::fromUtf8(obs_data_get_string(settings, name));
			}
			combo->addItem(QString::fromUtf8(obs_property_list_item_name(prop, i)), v);
			if (obs_property_list_item_disabled(prop, i))
				combo->setItemData((int)i, 0, Qt::UserRole - 1); // disables the item
			if (match)
				current = (int)i;
		}
		combo->setCurrentIndex(current);
		connect(combo, &QComboBox::activated, this, [this, prop, name, combo, format](int index) {
			QVariant v = combo->itemData(index);
			switch (format) {
			case OBS_COMBO_FORMAT_INT:
				obs_data_set_int(settings, name, v.toLongLong());
				break;
			case OBS_COMBO_FORMAT_FLOAT:
				obs_data_set_double(settings, name, v.toDouble());
				break;
			case OBS_COMBO_FORMAT_BOOL:
				obs_data_set_bool(settings, name, v.toBool());
				break;
			default:
				obs_data_set_string(settings, name, v.toString().toUtf8().constData());
			}
			changed(prop);
		});
		field = combo;
		break;
	}
	case OBS_PROPERTY_COLOR:
	case OBS_PROPERTY_COLOR_ALPHA: {
		bool alpha = obs_property_get_type(prop) == OBS_PROPERTY_COLOR_ALPHA;
		auto *button = new QPushButton();
		auto paint = [button](uint32_t abgr) {
			QColor c(abgr & 0xff, (abgr >> 8) & 0xff, (abgr >> 16) & 0xff);
			button->setText(c.name());
			button->setStyleSheet(QString("background:%1;color:%2").arg(c.name(), c.lightness() > 128 ? "black" : "white"));
		};
		paint((uint32_t)obs_data_get_int(settings, name));
		connect(button, &QPushButton::clicked, this, [this, prop, name, paint, alpha] {
			uint32_t abgr = (uint32_t)obs_data_get_int(settings, name);
			QColor initial(abgr & 0xff, (abgr >> 8) & 0xff, (abgr >> 16) & 0xff, alpha ? (abgr >> 24) : 255);
			QColor c = QColorDialog::getColor(initial, this, QString(),
							  alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
			if (!c.isValid())
				return;
			uint32_t a = alpha ? (uint32_t)c.alpha() : 0xffu;
			uint32_t value = (a << 24) | ((uint32_t)c.blue() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.red();
			obs_data_set_int(settings, name, value);
			paint(value);
			changed(prop);
		});
		field = button;
		break;
	}
	case OBS_PROPERTY_BUTTON: {
		auto *button = new QPushButton(desc);
		button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // natural width: text always fully visible
		connect(button, &QPushButton::clicked, this, [this, prop] {
			if (obs_property_button_clicked(prop, src))
				scheduleRebuild();
		});
		field = button;
		withLabel = false;
		break;
	}
	case OBS_PROPERTY_GROUP: {
		auto *box = new QGroupBox(desc);
		if (obs_property_group_type(prop) == OBS_GROUP_CHECKABLE) {
			box->setCheckable(true);
			box->setChecked(obs_data_get_bool(settings, name));
			connect(box, &QGroupBox::toggled, this, [this, prop, name](bool on) {
				obs_data_set_bool(settings, name, on);
				changed(prop);
			});
		}
		auto *inner = new QFormLayout(box);
		addProperties(obs_property_group_content(prop), inner);
		form->addRow(box);
		box->setEnabled(obs_property_enabled(prop));
		return;
	}
	default: {
		// Font, editable lists, frame rates... rare in audio filters.
		auto *label = new QLabel(T("UseObsProperties"));
		label->setEnabled(false);
		field = label;
	}
	}

	field->setEnabled(obs_property_enabled(prop));
	if (!tip.isEmpty())
		field->setToolTip(tip);
	if (withLabel)
		form->addRow(desc, field);
	else
		form->addRow(field);
}

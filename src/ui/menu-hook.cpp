#include "ui/menu-hook.h"

#include "ui/ex-filters-dialog.h"
#include "ui/ui-text.h"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QAction>
#include <QActionEvent>
#include <QApplication>
#include <QMenu>
#include <QPointer>

namespace {

// OBS's mixer menu (VolumeControl::showVolumeControlMenu) builds a fresh QMenu and puts the source on its actions
// as the "source" property (a QVariant holding OBSSource). There is no API for this menu, so we watch menus being
// filled and add our entry next to the "Filters" action that carries that property.
class MixerMenuHook : public QObject {
public:
	bool eventFilter(QObject *obj, QEvent *event) override
	{
		if (event->type() != QEvent::ActionAdded)
			return false;
		auto *menu = qobject_cast<QMenu *>(obj);
		if (!menu || menu->property("vstExHooked").toBool())
			return false;

		QAction *added = static_cast<QActionEvent *>(event)->action();
		QAction *filters = findFiltersAction(menu);
		if (!filters || added == filters)
			return false; // wait until the action after "Filters" is added, so ours can go in between

		obs_source_t *source = sourceOf(filters);
		if (!source)
			return false;

		menu->setProperty("vstExHooked", true);
		auto *ours = new QAction(T("MixerMenu.ExFilters"), menu);
		OBSWeakSource weak = OBSGetWeakRef(source);
		connect(ours, &QAction::triggered, ours, [weak] {
			OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
			if (src)
				ExFiltersDialog::open(src);
		});
		QList<QAction *> actions = menu->actions();
		int index = actions.indexOf(filters);
		menu->insertAction(index + 1 < actions.size() ? actions[index + 1] : nullptr, ours);
		return false;
	}

private:
	static QAction *findFiltersAction(QMenu *menu)
	{
		static const QString filtersText = QString::fromUtf8(obs_frontend_get_locale_string("Filters"));
		for (QAction *a : menu->actions())
			if (a->text() == filtersText && a->property("source").isValid())
				return a;
		return nullptr;
	}

	static obs_source_t *sourceOf(QAction *action)
	{
		QVariant v = action->property("source");
		// OBS doesn't Q_DECLARE_METATYPE it, so the name is compiler-generated, e.g. "OBSRef<obs_source*,...>".
		const char *type = v.metaType().name();
		if (!type || !strstr(type, "obs_source") || v.metaType().sizeOf() != sizeof(void *))
			return nullptr;
		// OBSSource (OBSRef<obs_source_t*>) is a single obs_source_t pointer; read it without needing
		// OBS's own metatype registration.
		return *static_cast<obs_source_t *const *>(v.constData());
	}
};

QPointer<MixerMenuHook> hook;

} // namespace

void install_mixer_menu_hook()
{
	if (hook)
		return;
	hook = new MixerMenuHook();
	qApp->installEventFilter(hook);
}

void remove_mixer_menu_hook()
{
	if (!hook)
		return;
	qApp->removeEventFilter(hook);
	delete hook;
}

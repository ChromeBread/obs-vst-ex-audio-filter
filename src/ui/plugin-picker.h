#pragma once

#include "host/plugin-host.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QTimer>

class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;

// Plugin browser: list, thumbnail grid or one-per-row detail view, sorting, user-defined favorites,
// hidden plugins, memos, screenshots.
class PluginPicker : public QDialog {
	Q_OBJECT

public:
	PluginPicker(QWidget *parent, bool multi);

	// Modal helpers: true and fill `chosen` if the user picked a plugin (or several).
	static bool pick(QWidget *parent, vstex::PluginDescription &chosen);
	static bool pickMany(QWidget *parent, QVector<vstex::PluginDescription> &chosen);

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private:
	enum View { ViewList, ViewGrid, ViewDetail };
	// Ids of the tabs in front of the favorites (which use 0..7). Never -1: QButtonGroup treats -1 as
	// "assign an id for me" and hands out -2, -3... itself.
	enum Tab { TabAll = -10, TabUsed = -11, TabHidden = -12 };

	bool exec(QVector<vstex::PluginDescription> &chosen);
	QVector<int> selectedIndices() const;
	void applyHiddenMode();
	QStringList targetKeys(int index) const;
	void selectIndices(const QVector<int> &indices);

	void refresh();
	void startScan(bool full);
	void editFolders();
	void rebuildFavoriteButtons();
	void rebuildColumns();
	void saveColumnOrder();
	void showPluginMenu(int index, const QPoint &globalPos);
	void showFavoriteButtonMenu(int fav, const QPoint &globalPos);
	void renameFavorite(int fav);
	void editMemo(int index);
	void setThumbnailFromFile(int index);
	void startBatchThumbnails();
	void captureNextThumbnail();
	void finishBatchThumbnails();
	void setView(View view);
	void setGridDelegate(class QAbstractItemDelegate *delegate);
	void setSort(int column, Qt::SortOrder order);
	void jumpToLetter(QChar letter);
	void showPreview(int index);
	void hidePreview();
	QPixmap thumbnail(int index, const QSize &size);
	int currentIndex() const
	{
		auto sel = selectedIndices();
		return sel.isEmpty() ? -1 : sel.first();
	}
	bool isGrid() const { return view != ViewList; }

	QLineEdit *search;
	QComboBox *sortBox;
	QPushButton *orderButton;
	QButtonGroup *viewGroup;
	QPushButton *hiddenButton;
	QHBoxLayout *favRow;
	QButtonGroup *favoriteGroup;
	QPushButton *addFavoriteButton;
	QPushButton *usedTab;
	QPushButton *hidTab;
	QStackedWidget *stack;
	QTableWidget *table;
	QListWidget *grid; // grid and detail views
	QVector<QToolButton *> letterButtons;
	QLabel *status;
	QPushButton *thumbsButton;
	QPushButton *foldersButton;
	QPushButton *okButton;
	QLabel *preview;
	QTimer previewTimer;
	int previewIndex = -1;
	View view = ViewList;
	int sortColumn;
	Qt::SortOrder sortOrder = Qt::AscendingOrder;
	bool columnsSized = false;
	bool restoringColumns = false;
	QVector<int> pressSelection;  // selection just before a click, so a ☆/HID click can apply to all of it
	QHash<int, int> tabScroll;    // vertical scroll position per view and tab
	int lastScrollKey = 0;
	bool viewSwitched = false;
	int scrollKey() const;
	int detailRowHeight = 0;      // details rows follow the window height (9 per screen)

	QElapsedTimer scanClock;

	// Batch thumbnail generation
	QVector<vstex::PluginDescription> thumbQueue;
	QElapsedTimer thumbClock;
	int thumbDone = 0;
	int thumbTotal = 0;
	int thumbFailed = 0;
	bool thumbRunning = false;

	QVector<vstex::PluginDescription> plugins; // effects in the catalog
	QHash<QString, QPixmap> thumbCache;	   // by "path|WxH"
};

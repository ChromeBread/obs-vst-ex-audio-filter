#include "ui/plugin-picker.h"

#include "plugin-meta.h"
#include "plugin-scanner.h"
#include "ui/editor-window.h"
#include "ui/ui-text.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {

// Logical columns. The favorite columns (one per favorite) come last logically but are shown
// between Description and Memo by default; the user can drag any column elsewhere.
enum Column { ColName, ColFormat, ColCategory, ColVendor, ColDescription, ColMemo, ColPath, ColHid, ColFav0 };

const QSize kGridThumb(192, 120);
const QSize kDetailThumb(200, 125);
constexpr int kPreviewWidth = 320;
constexpr int kPreviewDelayMs = 350;
constexpr int kMaxInitialColumnWidth = 600;

// Per-plugin time of the last batch run; a guess (load + capture delay) until one has run.
qint64 thumbMsPerPlugin = 1500 + EditorWindow::kCaptureDelayMs;

// Row tints: VST2 blue, VST3 green, every other row a bit stronger. Kept faint so text stays easy to read.
QColor rowColor(vstex::PluginFormat format, int row)
{
	int alpha = row % 2 ? 26 : 12;
	return format == vstex::PluginFormat::VST2 ? QColor(70, 130, 230, alpha) : QColor(70, 190, 110, alpha);
}

// "★1", or "★1 Name" once the user has named it.
QString favoriteLabel(int index)
{
	QString name = PluginMeta::instance().favoriteName(index);
	QString label = QStringLiteral("★%1").arg(index + 1);
	return name.isEmpty() ? label : label + ' ' + name;
}

QString columnTitle(int col)
{
	static const char *keys[] = {"Col.Name",	"Col.Format", "Col.Category", "Col.Vendor",
				     "Col.Description", "Col.Memo",   "Col.Path",     "Col.Hid"};
	return col >= ColFav0 ? favoriteLabel(col - ColFav0) : T(keys[col]);
}

// Stable ids for saving the column order: "c<logical>" for fixed columns, "f<n>" for favorite n.
QString columnId(int col)
{
	return col >= ColFav0 ? QString("f%1").arg(col - ColFav0) : QString("c%1").arg(col);
}

constexpr int kDetailRowsPerScreen = 9;
enum Role { RoleVendor = Qt::UserRole + 1, RoleNotes, RoleLine3, RoleFavorites };

// "★1 3" badge drawn at `topLeft`; nothing when the plugin is in no favorite. Same size in every view:
// the details view shrinks its pictures, so it draws the badge itself instead of baking it into the picture.
void drawFavoriteBadge(QPainter &p, QPoint topLeft, quint8 bits)
{
	if (!bits)
		return;
	QStringList nums;
	for (int i = 0; i < PluginMeta::kFavorites; i++)
		if (bits & (1u << i))
			nums << QString::number(i + 1);
	QString text = QStringLiteral("★") + nums.join(' ');
	p.save();
	QFont f = p.font();
	f.setBold(true);
	f.setPixelSize(qMax(12, kGridThumb.height() / 9));
	p.setFont(f);
	QRect box = p.fontMetrics().boundingRect(text).adjusted(-5, -2, 5, 2);
	box.moveTopLeft(topLeft);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 170));
	p.drawRoundedRect(box, 4, 4);
	p.setPen(QColor(255, 200, 40));
	p.drawText(box, Qt::AlignCenter, text);
	p.restore();
}

QPixmap withFavoriteBadge(QPixmap pm, quint8 bits)
{
	if (bits) {
		QPainter p(&pm);
		drawFavoriteBadge(p, QPoint(4, 4), bits);
	}
	return pm;
}

// Details row: picture on the left; vendor (left) and notes (right), the name in bold, then VST2/3 · category.
class DetailDelegate : public QStyledItemDelegate {
public:
	DetailDelegate(QObject *parent, const int *rowHeight) : QStyledItemDelegate(parent), rowHeight(rowHeight) {}

	QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
	{
		return QSize(1, qMax(40, *rowHeight)); // width: the view stretches rows to its own width
	}

	void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
	{
		p->save();
		bool selected = opt.state & QStyle::State_Selected;
		QVariant bg = index.data(Qt::BackgroundRole);
		if (bg.isValid())
			p->fillRect(opt.rect, bg.value<QBrush>());
		if (selected)
			p->fillRect(opt.rect, opt.palette.highlight());

		QRect r = opt.rect.adjusted(3, 2, -6, -2);
		QRect pic(r.topLeft(), QSize(r.height() * 16 / 10, r.height()));
		QPixmap pm = index.data(Qt::DecorationRole).value<QIcon>().pixmap(kDetailThumb);
		if (!pm.isNull()) {
			QRect target(QPoint(), pm.size().scaled(pic.size(), Qt::KeepAspectRatio));
			target.moveCenter(pic.center());
			p->setRenderHint(QPainter::SmoothPixmapTransform);
			p->drawPixmap(target, pm);
		}
		drawFavoriteBadge(*p, pic.topLeft() + QPoint(4, 4), quint8(index.data(RoleFavorites).toUInt()));

		QVariant fgData = index.data(Qt::ForegroundRole);
		QColor fg = selected ? opt.palette.highlightedText().color()
				     : (fgData.isValid() ? fgData.value<QBrush>().color() : opt.palette.text().color());
		QRect text = r.adjusted(pic.width() + 10, 0, 0, 0);
		int lh = text.height() / 3;
		QRect line1(text.left(), text.top(), text.width(), lh);
		QRect line2 = line1.translated(0, lh), line3 = line2.translated(0, lh);
		QFont font = opt.font;
		QFontMetrics fm(font);
		p->setFont(font);
		p->setPen(fg);
		QString notes = index.data(RoleNotes).toString().replace('\n', QStringLiteral(" / "));
		p->drawText(line1, Qt::AlignRight | Qt::AlignVCenter, fm.elidedText(notes, Qt::ElideRight, line1.width() / 2));
		p->drawText(line1, Qt::AlignLeft | Qt::AlignVCenter,
			    fm.elidedText(index.data(RoleVendor).toString(), Qt::ElideRight, line1.width() / 2 - 8));
		p->drawText(line3, Qt::AlignLeft | Qt::AlignVCenter,
			    fm.elidedText(index.data(RoleLine3).toString(), Qt::ElideRight, line3.width()));
		QFont bold = font;
		bold.setBold(true);
		bold.setPointSizeF(font.pointSizeF() * 1.15);
		p->setFont(bold);
		p->drawText(line2, Qt::AlignLeft | Qt::AlignVCenter,
			    QFontMetrics(bold).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, line2.width()));
		p->restore();
	}

private:
	const int *rowHeight;
};

QChar letterOf(const QString &name)
{
	QChar c = name.isEmpty() ? QChar('#') : name[0].toUpper();
	return c >= 'A' && c <= 'Z' ? c : QChar('#');
}

QString durationText(qint64 ms)
{
	qint64 secs = qMax<qint64>(1, (ms + 999) / 1000);
	if (secs < 60)
		return T("Duration.Sec").arg(secs);
	return T("Duration.MinSec").arg(secs / 60).arg(secs % 60);
}

// Grey card with the plugin name, for plugins without a screenshot yet.
QPixmap placeholder(const QString &name, const QSize &size)
{
	QPixmap pm(size);
	pm.fill(QColor(0x3a, 0x3a, 0x3a));
	QPainter p(&pm);
	p.setPen(QColor(0xcc, 0xcc, 0xcc));
	QFont f = p.font();
	f.setPointSize(11);
	p.setFont(f);
	p.drawText(pm.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, name);
	return pm;
}

} // namespace

PluginPicker::PluginPicker(QWidget *parent, bool multi) : QDialog(parent), sortColumn(ColName)
{
	setWindowTitle(T("PickPlugin"));
	resize(1000, 620);
	auto &meta = PluginMeta::instance();
	auto &catalog = PluginCatalog::instance();

	// --- top row: search, sort, view mode, hidden toggle
	search = new QLineEdit(this);
	search->setPlaceholderText(T("Search"));
	search->setClearButtonEnabled(true);
	sortBox = new QComboBox(this);
	sortBox->setToolTip(T("Browser.Sort.Tip"));
	orderButton = new QPushButton(this);
	orderButton->setToolTip(T("Browser.Order.Tip"));
	viewGroup = new QButtonGroup(this);
	auto *top = new QHBoxLayout();
	top->addWidget(search, 1);
	top->addWidget(new QLabel(T("Browser.Sort"), this));
	top->addWidget(sortBox);
	top->addWidget(orderButton);
	top->addSpacing(12);
	const char *viewKeys[] = {"Browser.List", "Browser.Grid", "Browser.Detail"};
	for (int v = ViewList; v <= ViewDetail; v++) {
		auto *b = new QPushButton(T(viewKeys[v]), this);
		b->setCheckable(true);
		b->setChecked(v == ViewList);
		viewGroup->addButton(b, v);
		top->addWidget(b);
	}
	hiddenButton = new QPushButton(this);
	hiddenButton->setCheckable(true);
	hiddenButton->setChecked(meta.uiValue("pickerHidden").toBool(true));
	hiddenButton->setToolTip(T("Browser.Hidden.Tip"));
	top->addSpacing(12);
	top->addWidget(hiddenButton);

	// --- favorites row: All + the user's favorites + "+"; exclusive filter buttons, right-click to rename/remove
	favoriteGroup = new QButtonGroup(this);
	favoriteGroup->setExclusive(true);
	favRow = new QHBoxLayout();
	auto *all = new QPushButton(T("Browser.All"), this);
	all->setCheckable(true);
	all->setChecked(true);
	favoriteGroup->addButton(all, TabAll);
	favRow->addWidget(all);
	usedTab = new QPushButton(T("Browser.Used"), this);
	usedTab->setContextMenuPolicy(Qt::CustomContextMenu);
	usedTab->setToolTip(T("Browser.Used.Tip").arg(PluginMeta::kMaxRecent));
	hidTab = new QPushButton(T("Col.Hid"), this);
	hidTab->setToolTip(T("Browser.HidTab.Tip"));
	for (QPushButton *b : {usedTab, hidTab}) {
		b->setCheckable(true);
		favoriteGroup->addButton(b, b == usedTab ? TabUsed : TabHidden);
		favRow->addWidget(b);
	}
	addFavoriteButton = new QPushButton(QStringLiteral("+"), this);
	addFavoriteButton->setFixedWidth(addFavoriteButton->sizeHint().height());
	favRow->addWidget(addFavoriteButton);
	favRow->addStretch(1);

	// --- list view. Sorting is done in refresh() so all views share the same order.
	table = new QTableWidget(this);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	auto selectionMode = multi ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection;
	table->setSelectionMode(selectionMode);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	table->setMouseTracking(true);
	table->setWordWrap(false);
	table->setContextMenuPolicy(Qt::CustomContextMenu);
	table->verticalHeader()->hide();
	QHeaderView *header = table->horizontalHeader();
	header->setSectionResizeMode(QHeaderView::Interactive);
	header->setSectionsMovable(true);
	// No stretching: a stretched last column (the file path) is squeezed into the view instead of scrolling.
	header->setStretchLastSection(false);
	header->setSortIndicatorShown(true);
	header->setSectionsClickable(true);
	table->viewport()->installEventFilter(this);

	// --- thumbnail grid / detail view, with an A-Z bar to jump through long lists
	grid = new QListWidget(this);
	grid->setMovement(QListView::Static);
	grid->setResizeMode(QListView::Adjust);
	grid->setUniformItemSizes(true);
	grid->setWordWrap(true);
	grid->setContextMenuPolicy(Qt::CustomContextMenu);
	grid->setSelectionMode(selectionMode);
	grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	grid->viewport()->installEventFilter(this);
	auto *letters = new QVBoxLayout();
	letters->setSpacing(0);
	letters->setContentsMargins(0, 0, 0, 0);
	for (QChar c : QStringLiteral("#ABCDEFGHIJKLMNOPQRSTUVWXYZ")) {
		auto *b = new QToolButton(this);
		b->setText(c);
		b->setAutoRaise(true);
		b->setFixedSize(30, 22);
		QFont f = b->font();
		f.setBold(true);
		b->setFont(f);
		connect(b, &QToolButton::clicked, this, [this, c] { jumpToLetter(c); });
		letterButtons << b;
		letters->addWidget(b);
	}
	letters->addStretch(1);
	auto *gridPage = new QWidget(this);
	auto *gridLayout = new QHBoxLayout(gridPage);
	gridLayout->setContentsMargins(0, 0, 0, 0);
	gridLayout->setSpacing(2);
	gridLayout->addWidget(grid, 1);
	gridLayout->addLayout(letters);

	stack = new QStackedWidget(this);
	stack->addWidget(table);
	stack->addWidget(gridPage);

	// Hover preview: a small image next to the list, shown after a short delay so it never gets in the way.
	preview = new QLabel(this, Qt::ToolTip);
	preview->setStyleSheet("border:1px solid #888;background:#222;");
	previewTimer.setSingleShot(true);
	previewTimer.setInterval(kPreviewDelayMs);
	connect(&previewTimer, &QTimer::timeout, this, [this] { showPreview(previewIndex); });

	// --- bottom row
	status = new QLabel(this);
	auto *scanButton = new QPushButton(T("Scan"), this);
	scanButton->setToolTip(T("Scan.Tip"));
	auto *rescan = new QPushButton(T("Rescan"), this);
	auto *folders = foldersButton = new QPushButton(T("Folders"), this);
	thumbsButton = new QPushButton(QStringLiteral("📷 ") + T("Thumbs.Batch"), this);
	thumbsButton->setToolTip(T("Thumbs.Batch.Tip"));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	okButton = buttons->button(QDialogButtonBox::Ok);
	auto *bottom = new QHBoxLayout();
	bottom->addWidget(scanButton);
	bottom->addWidget(rescan);
	bottom->addWidget(folders);
	bottom->addWidget(thumbsButton);
	bottom->addWidget(status, 1);
	bottom->addWidget(buttons);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(top);
	layout->addLayout(favRow);
	layout->addWidget(stack, 1);
	layout->addLayout(bottom);

	rebuildFavoriteButtons();
	rebuildColumns();
	applyHiddenMode();
	setView(ViewList);

	connect(search, &QLineEdit::textChanged, this, &PluginPicker::refresh);
	connect(hiddenButton, &QPushButton::toggled, this, [this](bool on) {
		PluginMeta::instance().setUiValue("pickerHidden", on);
		applyHiddenMode();
		refresh();
	});
	connect(usedTab, &QPushButton::customContextMenuRequested, this, [this](const QPoint &pos) {
		QMenu menu(this);
		QAction *clear = menu.addAction(T("Browser.ClearUsed"));
		clear->setEnabled(!PluginMeta::instance().recent().isEmpty());
		connect(clear, &QAction::triggered, this, [] { PluginMeta::instance().clearRecent(); });
		menu.exec(usedTab->mapToGlobal(pos));
	});
	connect(favoriteGroup, &QButtonGroup::idClicked, this, &PluginPicker::refresh);
	connect(viewGroup, &QButtonGroup::idClicked, this, [this](int v) { setView(View(v)); });
	connect(addFavoriteButton, &QPushButton::clicked, this, [] { PluginMeta::instance().addFavorite(); });
	connect(sortBox, &QComboBox::activated, this, [this] { setSort(sortBox->currentData().toInt(), sortOrder); });
	connect(orderButton, &QPushButton::clicked, this, [this] {
		setSort(sortColumn, sortOrder == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder);
	});
	connect(header, &QHeaderView::sectionClicked, this, [this](int col) {
		bool flip = col == sortColumn && sortOrder == Qt::AscendingOrder;
		setSort(col, flip ? Qt::DescendingOrder : Qt::AscendingOrder);
	});
	connect(header, &QHeaderView::sectionMoved, this, [this] {
		if (!restoringColumns)
			saveColumnOrder();
	});
	connect(table, &QTableWidget::itemSelectionChanged, this, [this] { okButton->setEnabled(currentIndex() >= 0); });
	connect(grid, &QListWidget::itemSelectionChanged, this, [this] { okButton->setEnabled(currentIndex() >= 0); });
	connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
		int index = table->item(row, ColName)->data(Qt::UserRole).toInt();
		if (col == ColMemo)
			editMemo(index);
		else if (col < ColHid)
			accept();
	});
	// One click on a ☆ or HID cell turns it on or off, for the whole selection if the row was part of it.
	connect(table, &QTableWidget::cellClicked, this, [this](int row, int col) {
		if (col < ColHid)
			return;
		auto &meta = PluginMeta::instance();
		int index = table->item(row, ColName)->data(Qt::UserRole).toInt();
		PluginMeta::Entry e = meta.get(PluginMeta::keyOf(plugins[index]));
		QStringList keys = targetKeys(index);
		if (pressSelection.contains(index))
			selectIndices(pressSelection); // the click itself narrowed the selection to one row
		if (col == ColHid)
			meta.setHidden(keys, !e.hidden);
		else
			meta.setFavorite(keys, col - ColFav0, !(e.favorites & (1u << (col - ColFav0))));
	});
	connect(grid, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
	connect(table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		if (auto *item = table->itemAt(pos))
			showPluginMenu(table->item(item->row(), ColName)->data(Qt::UserRole).toInt(),
				       table->viewport()->mapToGlobal(pos));
	});
	connect(grid, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		if (auto *item = grid->itemAt(pos))
			showPluginMenu(item->data(Qt::UserRole).toInt(), grid->viewport()->mapToGlobal(pos));
	});
	connect(table, &QTableWidget::itemEntered, this, [this](QTableWidgetItem *item) {
		int index = table->item(item->row(), ColName)->data(Qt::UserRole).toInt();
		if (index == previewIndex && (preview->isVisible() || previewTimer.isActive()))
			return;
		hidePreview();
		previewIndex = index;
		previewTimer.start();
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(scanButton, &QPushButton::clicked, this, [this] { startScan(false); });
	connect(rescan, &QPushButton::clicked, this, [this] { startScan(true); });
	connect(folders, &QPushButton::clicked, this, &PluginPicker::editFolders);
	connect(thumbsButton, &QPushButton::clicked, this, [this] {
		if (thumbRunning)
			thumbQueue.clear(); // stops after the plugin being captured now
		else
			startBatchThumbnails();
	});
	connect(&catalog, &PluginCatalog::progress, this, [this](int done, int total, const QString &current) {
		QString text = QString("%1 %2/%3").arg(T("Scanning")).arg(done).arg(total);
		if (done > 0 && scanClock.isValid())
			text += "  " + T("Remaining").arg(durationText(scanClock.elapsed() * (total - done) / done));
		status->setText(text + "  " + QDir::toNativeSeparators(current));
	});
	connect(&catalog, &PluginCatalog::finished, this, &PluginPicker::refresh);
	connect(&meta, &PluginMeta::changed, this, [this] {
		thumbCache.clear();
		rebuildColumns(); // favorites may have been added, removed or renamed
		rebuildFavoriteButtons();
		refresh();
	});

	setSort(ColName, Qt::AscendingOrder); // also fills the list
}

bool PluginPicker::pick(QWidget *parent, vstex::PluginDescription &chosen)
{
	QVector<vstex::PluginDescription> list;
	PluginPicker dlg(parent, false);
	if (!dlg.exec(list))
		return false;
	chosen = list.first();
	return true;
}

bool PluginPicker::pickMany(QWidget *parent, QVector<vstex::PluginDescription> &chosen)
{
	PluginPicker dlg(parent, true);
	return dlg.exec(chosen);
}

bool PluginPicker::exec(QVector<vstex::PluginDescription> &chosen)
{
	if (QDialog::exec() != QDialog::Accepted)
		return false;
	chosen.clear();
	for (int i : selectedIndices())
		chosen << plugins[i];
	// Oldest first, so the first chosen ends up at the top of "Used".
	for (auto it = chosen.rbegin(); it != chosen.rend(); ++it)
		PluginMeta::instance().addRecent(PluginMeta::keyOf(*it));
	return !chosen.isEmpty();
}

// In the order shown.
QVector<int> PluginPicker::selectedIndices() const
{
	QVector<int> out;
	if (isGrid()) {
		for (int i = 0; i < grid->count(); i++)
			if (grid->item(i)->isSelected())
				out << grid->item(i)->data(Qt::UserRole).toInt();
		return out;
	}
	for (int row = 0; row < table->rowCount(); row++)
		if (table->item(row, ColName)->isSelected())
			out << table->item(row, ColName)->data(Qt::UserRole).toInt();
	return out;
}

// The clicked plugin, or the whole selection when the clicked one is part of it.
QStringList PluginPicker::targetKeys(int index) const
{
	QVector<int> sel = pressSelection.contains(index) ? pressSelection : selectedIndices();
	if (!sel.contains(index))
		sel = {index};
	QStringList keys;
	for (int i : sel)
		keys << PluginMeta::keyOf(plugins[i]);
	return keys;
}

void PluginPicker::selectIndices(const QVector<int> &indices)
{
	QItemSelection selection;
	for (int row = 0; row < table->rowCount(); row++)
		if (indices.contains(table->item(row, ColName)->data(Qt::UserRole).toInt()))
			selection.select(table->model()->index(row, 0), table->model()->index(row, table->columnCount() - 1));
	table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}

// Hidden ON hides the plugins marked HID, the file column, the HID tab and the folders button.
void PluginPicker::applyHiddenMode()
{
	bool on = hiddenButton->isChecked();
	table->setColumnHidden(ColPath, on);
	if (!on)
		table->resizeColumnToContents(ColPath); // full paths, reachable with the horizontal scrollbar
	foldersButton->setVisible(!on); // shows folder paths; keep it out of reach while hidden
	hidTab->setVisible(!on);
	if (on && favoriteGroup->checkedId() == TabHidden)
		favoriteGroup->button(TabAll)->setChecked(true);
}

// Scanning loads every plugin, so it only runs when the user asks for it.
void PluginPicker::startScan(bool full)
{
	auto &catalog = PluginCatalog::instance();
	if (catalog.isScanning())
		return;
	scanClock.start();
	catalog.scan(full);
}

void PluginPicker::setGridDelegate(QAbstractItemDelegate *delegate)
{
	QAbstractItemDelegate *old = grid->itemDelegate();
	grid->setItemDelegate(delegate);
	if (old)
		old->deleteLater();
}

void PluginPicker::setView(View v)
{
	tabScroll[lastScrollKey] = (isGrid() ? static_cast<QAbstractItemView *>(grid) : table)->verticalScrollBar()->value();
	view = v;
	lastScrollKey = scrollKey();
	viewSwitched = true;
	pressSelection.clear(); // belongs to the list view
	viewGroup->button(v)->setChecked(true);
	hidePreview();
	stack->setCurrentIndex(v == ViewList ? 0 : 1);
	if (v == ViewGrid) {
		grid->setViewMode(QListView::IconMode);
		setGridDelegate(new QStyledItemDelegate(grid));
		grid->setUniformItemSizes(true);
		grid->setIconSize(kGridThumb);
		grid->setGridSize(kGridThumb + QSize(20, 44));
		grid->setSpacing(0);
	} else if (v == ViewDetail) {
		// One plugin per row across the full width: picture, then its details.
		grid->setViewMode(QListView::ListMode);
		detailRowHeight = grid->viewport()->height() / kDetailRowsPerScreen;
		setGridDelegate(new DetailDelegate(grid, &detailRowHeight));
		grid->setUniformItemSizes(false);
		grid->setIconSize(kDetailThumb);
		grid->setGridSize(QSize());
		grid->setSpacing(1);
	}
	refresh();
}

void PluginPicker::setSort(int column, Qt::SortOrder order)
{
	sortColumn = column;
	sortOrder = order;
	sortBox->setCurrentIndex(sortBox->findData(column));
	orderButton->setText(order == Qt::AscendingOrder ? QStringLiteral("▲") : QStringLiteral("▼"));
	table->horizontalHeader()->setSortIndicator(column, order);
	refresh();
}

// Columns follow the favorites: adding a favorite adds its ☆ column, removing one removes it.
void PluginPicker::rebuildColumns()
{
	int favs = PluginMeta::instance().favoriteCount();
	int count = ColFav0 + favs;
	restoringColumns = true;
	table->setColumnCount(count);
	QStringList titles;
	for (int c = 0; c < count; c++)
		titles << columnTitle(c);
	table->setHorizontalHeaderLabels(titles);
	for (int c = ColHid; c < count; c++)
		table->setColumnWidth(c, qMax(36, table->fontMetrics().horizontalAdvance(titles[c]) + 24));

	// Saved order, with columns that are new since then placed after their default neighbour.
	QVector<int> defaults;
	defaults << ColName << ColCategory << ColVendor << ColFormat << ColHid;
	for (int c = ColFav0; c < count; c++)
		defaults << c;
	defaults << ColMemo << ColDescription << ColPath;
	QVector<int> order;
	for (const auto &v : PluginMeta::instance().uiValue("pickerColumns2").toArray())
		for (int c : defaults)
			if (columnId(c) == v.toString() && !order.contains(c))
				order << c;
	for (int i = 0; i < defaults.size(); i++) {
		if (order.contains(defaults[i]))
			continue;
		int at = 0;
		for (int j = i - 1; j >= 0; j--)
			if (int pos = (int)order.indexOf(defaults[j]); pos >= 0) {
				at = pos + 1;
				break;
			}
		order.insert(at, defaults[i]);
	}
	QHeaderView *header = table->horizontalHeader();
	for (int v = 0; v < order.size(); v++)
		header->moveSection(header->visualIndex(order[v]), v);
	restoringColumns = false;

	int current = sortColumn < count ? sortColumn : ColName;
	sortBox->clear();
	for (int c : defaults)
		sortBox->addItem(columnTitle(c), c);
	sortColumn = current;
	sortBox->setCurrentIndex(sortBox->findData(current));
}

void PluginPicker::saveColumnOrder()
{
	QHeaderView *header = table->horizontalHeader();
	QJsonArray ids;
	for (int v = 0; v < header->count(); v++)
		ids << columnId(header->logicalIndex(v));
	PluginMeta::instance().setUiValue("pickerColumns2", ids);
}

void PluginPicker::rebuildFavoriteButtons()
{
	auto &meta = PluginMeta::instance();
	int count = meta.favoriteCount();
	int checked = favoriteGroup->checkedId();

	for (QAbstractButton *b : favoriteGroup->buttons()) {
		if (favoriteGroup->id(b) >= 0) {
			favoriteGroup->removeButton(b);
			delete b;
		}
	}
	for (int i = 0; i < count; i++) {
		// Long names are cut so 8 favorites never push the window wider than the screen.
		auto *b = new QPushButton(fontMetrics().elidedText(favoriteLabel(i), Qt::ElideRight, 140), this);
		b->setCheckable(true);
		b->setContextMenuPolicy(Qt::CustomContextMenu);
		b->setToolTip(favoriteLabel(i) + '\n' + T("Browser.Favorite.Tip"));
		connect(b, &QPushButton::customContextMenuRequested, this,
			[this, b, i](const QPoint &pos) { showFavoriteButtonMenu(i, b->mapToGlobal(pos)); });
		favoriteGroup->addButton(b, i);
		favRow->insertWidget(3 + i, b); // after All, Used, HID
	}
	favRow->removeWidget(addFavoriteButton);
	favRow->insertWidget(3 + count, addFavoriteButton); // All, Used, HID, favorites, then "+"
	addFavoriteButton->setEnabled(count < PluginMeta::kFavorites);
	addFavoriteButton->setToolTip(T("Browser.AddFavorite.Tip").arg(count).arg(PluginMeta::kFavorites));
	// Keep the current filter if that favorite still exists.
	QAbstractButton *keep = favoriteGroup->button(checked < count ? checked : TabAll);
	(keep ? keep : favoriteGroup->button(TabAll))->setChecked(true);
}

void PluginPicker::refresh()
{
	QSet<QString> keepKeys;
	for (int i : selectedIndices())
		if (i < plugins.size())
			keepKeys.insert(PluginMeta::keyOf(plugins[i]));

	auto &meta = PluginMeta::instance();
	const QVector<vstex::PluginDescription> catalog = PluginCatalog::instance().plugins();
	meta.adoptLegacyKeys(catalog);

	plugins.clear();
	for (const auto &d : catalog)
		if (d.numInputs > 0) // effects only; instruments have no audio input to process
			plugins << d;

	int favCount = meta.favoriteCount();
	QString needle = search->text().trimmed();
	int tab = favoriteGroup->checkedId();
	bool hideHidden = hiddenButton->isChecked();
	QStringList recent = meta.recent();

	struct Row {
		int index;
		quint8 favorites;
		bool hidden;
		QStringList cells; // fixed columns, ColName..ColPath
		int recentPos;
	};
	QVector<Row> rows;
	for (int i = 0; i < plugins.size(); i++) {
		const auto &d = plugins[i];
		QString key = PluginMeta::keyOf(d);
		PluginMeta::Entry e = meta.get(key);
		if (e.hidden && hideHidden)
			continue;
		if ((tab >= 0 && !(e.favorites & (1u << tab))) || (tab == TabUsed && !recent.contains(key)) ||
		    (tab == TabHidden && !e.hidden))
			continue;
		QStringList cells = {QString::fromStdString(d.name),
				     d.format == vstex::PluginFormat::VST2 ? QStringLiteral("2") : QStringLiteral("3"),
				     QString::fromStdString(d.category),
				     QString::fromStdString(d.vendor),
				     QString::fromStdString(d.description),
				     e.memo,
				     QDir::toNativeSeparators(QString::fromStdString(d.path))};
		if (!needle.isEmpty() &&
		    !(cells.join(' ') + ' ' + vstex::formatName(d.format)).contains(needle, Qt::CaseInsensitive))
			continue;
		rows.push_back({i, e.favorites, e.hidden, cells, (int)recent.indexOf(key)});
	}

	// A favorite column sorts the plugins in that favorite first. Ties fall back to the name.
	int col = sortColumn;
	bool ascending = sortOrder == Qt::AscendingOrder;
	std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
		if (tab == TabUsed) // newest first, whatever the sort column
			return a.recentPos < b.recentPos;
		int c;
		if (col == ColHid) {
			c = int(b.hidden) - int(a.hidden);
		} else if (col >= ColFav0) {
			quint8 bit = quint8(1u << (col - ColFav0));
			c = int(!(a.favorites & bit)) - int(!(b.favorites & bit));
		} else {
			c = QString::localeAwareCompare(a.cells[col], b.cells[col]);
		}
		if (c == 0)
			c = QString::localeAwareCompare(a.cells[ColName], b.cells[ColName]);
		return ascending ? c < 0 : c > 0;
	});

	// Rebuilding must not move the view (e.g. after editing a memo), so remember and restore the scroll position.
	QAbstractItemView *active = isGrid() ? static_cast<QAbstractItemView *>(grid) : table;
	int scrollX = active->horizontalScrollBar()->value();
	int scrollY = active->verticalScrollBar()->value();
	if (int key = scrollKey(); key != lastScrollKey) { // each view and tab keeps its own place
		tabScroll[lastScrollKey] = scrollY;
		scrollY = tabScroll.value(key, 0);
		lastScrollKey = key;
	} else if (viewSwitched) {
		scrollY = tabScroll.value(key, 0);
	}
	viewSwitched = false;

	table->setRowCount(0);
	grid->clear();
	QSet<QChar> initials;
	for (const Row &r : rows) {
		const auto &d = plugins[r.index];
		QString tip = r.hidden ? T("Browser.IsHidden") : QString(); // no file paths on hover
		bool selected = keepKeys.contains(PluginMeta::keyOf(d));

		if (isGrid()) {
			int n = grid->count();
			initials.insert(letterOf(r.cells[ColName]));
			QSize size = view == ViewDetail ? kDetailThumb : kGridThumb;
			QPixmap pic = thumbnail(r.index, size);
			if (view != ViewDetail) // the details delegate draws the badge at full size
				pic = withFavoriteBadge(pic, r.favorites);
			auto *item = new QListWidgetItem(QIcon(pic), r.cells[ColName], grid);
			item->setData(Qt::UserRole, r.index);
			if (view == ViewDetail) {
				QString line3 = vstex::formatName(d.format);
				if (!r.cells[ColCategory].isEmpty())
					line3 += QStringLiteral("  ·  ") + r.cells[ColCategory];
				item->setData(RoleVendor, r.cells[ColVendor]);
				item->setData(RoleNotes, r.cells[ColMemo]);
				item->setData(RoleLine3, line3);
				item->setData(RoleFavorites, uint(r.favorites));
			}
			item->setToolTip(tip);
			if (view == ViewDetail)
				item->setBackground(rowColor(d.format, n));
			if (r.hidden)
				item->setForeground(Qt::gray);
			item->setSelected(selected);
			continue;
		}

		int row = table->rowCount();
		table->insertRow(row);
		QColor tint = rowColor(d.format, row);
		for (int c = 0; c < ColFav0 + favCount; c++) {
			QTableWidgetItem *item;
			// (ColHid and favorites are toggles, drawn as check marks)
			if (c == ColHid) {
				item = new QTableWidgetItem(r.hidden ? QStringLiteral("☑") : QStringLiteral("☐"));
				item->setTextAlignment(Qt::AlignCenter);
				item->setToolTip(T("Browser.Hid.CellTip"));
			} else if (c >= ColFav0) {
				bool on = r.favorites & (1u << (c - ColFav0));
				item = new QTableWidgetItem(on ? QStringLiteral("★") : QStringLiteral("☆"));
				item->setTextAlignment(Qt::AlignCenter);
				item->setToolTip(T("Browser.Favorite.CellTip").arg(favoriteLabel(c - ColFav0)));
			} else {
				const QString &text = r.cells[c];
				item = new QTableWidgetItem(c == ColMemo ? QString(text).replace('\n', QStringLiteral(" / "))
									 : text);
				item->setToolTip((c == ColDescription || c == ColMemo) && !text.isEmpty() ? text : tip);
			}
			item->setBackground(tint);
			if (r.hidden) {
				item->setForeground(Qt::gray);
				QFont f = item->font();
				f.setItalic(true);
				item->setFont(f);
			}
			table->setItem(row, c, item);
		}
		table->item(row, ColName)->setData(Qt::UserRole, r.index);
		if (selected) // select without making it current, which would scroll to it
			table->selectionModel()->select(table->model()->index(row, 0),
							QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}

	// Fit columns to their contents once; after that the widths are the user's.
	if (!columnsSized && table->rowCount() > 0) {
		for (int c = 0; c < ColFav0; c++) {
			table->resizeColumnToContents(c);
			if (c != ColPath) // paths stay whole; the horizontal scrollbar reaches them
				table->setColumnWidth(c, qMin(table->columnWidth(c), kMaxInitialColumnWidth));
		}
		columnsSized = true;
	}
	for (QToolButton *b : letterButtons) // only letters that lead somewhere, so the rest can be bigger
		b->setVisible(initials.contains(b->text()[0]));

	active->doItemsLayout();
	active->horizontalScrollBar()->setValue(scrollX);
	active->verticalScrollBar()->setValue(scrollY);

	hiddenButton->setText(T("Browser.Hidden")); // no count: how many plugins are hidden is itself private
	usedTab->setText(T("Browser.Used") + QString(" (%1)").arg(recent.size()));
	okButton->setEnabled(currentIndex() >= 0);
	if (PluginCatalog::instance().isScanning() || thumbRunning)
		return;
	status->setText(plugins.isEmpty() ? T("Browser.NotScanned") : T("PluginCount").arg(rows.size()));
}

int PluginPicker::scrollKey() const
{
	return int(view) * 1000 + favoriteGroup->checkedId();
}

void PluginPicker::jumpToLetter(QChar letter)
{
	for (int i = 0; i < grid->count(); i++) {
		QListWidgetItem *item = grid->item(i);
		if (letterOf(item->text()) == letter) {
			grid->scrollToItem(item, QAbstractItemView::PositionAtTop);
			return;
		}
	}
}

QPixmap PluginPicker::thumbnail(int index, const QSize &size)
{
	const auto &d = plugins[index];
	QString path = PluginMeta::instance().thumbnailPath(d);
	QString cacheKey = QString("%1|%2x%3").arg(path.isEmpty() ? PluginMeta::keyOf(d) : path).arg(size.width()).arg(size.height());
	auto it = thumbCache.find(cacheKey);
	if (it != thumbCache.end())
		return *it;

	QPixmap pm;
	if (!path.isEmpty())
		pm = QPixmap(path).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	if (pm.isNull())
		pm = placeholder(QString::fromStdString(d.name), size);
	thumbCache.insert(cacheKey, pm);
	return pm;
}

void PluginPicker::showPreview(int index)
{
	if (index < 0 || index >= plugins.size() || !table->underMouse())
		return;
	if (PluginMeta::instance().thumbnailPath(plugins[index]).isEmpty())
		return; // nothing to show: stay out of the way
	QPixmap pm = thumbnail(index, QSize(kPreviewWidth, kPreviewWidth));
	preview->setPixmap(pm);
	preview->adjustSize();

	// To the right of the dialog if there is room, else left; vertically at the cursor.
	QPoint cursor = QCursor::pos();
	QRect screen = this->screen()->availableGeometry();
	QRect dlg = frameGeometry();
	int x = dlg.right() + 8;
	if (x + preview->width() > screen.right())
		x = qMax(screen.left(), dlg.left() - preview->width() - 8);
	int y = qBound(screen.top(), cursor.y() - preview->height() / 2, screen.bottom() - preview->height());
	preview->move(x, y);
	preview->show();
}

void PluginPicker::hidePreview()
{
	previewTimer.stop();
	preview->hide();
	previewIndex = -1;
}

bool PluginPicker::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == grid->viewport() && event->type() == QEvent::Wheel) {
		// One notch = one row of thumbnails, or three detail rows.
		int step = view == ViewGrid ? grid->gridSize().height() : qMax(40, detailRowHeight) * 3;
		QScrollBar *bar = grid->verticalScrollBar();
		bar->setValue(bar->value() - static_cast<QWheelEvent *>(event)->angleDelta().y() * step / 120);
		return true;
	}
	if (obj == grid->viewport() && event->type() == QEvent::Resize && view == ViewDetail) {
		int h = grid->viewport()->height() / kDetailRowsPerScreen;
		if (h != detailRowHeight) {
			detailRowHeight = h;
			QTimer::singleShot(0, grid, [this] { grid->doItemsLayout(); });
		}
	}
	if (obj == table->viewport() && event->type() == QEvent::MouseButtonPress)
		pressSelection = selectedIndices();
	if (obj == table->viewport() && (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress))
		hidePreview();
	return QDialog::eventFilter(obj, event);
}

void PluginPicker::showFavoriteButtonMenu(int fav, const QPoint &globalPos)
{
	auto &meta = PluginMeta::instance();
	QMenu menu(this);
	connect(menu.addAction(T("Browser.RenameFavorite")), &QAction::triggered, this, [this, fav] { renameFavorite(fav); });
	// Only the last one can go, so favorite numbers never shift under the user.
	if (fav == meta.favoriteCount() - 1 && fav > 0)
		connect(menu.addAction(T("Browser.RemoveFavorite")), &QAction::triggered, this, [this, fav] {
			if (QMessageBox::question(this, T("Browser.RemoveFavorite"),
						  T("Browser.RemoveFavorite.Confirm").arg(favoriteLabel(fav))) ==
			    QMessageBox::Yes)
				PluginMeta::instance().removeLastFavorite();
		});
	menu.exec(globalPos);
}

void PluginPicker::showPluginMenu(int index, const QPoint &globalPos)
{
	hidePreview();
	auto &meta = PluginMeta::instance();
	const auto d = plugins[index];
	QString key = PluginMeta::keyOf(d);
	PluginMeta::Entry e = meta.get(key);

	QStringList keys = targetKeys(index);
	QMenu menu(this);
	QMenu *favMenu = menu.addMenu(QStringLiteral("★ ") + T("Browser.Favorites"));
	for (int i = 0; i < meta.favoriteCount(); i++) {
		QAction *a = favMenu->addAction(favoriteLabel(i));
		a->setCheckable(true);
		a->setChecked(e.favorites & (1u << i));
		connect(a, &QAction::toggled, this, [keys, i](bool on) { PluginMeta::instance().setFavorite(keys, i, on); });
	}
	connect(menu.addAction(e.hidden ? T("Browser.Unhide") : T("Browser.Hide")), &QAction::triggered, this,
		[keys, e] { PluginMeta::instance().setHidden(keys, !e.hidden); });
	connect(menu.addAction(T("Browser.EditMemo")), &QAction::triggered, this, [this, index] { editMemo(index); });
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("📷 ") + T("Browser.Capture")), &QAction::triggered, this,
		[this, d] { EditorWindow::captureThumbnail(d, this); });
	connect(menu.addAction(T("Browser.ThumbFromFile")), &QAction::triggered, this,
		[this, index] { setThumbnailFromFile(index); });
	menu.exec(globalPos);
}

void PluginPicker::renameFavorite(int fav)
{
	auto &meta = PluginMeta::instance();
	bool ok = false;
	QString name = QInputDialog::getText(this, T("Browser.RenameFavorite"), T("Browser.RenameFavorite.Prompt").arg(fav + 1),
					     QLineEdit::Normal, meta.favoriteName(fav), &ok);
	if (ok)
		meta.setFavoriteName(fav, name.trimmed());
}

void PluginPicker::editMemo(int index)
{
	auto &meta = PluginMeta::instance();
	QString key = PluginMeta::keyOf(plugins[index]);
	bool ok = false;
	QString memo = QInputDialog::getMultiLineText(this, T("Browser.EditMemo"),
						      QString::fromStdString(plugins[index].name), meta.get(key).memo, &ok);
	if (ok)
		meta.setMemo(key, memo.trimmed());
}

void PluginPicker::setThumbnailFromFile(int index)
{
	QString path = QFileDialog::getOpenFileName(this, T("Browser.ThumbFromFile"), QString(),
						    "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
	if (path.isEmpty())
		return;
	QImage image(path);
	if (!image.isNull())
		PluginMeta::instance().setThumbnail(plugins[index], image);
}

// Opens every editor once, screenshots it and closes it again.
void PluginPicker::startBatchThumbnails()
{
	auto &meta = PluginMeta::instance();
	QVector<vstex::PluginDescription> missing, all;
	for (const auto &d : plugins) {
		if (!d.hasEditor || meta.get(PluginMeta::keyOf(d)).hidden)
			continue;
		all << d;
		if (!meta.hasUserThumbnail(d))
			missing << d;
	}
	if (all.isEmpty()) {
		QMessageBox::information(this, T("Thumbs.Batch"), T("Browser.NotScanned"));
		return;
	}

	QMessageBox box(QMessageBox::Question, T("Thumbs.Batch"), T("Thumbs.Batch.Confirm"), QMessageBox::Cancel, this);
	auto addChoice = [&](const QVector<vstex::PluginDescription> &list, const char *key) {
		QString text = T(key).arg(list.size()).arg(durationText(thumbMsPerPlugin * list.size()));
		QPushButton *b = box.addButton(text, QMessageBox::AcceptRole);
		b->setEnabled(!list.isEmpty());
		return b;
	};
	QPushButton *missingButton = addChoice(missing, "Thumbs.Batch.Missing");
	QPushButton *allButton = addChoice(all, "Thumbs.Batch.All");
	box.setDefaultButton(missing.isEmpty() ? allButton : missingButton);
	box.exec();
	if (box.clickedButton() == missingButton)
		thumbQueue = missing;
	else if (box.clickedButton() == allButton)
		thumbQueue = all;
	else
		return;

	thumbRunning = true;
	thumbDone = 0;
	thumbFailed = 0;
	thumbTotal = (int)thumbQueue.size();
	thumbClock.start();
	thumbsButton->setText(T("Thumbs.Stop"));
	captureNextThumbnail();
}

void PluginPicker::captureNextThumbnail()
{
	if (thumbQueue.isEmpty()) {
		finishBatchThumbnails();
		return;
	}
	vstex::PluginDescription d = thumbQueue.takeFirst();
	qint64 perPlugin = thumbDone > 0 ? thumbClock.elapsed() / thumbDone : thumbMsPerPlugin;
	status->setText(T("Thumbs.Progress")
				.arg(thumbDone + 1)
				.arg(thumbTotal)
				.arg(durationText(perPlugin * (thumbTotal - thumbDone)))
				.arg(QString::fromStdString(d.name)));
	EditorWindow::captureThumbnail(d, this, [this](bool ok) {
		thumbDone++;
		if (!ok)
			thumbFailed++;
		// Let the closed editor go away before loading the next plugin.
		QTimer::singleShot(0, this, &PluginPicker::captureNextThumbnail);
	});
}

void PluginPicker::finishBatchThumbnails()
{
	if (thumbDone > 0)
		thumbMsPerPlugin = thumbClock.elapsed() / thumbDone;
	thumbRunning = false;
	thumbsButton->setText(QStringLiteral("📷 ") + T("Thumbs.Batch"));
	refresh();
	status->setText(T("Thumbs.Done")
				.arg(thumbDone - thumbFailed)
				.arg(thumbFailed)
				.arg(durationText(thumbClock.elapsed())));
}

void PluginPicker::editFolders()
{
	auto &catalog = PluginCatalog::instance();
	bool ok = false;
	QString text = QInputDialog::getMultiLineText(this, T("Folders"), T("FoldersHelp"),
						      QDir::toNativeSeparators(catalog.searchPaths().join('\n')), &ok);
	if (!ok)
		return;
	QStringList list;
	for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
		if (!line.trimmed().isEmpty())
			list << QDir::cleanPath(QDir::fromNativeSeparators(line.trimmed()));
	catalog.setSearchPaths(list);
	status->setText(T("Folders.Changed"));
}

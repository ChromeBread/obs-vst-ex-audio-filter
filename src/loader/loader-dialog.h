#pragma once

#include <QDialog>

class QWidget;

// Tools > VST Enigmax Loader: version, VST folders, thumbnails, screenshots, data folder.
class LoaderDialog : public QDialog {
	Q_OBJECT

public:
	static void open();

private:
	explicit LoaderDialog(QWidget *parent);
	QWidget *buildShotsPage(const QString &dir, const char *countKey, bool highRes);
	QWidget *buildFoldersPage();
	QWidget *buildFilesPage();
	QWidget *buildTextPage(const QString &text, const QString &fileName);
	QWidget *buildHandoverPage();
	void reopen();
};

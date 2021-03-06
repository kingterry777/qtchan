#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "boardtab.h"
#include "threadtab.h"
#include "threadform.h"
#include "notificationview.h"
#include <QFile>
#include <QString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>
#include <QStandardItem>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QShortcut>
#include <QDesktopServices>

//TODO decouple item model/view logic to another class
MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	ui->pushButton->hide();
	ui->navBar->hide();
	ui->treeView->setModel(model);
	QSettings settings;


	selectionModel = ui->treeView->selectionModel();
	selectionConnection = connect(selectionModel,&QItemSelectionModel::selectionChanged,this,
								  &MainWindow::onSelectionChanged, Qt::UniqueConnection);
	settingsView.setParent(this,Qt::Tool
						   | Qt::WindowMaximizeButtonHint
						   | Qt::WindowCloseButtonHint);
	connect(&settingsView,&Settings::update,this,&MainWindow::onUpdateSettings);
	connect(ui->treeView,&TreeView::treeMiddleClicked,model,&TreeModel::removeTab,Qt::DirectConnection);
	connect(ui->treeView,&TreeView::hideNavBar,ui->navBar,&QWidget::hide,Qt::DirectConnection);
	connect(model,&TreeModel::selectTab,ui->treeView,&TreeView::selectTab,Qt::DirectConnection);
	connect(model,&TreeModel::loadFromSearch,this,&MainWindow::loadFromSearch);
	connect(model,&TreeModel::removingTab,this,&MainWindow::onRemoveTab);
	connect(&aTab,&ArchiveTab::loadThread,[=](QString threadString){
		loadFromSearch(threadString,QString(),Q_NULLPTR,true);
	});
	setStyleSheet(settings.value("style/MainWindow","background-color: #191919; color:white").toString());
	int fontSize = settings.value("fontSize",14).toInt();
	if(settings.value("hideMenuBar",false).toBool()) ui->menuBar->hide();
	QFont temp = ui->treeView->font();
	temp.setPointSize(fontSize);
	ui->treeView->setFont(temp);
	ui->navBar->setFont(temp);

	//instructions
	QFile file(":/readstartup.md");
	if(file.open(QIODevice::ReadOnly)){
		QByteArray dump = file.readAll();
		file.close();
		ui->help->setFont(temp);
		ui->help->setText(QString(dump));
	}
	QList<int> sizes;
	sizes << 150 << this->width()-150;
	ui->splitter->setSizes(sizes);
	this->setShortcuts();
}

//work around because if you haven't viewed a tab setStyleSheet will segfault
void MainWindow::viewAllTabs(){
	int curr = ui->content->currentIndex();
	int count = ui->content->count();
	for(int i=0;i<count;i++){
		ui->content->setCurrentIndex(i);
	}
	ui->content->setCurrentIndex(curr);
}

void MainWindow::onUpdateSettings(QString field, QVariant value){
	if(field == "use4chanPass" && value.toBool() == true){
		QSettings settings;
		QString defaultCookies = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/qtchan/cookies.conf";
		nc->loadCookies(settings.value("passFile",defaultCookies).toString());
	}
	else if(field == "autoUpdate") emit setAutoUpdate(value.toBool());
	else if(field == "autoExpand") emit setAutoExpand(value.toBool());
	else if(field == "style/MainWindow"){
		viewAllTabs();
		setStyleSheet(value.toString());
		emit updateStyles("MainWindow",value.toString());
	}
	else if(field == "style/ThreadForm"){
		viewAllTabs();
		emit updateStyles("ThreadForm",value.toString());
	}
}

void MainWindow::closeEvent(QCloseEvent *event){
	(void)event;
	QApplication::closeAllWindows();
}

void MainWindow::onRemoveTab(TreeItem* tn){
	tn->tab->deleteLater();
	Tab deletingTab = tabs.value(tn->tab);
	if(closedList.size() == 100) closedList.pop_front();
	closedList.append(qMakePair(deletingTab.query,deletingTab.display));
	TreeItem *parent = tn->parent;
	tabs.remove(tn->tab);
	QWidget *cw = (parent->childCount() > 0) ? currentWidget() : parent->tab;
	if(!cw) return;
	Tab current = tabs.value(cw);
	if(!current.tn) return;
	QModelIndex ind = model->getIndex(current.tn);
	if(ind != ui->treeView->rootIndex()){
		selectionModel->setCurrentIndex(ind,QItemSelectionModel::ClearAndSelect);
		ui->treeView->setCurrentIndex(ind);
	}
}

void MainWindow::setShortcuts()
{
	QSettings settings;
	settings.beginGroup("keybinds");
	//hiding the menu bar disables other qmenu actions shortcuts
	addShortcut(QKeySequence(settings.value("hideMenu","F11").toString()),this,[=]{
		ui->menuBar->isHidden() ? ui->menuBar->show() : ui->menuBar->hide();
	});

	addShortcut(QKeySequence(settings.value("prevTab","ctrl+shift+tab").toString()),this,&MainWindow::prevTab);
	addShortcut(QKeySequence(settings.value("nextTab","ctrl+tab").toString()),this,&MainWindow::nextTab);

	addShortcut(QKeySequence(settings.value("firstTab","ctrl+1").toString()),this, [=]{
		if(!model->rowCount()) return;
		selectionModel->setCurrentIndex(model->index(0,0),QItemSelectionModel::ClearAndSelect);
		ui->treeView->setCurrentIndex(model->index(0,0));
	});

	addShortcut(QKeySequence(settings.value("prevParent","ctrl+2").toString()),this,&MainWindow::prevParent);
	addShortcut(QKeySequence(settings.value("nextParent","ctrl+3").toString()),this,&MainWindow::nextParent);

	addShortcut(QKeySequence(settings.value("lastTab","ctrl+4").toString()),this,[=]{
		if(!model->rowCount()) return;
		TreeItem *tm = model->getItem(model->index(model->rowCount(),0));
		while(tm->childCount()){
			tm = tm->child(tm->childCount()-1);
		}
		QModelIndex qmi = model->getIndex(tm);
		selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
		ui->treeView->setCurrentIndex(qmi);
	});
	addShortcut(QKeySequence(settings.value("closeTab2","delete").toString()), this, &MainWindow::deleteSelected,
				Qt::AutoConnection,Qt::WindowShortcut);
	addShortcut(QKeySequence(settings.value("closeTab","ctrl+w").toString()),this,&MainWindow::deleteSelected);
	addShortcut(QKeySequence(settings.value("navBar","ctrl+l").toString()),this,&MainWindow::focusBar);
	addShortcut(QKeySequence(settings.value("autoUpdate","ctrl+u").toString()),this,&MainWindow::toggleAutoUpdate);
	addShortcut(QKeySequence(settings.value("autoExpand","ctrl+e").toString()),this,&MainWindow::toggleAutoExpand);

	addShortcut(QKeySequence(settings.value("fileManager","ctrl+o").toString()),this,&MainWindow::openExplorer);
	addShortcut(QKeySequence(settings.value("textSmaller",QKeySequence(QKeySequence::ZoomOut).toString()).toString()),this,[=](){
		qDebug() << "decreasing text size";
		QSettings settings;
		int fontSize = settings.value("fontSize",14).toInt()-2;
		if(fontSize < 4) fontSize = 4;
		settings.setValue("fontSize",fontSize);
		QFont temp = ui->treeView->font();
		temp.setPointSize(fontSize);
		ui->treeView->setFont(temp);
		ui->navBar->setFont(temp);
		ui->help->setFont(temp);
		emit setFontSize(fontSize);
	});

	addShortcut(QKeySequence(settings.value("textBigger",QKeySequence(QKeySequence::ZoomIn).toString()).toString()),this,[=](){
		qDebug() << "increasing text size";
		QSettings settings;
		int fontSize = settings.value("fontSize",14).toInt()+2;
		settings.setValue("fontSize",fontSize);
		QFont temp = ui->treeView->font();
		temp.setPointSize(fontSize);
		ui->treeView->setFont(temp);
		ui->navBar->setFont(temp);
		ui->help->setFont(temp);
		emit setFontSize(fontSize);
	});

	addShortcut(QKeySequence(settings.value("imagesSmaller","ctrl+9").toString()),this,[=](){
		qDebug() << "decreasing image size";
		QSettings settings;
		int imageSize = settings.value("imageSize",250).toInt()-25;
		if(imageSize < 25) imageSize = 25;
		settings.setValue("imageSize",imageSize);
		emit setImageSize(imageSize);
	});

	addShortcut(QKeySequence(settings.value("imagesBigger","ctrl+0").toString()),this,[=](){
		qDebug() << "increasing image size";
		QSettings settings;
		int imageSize = settings.value("imageSize",250).toInt()+25;
		settings.setValue("imageSize",imageSize);
		emit setImageSize(imageSize);
	});

	ui->actionSave->setShortcut(QKeySequence(settings.value("saveSession","ctrl+s").toString()));
	ui->actionSave->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(ui->actionSave,&QAction::triggered,[=]{saveSession();});
	addShortcut(QKeySequence(settings.value("saveSession","ctrl+s").toString()),this,[=]{saveSession();});

	ui->actionReload->setShortcut(QKeySequence(settings.value("refreshTabs","ctrl+r").toString()));
	ui->actionReload->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(ui->actionReload,&QAction::triggered,this,&MainWindow::reloadTabs);
	addShortcut(QKeySequence(settings.value("refreshTabs","ctrl+r").toString()),this,&MainWindow::reloadTabs);

	ui->actionSettings->setShortcut(QKeySequence(settings.value("toggleSettings","ctrl+p").toString()));
	ui->actionSettings->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(ui->actionSettings,&QAction::triggered,this,&MainWindow::toggleSettingsView);
	addShortcut(QKeySequence(settings.value("toggleSettings","ctrl+p").toString()),this,&MainWindow::toggleSettingsView);


	ui->actionExit->setShortcut(QKeySequence(settings.value("quit","ctrl+q").toString()));
	ui->actionExit->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(ui->actionExit,&QAction::triggered,this,&QApplication::quit);
	addShortcut(QKeySequence(settings.value("quit","ctrl+q").toString()),this,&QApplication::quit);

	ui->actionReloadFilters->setShortcut(QKeySequence(settings.value("reloadFilters","F7").toString()));
	ui->actionReloadFilters->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(ui->actionReloadFilters,&QAction::triggered,[=](){
		filter.loadFilterFile2();
		emit reloadFilters();
	});

	ui->actionAbout->setShortcut(QKeySequence(settings.value("showHelp","F1").toString()));
	ui->actionAbout->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(ui->actionAbout,&QAction::triggered,this,&MainWindow::showHelp);

	addShortcut(QKeySequence(settings.value("reloadFilters","F7").toString()),this,[=]{
		filter.loadFilterFile2();
		emit reloadFilters();
	});
	addShortcut(QKeySequence(settings.value("find","ctrl+f").toString()),this,[=]{
		TreeItem *item = model->getItem(selectionModel->currentIndex());
		if(!item) return;
		if(item->type == TreeItemType::thread){
			ThreadTab *tab = static_cast<ThreadTab*>(item->tab);
			tab->focusIt();
		}
		else{
			BoardTab *tab = static_cast<BoardTab*>(item->tab);
			tab->focusIt();
		}
	});

	addShortcut(QKeySequence(settings.value("closeChildTabs","ctrl+k").toString()),this,&MainWindow::removeChildTabs);

	//TODO clean-up and fix focus back to mainwindow
	addShortcut(QKeySequence(settings.value("toggleNotifications","F9").toString()),this,[=]{
		if(nv->isVisible()){
			nv->hide();
			activateWindow();
		}
		else{
			nv->reAdjust();
			nv->show();
		}
	});
	addShortcut(QKeySequence(settings.value("hideNavBar","Esc").toString()),ui->navBar,&QLineEdit::hide,
				Qt::DirectConnection,Qt::WidgetWithChildrenShortcut);
	addShortcut(QKeySequence(settings.value("showHelp","F1").toString()),this,&MainWindow::showHelp);
	addShortcut(QKeySequence(settings.value("focusTree","F3").toString()),this,&MainWindow::focusTree);
	addShortcut(QKeySequence(settings.value("focusTab","F4").toString()),this,[=]{
		if(QWidget *temp = currentWidget()){
			if (tabs.find(temp).value().type == Tab::TabType::Board) qobject_cast<BoardTab*>(temp)->focusMain();
			else qobject_cast<ThreadTab*>(temp)->focusMain();
		}
	});
	//addShortcut(Qt::Key_F6,this,&MainWindow::focusBar);
	addShortcut(QKeySequence(settings.value("showArchive","F8").toString()),&aTab,&ArchiveTab::show);

	//session shortcuts
	for(int i=Qt::Key_F1, j=0; i<=Qt::Key_F4; i++, j++){
		QAction *saveShortcut = new QAction(this);
		saveShortcut->setShortcut(QKeySequence(Qt::ControlModifier+i));
		connect(saveShortcut, &QAction::triggered,[=]{
			qDebug() << "saving shortcut";
			saveSession(QString::number(j));
		});
		this->addAction(saveShortcut);
		QAction *loadShortcut = new QAction(this);
		loadShortcut->setShortcut(QKeySequence(Qt::ShiftModifier+i));
		connect(loadShortcut, &QAction::triggered,[=]{
			loadSession(QString::number(j));
		});
		this->addAction(loadShortcut);
	}
	addShortcut(QKeySequence(settings.value("saveSession2","F5").toString()),this,[=]{saveSession();});
	addShortcut(QKeySequence(settings.value("loadSession","F6").toString()),this,[=]{loadSession();});
	addShortcut(QKeySequence(settings.value("prevSession","ctrl+F5").toString()),this,[=]{
		QSettings settings;
		int slot = settings.value("sessionSlot",0).toInt();
		if(--slot < 0) slot = 9;
		settings.setValue("sessionSlot",slot);
		qDebug() << "current session slot:" << slot;
	});
	addShortcut(QKeySequence(settings.value("nextSession","ctrl+F6").toString()),this,[=]{
		QSettings settings;
		int slot = settings.value("sessionSlot",0).toInt();
		if(++slot > 9) slot = 0;
		settings.setValue("sessionSlot",slot);
		qDebug() << "current session slot:" << slot;
	});

	addShortcut(QKeySequence(settings.value("undoCloseTab","ctrl+shift+t").toString()),this,[=]{
		if(!closedList.length()) return;
		QPair<QString, QString> tabInfo = closedList.last();
		loadFromSearch(tabInfo.first, tabInfo.second, Q_NULLPTR, false);
		closedList.pop_back();
	});
}

void MainWindow::reloadTabs(){
	QMapIterator<QWidget*,Tab> i(tabs);
	while(i.hasNext()) {
		Tab tab = i.next().value();
		if(tab.type == Tab::TabType::Board) static_cast<BoardTab*>(tab.TabPointer)->getPosts();
		else static_cast<ThreadTab*>(tab.TabPointer)->getPosts();
	}
}
void MainWindow::toggleSettingsView(){
	if(settingsView.isVisible()){
		qDebug() << "hiding settings window";
		settingsView.hide();
	}
	else{
		qDebug() << "showing settings window";
		settingsView.show();
	}
}

void MainWindow::showHelp(){
	ui->content->setCurrentIndex(0);
	setWindowTitle("qtchan - help");
}

template<typename T, typename F>
void MainWindow::addShortcut(QKeySequence key,const T connectTo, F func,
							 Qt::ConnectionType type, Qt::ShortcutContext context){
	QAction *newAction = new QAction(this);
	newAction->setShortcut(key);
	newAction->setShortcutContext(context);
	connect(newAction,&QAction::triggered,connectTo,func,type);
	this->addAction(newAction);
}

MainWindow::~MainWindow()
{
	disconnect(selectionConnection);
	disconnect(model,&TreeModel::removingTab,this,&MainWindow::onRemoveTab);
	saveSession();
	you.saveYou(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/qtchan/you.conf");
	aTab.close();
	aTab.deleteLater();
	delete ui;
	delete model;
	Chans::deleteAPIs();
}

//TODO put toggle functions in 1 function with argument
void MainWindow::toggleAutoUpdate()
{
	QSettings settings;
	bool autoUpdate = !settings.value("autoUpdate").toBool();
	qDebug () << "setting autoUpdate to" << autoUpdate;
	settings.setValue("autoUpdate",autoUpdate);
	emit setAutoUpdate(autoUpdate);
	settingsView.refreshValues();
}

void MainWindow::toggleAutoExpand()
{
	QSettings settings;
	bool autoExpand = !settings.value("autoExpand").toBool();
	qDebug () << "setting autoExpand to" << autoExpand;
	settings.setValue("autoExpand",autoExpand);
	emit setAutoExpand(autoExpand);
	settingsView.refreshValues();
}

void MainWindow::updateSettings(QString field, QVariant value){
	if(field == "autoUpdate")
		emit setAutoUpdate(value.toBool());
	else if(field == "autoExpand")
		emit setAutoExpand(value.toBool());
	else if(field == "use4chanPass"){
		emit setUse4chanPass(value.toBool());
		if(value.toBool()){
			QSettings settings;
			QString defaultCookies = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/qtchan/cookies.conf";
			nc->loadCookies(settings.value("passFile",defaultCookies).toString());
		}
		else{
			nc->removeCookies();
		}
	}
}

void MainWindow::openExplorer(){
	QString url;
	if(!ui->content->count()) return;
	TreeItem *item = model->getItem(selectionModel->currentIndex());
	if(item->type == TreeItemType::thread){
		ThreadTab *tab = static_cast<ThreadTab*>(item->tab);
		url = QDir(tab->api->name()+'/'+tab->board+'/'+tab->thread).absolutePath();
	}
	else{
		BoardTab *tab = static_cast<BoardTab*>(item->tab);
		url = QDir(tab->api->name()+'/'+tab->board).absolutePath();
	}
	qDebug() << "Opening folder" << url;
	QDesktopServices::openUrl(QUrl::fromLocalFile(url));
}

void MainWindow::nextTab()
{
	QModelIndex qmi = ui->treeView->currentIndex();
	QKeyEvent event(QEvent::KeyPress,Qt::Key_Down,Q_NULLPTR);
	QApplication::sendEvent(ui->treeView, &event);
	if(qmi == ui->treeView->currentIndex()){
		qmi = model->index(0,0);
		selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
		ui->treeView->setCurrentIndex(qmi);
	}
}

void MainWindow::prevTab()
{
	QModelIndex qmi = ui->treeView->currentIndex();
	QKeyEvent event(QEvent::KeyPress,Qt::Key_Up,Q_NULLPTR);
	QApplication::sendEvent(ui->treeView, &event);
	if(qmi == ui->treeView->currentIndex()){
		qmi = model->index(model->rowCount()-1,0);
		while(model->hasChildren(qmi) && ui->treeView->isExpanded(qmi)) {
			qmi = qmi.child(model->rowCount(qmi)-1,0);
		}
		selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
		ui->treeView->setCurrentIndex(qmi);
	}
}

void MainWindow::prevParent(){
	int rowCount = model->rowCount();
	if(!rowCount) return;
	QModelIndex qmi = ui->treeView->currentIndex();
	if(qmi.parent() != QModelIndex()){
		while(qmi.parent() != QModelIndex()){
			qmi = qmi.parent();
		}
		selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
		ui->treeView->setCurrentIndex(qmi);
		return;
	}
	int row = qmi.row()-1;
	if(row == -1) row = rowCount-1;
	qmi = qmi.sibling(row,0);
	selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
	ui->treeView->setCurrentIndex(qmi);

}

void MainWindow::nextParent(){
	int rowCount = model->rowCount();
	if(!rowCount) return;
	QModelIndex qmi = ui->treeView->currentIndex();
	while(qmi.parent() != QModelIndex()){
		qmi = qmi.parent();
	}
	int row = qmi.row()+1;
	if(row == rowCount) row = 0;
	qmi = qmi.sibling(row,0);
	selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
	ui->treeView->setCurrentIndex(qmi);
}

void MainWindow::on_pushButton_clicked()
{
	QString searchString = ui->navBar->text();
	ui->navBar->hide();
	loadFromSearch(searchString,QString(),Q_NULLPTR,true);
}

TreeItem *MainWindow::loadFromSearch(QString query, QString display, TreeItem *childOf, bool select)
{
	qDebug() << query;
	Chan *api = Chans::stringToType(query);
	if(api==Q_NULLPTR){
		qDebug() << query << "is an invalid URL";
		return Q_NULLPTR;
	}
	qDebug() << query;
	QRegularExpressionMatch match = api->regToThread().match(query);
	QRegularExpressionMatch match2;
	BoardTab *bt;
	if (!match.hasMatch()) {
		match2 = api->regToCatalog().match(query);
	}
	else{
		TreeItem *tnParent;
		if(childOf == Q_NULLPTR)tnParent = model->root;
		else tnParent = childOf;
		return onNewThread(this,api,match.captured("board"),match.captured("thread"),display,tnParent);
	}
	QString sessionQuery;
	if(match2.hasMatch()) {
		bt = new BoardTab(api,match2.captured(1),BoardType::Catalog,match2.captured(2),this);
		if(!display.length()) display = '/' % match2.captured("board") % '/' % match2.captured("search");
		sessionQuery = display;
	}
	else{
		bt = new BoardTab(api,query,BoardType::Index,"",this);
		if(!display.length()) display = "/"+query+"/";
		sessionQuery = '/' % query;
	}
	qDebug().noquote() << "loading" << display;
	ui->content->addWidget(bt);

	QList<QVariant> list;
	list.append(display);
	TreeItem *tnParent;
	if(childOf == Q_NULLPTR) tnParent = model->root;
	else tnParent = childOf;
	TreeItem *tnNew = new TreeItem(list,tnParent,bt, TreeItemType::board);
	tnNew->api = api->name();
	tnNew->query = sessionQuery;
	tnNew->display = display;
	Tab tab = {Tab::TabType::Board,bt,tnNew,query,display};
	tabs.insert(bt,tab);
	model->addTab(tnNew,tnParent,select);
	ui->treeView->setExpanded(model->getIndex(tnNew).parent(),true);
	return tnNew;
}

TreeItem *MainWindow::onNewThread(QWidget *parent, Chan *api, QString board, QString thread,
								  QString display, TreeItem *childOf)
{
	(void)parent;
	qDebug().noquote().nospace()  << "loading /" << board << "/" << thread;
	QString query = "/"+board+"/"+thread;
	if(!display.length()) display = query;
	bool isFromSession = (display == query) ? false : true;
	ThreadTab *tt = new ThreadTab(api,board,thread,ui->content,isFromSession);
	ui->content->addWidget(tt);
	QList<QVariant> list;
	list.append(display);

	TreeItem *tnNew = new TreeItem(list,model->root,tt, TreeItemType::thread);
	tnNew->api = api->name();
	tnNew->query = query;
	tnNew->display = display;
	tt->tn = tnNew;
	Tab tab = {Tab::TabType::Thread,tt,tnNew,query,display};
	tabs.insert(tt,tab);
	connect(tt,&ThreadTab::unseen,this,&MainWindow::updateSeen);
	model->addTab(tnNew,childOf,false);
	ui->treeView->setExpanded(model->getIndex(childOf),true);
	return tnNew;
}

void MainWindow::updateSeen(int formsUnseen){
	TreeItem *tn = static_cast<ThreadTab*>(sender())->tn;
	tn->setData(1,formsUnseen);
	tn->unseen = formsUnseen;
}

void MainWindow::onSelectionChanged()
{
	QModelIndexList list = ui->treeView->selected();
	if(list.size()) {
		QPointer<QWidget> tab = model->getItem(list.at(0))->tab;
		if(tab){
			ui->content->setCurrentWidget(tab);
			setWindowTitle(tab->windowTitle());
			currentTab = tab;
		}
	}
	else setWindowTitle("qtchan");
}

void MainWindow::deleteSelected()
{
	if(!tabs.size()) return;
	QModelIndexList list = ui->treeView->selected();
	disconnect(selectionConnection);
	if(!list.size()){
		Tab tab = tabs.value(ui->content->currentWidget());
		if(tab.tn) tab.tn->deleteLater();
	}
	else foreach(QModelIndex index, list) {
		model->removeTab(index);
	}
	onSelectionChanged();
	selectionConnection = connect(selectionModel,&QItemSelectionModel::selectionChanged,this,
								  &MainWindow::onSelectionChanged, Qt::UniqueConnection);
}

void MainWindow::removeChildTabs(){
	foreach(QModelIndex index, ui->treeView->selected()) {
		model->removeChildren(index);
	}
}

QWidget *MainWindow::currentWidget()
{
	if(tabs.size() && ui->content->count())
		return ui->content->currentWidget();
	else return Q_NULLPTR;
}

void MainWindow::on_navBar_returnPressed()
{
	on_pushButton_clicked();
	ui->treeView->setFocus();
}

void MainWindow::focusTree()
{
	ui->treeView->setFocus();
}

void MainWindow::focusBar()
{
	if(ui->tst->isHidden()) ui->tst->show();
	if(ui->navBar->isHidden()) ui->navBar->show();
	ui->navBar->setFocus();
	ui->navBar->selectAll();
}

void MainWindow::saveSession(QString slot)
{
	QSettings settings;
	if(slot.isEmpty()) slot = settings.value("sessionSlot","0").toString();
	QString sessionPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/qtchan/sessions/";
	QDir().mkpath(sessionPath);
	QString sessionFile = sessionPath + settings.value("sessionFile","session").toString();
	model->saveSessionToFile(sessionFile,slot,ui->treeView->currentIndex());
}

void MainWindow::loadSession(QString slot)
{
	QSettings settings;
	if(slot.isEmpty()) slot = settings.value("sessionSlot","0").toString();
	QString sessionFile = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/qtchan/sessions/" + settings.value("sessionFile","session").toString();
	QModelIndex qmi = model->loadSessionFromFile(sessionFile,slot);
	selectionModel->setCurrentIndex(qmi,QItemSelectionModel::ClearAndSelect);
	ui->treeView->setCurrentIndex(qmi);
}

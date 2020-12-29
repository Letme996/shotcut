/*
 * Copyright (c) 2011-2020 Meltytech, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "scrubbar.h"
#include "openotherdialog.h"
#include "player.h"
#include "defaultlayouts.h"
#include "widgets/alsawidget.h"
#include "widgets/colorbarswidget.h"
#include "widgets/colorproducerwidget.h"
#include "widgets/countproducerwidget.h"
#include "widgets/decklinkproducerwidget.h"
#include "widgets/directshowvideowidget.h"
#include "widgets/isingwidget.h"
#include "widgets/jackproducerwidget.h"
#include "widgets/toneproducerwidget.h"
#include "widgets/lissajouswidget.h"
#include "widgets/networkproducerwidget.h"
#include "widgets/noisewidget.h"
#include "widgets/plasmawidget.h"
#include "widgets/pulseaudiowidget.h"
#include "widgets/video4linuxwidget.h"
#include "widgets/x11grabwidget.h"
#include "widgets/avformatproducerwidget.h"
#include "widgets/imageproducerwidget.h"
#include "widgets/blipproducerwidget.h"
#include "widgets/newprojectfolder.h"
#include "docks/recentdock.h"
#include "docks/encodedock.h"
#include "docks/jobsdock.h"
#include "jobqueue.h"
#include "docks/playlistdock.h"
#include "glwidget.h"
#include "controllers/filtercontroller.h"
#include "controllers/scopecontroller.h"
#include "docks/filtersdock.h"
#include "dialogs/customprofiledialog.h"
#include "settings.h"
#include "leapnetworklistener.h"
#include "database.h"
#include "widgets/gltestwidget.h"
#include "docks/timelinedock.h"
#include "widgets/lumamixtransition.h"
#include "qmltypes/qmlutilities.h"
#include "qmltypes/qmlapplication.h"
#include "autosavefile.h"
#include "commands/playlistcommands.h"
#include "shotcut_mlt_properties.h"
#include "widgets/avfoundationproducerwidget.h"
#include "dialogs/textviewerdialog.h"
#include "widgets/gdigrabwidget.h"
#include "models/audiolevelstask.h"
#include "widgets/trackpropertieswidget.h"
#include "widgets/timelinepropertieswidget.h"
#include "dialogs/unlinkedfilesdialog.h"
#include "docks/keyframesdock.h"
#include "util.h"
#include "models/keyframesmodel.h"
#include "dialogs/listselectiondialog.h"
#include "widgets/textproducerwidget.h"
#include "qmltypes/qmlprofile.h"
#include "dialogs/longuitask.h"
#include "dialogs/systemsyncdialog.h"
#include "proxymanager.h"
#ifdef Q_OS_WIN
#include "windowstools.h"
#endif

#include <QtWidgets>
#include <Logger.h>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QMutexLocker>
#include <QQuickItem>
#include <QtNetwork>
#include <QJsonDocument>
#include <QJSEngine>
#include <QDirIterator>
#include <QQuickWindow>
#include <QVersionNumber>
#include <clocale>

static bool eventDebugCallback(void **data)
{
    QEvent *event = reinterpret_cast<QEvent*>(data[1]);
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        QObject *receiver = reinterpret_cast<QObject*>(data[0]);
        LOG_DEBUG() << event << "->" << receiver;
    }
    else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        QObject *receiver = reinterpret_cast<QObject*>(data[0]);
        LOG_DEBUG() << event << "->" << receiver;
    }
    return false;
}

static const int AUTOSAVE_TIMEOUT_MS = 60000;
static const char* kReservedLayoutPrefix = "__%1";
static const char* kLayoutSwitcherName("layoutSwitcherGrid");

MainWindow::MainWindow()
    : QMainWindow(0)
    , ui(new Ui::MainWindow)
    , m_isKKeyPressed(false)
    , m_keyerGroup(0)
    , m_previewScaleGroup(0)
    , m_keyerMenu(0)
    , m_isPlaylistLoaded(false)
    , m_exitCode(EXIT_SUCCESS)
    , m_navigationPosition(0)
    , m_upgradeUrl("https://www.shotcut.org/download/")
    , m_keyframesDock(0)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    QLibrary libJack("libjack.so.0");
    if (!libJack.load()) {
        QMessageBox::critical(this, qApp->applicationName(),
            tr("Error: This program requires the JACK 1 library.\n\nPlease install it using your package manager. It may be named libjack0, jack-audio-connection-kit, jack, or similar."));
        ::exit(EXIT_FAILURE);
    } else {
        libJack.unload();
    }
    QLibrary libSDL("libSDL2-2.0.so.0");
    if (!libSDL.load()) {
        QMessageBox::critical(this, qApp->applicationName(),
            tr("Error: This program requires the SDL 2 library.\n\nPlease install it using your package manager. It may be named libsdl2-2.0-0, SDL2, or similar."));
        ::exit(EXIT_FAILURE);
    } else {
        libSDL.unload();
    }
#endif

    if (!qgetenv("OBSERVE_FOCUS").isEmpty()) {
        connect(qApp, &QApplication::focusChanged,
                this, &MainWindow::onFocusChanged);
        connect(qApp, &QGuiApplication::focusObjectChanged,
                this, &MainWindow::onFocusObjectChanged);
        connect(qApp, &QGuiApplication::focusWindowChanged,
                this, &MainWindow::onFocusWindowChanged);
    }

    if (!qgetenv("EVENT_DEBUG").isEmpty())
        QInternal::registerCallback(QInternal::EventNotifyCallback, eventDebugCallback);

    LOG_DEBUG() << "begin";
    LOG_INFO() << "device pixel ratio =" << devicePixelRatioF();
#ifndef Q_OS_WIN
    new GLTestWidget(this);
#endif
    connect(&m_autosaveTimer, SIGNAL(timeout()), this, SLOT(onAutosaveTimeout()));
    m_autosaveTimer.start(AUTOSAVE_TIMEOUT_MS);

    // Initialize all QML types
    QmlUtilities::registerCommonTypes();

    // Create the UI.
    ui->setupUi(this);
#ifdef Q_OS_MAC
    // Qt 5 on OS X supports the standard Full Screen window widget.
    ui->actionEnter_Full_Screen->setVisible(false);
    // OS X has a standard Full Screen shortcut we should use.
    ui->actionEnter_Full_Screen->setShortcut(QKeySequence((Qt::CTRL + Qt::META + Qt::Key_F)));
#endif
    setDockNestingEnabled(true);
    ui->statusBar->hide();

    // Connect UI signals.
    connect(ui->actionOpen, SIGNAL(triggered()), this, SLOT(openVideo()));
    connect(ui->actionAbout_Qt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(this, &MainWindow::producerOpened, this, &MainWindow::onProducerOpened);
    connect(ui->mainToolBar, SIGNAL(visibilityChanged(bool)), SLOT(onToolbarVisibilityChanged(bool)));

    // Accept drag-n-drop of files.
    this->setAcceptDrops(true);

    // Setup the undo stack.
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(Settings.undoLimit());
    QAction *undoAction = m_undoStack->createUndoAction(this);
    QAction *redoAction = m_undoStack->createRedoAction(this);
    undoAction->setIcon(QIcon::fromTheme("edit-undo", QIcon(":/icons/oxygen/32x32/actions/edit-undo.png")));
    redoAction->setIcon(QIcon::fromTheme("edit-redo", QIcon(":/icons/oxygen/32x32/actions/edit-redo.png")));
    undoAction->setShortcut(QApplication::translate("MainWindow", "Ctrl+Z", 0));
#ifdef Q_OS_WIN
    redoAction->setShortcut(QApplication::translate("MainWindow", "Ctrl+Y", 0));
#else
    redoAction->setShortcut(QApplication::translate("MainWindow", "Ctrl+Shift+Z", 0));
#endif
    ui->menuEdit->insertAction(ui->actionCut, undoAction);
    ui->menuEdit->insertAction(ui->actionCut, redoAction);
    ui->menuEdit->insertSeparator(ui->actionCut);
    ui->actionUndo->setIcon(undoAction->icon());
    ui->actionRedo->setIcon(redoAction->icon());
    ui->actionUndo->setToolTip(undoAction->toolTip());
    ui->actionRedo->setToolTip(redoAction->toolTip());
    connect(m_undoStack, SIGNAL(canUndoChanged(bool)), ui->actionUndo, SLOT(setEnabled(bool)));
    connect(m_undoStack, SIGNAL(canRedoChanged(bool)), ui->actionRedo, SLOT(setEnabled(bool)));

    // Add the player widget.
    m_player = new Player;
    MLT.videoWidget()->installEventFilter(this);
    ui->centralWidget->layout()->addWidget(m_player);
    connect(this, &MainWindow::producerOpened, m_player, &Player::onProducerOpened);
    connect(m_player, SIGNAL(showStatusMessage(QString)), this, SLOT(showStatusMessage(QString)));
    connect(m_player, SIGNAL(inChanged(int)), this, SLOT(onCutModified()));
    connect(m_player, SIGNAL(outChanged(int)), this, SLOT(onCutModified()));
    connect(m_player, SIGNAL(tabIndexChanged(int)), SLOT(onPlayerTabIndexChanged(int)));
    connect(MLT.videoWidget(), SIGNAL(started()), SLOT(processMultipleFiles()));
    connect(MLT.videoWidget(), SIGNAL(paused()), m_player, SLOT(showPaused()));
    connect(MLT.videoWidget(), SIGNAL(playing()), m_player, SLOT(showPlaying()));
    connect(MLT.videoWidget(), SIGNAL(toggleZoom(bool)), m_player, SLOT(toggleZoom(bool)));

    setupSettingsMenu();
    setupOpenOtherMenu();
    readPlayerSettings();
    configureVideoWidget();

    // setup the layout switcher
    auto group = new QActionGroup(this);
    group->addAction(ui->actionLayoutLogging);
    group->addAction(ui->actionLayoutEditing);
    group->addAction(ui->actionLayoutEffects);
    group->addAction(ui->actionLayoutAudio);
    group->addAction(ui->actionLayoutColor);
    group->addAction(ui->actionLayoutPlayer);
    switch (Settings.layoutMode()) {
    case LayoutMode::Custom:
        break;
    case LayoutMode::Logging:
        ui->actionLayoutLogging->setChecked(true);
        break;
    case LayoutMode::Editing:
        ui->actionLayoutEditing->setChecked(true);
        break;
    case LayoutMode::Effects:
        ui->actionLayoutEffects->setChecked(true);
        break;
    case LayoutMode::Color:
        ui->actionLayoutColor->setChecked(true);
        break;
    case LayoutMode::Audio:
        ui->actionLayoutAudio->setChecked(true);
        break;
    case LayoutMode::PlayerOnly:
        ui->actionLayoutPlayer->setChecked(true);
        break;
    default:
        ui->actionLayoutEditing->setChecked(true);
        break;
    }
    // Center the layout actions in the remaining toolbar space.
    auto spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mainToolBar->insertWidget(ui->dummyAction, spacer);
    spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mainToolBar->addWidget(spacer);
    updateLayoutSwitcher();

#ifndef SHOTCUT_NOUPGRADE
    if (Settings.noUpgrade() || qApp->property("noupgrade").toBool())
#endif
        delete ui->actionUpgrade;

    // Add the docks.
    m_scopeController = new ScopeController(this, ui->menuView);
    QDockWidget* audioMeterDock = findChild<QDockWidget*>("AudioPeakMeterDock");
    if (audioMeterDock) {
        audioMeterDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_1));
        connect(ui->actionAudioMeter, SIGNAL(triggered()), audioMeterDock->toggleViewAction(), SLOT(trigger()));
    }

    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->hide();
    m_propertiesDock->setObjectName("propertiesDock");
    m_propertiesDock->setWindowIcon(ui->actionProperties->icon());
    m_propertiesDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_2));
    m_propertiesDock->toggleViewAction()->setIcon(ui->actionProperties->icon());
    m_propertiesDock->setMinimumWidth(300);
    QScrollArea* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    m_propertiesDock->setWidget(scroll);
    ui->menuView->addAction(m_propertiesDock->toggleViewAction());
    connect(m_propertiesDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onPropertiesDockTriggered(bool)));
    connect(ui->actionProperties, SIGNAL(triggered()), this, SLOT(onPropertiesDockTriggered()));

    m_recentDock = new RecentDock(this);
    m_recentDock->hide();
    m_recentDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_3));
    ui->menuView->addAction(m_recentDock->toggleViewAction());
    connect(m_recentDock, SIGNAL(itemActivated(QString)), this, SLOT(open(QString)));
    connect(m_recentDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onRecentDockTriggered(bool)));
    connect(ui->actionRecent, SIGNAL(triggered()), this, SLOT(onRecentDockTriggered()));
    connect(this, SIGNAL(openFailed(QString)), m_recentDock, SLOT(remove(QString)));
    connect(m_recentDock, &RecentDock::deleted, m_player->projectWidget(), &NewProjectFolder::updateRecentProjects);

    m_playlistDock = new PlaylistDock(this);
    m_playlistDock->hide();
    m_playlistDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_4));
    ui->menuView->addAction(m_playlistDock->toggleViewAction());
    connect(m_playlistDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onPlaylistDockTriggered(bool)));
    connect(ui->actionPlaylist, SIGNAL(triggered()), this, SLOT(onPlaylistDockTriggered()));
    connect(m_playlistDock, SIGNAL(clipOpened(Mlt::Producer*, bool)), this, SLOT(openCut(Mlt::Producer*, bool)));
    connect(m_playlistDock, SIGNAL(itemActivated(int)), this, SLOT(seekPlaylist(int)));
    connect(m_playlistDock, SIGNAL(showStatusMessage(QString)), this, SLOT(showStatusMessage(QString)));
    connect(m_playlistDock->model(), SIGNAL(created()), this, SLOT(onPlaylistCreated()));
    connect(m_playlistDock->model(), SIGNAL(cleared()), this, SLOT(onPlaylistCleared()));
    connect(m_playlistDock->model(), SIGNAL(closed()), this, SLOT(onPlaylistClosed()));
    connect(m_playlistDock->model(), SIGNAL(modified()), this, SLOT(onPlaylistModified()));
    connect(m_playlistDock->model(), SIGNAL(loaded()), this, SLOT(onPlaylistLoaded()));
    connect(this, SIGNAL(producerOpened()), m_playlistDock, SLOT(onProducerOpened()));
    if (!Settings.playerGPU())
        connect(m_playlistDock->model(), SIGNAL(loaded()), this, SLOT(updateThumbnails()));
    connect(m_player, &Player::inChanged, m_playlistDock, &PlaylistDock::onInChanged);
    connect(m_player, &Player::outChanged, m_playlistDock, &PlaylistDock::onOutChanged);
    connect(m_playlistDock->model(), &PlaylistModel::inChanged, this, &MainWindow::onPlaylistInChanged);
    connect(m_playlistDock->model(), &PlaylistModel::outChanged, this, &MainWindow::onPlaylistOutChanged);

    m_timelineDock = new TimelineDock(this);
    m_timelineDock->hide();
    m_timelineDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_5));
    ui->menuView->addAction(m_timelineDock->toggleViewAction());
    connect(m_timelineDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onTimelineDockTriggered(bool)));
    connect(ui->actionTimeline, SIGNAL(triggered()), SLOT(onTimelineDockTriggered()));
    connect(m_player, SIGNAL(seeked(int)), m_timelineDock, SLOT(onSeeked(int)));
    connect(m_timelineDock, SIGNAL(seeked(int)), SLOT(seekTimeline(int)));
    connect(m_timelineDock, SIGNAL(clipClicked()), SLOT(onTimelineClipSelected()));
    connect(m_timelineDock, SIGNAL(showStatusMessage(QString)), this, SLOT(showStatusMessage(QString)));
    connect(m_timelineDock->model(), SIGNAL(showStatusMessage(QString)), this, SLOT(showStatusMessage(QString)));
    connect(m_timelineDock->model(), SIGNAL(created()), SLOT(onMultitrackCreated()));
    connect(m_timelineDock->model(), SIGNAL(closed()), SLOT(onMultitrackClosed()));
    connect(m_timelineDock->model(), SIGNAL(modified()), SLOT(onMultitrackModified()));
    connect(m_timelineDock->model(), SIGNAL(durationChanged()), SLOT(onMultitrackDurationChanged()));
    connect(m_timelineDock, SIGNAL(clipOpened(Mlt::Producer*)), SLOT(openCut(Mlt::Producer*)));
    connect(m_timelineDock->model(), &MultitrackModel::seeked, this, &MainWindow::seekTimeline);
    connect(m_timelineDock->model(), SIGNAL(scaleFactorChanged()), m_player, SLOT(pause()));
    connect(m_timelineDock, SIGNAL(selected(Mlt::Producer*)), SLOT(loadProducerWidget(Mlt::Producer*)));
    connect(m_timelineDock, SIGNAL(selectionChanged()), SLOT(onTimelineSelectionChanged()));
    connect(m_timelineDock, SIGNAL(clipCopied()), SLOT(onClipCopied()));
    connect(m_timelineDock, SIGNAL(filteredClicked()), SLOT(onFiltersDockTriggered()));
    connect(m_playlistDock, SIGNAL(addAllTimeline(Mlt::Playlist*)), SLOT(onTimelineDockTriggered()));
    connect(m_playlistDock, SIGNAL(addAllTimeline(Mlt::Playlist*, bool)), SLOT(onAddAllToTimeline(Mlt::Playlist*, bool)));
    connect(m_player, SIGNAL(previousSought()), m_timelineDock, SLOT(seekPreviousEdit()));
    connect(m_player, SIGNAL(nextSought()), m_timelineDock, SLOT(seekNextEdit()));

    m_filterController = new FilterController(this);
    m_filtersDock = new FiltersDock(m_filterController->metadataModel(), m_filterController->attachedModel(), this);
    m_filtersDock->setMinimumSize(400, 300);
    m_filtersDock->hide();
    m_filtersDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_6));
    ui->menuView->addAction(m_filtersDock->toggleViewAction());
    connect(m_filtersDock, SIGNAL(currentFilterRequested(int)), m_filterController, SLOT(setCurrentFilter(int)), Qt::QueuedConnection);
    connect(m_filtersDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onFiltersDockTriggered(bool)));
    connect(ui->actionFilters, SIGNAL(triggered()), this, SLOT(onFiltersDockTriggered()));
    connect(m_filterController, SIGNAL(currentFilterChanged(QmlFilter*, QmlMetadata*, int)), m_filtersDock, SLOT(setCurrentFilter(QmlFilter*, QmlMetadata*, int)));
    connect(this, SIGNAL(producerOpened()), m_filterController, SLOT(setProducer()));
    connect(m_filterController->attachedModel(), SIGNAL(changed()), SLOT(onFilterModelChanged()));
    connect(m_filtersDock, SIGNAL(changed()), SLOT(onFilterModelChanged()));
    connect(m_filterController, SIGNAL(filterChanged(Mlt::Filter*)),
            m_timelineDock->model(), SLOT(onFilterChanged(Mlt::Filter*)));
    connect(m_filterController->attachedModel(), SIGNAL(addedOrRemoved(Mlt::Producer*)),
            m_timelineDock->model(), SLOT(filterAddedOrRemoved(Mlt::Producer*)));
    connect(&QmlApplication::singleton(), SIGNAL(filtersPasted(Mlt::Producer*)),
            m_timelineDock->model(), SLOT(filterAddedOrRemoved(Mlt::Producer*)));
    connect(&QmlApplication::singleton(), &QmlApplication::filtersPasted,
            this, &MainWindow::onProducerModified);
    connect(m_filterController, SIGNAL(statusChanged(QString)), this, SLOT(showStatusMessage(QString)));
    connect(m_timelineDock, SIGNAL(fadeInChanged(int)), m_filterController, SLOT(onFadeInChanged()));
    connect(m_timelineDock, SIGNAL(fadeOutChanged(int)), m_filterController, SLOT(onFadeOutChanged()));
    connect(m_timelineDock, SIGNAL(selected(Mlt::Producer*)), m_filterController, SLOT(setProducer(Mlt::Producer*)));
    connect(m_player, SIGNAL(seeked(int)), m_filtersDock, SLOT(onSeeked(int)), Qt::QueuedConnection);
    connect(m_filtersDock, SIGNAL(seeked(int)), SLOT(seekKeyframes(int)));
    connect(MLT.videoWidget(), SIGNAL(frameDisplayed(const SharedFrame&)), m_filtersDock, SLOT(onShowFrame(const SharedFrame&)));
    connect(m_player, SIGNAL(inChanged(int)), m_filtersDock, SIGNAL(producerInChanged(int)));
    connect(m_player, SIGNAL(outChanged(int)), m_filtersDock, SIGNAL(producerOutChanged(int)));
    connect(m_player, SIGNAL(inChanged(int)), m_filterController, SLOT(onFilterInChanged(int)));
    connect(m_player, SIGNAL(outChanged(int)), m_filterController, SLOT(onFilterOutChanged(int)));
    connect(m_timelineDock->model(), SIGNAL(filterInChanged(int, Mlt::Filter*)), m_filterController, SLOT(onFilterInChanged(int, Mlt::Filter*)));
    connect(m_timelineDock->model(), SIGNAL(filterOutChanged(int, Mlt::Filter*)), m_filterController, SLOT(onFilterOutChanged(int, Mlt::Filter*)));

    m_keyframesDock = new KeyframesDock(m_filtersDock->qmlProducer(), this);
    m_keyframesDock->hide();
    m_keyframesDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_7));
    ui->menuView->addAction(m_keyframesDock->toggleViewAction());
    connect(m_keyframesDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onKeyframesDockTriggered(bool)));
    connect(ui->actionKeyframes, SIGNAL(triggered()), this, SLOT(onKeyframesDockTriggered()));
    connect(m_filterController, SIGNAL(currentFilterChanged(QmlFilter*, QmlMetadata*, int)), m_keyframesDock, SLOT(setCurrentFilter(QmlFilter*, QmlMetadata*)));
    connect(m_keyframesDock, SIGNAL(visibilityChanged(bool)), m_filtersDock->qmlProducer(), SLOT(remakeAudioLevels(bool)));

    m_historyDock = new QDockWidget(tr("History"), this);
    m_historyDock->hide();
    m_historyDock->setObjectName("historyDock");
    m_historyDock->setWindowIcon(ui->actionHistory->icon());
    m_historyDock->toggleViewAction()->setIcon(ui->actionHistory->icon());
    m_historyDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_8));
    m_historyDock->setMinimumWidth(150);
    ui->menuView->addAction(m_historyDock->toggleViewAction());
    connect(m_historyDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onHistoryDockTriggered(bool)));
    connect(ui->actionHistory, SIGNAL(triggered()), this, SLOT(onHistoryDockTriggered()));
    QUndoView* undoView = new QUndoView(m_undoStack, m_historyDock);
    undoView->setObjectName("historyView");
    undoView->setAlternatingRowColors(true);
    undoView->setSpacing(2);
    m_historyDock->setWidget(undoView);
    ui->actionUndo->setDisabled(true);
    ui->actionRedo->setDisabled(true);

    m_encodeDock = new EncodeDock(this);
    m_encodeDock->hide();
    m_encodeDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_9));
    ui->menuView->addAction(m_encodeDock->toggleViewAction());
    connect(this, SIGNAL(producerOpened()), m_encodeDock, SLOT(onProducerOpened()));
    connect(ui->actionEncode, SIGNAL(triggered()), this, SLOT(onEncodeTriggered()));
    connect(ui->actionExportVideo, SIGNAL(triggered()), this, SLOT(onEncodeTriggered()));
    connect(m_encodeDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onEncodeTriggered(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), m_player, SLOT(onCaptureStateChanged(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), m_propertiesDock, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), m_recentDock, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), m_filtersDock, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), m_keyframesDock, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), ui->actionOpen, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), ui->actionOpenOther, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), ui->actionExit, SLOT(setDisabled(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), this, SLOT(onCaptureStateChanged(bool)));
    connect(m_encodeDock, SIGNAL(captureStateChanged(bool)), m_historyDock, SLOT(setDisabled(bool)));
    connect(this, SIGNAL(profileChanged()), m_encodeDock, SLOT(onProfileChanged()));
    connect(this, SIGNAL(profileChanged()), SLOT(onProfileChanged()));
    connect(this, SIGNAL(profileChanged()), &QmlProfile::singleton(), SIGNAL(profileChanged()));
    connect(this, SIGNAL(audioChannelsChanged()), m_encodeDock, SLOT(onAudioChannelsChanged()));
    connect(m_playlistDock->model(), SIGNAL(modified()), m_encodeDock, SLOT(onProducerOpened()));
    connect(m_timelineDock, SIGNAL(clipCopied()), m_encodeDock, SLOT(onProducerOpened()));
    m_encodeDock->onProfileChanged();

    m_jobsDock = new JobsDock(this);
    m_jobsDock->hide();
    m_jobsDock->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_0));
    ui->menuView->addAction(m_jobsDock->toggleViewAction());
    connect(&JOBS, SIGNAL(jobAdded()), m_jobsDock, SLOT(onJobAdded()));
    connect(m_jobsDock->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onJobsDockTriggered(bool)));
    connect(ui->actionJobs, SIGNAL(triggered()), this, SLOT(onJobsDockTriggered()));

    addDockWidget(Qt::LeftDockWidgetArea, m_propertiesDock);
    addDockWidget(Qt::RightDockWidgetArea, m_recentDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_playlistDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_filtersDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_keyframesDock);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_encodeDock);
    addDockWidget(Qt::RightDockWidgetArea, m_jobsDock);
    tabifyDockWidget(m_propertiesDock, m_playlistDock);
    tabifyDockWidget(m_playlistDock, m_filtersDock);
    tabifyDockWidget(m_filtersDock, m_encodeDock);
    splitDockWidget(m_recentDock, findChild<QDockWidget*>("AudioWaveformDock"), Qt::Vertical);
    splitDockWidget(audioMeterDock, m_recentDock, Qt::Horizontal);
    tabifyDockWidget(m_recentDock, m_historyDock);
    tabifyDockWidget(m_historyDock, m_jobsDock);
    tabifyDockWidget(m_keyframesDock, m_timelineDock);
    m_recentDock->raise();
    resetDockCorners();

    // Configure the View menu.
    ui->menuView->addSeparator();
    ui->menuView->addAction(ui->actionApplicationLog);

    // connect video widget signals
    Mlt::GLWidget* videoWidget = (Mlt::GLWidget*) &(MLT);
    connect(videoWidget, SIGNAL(dragStarted()), m_playlistDock, SLOT(onPlayerDragStarted()));
    connect(videoWidget, SIGNAL(seekTo(int)), m_player, SLOT(seek(int)));
    connect(videoWidget, SIGNAL(gpuNotSupported()), this, SLOT(onGpuNotSupported()));
    connect(videoWidget->quickWindow(), SIGNAL(sceneGraphInitialized()), SLOT(onSceneGraphInitialized()), Qt::QueuedConnection);
    connect(videoWidget, SIGNAL(frameDisplayed(const SharedFrame&)), m_scopeController, SIGNAL(newFrame(const SharedFrame&)));
    connect(m_filterController, SIGNAL(currentFilterChanged(QmlFilter*, QmlMetadata*, int)), videoWidget, SLOT(setCurrentFilter(QmlFilter*, QmlMetadata*)));

    readWindowSettings();

    setFocus();
    setCurrentFile("");

    LeapNetworkListener* leap = new LeapNetworkListener(this);
    connect(leap, SIGNAL(shuttle(float)), SLOT(onShuttle(float)));
    connect(leap, SIGNAL(jogRightFrame()), SLOT(stepRightOneFrame()));
    connect(leap, SIGNAL(jogRightSecond()), SLOT(stepRightOneSecond()));
    connect(leap, SIGNAL(jogLeftFrame()), SLOT(stepLeftOneFrame()));
    connect(leap, SIGNAL(jogLeftSecond()), SLOT(stepLeftOneSecond()));

    connect(&m_network, SIGNAL(finished(QNetworkReply*)), SLOT(onUpgradeCheckFinished(QNetworkReply*)));

    QThreadPool::globalInstance()->setMaxThreadCount(qMin(4, QThreadPool::globalInstance()->maxThreadCount()));

    ProxyManager::removePending();

    LOG_DEBUG() << "end";
}

void MainWindow::onFocusWindowChanged(QWindow *) const
{
    LOG_DEBUG() << "Focuswindow changed";
    LOG_DEBUG() << "Current focusWidget:" << QApplication::focusWidget();
    LOG_DEBUG() << "Current focusObject:" << QApplication::focusObject();
    LOG_DEBUG() << "Current focusWindow:" << QApplication::focusWindow();
}

void MainWindow::onFocusObjectChanged(QObject *) const
{
    LOG_DEBUG() << "Focusobject changed";
    LOG_DEBUG() << "Current focusWidget:" << QApplication::focusWidget();
    LOG_DEBUG() << "Current focusObject:" << QApplication::focusObject();
    LOG_DEBUG() << "Current focusWindow:" << QApplication::focusWindow();
}

void MainWindow::onTimelineClipSelected()
{
    // Synchronize navigation position with timeline selection.
    TimelineDock * t = m_timelineDock;

    if (t->selection().isEmpty())
        return;

    m_navigationPosition = t->centerOfClip(t->selection().first().y(), t->selection().first().x());

    // Switch to Project player.
    if (m_player->tabIndex() != Player::ProjectTabIndex) {
        t->saveAndClearSelection();
        m_player->onTabBarClicked(Player::ProjectTabIndex);
    }
}

void MainWindow::onAddAllToTimeline(Mlt::Playlist* playlist, bool skipProxy)
{
    // We stop the player because of a bug on Windows that results in some
    // strange memory leak when using Add All To Timeline, more noticeable
    // with (high res?) still image files.
    if (MLT.isSeekable())
        m_player->pause();
    else
        m_player->stop();
    m_timelineDock->appendFromPlaylist(playlist, skipProxy);
}

MainWindow& MainWindow::singleton()
{
    static MainWindow* instance = new MainWindow;
    return *instance;
}

MainWindow::~MainWindow()
{
    delete ui;
    Mlt::Controller::destroy();
}

void MainWindow::setupSettingsMenu()
{
    LOG_DEBUG() << "begin";
    QActionGroup* group = new QActionGroup(this);
    group->addAction(ui->actionChannels1);
    group->addAction(ui->actionChannels2);
    group->addAction(ui->actionChannels6);
    group = new QActionGroup(this);
    group->addAction(ui->actionOneField);
    group->addAction(ui->actionLinearBlend);

    m_previewScaleGroup = new QActionGroup(this);
    m_previewScaleGroup->addAction(ui->actionPreviewNone);
    m_previewScaleGroup->addAction(ui->actionPreview360);
    m_previewScaleGroup->addAction(ui->actionPreview540);
    m_previewScaleGroup->addAction(ui->actionPreview720);

    //XXX workaround yadif crashing with mlt_transition
//    group->addAction(ui->actionYadifTemporal);
//    group->addAction(ui->actionYadifSpatial);
    ui->actionYadifTemporal->setVisible(false);
    ui->actionYadifSpatial->setVisible(false);

    group = new QActionGroup(this);
    group->addAction(ui->actionNearest);
    group->addAction(ui->actionBilinear);
    group->addAction(ui->actionBicubic);
    group->addAction(ui->actionHyper);
    if (Settings.playerGPU()) {
        group = new QActionGroup(this);
        group->addAction(ui->actionGammaRec709);
        group->addAction(ui->actionGammaSRGB);
    } else {
        delete ui->menuGamma;
    }
    m_profileGroup = new QActionGroup(this);
    m_profileGroup->addAction(ui->actionProfileAutomatic);
    ui->actionProfileAutomatic->setData(QString());
    buildVideoModeMenu(ui->menuProfile, m_customProfileMenu, m_profileGroup, ui->actionAddCustomProfile, ui->actionProfileRemove);

    // Add the SDI and HDMI devices to the Settings menu.
    m_externalGroup = new QActionGroup(this);
    ui->actionExternalNone->setData(QString());
    m_externalGroup->addAction(ui->actionExternalNone);

    QList<QScreen*> screens = QGuiApplication::screens();
    int n = screens.size();
    for (int i = 0; n > 1 && i < n; i++) {
        QAction* action = new QAction(tr("Screen %1 (%2 x %3 @ %4 Hz)").arg(i)
            .arg(screens[i]->size().width() * screens[i]->devicePixelRatio())
            .arg(screens[i]->size().height() * screens[i]->devicePixelRatio())
            .arg(screens[i]->refreshRate()), this);
        action->setCheckable(true);
        action->setData(i);
        m_externalGroup->addAction(action);
    }

    Mlt::Profile profile;
    Mlt::Consumer decklink(profile, "decklink:");
    if (decklink.is_valid()) {
        decklink.set("list_devices", 1);
        int n = decklink.get_int("devices");
        for (int i = 0; i < n; ++i) {
            QString device(decklink.get(QString("device.%1").arg(i).toLatin1().constData()));
            if (!device.isEmpty()) {
                QAction* action = new QAction(device, this);
                action->setCheckable(true);
                action->setData(QString("decklink:%1").arg(i));
                m_externalGroup->addAction(action);

                if (!m_keyerGroup) {
                    m_keyerGroup = new QActionGroup(this);
                    action = new QAction(tr("Off"), m_keyerGroup);
                    action->setData(QVariant(0));
                    action->setCheckable(true);
                    action = new QAction(tr("Internal"), m_keyerGroup);
                    action->setData(QVariant(1));
                    action->setCheckable(true);
                    action = new QAction(tr("External"), m_keyerGroup);
                    action->setData(QVariant(2));
                    action->setCheckable(true);
                }
            }
        }
    }
    if (m_externalGroup->actions().count() > 1)
        ui->menuExternal->addActions(m_externalGroup->actions());
    else {
        delete ui->menuExternal;
        ui->menuExternal = 0;
    }
    if (m_keyerGroup) {
        m_keyerMenu = ui->menuExternal->addMenu(tr("DeckLink Keyer"));
        m_keyerMenu->addActions(m_keyerGroup->actions());
        m_keyerMenu->setDisabled(true);
        connect(m_keyerGroup, SIGNAL(triggered(QAction*)), this, SLOT(onKeyerTriggered(QAction*)));
    }
    connect(m_externalGroup, SIGNAL(triggered(QAction*)), this, SLOT(onExternalTriggered(QAction*)));
    connect(m_profileGroup, SIGNAL(triggered(QAction*)), this, SLOT(onProfileTriggered(QAction*)));

    // Setup the language menu actions
    m_languagesGroup = new QActionGroup(this);
    QAction* a;
    a = new QAction(QLocale::languageToString(QLocale::Arabic), m_languagesGroup);
    a->setCheckable(true);
    a->setData("ar");
    a = new QAction(QLocale::languageToString(QLocale::Catalan), m_languagesGroup);
    a->setCheckable(true);
    a->setData("ca");
    a = new QAction(QLocale::languageToString(QLocale::Chinese).append(" (China)"), m_languagesGroup);
    a->setCheckable(true);
    a->setData("zh_CN");
    a = new QAction(QLocale::languageToString(QLocale::Chinese).append(" (Taiwan)"), m_languagesGroup);
    a->setCheckable(true);
    a->setData("zh_TW");
    a = new QAction(QLocale::languageToString(QLocale::Czech), m_languagesGroup);
    a->setCheckable(true);
    a->setData("cs");
    a = new QAction(QLocale::languageToString(QLocale::Danish), m_languagesGroup);
    a->setCheckable(true);
    a->setData("da");
    a = new QAction(QLocale::languageToString(QLocale::Dutch), m_languagesGroup);
    a->setCheckable(true);
    a->setData("nl");
    a = new QAction(QLocale::languageToString(QLocale::English).append(" (Great Britain)"), m_languagesGroup);
    a->setCheckable(true);
    a->setData("en_GB");
    a = new QAction(QLocale::languageToString(QLocale::English).append(" (United States)"), m_languagesGroup);
    a->setCheckable(true);
    a->setData("en_US");
    a = new QAction(QLocale::languageToString(QLocale::Estonian), m_languagesGroup);
    a->setCheckable(true);
    a->setData("et");
    a = new QAction(QLocale::languageToString(QLocale::Finnish), m_languagesGroup);
    a->setCheckable(true);
    a->setData("fi");
    a = new QAction(QLocale::languageToString(QLocale::French), m_languagesGroup);
    a->setCheckable(true);
    a->setData("fr");
    a = new QAction(QLocale::languageToString(QLocale::Gaelic), m_languagesGroup);
    a->setCheckable(true);
    a->setData("gd");
    a = new QAction(QLocale::languageToString(QLocale::Galician), m_languagesGroup);
    a->setCheckable(true);
    a->setData("gl");
    a = new QAction(QLocale::languageToString(QLocale::German), m_languagesGroup);
    a->setCheckable(true);
    a->setData("de");
    a = new QAction(QLocale::languageToString(QLocale::Greek), m_languagesGroup);
    a->setCheckable(true);
    a->setData("el");
    a = new QAction(QLocale::languageToString(QLocale::Hungarian), m_languagesGroup);
    a->setCheckable(true);
    a->setData("hu");
    a = new QAction(QLocale::languageToString(QLocale::Italian), m_languagesGroup);
    a->setCheckable(true);
    a->setData("it");
    a = new QAction(QLocale::languageToString(QLocale::Japanese), m_languagesGroup);
    a->setCheckable(true);
    a->setData("ja");
    a = new QAction(QLocale::languageToString(QLocale::Korean), m_languagesGroup);
    a->setCheckable(true);
    a->setData("ko");
    a = new QAction(QLocale::languageToString(QLocale::Nepali), m_languagesGroup);
    a->setCheckable(true);
    a->setData("ne");
    a = new QAction(QLocale::languageToString(QLocale::NorwegianBokmal), m_languagesGroup);
    a->setCheckable(true);
    a->setData("nb");
    a = new QAction(QLocale::languageToString(QLocale::NorwegianNynorsk), m_languagesGroup);
    a->setCheckable(true);
    a->setData("nn");
    a = new QAction(QLocale::languageToString(QLocale::Occitan), m_languagesGroup);
    a->setCheckable(true);
    a->setData("oc");
    a = new QAction(QLocale::languageToString(QLocale::Polish), m_languagesGroup);
    a->setCheckable(true);
    a->setData("pl");
    a = new QAction(QLocale::languageToString(QLocale::Portuguese).append(" (Brazil)"), m_languagesGroup);
    a->setCheckable(true);
    a->setData("pt_BR");
    a = new QAction(QLocale::languageToString(QLocale::Portuguese).append(" (Portugal)"), m_languagesGroup);
    a->setCheckable(true);
    a->setData("pt_PT");
    a = new QAction(QLocale::languageToString(QLocale::Russian), m_languagesGroup);
    a->setCheckable(true);
    a->setData("ru");
    a = new QAction(QLocale::languageToString(QLocale::Slovak), m_languagesGroup);
    a->setCheckable(true);
    a->setData("sk");
    a = new QAction(QLocale::languageToString(QLocale::Slovenian), m_languagesGroup);
    a->setCheckable(true);
    a->setData("sl");
    a = new QAction(QLocale::languageToString(QLocale::Spanish), m_languagesGroup);
    a->setCheckable(true);
    a->setData("es");
    a = new QAction(QLocale::languageToString(QLocale::Swedish), m_languagesGroup);
    a->setCheckable(true);
    a->setData("sv");
    a = new QAction(QLocale::languageToString(QLocale::Thai), m_languagesGroup);
    a->setCheckable(true);
    a->setData("th");
    a = new QAction(QLocale::languageToString(QLocale::Turkish), m_languagesGroup);
    a->setCheckable(true);
    a->setData("tr");
    a = new QAction(QLocale::languageToString(QLocale::Ukrainian), m_languagesGroup);
    a->setCheckable(true);
    a->setData("uk");
    ui->menuLanguage->addActions(m_languagesGroup->actions());
    const QString locale = Settings.language();
    foreach (QAction* action, m_languagesGroup->actions()) {
        if (action->data().toString().startsWith(locale)) {
            action->setChecked(true);
            break;
        }
    }
    connect(m_languagesGroup, SIGNAL(triggered(QAction*)), this, SLOT(onLanguageTriggered(QAction*)));

    // Setup the themes actions
    group = new QActionGroup(this);
    group->addAction(ui->actionSystemTheme);
    group->addAction(ui->actionFusionDark);
    group->addAction(ui->actionFusionLight);
    if (Settings.theme() == "dark")
        ui->actionFusionDark->setChecked(true);
    else if (Settings.theme() == "light")
        ui->actionFusionLight->setChecked(true);
    else
        ui->actionSystemTheme->setChecked(true);

#ifdef Q_OS_WIN
    // On Windows, if there is no JACK or it is not running
    // then Shotcut crashes inside MLT's call to jack_client_open().
    // Therefore, the JACK option for Shotcut is banned on Windows.
    delete ui->actionJack;
    ui->actionJack = 0;
#endif
#if !defined(Q_OS_MAC)
    // Setup the display method actions.
    if (!Settings.playerGPU()) {
        group = new QActionGroup(this);
#if defined(Q_OS_WIN)
        ui->actionDrawingAutomatic->setData(0);
        group->addAction(ui->actionDrawingAutomatic);
        ui->actionDrawingDirectX->setData(Qt::AA_UseOpenGLES);
        group->addAction(ui->actionDrawingDirectX);
#else
        delete ui->actionDrawingAutomatic;
        delete ui->actionDrawingDirectX;
#endif
        ui->actionDrawingOpenGL->setData(Qt::AA_UseDesktopOpenGL);
        group->addAction(ui->actionDrawingOpenGL);
        ui->actionDrawingSoftware->setData(Qt::AA_UseSoftwareOpenGL);
        group->addAction(ui->actionDrawingSoftware);
        connect(group, SIGNAL(triggered(QAction*)), this, SLOT(onDrawingMethodTriggered(QAction*)));
        switch (Settings.drawMethod()) {
        case Qt::AA_UseDesktopOpenGL:
            ui->actionDrawingOpenGL->setChecked(true);
            break;
#if defined(Q_OS_WIN)
        case Qt::AA_UseOpenGLES:
            ui->actionDrawingDirectX->setChecked(true);
            break;
#endif
        case Qt::AA_UseSoftwareOpenGL:
            ui->actionDrawingSoftware->setChecked(true);
            break;
#if defined(Q_OS_WIN)
        default:
            ui->actionDrawingAutomatic->setChecked(true);
            break;
#else
        default:
            ui->actionDrawingOpenGL->setChecked(true);
            break;
#endif
        }
    } else {
        // GPU mode only works with OpenGL.
        delete ui->menuDrawingMethod;
        ui->menuDrawingMethod = 0;
    }
#else  // Q_OS_MAC
    delete ui->menuDrawingMethod;
    ui->menuDrawingMethod = 0;
#endif

    // Add custom layouts to View > Layout submenu.
    m_layoutGroup = new QActionGroup(this);
    connect(m_layoutGroup, SIGNAL(triggered(QAction*)), SLOT(onLayoutTriggered(QAction*)));
    if (Settings.layouts().size() > 0) {
        ui->menuLayout->addAction(ui->actionLayoutRemove);
        ui->menuLayout->addSeparator();
    }
    foreach (QString name, Settings.layouts())
        ui->menuLayout->addAction(addLayout(m_layoutGroup, name));

    if (qApp->property("clearRecent").toBool()) {
        ui->actionClearRecentOnExit->setVisible(false);
        Settings.setRecent(QStringList());
        Settings.setClearRecent(true);
    } else {
        ui->actionClearRecentOnExit->setChecked(Settings.clearRecent());
    }


    // Initialze the proxy submenu
    ui->actionUseProxy->setChecked(Settings.proxyEnabled());
    ui->actionProxyUseProjectFolder->setChecked(Settings.proxyUseProjectFolder());
    ui->actionProxyUseHardware->setChecked(Settings.proxyUseHardware());

    LOG_DEBUG() << "end";
}

void MainWindow::setupOpenOtherMenu()
{
    // Open Other toolbar menu button
    QScopedPointer<Mlt::Properties> mltProducers(MLT.repository()->producers());
    QScopedPointer<Mlt::Properties> mltFilters(MLT.repository()->filters());
    QMenu* otherMenu = new QMenu(this);
    ui->actionOpenOther2->setMenu(otherMenu);

    // populate the generators
    if (mltProducers->get_data("color")) {
        otherMenu->addAction(tr("Color"), this, SLOT(onOpenOtherTriggered()))->setObjectName("color");
        if (!Settings.playerGPU() && mltProducers->get_data("qtext") && mltFilters->get_data("dynamictext"))
            otherMenu->addAction(tr("Text"), this, SLOT(onOpenOtherTriggered()))->setObjectName("text");
    }
    if (mltProducers->get_data("noise"))
        otherMenu->addAction(tr("Noise"), this, SLOT(onOpenOtherTriggered()))->setObjectName("noise");
    if (mltProducers->get_data("frei0r.ising0r"))
        otherMenu->addAction(tr("Ising"), this, SLOT(onOpenOtherTriggered()))->setObjectName("ising0r");
    if (mltProducers->get_data("frei0r.lissajous0r"))
        otherMenu->addAction(tr("Lissajous"), this, SLOT(onOpenOtherTriggered()))->setObjectName("lissajous0r");
    if (mltProducers->get_data("frei0r.plasma"))
        otherMenu->addAction(tr("Plasma"), this, SLOT(onOpenOtherTriggered()))->setObjectName("plasma");
    if (mltProducers->get_data("frei0r.test_pat_B"))
        otherMenu->addAction(tr("Color Bars"), this, SLOT(onOpenOtherTriggered()))->setObjectName("test_pat_B");
    if (mltProducers->get_data("tone"))
        otherMenu->addAction(tr("Audio Tone"), this, SLOT(onOpenOtherTriggered()))->setObjectName("tone");
    if (mltProducers->get_data("count"))
        otherMenu->addAction(tr("Count"), this, SLOT(onOpenOtherTriggered()))->setObjectName("count");
    if (mltProducers->get_data("blipflash"))
        otherMenu->addAction(tr("Blip Flash"), this, SLOT(onOpenOtherTriggered()))->setObjectName("blipflash");

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    otherMenu->addAction(tr("Video4Linux"), this, SLOT(onOpenOtherTriggered()))->setObjectName("v4l2");
    otherMenu->addAction(tr("PulseAudio"), this, SLOT(onOpenOtherTriggered()))->setObjectName("pulse");
    otherMenu->addAction(tr("JACK Audio"), this, SLOT(onOpenOtherTriggered()))->setObjectName("jack");
    otherMenu->addAction(tr("ALSA Audio"), this, SLOT(onOpenOtherTriggered()))->setObjectName("alsa");
#elif defined(Q_OS_WIN) || defined(Q_OS_MAC)
    otherMenu->addAction(tr("Audio/Video Device"), this, SLOT(onOpenOtherTriggered()))->setObjectName("device");
#endif
    if (mltProducers->get_data("decklink"))
        otherMenu->addAction(tr("SDI/HDMI"), this, SLOT(onOpenOtherTriggered()))->setObjectName("decklink");
}

QAction* MainWindow::addProfile(QActionGroup* actionGroup, const QString& desc, const QString& name)
{
    QAction* action = new QAction(desc, this);
    action->setCheckable(true);
    action->setData(name);
    actionGroup->addAction(action);
    return action;
}

QAction*MainWindow::addLayout(QActionGroup* actionGroup, const QString& name)
{
    QAction* action = new QAction(name, this);
    actionGroup->addAction(action);
    return action;
}

void MainWindow::open(Mlt::Producer* producer)
{
    if (!producer->is_valid())
        showStatusMessage(tr("Failed to open "));
    else if (producer->get_int("error"))
        showStatusMessage(tr("Failed to open ") + producer->get("resource"));

    bool ok = false;
    int screen = Settings.playerExternal().toInt(&ok);
    if (ok && screen != QApplication::desktop()->screenNumber(this))
        m_player->moveVideoToScreen(screen);

    // no else here because open() will delete the producer if open fails
    if (!MLT.setProducer(producer)) {
        emit producerOpened();
        if (!MLT.profile().is_explicit() || MLT.URL().endsWith(".mlt") || MLT.URL().endsWith(".xml"))
            emit profileChanged();
    }
    m_player->setFocus();
    m_playlistDock->setUpdateButtonEnabled(false);

    // Needed on Windows. Upon first file open, window is deactivated, perhaps OpenGL-related.
    activateWindow();
}

bool MainWindow::isCompatibleWithGpuMode(MltXmlChecker& checker)
{
    if (checker.needsGPU() && !Settings.playerGPU() && Settings.playerWarnGPU()) {
        LOG_INFO() << "file uses GPU but GPU not enabled";
        QMessageBox dialog(QMessageBox::Warning,
           qApp->applicationName(),
           tr("The file you opened uses GPU effects, but GPU effects are not enabled.\n\n"
              "GPU effects are EXPERIMENTAL, UNSTABLE and UNSUPPORTED! Unsupported means do not report bugs about it.\n\n"
              "Do you want to enable GPU effects and restart?"),
           QMessageBox::No |
           QMessageBox::Yes,
           this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        int r = dialog.exec();
        if (r == QMessageBox::Yes) {
            ui->actionGPU->setChecked(true);
            m_exitCode = EXIT_RESTART;
            QApplication::closeAllWindows();
        }
        return false;
    }
    else if (checker.needsCPU() && Settings.playerGPU()) {
        LOG_INFO() << "file uses GPU incompatible filters but GPU is enabled";
        QMessageBox dialog(QMessageBox::Question,
           qApp->applicationName(),
           tr("The file you opened uses CPU effects that are incompatible with GPU effects, but GPU effects are enabled.\n"
              "Do you want to disable GPU effects and restart?"),
           QMessageBox::No |
           QMessageBox::Yes,
           this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        int r = dialog.exec();
        if (r == QMessageBox::Yes) {
            ui->actionGPU->setChecked(false);
            m_exitCode = EXIT_RESTART;
            QApplication::closeAllWindows();
        }
        return false;
    }
    return true;
}

bool MainWindow::saveRepairedXmlFile(MltXmlChecker& checker, QString& fileName)
{
    QFileInfo fi(fileName);
    QFile repaired(QString("%1/%2 - %3.%4").arg(fi.path())
        .arg(fi.completeBaseName()).arg(tr("Repaired")).arg(fi.suffix()));
    repaired.open(QIODevice::WriteOnly);
    LOG_INFO() << "repaired MLT XML file name" << repaired.fileName();
    QFile temp(checker.tempFileName());
    if (temp.exists() && repaired.exists()) {
        temp.open(QIODevice::ReadOnly);
        QByteArray xml = temp.readAll();
        temp.close();

        qint64 n = repaired.write(xml);
        while (n > 0 && n < xml.size()) {
            qint64 x = repaired.write(xml.right(xml.size() - n));
            if (x > 0)
                n += x;
            else
                n = x;
        }
        repaired.close();
        if (n == xml.size()) {
            fileName = repaired.fileName();
            return true;
        }
    }
    QMessageBox::warning(this, qApp->applicationName(), tr("Repairing the project failed."));
    LOG_WARNING() << "repairing failed";
    return false;
}

bool MainWindow::isXmlRepaired(MltXmlChecker& checker, QString& fileName)
{
    bool result = true;
    if (checker.isCorrected()) {
        LOG_WARNING() << fileName;
        QMessageBox dialog(QMessageBox::Question,
           qApp->applicationName(),
           tr("Shotcut noticed some problems in your project.\n"
              "Do you want Shotcut to try to repair it?\n\n"
              "If you choose Yes, Shotcut will create a copy of your project\n"
              "with \"- Repaired\" in the file name and open it."),
           QMessageBox::No |
           QMessageBox::Yes,
           this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        int r = dialog.exec();
        if (r == QMessageBox::Yes)
            result = saveRepairedXmlFile(checker, fileName);
    }
    else if (checker.unlinkedFilesModel().rowCount() > 0) {
        UnlinkedFilesDialog dialog(this);
        dialog.setModel(checker.unlinkedFilesModel());
        dialog.setWindowModality(QmlApplication::dialogModality());
        if (dialog.exec() == QDialog::Accepted) {
            if (checker.check(fileName) && checker.isCorrected())
                result = saveRepairedXmlFile(checker, fileName);
        } else {
            result = false;
        }
    }
    return result;
}

bool MainWindow::checkAutoSave(QString &url)
{
    QMutexLocker locker(&m_autosaveMutex);

    // check whether autosave files exist:
    QSharedPointer<AutoSaveFile> stale(AutoSaveFile::getFile(url));
    if (stale) {
        QMessageBox dialog(QMessageBox::Question, qApp->applicationName(),
           tr("Auto-saved files exist. Do you want to recover them now?"),
           QMessageBox::No | QMessageBox::Yes, this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        int r = dialog.exec();
        if (r == QMessageBox::Yes) {
            if (!stale->open(QIODevice::ReadWrite)) {
                LOG_WARNING() << "failed to recover autosave file" << url;
            } else {
                m_autosaveFile = stale;
                url = stale->fileName();
                return true;
            }
        }
    }

    // create new autosave object
    m_autosaveFile.reset(new AutoSaveFile(url));

    return false;
}

void MainWindow::stepLeftBySeconds(int sec)
{
    m_player->seek(m_player->position() + sec * qRound(MLT.profile().fps()));
}

void MainWindow::doAutosave()
{
    QMutexLocker locker(&m_autosaveMutex);
    if (m_autosaveFile) {
        bool success = false;
        if (m_autosaveFile->isOpen() || m_autosaveFile->open(QIODevice::ReadWrite)) {
            m_autosaveFile->close();
            success = saveXML(m_autosaveFile->fileName(), false /* without relative paths */);
            m_autosaveFile->open(QIODevice::ReadWrite);
        }
        if (!success) {
            LOG_ERROR() << "failed to open autosave file for writing" << m_autosaveFile->fileName();
        }
    }
}

void MainWindow::setFullScreen(bool isFullScreen)
{
    if (isFullScreen) {
#ifdef Q_OS_WIN
        showMaximized();
#else
        showFullScreen();
#endif
        ui->actionEnter_Full_Screen->setVisible(false);
    }
}

QString MainWindow::untitledFileName() const
{
    QDir dir = Settings.appDataLocation();
    if (!dir.exists()) dir.mkpath(dir.path());
    return dir.filePath("__untitled__.mlt");
}

void MainWindow::setProfile(const QString &profile_name)
{
    LOG_DEBUG() << profile_name;
    MLT.setProfile(profile_name);
    emit profileChanged();
}

bool MainWindow::isSourceClipMyProject(QString resource, bool withDialog)
{
    if (m_player->tabIndex() == Player::ProjectTabIndex && MLT.savedProducer() && MLT.savedProducer()->is_valid())
        resource = QString::fromUtf8(MLT.savedProducer()->get("resource"));
    if (!resource.isEmpty() && QDir(resource) == QDir(fileName())) {
        if (withDialog) {
            QMessageBox dialog(QMessageBox::Information,
                               qApp->applicationName(),
                               tr("You cannot add a project to itself!"),
                               QMessageBox::Ok,
                               this);
            dialog.setDefaultButton(QMessageBox::Ok);
            dialog.setEscapeButton(QMessageBox::Ok);
            dialog.setWindowModality(QmlApplication::dialogModality());
            dialog.exec();
        }
        return true;
    }
    return false;
}

bool MainWindow::keyframesDockIsVisible() const
{
    return m_keyframesDock && m_keyframesDock->isVisible();
}

void MainWindow::setAudioChannels(int channels)
{
    LOG_DEBUG() << channels;
    MLT.videoWidget()->setProperty("audio_channels", channels);
    MLT.setAudioChannels(channels);
    if (channels == 1)
        ui->actionChannels1->setChecked(true);
    else if (channels == 2)
        ui->actionChannels2->setChecked(true);
    else if (channels == 6)
        ui->actionChannels6->setChecked(true);
    emit audioChannelsChanged();
}

void MainWindow::showSaveError()
{
    QMessageBox dialog(QMessageBox::Critical,
                       qApp->applicationName(),
                       tr("There was an error saving. Please try again."),
                       QMessageBox::Ok,
                       this);
    dialog.setDefaultButton(QMessageBox::Ok);
    dialog.setEscapeButton(QMessageBox::Ok);
    dialog.setWindowModality(QmlApplication::dialogModality());
    dialog.exec();
}

void MainWindow::setPreviewScale(int scale)
{
    LOG_DEBUG() << scale;
    switch (scale) {
    case 360:
        ui->actionPreview360->setChecked(true);
        break;
    case 540:
        ui->actionPreview540->setChecked(true);
        break;
    case 720:
        ui->actionPreview720->setChecked(true);
        break;
    default:
        ui->actionPreviewNone->setChecked(true);
        break;
    }
    MLT.setPreviewScale(scale);
    MLT.refreshConsumer();
}

void MainWindow::setVideoModeMenu()
{
    // Find a matching video mode
    for (const auto action : m_profileGroup->actions()) {
        auto s = action->data().toString();
        Mlt::Profile profile(s.toUtf8().constData());
        if (MLT.profile().width() == profile.width() &&
                MLT.profile().height() == profile.height() &&
                MLT.profile().sample_aspect_num() == profile.sample_aspect_num() &&
                MLT.profile().sample_aspect_den() == profile.sample_aspect_den() &&
                MLT.profile().frame_rate_num() == profile.frame_rate_num() &&
                MLT.profile().frame_rate_den() == profile.frame_rate_den() &&
                MLT.profile().colorspace() == profile.colorspace() &&
                MLT.profile().progressive() == profile.progressive()) {
            // Select it
            action->setChecked(true);
            return;
        }
    }
    // Choose Automatic if nothing found
    m_profileGroup->actions().first()->setChecked(true);
}

void MainWindow::resetVideoModeMenu()
{
    // Change selected Video Mode back to Settings
    for (const auto action : m_profileGroup->actions()) {
        if (action->data().toString() == Settings.playerProfile()) {
            action->setChecked(true);
            break;
        }
    }
}

void MainWindow::resetDockCorners()
{
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
}

static void autosaveTask(MainWindow* p)
{
    LOG_DEBUG_TIME();
    p->doAutosave();
}

void MainWindow::onAutosaveTimeout()
{
    if (isWindowModified()) {
        QtConcurrent::run(autosaveTask, this);
    }
    if (Util::isMemoryLow()) {
        MLT.pause();
        QMessageBox dialog(QMessageBox::Critical,
                           qApp->applicationName(),
                           tr("You are running low on available memory!\n\n"
                              "Please close other applications or web browser tabs and retry.\n"
                              "Or save and restart Shotcut."),
                           QMessageBox::Retry | QMessageBox::Save | QMessageBox::Ignore,
                           this);
        dialog.setDefaultButton(QMessageBox::Retry);
        dialog.setEscapeButton(QMessageBox::Ignore);
        dialog.setWindowModality(QmlApplication::dialogModality());
        switch (dialog.exec()) {
        case QMessageBox::Save:
            on_actionSave_triggered();
            m_exitCode = EXIT_RESTART;
            QApplication::closeAllWindows();
            break;
        case QMessageBox::Retry:
            onAutosaveTimeout();
            break;
        default:
            break;
        }
    }
}

void MainWindow::open(QString url, const Mlt::Properties* properties, bool play)
{
    LOG_DEBUG() << url;
    bool modified = false;
    MltXmlChecker checker;
    QFileInfo info(url);

    if (info.isRelative()) {
        QDir pwd(QDir::currentPath());
        url = pwd.filePath(url);
    }
    if (url.endsWith(".mlt") || url.endsWith(".xml")) {
        if (url != untitledFileName()) {
            showStatusMessage(tr("Opening %1").arg(url));
            QCoreApplication::processEvents();
        }
    }
    if (checker.check(url)) {
        if (!isCompatibleWithGpuMode(checker))
            return;
    }
    if (url.endsWith(".mlt") || url.endsWith(".xml")) {
        // only check for a modified project when loading a project, not a simple producer
        if (!continueModified())
            return;
        QCoreApplication::processEvents();
        // close existing project
        if (playlist())
            m_playlistDock->model()->close();
        if (multitrack())
            m_timelineDock->model()->close();
        MLT.purgeMemoryPool();
        if (!isXmlRepaired(checker, url))
            return;
        modified = checkAutoSave(url);
        if (modified) {
            if (checker.check(url)) {
                if (!isCompatibleWithGpuMode(checker))
                    return;
            }
            if (!isXmlRepaired(checker, url))
                return;
        }
        // let the new project change the profile
        if (modified || QFile::exists(url)) {
            MLT.profile().set_explicit(false);
            setWindowModified(modified);
        }
    }
    if (!playlist() && !multitrack()) {
        if (!modified && !continueModified())
            return;
        setCurrentFile("");
        setWindowModified(modified);
        MLT.resetURL();
        // Return to automatic video mode if selected.
        if (m_profileGroup->checkedAction() && m_profileGroup->checkedAction()->data().toString().isEmpty())
            MLT.profile().set_explicit(false);
    }
    if (url.endsWith(".mlt") || url.endsWith(".xml")) {
        checker.setLocale();
        LOG_INFO() << "decimal point" << MLT.decimalPoint();
    }
    QString urlToOpen = checker.isUpdated()? checker.tempFileName() : url;
    if (!MLT.open(QDir::fromNativeSeparators(urlToOpen), QDir::fromNativeSeparators(url))
            && MLT.producer() && MLT.producer()->is_valid()) {
        Mlt::Properties* props = const_cast<Mlt::Properties*>(properties);
        if (props && props->is_valid())
            mlt_properties_inherit(MLT.producer()->get_properties(), props->get_properties());
        m_player->setPauseAfterOpen(!play || !MLT.isClip());

        setAudioChannels(MLT.audioChannels());
        if (url.endsWith(".mlt") || url.endsWith(".xml")) {
            setVideoModeMenu();
        }

        open(MLT.producer());
        if (url.startsWith(AutoSaveFile::path())) {
            QMutexLocker locker(&m_autosaveMutex);
            if (m_autosaveFile && m_autosaveFile->managedFileName() != untitledFileName()) {
                m_recentDock->add(m_autosaveFile->managedFileName());
                LOG_INFO() << m_autosaveFile->managedFileName();
            }
        } else {
            m_recentDock->add(url);
            LOG_INFO() << url;
        }
    }
    else if (url != untitledFileName()) {
        showStatusMessage(tr("Failed to open ") + url);
        emit openFailed(url);
    }
}

void MainWindow::openMultiple(const QStringList& paths)
{
    if (paths.size() > 1) {
        QList<QUrl> urls;
        foreach (const QString& s, paths)
            urls << s;
        openMultiple(urls);
    } else if (!paths.isEmpty()) {
        open(paths.first());
    }
}

void MainWindow::openMultiple(const QList<QUrl>& urls)
{
    if (urls.size() > 1) {
        m_multipleFiles = Util::sortedFileList(Util::expandDirectories(urls));
        open(m_multipleFiles.first());
    } else {
        QUrl url = urls.first();
        open(Util::removeFileScheme(url));
    }
}

void MainWindow::openVideo()
{
    QString path = Settings.openPath();
#ifdef Q_OS_MAC
    path.append("/*");
#endif
    LOG_DEBUG() << Util::getFileDialogOptions();
    QStringList filenames = QFileDialog::getOpenFileNames(this, tr("Open File"), path,
        tr("All Files (*);;MLT XML (*.mlt)"), nullptr, Util::getFileDialogOptions());

    if (filenames.length() > 0) {
        Settings.setOpenPath(QFileInfo(filenames.first()).path());
        activateWindow();
        if (filenames.length() > 1)
            m_multipleFiles = filenames;
        open(filenames.first());
    }
    else {
        // If file invalid, then on some platforms the dialog messes up SDL.
        MLT.onWindowResize();
        activateWindow();
    }
}

void MainWindow::openCut(Mlt::Producer* producer, bool play)
{
    m_player->setPauseAfterOpen(!play);
    open(producer);
    MLT.seek(producer->get_in());
}

void MainWindow::hideProducer()
{
    // This is a hack to release references to the old producer, but it
    // probably leaves a reference to the new color producer somewhere not
    // yet identified (root cause).
    openCut(new Mlt::Producer(MLT.profile(), "color:_hide"));
    QCoreApplication::processEvents();
    openCut(new Mlt::Producer(MLT.profile(), "color:_hide"));
    QCoreApplication::processEvents();

    QScrollArea* scrollArea = (QScrollArea*) m_propertiesDock->widget();
    delete scrollArea->widget();
    scrollArea->setWidget(nullptr);
    m_player->reset();

    QCoreApplication::processEvents();
}

void MainWindow::closeProducer()
{
    hideProducer();
    MLT.stop();
    MLT.close();
    MLT.setSavedProducer(0);
}

void MainWindow::showStatusMessage(QAction* action, int timeoutSeconds)
{
    // This object takes ownership of the passed action.
    // This version does not currently log its message.
    m_statusBarAction.reset(action);
    action->setParent(0);
    m_player->setStatusLabel(action->text(), timeoutSeconds, action);
}

void MainWindow::showStatusMessage(const QString& message, int timeoutSeconds)
{
    LOG_INFO() << message;
    m_player->setStatusLabel(message, timeoutSeconds, 0 /* QAction */);
    m_statusBarAction.reset();
}

void MainWindow::seekPlaylist(int start)
{
    if (!playlist()) return;
    // we bypass this->open() to prevent sending producerOpened signal to self, which causes to reload playlist
    if (!MLT.producer() || (void*) MLT.producer()->get_producer() != (void*) playlist()->get_playlist())
        MLT.setProducer(new Mlt::Producer(*playlist()));
    m_player->setIn(-1);
    m_player->setOut(-1);
    // since we do not emit producerOpened, these components need updating
    on_actionJack_triggered(ui->actionJack && ui->actionJack->isChecked());
    m_player->onProducerOpened(false);
    m_encodeDock->onProducerOpened();
    m_filterController->setProducer();
    updateMarkers();
    MLT.seek(start);
    m_player->setFocus();
    m_player->switchToTab(Player::ProjectTabIndex);
}

void MainWindow::seekTimeline(int position, bool seekPlayer)
{
    if (!multitrack()) return;
    // we bypass this->open() to prevent sending producerOpened signal to self, which causes to reload playlist
    if (MLT.producer() && (void*) MLT.producer()->get_producer() != (void*) multitrack()->get_producer()) {
        MLT.setProducer(new Mlt::Producer(*multitrack()));
        m_player->setIn(-1);
        m_player->setOut(-1);
        // since we do not emit producerOpened, these components need updating
        on_actionJack_triggered(ui->actionJack && ui->actionJack->isChecked());
        m_player->onProducerOpened(false);
        m_encodeDock->onProducerOpened();
        m_filterController->setProducer();
        updateMarkers();
        m_player->setFocus();
        m_player->switchToTab(Player::ProjectTabIndex);
        m_timelineDock->emitSelectedFromSelection();
    }
    if (seekPlayer)
        m_player->seek(position);
    else
        m_player->pause();
}

void MainWindow::seekKeyframes(int position)
{
    m_player->seek(position);
}

void MainWindow::readPlayerSettings()
{
    LOG_DEBUG() << "begin";
    ui->actionRealtime->setChecked(Settings.playerRealtime());
    ui->actionProgressive->setChecked(Settings.playerProgressive());
    ui->actionScrubAudio->setChecked(Settings.playerScrubAudio());
    if (ui->actionJack)
        ui->actionJack->setChecked(Settings.playerJACK());
    if (ui->actionGPU) {
        MLT.videoWidget()->setProperty("gpu", ui->actionGPU->isChecked());
        ui->actionGPU->setChecked(Settings.playerGPU());
    }

    QString external = Settings.playerExternal();
    bool ok = false;
    external.toInt(&ok);
    auto isExternalPeripheral = !external.isEmpty() && !ok;

    setAudioChannels(Settings.playerAudioChannels());

    if (isExternalPeripheral) {
        setPreviewScale(0);
        m_previewScaleGroup->setEnabled(false);
    } else {
        setPreviewScale(Settings.playerPreviewScale());
        m_previewScaleGroup->setEnabled(true);
    }

    QString deinterlacer = Settings.playerDeinterlacer();
    QString interpolation = Settings.playerInterpolation();

    if (deinterlacer == "onefield")
        ui->actionOneField->setChecked(true);
    else if (deinterlacer == "linearblend")
        ui->actionLinearBlend->setChecked(true);
    else if (deinterlacer == "yadif-nospatial")
        ui->actionYadifTemporal->setChecked(true);
    else
        ui->actionYadifSpatial->setChecked(true);

    if (interpolation == "nearest")
        ui->actionNearest->setChecked(true);
    else if (interpolation == "bilinear")
        ui->actionBilinear->setChecked(true);
    else if (interpolation == "bicubic")
        ui->actionBicubic->setChecked(true);
    else
        ui->actionHyper->setChecked(true);

    foreach (QAction* a, m_externalGroup->actions()) {
        if (a->data() == external) {
            a->setChecked(true);
            if (a->data().toString().startsWith("decklink") && m_keyerMenu)
                m_keyerMenu->setEnabled(true);
            break;
        }
    }

    if (m_keyerGroup) {
        int keyer = Settings.playerKeyerMode();
        foreach (QAction* a, m_keyerGroup->actions()) {
            if (a->data() == keyer) {
                a->setChecked(true);
                break;
            }
        }
    }

    QString profile = Settings.playerProfile();
    // Automatic not permitted for SDI/HDMI
    if (isExternalPeripheral && profile.isEmpty())
        profile = "atsc_720p_50";
    foreach (QAction* a, m_profileGroup->actions()) {
        // Automatic not permitted for SDI/HDMI
        if (a->data().toString().isEmpty() && !external.isEmpty() && !ok)
            a->setDisabled(true);
        if (a->data().toString() == profile) {
            a->setChecked(true);
            break;
        }
    }

    QString gamma = Settings.playerGamma();
    if (gamma == "bt709")
        ui->actionGammaRec709->setChecked(true);
    else
        ui->actionGammaSRGB->setChecked(true);

    LOG_DEBUG() << "end";
}

void MainWindow::readWindowSettings()
{
    LOG_DEBUG() << "begin";
    Settings.setWindowGeometryDefault(saveGeometry());
    Settings.setWindowStateDefault(saveState());
    Settings.sync();
    if (!Settings.windowGeometry().isEmpty()) {
        restoreGeometry(Settings.windowGeometry());
        restoreState(Settings.windowState());
#ifdef Q_OS_MAC
        m_filtersDock->setFloating(false);
#endif
    } else {
        restoreState(kLayoutEditingDefault);
    }
    LOG_DEBUG() << "end";
}

void MainWindow::writeSettings()
{
#ifndef Q_OS_MAC
    if (isFullScreen())
        showNormal();
#endif
    Settings.setPlayerGPU(ui->actionGPU->isChecked());
    Settings.setWindowGeometry(saveGeometry());
    Settings.setWindowState(saveState());
    Settings.sync();
}

void MainWindow::configureVideoWidget()
{
    LOG_DEBUG() << "begin";
    if (m_profileGroup->checkedAction())
        setProfile(m_profileGroup->checkedAction()->data().toString());
    MLT.videoWidget()->setProperty("realtime", ui->actionRealtime->isChecked());
    bool ok = false;
    m_externalGroup->checkedAction()->data().toInt(&ok);
    if (!ui->menuExternal || m_externalGroup->checkedAction()->data().toString().isEmpty() || ok) {
        MLT.videoWidget()->setProperty("progressive", ui->actionProgressive->isChecked());
    } else {
        MLT.videoWidget()->setProperty("mlt_service", m_externalGroup->checkedAction()->data());
        MLT.videoWidget()->setProperty("progressive", MLT.profile().progressive());
        ui->actionProgressive->setEnabled(false);
    }
    if (ui->actionChannels1->isChecked())
        setAudioChannels(1);
    else if (ui->actionChannels2->isChecked())
        setAudioChannels(2);
    else
        setAudioChannels(6);
    if (ui->actionOneField->isChecked())
        MLT.videoWidget()->setProperty("deinterlace_method", "onefield");
    else if (ui->actionLinearBlend->isChecked())
        MLT.videoWidget()->setProperty("deinterlace_method", "linearblend");
    else if (ui->actionYadifTemporal->isChecked())
        MLT.videoWidget()->setProperty("deinterlace_method", "yadif-nospatial");
    else
        MLT.videoWidget()->setProperty("deinterlace_method", "yadif");
    if (ui->actionNearest->isChecked())
        MLT.videoWidget()->setProperty("rescale", "nearest");
    else if (ui->actionBilinear->isChecked())
        MLT.videoWidget()->setProperty("rescale", "bilinear");
    else if (ui->actionBicubic->isChecked())
        MLT.videoWidget()->setProperty("rescale", "bicubic");
    else
        MLT.videoWidget()->setProperty("rescale", "hyper");
    if (m_keyerGroup)
        MLT.videoWidget()->setProperty("keyer", m_keyerGroup->checkedAction()->data());
    LOG_DEBUG() << "end";
}

void MainWindow::setCurrentFile(const QString &filename)
{
    QString shownName = tr("Untitled");
    if (filename == untitledFileName())
        m_currentFile.clear();
    else
        m_currentFile = filename;
    if (!m_currentFile.isEmpty())
        shownName = QFileInfo(m_currentFile).fileName();
#ifdef Q_OS_MAC
    setWindowTitle(QString("%1 - %2").arg(shownName).arg(qApp->applicationName()));
#else
    setWindowTitle(QString("%1[*] - %2").arg(shownName).arg(qApp->applicationName()));
#endif
}

void MainWindow::on_actionAbout_Shotcut_triggered()
{
    QMessageBox::about(this, tr("About Shotcut"),
             tr("<h1>Shotcut version %1</h1>"
                "<p><a href=\"https://www.shotcut.org/\">Shotcut</a> is a free, open source, cross platform video editor.</p>"
                "<small><p>Copyright &copy; 2011-2020 <a href=\"https://www.meltytech.com/\">Meltytech</a>, LLC</p>"
                "<p>Licensed under the <a href=\"https://www.gnu.org/licenses/gpl.html\">GNU General Public License v3.0</a></p>"
                "<p>This program proudly uses the following projects:<ul>"
                "<li><a href=\"https://www.qt.io/\">Qt</a> application and UI framework</li>"
                "<li><a href=\"https://www.mltframework.org/\">MLT</a> multimedia authoring framework</li>"
                "<li><a href=\"https://www.ffmpeg.org/\">FFmpeg</a> multimedia format and codec libraries</li>"
                "<li><a href=\"https://www.videolan.org/developers/x264.html\">x264</a> H.264 encoder</li>"
                "<li><a href=\"https://www.webmproject.org/\">WebM</a> VP8 and VP9 encoders</li>"
                "<li><a href=\"http://lame.sourceforge.net/\">LAME</a> MP3 encoder</li>"
                "<li><a href=\"https://www.dyne.org/software/frei0r/\">Frei0r</a> video plugins</li>"
                "<li><a href=\"https://www.ladspa.org/\">LADSPA</a> audio plugins</li>"
                "<li><a href=\"http://www.defaulticon.com/\">DefaultIcon</a> icon collection by <a href=\"http://www.interactivemania.com/\">interactivemania</a></li>"
                "<li><a href=\"http://www.oxygen-icons.org/\">Oxygen</a> icon collection</li>"
                "</ul></p>"
                "<p>The source code used to build this program can be downloaded from "
                "<a href=\"https://www.shotcut.org/\">shotcut.org</a>.</p>"
                "This program is distributed in the hope that it will be useful, "
                "but WITHOUT ANY WARRANTY; without even the implied warranty of "
                "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.</small>"
                ).arg(qApp->applicationVersion()));
}


void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->isAccepted() && event->key() != Qt::Key_F12) return;

    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Home:
        m_player->seek(0);
        break;
    case Qt::Key_End:
        if (MLT.producer())
            m_player->seek(MLT.producer()->get_length() - 1);
        break;
    case Qt::Key_Left:
        if ((event->modifiers() & Qt::ControlModifier) && m_timelineDock->isVisible()) {
            if (m_timelineDock->selection().isEmpty()) {
                m_timelineDock->selectClipUnderPlayhead();
            } else if (m_timelineDock->selection().size() == 1) {
                int newIndex = m_timelineDock->selection().first().x() - 1;
                if (newIndex < 0)
                    break;
                m_timelineDock->setSelection(QList<QPoint>() << QPoint(newIndex, m_timelineDock->selection().first().y()));
                m_navigationPosition = m_timelineDock->centerOfClip(m_timelineDock->currentTrack(), newIndex);
            }
        } else {
            stepLeftOneFrame();
        }
        break;
    case Qt::Key_Right:
        if ((event->modifiers() & Qt::ControlModifier) && m_timelineDock->isVisible()) {
            if (m_timelineDock->selection().isEmpty()) {
                m_timelineDock->selectClipUnderPlayhead();
            } else if (m_timelineDock->selection().size() == 1) {
                int newIndex = m_timelineDock->selection().first().x() + 1;
                if (newIndex >= m_timelineDock->clipCount(-1))
                    break;
                m_timelineDock->setSelection(QList<QPoint>() << QPoint(newIndex, m_timelineDock->selection().first().y()));
                m_navigationPosition = m_timelineDock->centerOfClip(m_timelineDock->currentTrack(), newIndex);
            }
        } else {
            stepRightOneFrame();
        }
        break;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        {
            int directionMultiplier = event->key() == Qt::Key_PageUp ? -1 : 1;
            int seconds = 1;
            if (event->modifiers() & Qt::ControlModifier)
                seconds *= 5;
            if (event->modifiers() & Qt::ShiftModifier)
                seconds *= 2;
            stepLeftBySeconds(seconds * directionMultiplier);
        }
        break;
    case Qt::Key_Space:
#ifdef Q_OS_MAC
        // Spotlight defaults to Cmd+Space, so also accept Ctrl+Space.
        if ((event->modifiers() == Qt::MetaModifier || (event->modifiers() & Qt::ControlModifier)) && m_timelineDock->isVisible())
#else
        if (event->modifiers() == Qt::ControlModifier && m_timelineDock->isVisible())
#endif
            m_timelineDock->selectClipUnderPlayhead();
        else
            handled = false;
        break;
    case Qt::Key_A:
        if (event->modifiers() == Qt::ShiftModifier) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_actionAppendCut_triggered();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_actionSelectAll_triggered();
        } else if (event->modifiers() == Qt::ControlModifier) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->selectAll();
        } else if (event->modifiers() == Qt::NoModifier) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->append(-1);
        }
        break;
    case Qt::Key_C:
        if (event->modifiers() == Qt::ShiftModifier && m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_actionCopy_triggered();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::AltModifier)) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->copyToSource();
        } else if (isMultitrackValid()) {
            m_timelineDock->show();
            m_timelineDock->raise();
            if (m_timelineDock->selection().isEmpty()) {
                m_timelineDock->copyClip(-1, -1);
            } else {
                auto& selected = m_timelineDock->selection().first();
                m_timelineDock->copyClip(selected.y(), selected.x());
            }
        }
        break;
    case Qt::Key_D:
        if (event->modifiers() == Qt::ControlModifier) {
            m_timelineDock->setSelection();
            m_timelineDock->model()->reload();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_actionSelectNone_triggered();
        } else {
            handled = false;
        }
        break;
    case Qt::Key_F:
        if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ControlModifier) {
            m_filtersDock->show();
            m_filtersDock->raise();
            m_filtersDock->widget()->setFocus();
            m_filtersDock->openFilterMenu();
        } else if (event->modifiers() == Qt::ShiftModifier) {
            filterController()->removeCurrent();
#ifdef Q_OS_MAC
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::MetaModifier)) {
            on_actionEnter_Full_Screen_triggered();
#else
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
            on_actionEnter_Full_Screen_triggered();
#endif
        } else {
            handled = false;
        }
        break;
    case Qt::Key_H:
#ifdef Q_OS_MAC
        // OS X uses Cmd+H to hide an app.
        if (event->modifiers() & Qt::MetaModifier && isMultitrackValid())
#else
        if (event->modifiers() & Qt::ControlModifier && isMultitrackValid())
#endif
            m_timelineDock->toggleTrackHidden(m_timelineDock->currentTrack());
        break;
    case Qt::Key_J:
        if (m_isKKeyPressed)
            m_player->seek(m_player->position() - 1);
        else
            m_player->rewind(false);
        break;
    case Qt::Key_K:
            m_player->pause();
            m_isKKeyPressed = true;
        break;
    case Qt::Key_L:
#ifdef Q_OS_MAC
        // OS X uses Cmd+H to hide an app and Cmd+M to minimize. Therefore, we force
        // it to be the apple keyboard control key aka meta. Therefore, to be
        // consistent with all track header toggles, we make the lock toggle also use
        // meta.
        if (event->modifiers() & Qt::MetaModifier && isMultitrackValid())
#else
        if (event->modifiers() & Qt::ControlModifier && isMultitrackValid())
#endif
            m_timelineDock->setTrackLock(m_timelineDock->currentTrack(), !m_timelineDock->isTrackLocked(m_timelineDock->currentTrack()));
        else if (m_isKKeyPressed)
            m_player->seek(m_player->position() + 1);
        else
            m_player->fastForward(false);
        break;
    case Qt::Key_M:
#ifdef Q_OS_MAC
        // OS X uses Cmd+M to minimize an app.
        if (event->modifiers() & Qt::MetaModifier && isMultitrackValid())
#else
        if (event->modifiers() & Qt::ControlModifier && isMultitrackValid())
#endif
            m_timelineDock->toggleTrackMute(m_timelineDock->currentTrack());
        break;
    case Qt::Key_I:
        if (event->modifiers() == Qt::ControlModifier) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->addVideoTrack();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::AltModifier)) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->insertTrack();
        } else {
            setInToCurrent(event->modifiers() & Qt::ShiftModifier);
        }
        break;
    case Qt::Key_O:
        setOutToCurrent(event->modifiers() & Qt::ShiftModifier);
        break;
    case Qt::Key_P:
        if (event->modifiers() == Qt::ControlModifier) {
            Settings.setTimelineSnap(!Settings.timelineSnap());
        } else if (event->modifiers() & Qt::ControlModifier) {
            if (event->modifiers() & Qt::AltModifier) {
                Settings.setTimelineScrollZoom(!Settings.timelineScrollZoom());
            }
            if (event->modifiers() & Qt::ShiftModifier) {
                Settings.setTimelineCenterPlayhead(!Settings.timelineCenterPlayhead());
            }
        }
        break;
    case Qt::Key_R:
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->modifiers() & Qt::AltModifier) {
                Settings.setTimelineRippleAllTracks(!Settings.timelineRippleAllTracks());
            } else if (event->modifiers() & Qt::ShiftModifier) {
                Settings.setTimelineRippleAllTracks(!Settings.timelineRipple());
                Settings.setTimelineRipple(!Settings.timelineRipple());
            } else {
                Settings.setTimelineRipple(!Settings.timelineRipple());
            }
        } else if (isMultitrackValid()) {
            m_timelineDock->show();
            m_timelineDock->raise();
            if (MLT.isClip() || m_timelineDock->selection().isEmpty()) {
                m_timelineDock->replace(-1, -1);
            } else {
                auto& selected = m_timelineDock->selection().first();
                m_timelineDock->replace(selected.y(), selected.x());
            }
        }
        break;
    case Qt::Key_S:
        if (isMultitrackValid())
            m_timelineDock->splitClip();
        break;
    case Qt::Key_U:
        if (event->modifiers() == Qt::ControlModifier) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->addAudioTrack();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::AltModifier)) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->removeTrack();
        }
        break;
    case Qt::Key_V: // Avid Splice In
        if (event->modifiers() == Qt::ShiftModifier) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_actionInsertCut_triggered();
        } else {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->insert(-1);
        }
        break;
    case Qt::Key_B:
        if (event->modifiers() & Qt::ControlModifier && event->modifiers() & Qt::AltModifier) {
            // Toggle track blending.
            int trackIndex = m_timelineDock->currentTrack();
            bool isBottomVideo = m_timelineDock->model()->data(m_timelineDock->model()->index(trackIndex), MultitrackModel::IsBottomVideoRole).toBool();
            if (!isBottomVideo) {
                bool isComposite = m_timelineDock->model()->data(m_timelineDock->model()->index(trackIndex), MultitrackModel::IsCompositeRole).toBool();
                m_timelineDock->setTrackComposite(trackIndex, !isComposite);
            }
        } else if (event->modifiers() == Qt::ShiftModifier) {
            if (m_playlistDock->model()->rowCount() > 0) {
                // Update playlist item.
                m_playlistDock->show();
                m_playlistDock->raise();
                m_playlistDock->on_actionUpdate_triggered();
            }
        } else {
            // Overwrite on timeline.
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->overwrite(-1);
        }
        break;
    case Qt::Key_Escape: // Avid Toggle Active Monitor
        if (MLT.isPlaylist()) {
            if (isMultitrackValid())
                m_player->onTabBarClicked(Player::ProjectTabIndex);
            else if (MLT.savedProducer())
                m_player->onTabBarClicked(Player::SourceTabIndex);
            else
                m_playlistDock->on_actionOpen_triggered();
        } else if (MLT.isMultitrack()) {
            if (MLT.savedProducer())
                m_player->onTabBarClicked(Player::SourceTabIndex);
            // TODO else open clip under playhead of current track if available
        } else {
            if (isMultitrackValid() || (playlist() && playlist()->count() > 0))
                m_player->onTabBarClicked(Player::ProjectTabIndex);
        }
        break;
    case Qt::Key_Up:
        if (m_playlistDock->isVisible() && event->modifiers() & Qt::AltModifier && m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->raise();
            m_playlistDock->decrementIndex();
            m_playlistDock->on_actionOpen_triggered();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
            if (m_playlistDock->model()->rowCount() > 0) {
                m_playlistDock->raise();
                m_playlistDock->moveClipUp();
                m_playlistDock->decrementIndex();
            }
        } else if (isMultitrackValid()) {
            int newClipIndex = -1;
            int trackIndex = m_timelineDock->currentTrack() - 1;
            if ((event->modifiers() & Qt::ControlModifier) &&
                    !m_timelineDock->selection().isEmpty() &&
                    trackIndex > -1) {

                newClipIndex = m_timelineDock->clipIndexAtPosition(trackIndex, m_navigationPosition);
            }

            m_timelineDock->incrementCurrentTrack(-1);

            if (newClipIndex >= 0) {
                newClipIndex = qMin(newClipIndex, m_timelineDock->clipCount(trackIndex) - 1);
                m_timelineDock->setSelection(QList<QPoint>() << QPoint(newClipIndex, trackIndex));
            }

        }
        break;
    case Qt::Key_Down:
        if (m_playlistDock->isVisible() && event->modifiers() & Qt::AltModifier && m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->raise();
            m_playlistDock->incrementIndex();
            m_playlistDock->on_actionOpen_triggered();
        } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
            if (m_playlistDock->model()->rowCount() > 0) {
                m_playlistDock->raise();
                m_playlistDock->moveClipDown();
                m_playlistDock->incrementIndex();
            }
        } else if (isMultitrackValid()) {
            int newClipIndex = -1;
            int trackIndex = m_timelineDock->currentTrack() + 1;
            if ((event->modifiers() & Qt::ControlModifier) &&
                    !m_timelineDock->selection().isEmpty() &&
                    trackIndex < m_timelineDock->model()->trackList().count()) {

                newClipIndex = m_timelineDock->clipIndexAtPosition(trackIndex, m_navigationPosition);
            }

            m_timelineDock->incrementCurrentTrack(1);

            if (newClipIndex >= 0) {
                newClipIndex = qMin(newClipIndex, m_timelineDock->clipCount(trackIndex) - 1);
                m_timelineDock->setSelection(QList<QPoint>() << QPoint(newClipIndex, trackIndex));
            }

        }
        break;
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:
        if (!event->modifiers() && m_playlistDock->isVisible() && m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->raise();
            m_playlistDock->setIndex(event->key() - Qt::Key_1);
        }
        break;
    case Qt::Key_0:
        if (!event->modifiers() ) {
            if (m_timelineDock->isVisible()) {
                emit m_timelineDock->zoomToFit();
            } else if (m_playlistDock->isVisible() && m_playlistDock->model()->rowCount() > 0) {
                m_playlistDock->raise();
                m_playlistDock->setIndex(9);
            }
        }
        if (m_keyframesDock->isVisible() && (event->modifiers() & Qt::AltModifier)) {
            emit m_keyframesDock->zoomToFit();
        }
        break;
    case Qt::Key_X: // Avid Extract
        if (event->modifiers() == Qt::ShiftModifier && m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_removeButton_clicked();
        } else if (isMultitrackValid()) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->removeSelection();
        }
        break;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        if (isMultitrackValid()) {
            m_timelineDock->show();
            m_timelineDock->raise();
            if (event->modifiers() == Qt::ShiftModifier)
                m_timelineDock->removeSelection();
            else
                m_timelineDock->liftSelection();
        } else if (m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_removeButton_clicked();
        }
        break;
    case Qt::Key_Z: // Avid Lift
        if (event->modifiers() == Qt::ShiftModifier && m_playlistDock->model()->rowCount() > 0) {
            m_playlistDock->show();
            m_playlistDock->raise();
            m_playlistDock->on_removeButton_clicked();
        } else if (isMultitrackValid() && event->modifiers() == Qt::NoModifier) {
            m_timelineDock->show();
            m_timelineDock->raise();
            m_timelineDock->liftSelection();
        }
        break;
    case Qt::Key_Minus:
        if (m_timelineDock->isVisible() && !(event->modifiers() & Qt::AltModifier)) {
            if (event->modifiers() & Qt::ControlModifier)
                emit m_timelineDock->makeTracksShorter();
            else
                emit m_timelineDock->zoomOut();
        }
        if (m_keyframesDock->isVisible() && (event->modifiers() & Qt::AltModifier)) {
            emit m_keyframesDock->zoomOut();
        }
        break;
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        if (m_timelineDock->isVisible() && !(event->modifiers() & Qt::AltModifier)) {
            if (event->modifiers() & Qt::ControlModifier)
                emit m_timelineDock->makeTracksTaller();
            else
                emit m_timelineDock->zoomIn();
        }
        if (m_keyframesDock->isVisible() && (event->modifiers() & Qt::AltModifier)) {
            emit m_keyframesDock->zoomIn();
        }
        break;
    case Qt::Key_Enter: // Seek to current playlist item
    case Qt::Key_Return:
        if (m_playlistDock->isVisible() && m_playlistDock->position() >= 0) {
            if (event->modifiers() == Qt::ShiftModifier)
                seekPlaylist(m_playlistDock->position());
            else if (event->modifiers() == Qt::ControlModifier)
                m_playlistDock->on_actionOpen_triggered();
        }
        break;
    case Qt::Key_F2:
        onPropertiesDockTriggered(true);
        emit renameRequested();
        break;
    case Qt::Key_F3:
        onRecentDockTriggered(true);
        m_recentDock->find();
        break;
    case Qt::Key_F5:
        m_timelineDock->model()->reload();
        m_keyframesDock->model().reload();
        break;
    case Qt::Key_F11:
        on_actionEnter_Full_Screen_triggered();
        break;
    case Qt::Key_F12:
        LOG_DEBUG() << "event isAccepted:" << event->isAccepted();
        LOG_DEBUG() << "Current focusWidget:" << QApplication::focusWidget();
        LOG_DEBUG() << "Current focusObject:" << QApplication::focusObject();
        LOG_DEBUG() << "Current focusWindow:" << QApplication::focusWindow();
        break;
    case Qt::Key_BracketLeft:
        if (filterController()->currentFilter() && m_filtersDock->qmlProducer()) {
            if (event->modifiers() == Qt::AltModifier) {
                emit m_keyframesDock->seekPreviousSimple();
            } else {
                int i = m_filtersDock->qmlProducer()->position() + m_filtersDock->qmlProducer()->in();
                filterController()->currentFilter()->setIn(i);
            }
        }
        break;
    case Qt::Key_BracketRight:
        if (filterController()->currentFilter() && m_filtersDock->qmlProducer()) {
            if (event->modifiers() == Qt::AltModifier) {
                emit m_keyframesDock->seekNextSimple();
            } else {
                int i = m_filtersDock->qmlProducer()->position() + m_filtersDock->qmlProducer()->in();
                filterController()->currentFilter()->setOut(i);
            }
        }
        break;
    case Qt::Key_BraceLeft:
        if (filterController()->currentFilter() && m_filtersDock->qmlProducer()) {
            int i = m_filtersDock->qmlProducer()->position() + m_filtersDock->qmlProducer()->in() - filterController()->currentFilter()->in();
            filterController()->currentFilter()->setAnimateIn(i);
        }
        break;
    case Qt::Key_BraceRight:
        if (filterController()->currentFilter() && m_filtersDock->qmlProducer()) {
            int i = filterController()->currentFilter()->out() - (m_filtersDock->qmlProducer()->position() + m_filtersDock->qmlProducer()->in());
            filterController()->currentFilter()->setAnimateOut(i);
        }
        break;
    case Qt::Key_Semicolon:
        if (filterController()->currentFilter() && m_filtersDock->qmlProducer() && m_keyframesDock->currentParameter() >= 0) {
            auto position = m_filtersDock->qmlProducer()->position() - (filterController()->currentFilter()->in() - m_filtersDock->qmlProducer()->in());
            auto parameterIndex = m_keyframesDock->currentParameter();
            if (m_keyframesDock->model().isKeyframe(parameterIndex, position)) {
                auto keyframeIndex = m_keyframesDock->model().keyframeIndex(parameterIndex, position);
                m_keyframesDock->model().remove(parameterIndex, keyframeIndex);
            } else {
                m_keyframesDock->model().addKeyframe(parameterIndex, position);
            }
        }
        break;
    default:
        handled = false;
        break;
    }

    if (handled)
        event->setAccepted(handled);
    else
        QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_K) {
        m_isKKeyPressed = false;
        event->setAccepted(true);
    } else {
        QMainWindow::keyReleaseEvent(event);
    }
}

void MainWindow::hideSetDataDirectory()
{
    delete ui->actionAppDataSet;
}

QAction *MainWindow::actionAddCustomProfile() const
{
    return ui->actionAddCustomProfile;
}

QAction *MainWindow::actionProfileRemove() const
{
    return ui->actionProfileRemove;
}

void MainWindow::buildVideoModeMenu(QMenu* topMenu, QMenu*& customMenu, QActionGroup* group, QAction* addAction, QAction* removeAction)
{
    topMenu->addAction(addProfile(group, "HD 720p 50 fps", "atsc_720p_50"));
    topMenu->addAction(addProfile(group, "HD 720p 59.94 fps", "atsc_720p_5994"));
    topMenu->addAction(addProfile(group, "HD 720p 60 fps", "atsc_720p_60"));
    topMenu->addAction(addProfile(group, "HD 1080i 25 fps", "atsc_1080i_50"));
    topMenu->addAction(addProfile(group, "HD 1080i 29.97 fps", "atsc_1080i_5994"));
    topMenu->addAction(addProfile(group, "HD 1080p 23.98 fps", "atsc_1080p_2398"));
    topMenu->addAction(addProfile(group, "HD 1080p 24 fps", "atsc_1080p_24"));
    topMenu->addAction(addProfile(group, "HD 1080p 25 fps", "atsc_1080p_25"));
    topMenu->addAction(addProfile(group, "HD 1080p 29.97 fps", "atsc_1080p_2997"));
    topMenu->addAction(addProfile(group, "HD 1080p 30 fps", "atsc_1080p_30"));
    topMenu->addAction(addProfile(group, "HD 1080p 50 fps", "atsc_1080p_50"));
    topMenu->addAction(addProfile(group, "HD 1080p 59.94 fps", "atsc_1080p_5994"));
    topMenu->addAction(addProfile(group, "HD 1080p 60 fps", "atsc_1080p_60"));
    topMenu->addAction(addProfile(group, "SD NTSC", "dv_ntsc"));
    topMenu->addAction(addProfile(group, "SD PAL", "dv_pal"));
    topMenu->addAction(addProfile(group, "UHD 2160p 23.98 fps", "uhd_2160p_2398"));
    topMenu->addAction(addProfile(group, "UHD 2160p 24 fps", "uhd_2160p_24"));
    topMenu->addAction(addProfile(group, "UHD 2160p 25 fps", "uhd_2160p_25"));
    topMenu->addAction(addProfile(group, "UHD 2160p 29.97 fps", "uhd_2160p_2997"));
    topMenu->addAction(addProfile(group, "UHD 2160p 30 fps", "uhd_2160p_30"));
    topMenu->addAction(addProfile(group, "UHD 2160p 50 fps", "uhd_2160p_50"));
    topMenu->addAction(addProfile(group, "UHD 2160p 59.94 fps", "uhd_2160p_5994"));
    topMenu->addAction(addProfile(group, "UHD 2160p 60 fps", "uhd_2160p_60"));
    QMenu* menu = topMenu->addMenu(tr("Non-Broadcast"));
    menu->addAction(addProfile(group, "640x480 4:3 NTSC", "square_ntsc"));
    menu->addAction(addProfile(group, "768x576 4:3 PAL", "square_pal"));
    menu->addAction(addProfile(group, "854x480 16:9 NTSC", "square_ntsc_wide"));
    menu->addAction(addProfile(group, "1024x576 16:9 PAL", "square_pal_wide"));
    menu->addAction(addProfile(group, tr("DVD Widescreen NTSC"), "dv_ntsc_wide"));
    menu->addAction(addProfile(group, tr("DVD Widescreen PAL"), "dv_pal_wide"));
    menu->addAction(addProfile(group, "HD 720p 23.98 fps", "atsc_720p_2398"));
    menu->addAction(addProfile(group, "HD 720p 24 fps", "atsc_720p_24"));
    menu->addAction(addProfile(group, "HD 720p 25 fps", "atsc_720p_25"));
    menu->addAction(addProfile(group, "HD 720p 29.97 fps", "atsc_720p_2997"));
    menu->addAction(addProfile(group, "HD 720p 30 fps", "atsc_720p_30"));
    menu->addAction(addProfile(group, "HD 1080i 60 fps", "atsc_1080i_60"));
    menu->addAction(addProfile(group, "HDV 1080i 25 fps", "hdv_1080_50i"));
    menu->addAction(addProfile(group, "HDV 1080i 29.97 fps", "hdv_1080_60i"));
    menu->addAction(addProfile(group, "HDV 1080p 25 fps", "hdv_1080_25p"));
    menu->addAction(addProfile(group, "HDV 1080p 29.97 fps", "hdv_1080_30p"));
    menu->addAction(addProfile(group, tr("Square 1080p 30 fps"), "square_1080p_30"));
    menu->addAction(addProfile(group, tr("Square 1080p 60 fps"), "square_1080p_60"));
    menu->addAction(addProfile(group, tr("Vertical HD 30 fps"), "vertical_hd_30"));
    menu->addAction(addProfile(group, tr("Vertical HD 60 fps"), "vertical_hd_60"));
    customMenu = topMenu->addMenu(tr("Custom"));
    customMenu->addAction(addAction);
    // Load custom profiles
    QDir dir(Settings.appDataLocation());
    if (dir.cd("profiles")) {
        QStringList profiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
        if (profiles.length() > 0) {
            customMenu->addAction(removeAction);
            customMenu->addSeparator();
        }
        foreach (QString name, profiles)
            customMenu->addAction(addProfile(group, name, dir.filePath(name)));
    }
}

void MainWindow::newProject(const QString &filename, bool isProjectFolder)
{
    if (isProjectFolder) {
        QFileInfo info(filename);
        MLT.setProjectFolder(info.absolutePath());
    }
    if (saveXML(filename)) {
        QMutexLocker locker(&m_autosaveMutex);
        if (m_autosaveFile)
            m_autosaveFile->changeManagedFile(filename);
        else
            m_autosaveFile.reset(new AutoSaveFile(filename));
        setCurrentFile(filename);
        setWindowModified(false);
        if (MLT.producer())
            showStatusMessage(tr("Saved %1").arg(m_currentFile));
        m_undoStack->setClean();
        m_recentDock->add(filename);
    } else {
        showSaveError();
    }
}

void MainWindow::addCustomProfile(const QString &name, QMenu *menu, QAction *action, QActionGroup *group)
{
    // Add new profile to the menu.
    QDir dir(Settings.appDataLocation());
    if (dir.cd("profiles")) {
        QStringList profiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
        if (profiles.length() == 1) {
            menu->addAction(action);
            menu->addSeparator();
        }
        action = addProfile(group, name, dir.filePath(name));
        action->setChecked(true);
        menu->addAction(action);
        Settings.setPlayerProfile(dir.filePath(name));
        Settings.sync();
    }
}

void MainWindow::removeCustomProfiles(const QStringList &profiles, QDir& dir, QMenu *menu, QAction *action)
{
    foreach(const QString& profile, profiles) {
        // Remove the file.
        dir.remove(profile);
        // Locate the menu item.
        foreach (QAction* a, menu->actions()) {
            if (a->text() == profile) {
                // Remove the menu item.
                delete a;
                break;
            }
        }
    }
    // If no more custom video modes.
    if (menu->actions().size() == 3) {
        // Remove the Remove action and separator.
        menu->removeAction(action);
        foreach (QAction* a, menu->actions()) {
            if (a->isSeparator()) {
                delete a;
                break;
            }
        }
    }
}

// Drag-n-drop events

bool MainWindow::eventFilter(QObject* target, QEvent* event)
{
    if (event->type() == QEvent::DragEnter && target == MLT.videoWidget()) {
        dragEnterEvent(static_cast<QDragEnterEvent*>(event));
        return true;
    } else if (event->type() == QEvent::Drop && target == MLT.videoWidget()) {
        dropEvent(static_cast<QDropEvent*>(event));
        return true;
    } else if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        if (QEvent::KeyPress == event->type()) {
            // Let Shift+Escape be a global hook to defocus a widget (assign global player focus).
            auto keyEvent = static_cast<QKeyEvent*>(event);
            if (Qt::Key_Escape == keyEvent->key() && Qt::ShiftModifier == keyEvent->modifiers()) {
                m_player->setFocus();
                return true;
            }
        }
        QQuickWidget * focusedQuickWidget = qobject_cast<QQuickWidget*>(qApp->focusWidget());
        if (focusedQuickWidget && focusedQuickWidget->quickWindow()->activeFocusItem()) {
            event->accept();
            focusedQuickWidget->quickWindow()->sendEvent(focusedQuickWidget->quickWindow()->activeFocusItem(), event);
            QWidget * w = focusedQuickWidget->parentWidget();
            if (!event->isAccepted())
                qApp->sendEvent(w, event);
            return true;
        }
    }
    return QMainWindow::eventFilter(target, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // Simulate the player firing a dragStarted even to make the playlist close
    // its help text view. This lets one drop a clip directly into the playlist
    // from a fresh start.
    Mlt::GLWidget* videoWidget = (Mlt::GLWidget*) &Mlt::Controller::singleton();
    emit videoWidget->dragStarted();

    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasFormat("application/x-qabstractitemmodeldatalist")) {
        QByteArray encoded = mimeData->data("application/x-qabstractitemmodeldatalist");
        QDataStream stream(&encoded, QIODevice::ReadOnly);
        QMap<int,  QVariant> roleDataMap;
        while (!stream.atEnd()) {
            int row, col;
            stream >> row >> col >> roleDataMap;
        }
        if (roleDataMap.contains(Qt::ToolTipRole)) {
            // DisplayRole is just basename, ToolTipRole contains full path
            open(roleDataMap[Qt::ToolTipRole].toString());
            event->acceptProposedAction();
        }
    }
    else if (mimeData->hasUrls()) {
        openMultiple(mimeData->urls());
        event->acceptProposedAction();
    }
    else if (mimeData->hasFormat(Mlt::XmlMimeType )) {
        m_playlistDock->on_actionOpen_triggered();
        event->acceptProposedAction();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (continueJobsRunning() && continueModified()) {
        LOG_DEBUG() << "begin";
        JOBS.cleanup();
        writeSettings();
        if (m_exitCode == EXIT_SUCCESS) {
            MLT.stop();
        } else {
            if (multitrack())
                m_timelineDock->model()->close();
            if (playlist())
                m_playlistDock->model()->close();
            else
                onMultitrackClosed();
        }
        QThreadPool::globalInstance()->clear();
        AudioLevelsTask::closeAll();
        event->accept();
        emit aboutToShutDown();
        if (m_exitCode == EXIT_SUCCESS) {
            QApplication::quit();
            LOG_DEBUG() << "end";
            ::_Exit(0);
        } else {
            QApplication::exit(m_exitCode);
            LOG_DEBUG() << "end";
        }
        return;
    }
    event->ignore();
}

void MainWindow::showEvent(QShowEvent* event)
{
    // This is needed to prevent a crash on windows on startup when timeline
    // is visible and dock title bars are hidden.
    Q_UNUSED(event)
    ui->actionShowTitleBars->setChecked(Settings.showTitleBars());
    on_actionShowTitleBars_triggered(Settings.showTitleBars());
    ui->actionShowToolbar->setChecked(Settings.showToolBar());
    on_actionShowToolbar_triggered(Settings.showToolBar());
    ui->actionShowTextUnderIcons->setChecked(Settings.textUnderIcons());
    on_actionShowTextUnderIcons_toggled(Settings.textUnderIcons());
    ui->actionShowSmallIcons->setChecked(Settings.smallIcons());
    on_actionShowSmallIcons_toggled(Settings.smallIcons());

    windowHandle()->installEventFilter(this);
    Database::singleton(this);

#ifndef SHOTCUT_NOUPGRADE
    if (!Settings.noUpgrade() && !qApp->property("noupgrade").toBool())
        QTimer::singleShot(0, this, SLOT(showUpgradePrompt()));
#endif

#ifdef Q_OS_WIN
    WindowsTaskbarButton::getInstance().setParentWindow(this);
#endif
    onAutosaveTimeout();
}

void MainWindow::on_actionOpenOther_triggered()
{
    // these static are used to open dialog with previous configuration
    OpenOtherDialog dialog(this);

    if (MLT.producer())
        dialog.load(MLT.producer());
    if (dialog.exec() == QDialog::Accepted) {
        closeProducer();
        open(dialog.newProducer(MLT.profile()));
    }
}

void MainWindow::onProducerOpened(bool withReopen)
{
    QWidget* w = loadProducerWidget(MLT.producer());
    if (withReopen && w && !MLT.producer()->get(kMultitrackItemProperty)) {
        if (-1 != w->metaObject()->indexOfSignal("producerReopened()"))
            connect(w, SIGNAL(producerReopened()), m_player, SLOT(onProducerOpened()));
    }
    else if (MLT.isPlaylist()) {
        m_playlistDock->model()->load();
        if (playlist()) {
            m_isPlaylistLoaded = true;
            m_player->setIn(-1);
            m_player->setOut(-1);
            m_playlistDock->setVisible(true);
            m_playlistDock->raise();
            m_player->enableTab(Player::ProjectTabIndex);
            m_player->switchToTab(Player::ProjectTabIndex);
        }
    }
    else if (MLT.isMultitrack()) {
        m_timelineDock->blockSelection(true);
        m_timelineDock->model()->load();
        m_timelineDock->blockSelection(false);
        if (isMultitrackValid()) {
            m_player->setIn(-1);
            m_player->setOut(-1);
            m_timelineDock->setVisible(true);
            m_timelineDock->raise();
            m_player->enableTab(Player::ProjectTabIndex);
            m_player->switchToTab(Player::ProjectTabIndex);
            m_timelineDock->selectMultitrack();
            QTimer::singleShot(0, [=]() {
                m_timelineDock->setSelection();
            });
        }
    }
    if (MLT.isClip()) {
        m_player->enableTab(Player::SourceTabIndex);
        m_player->switchToTab(Player::SourceTabIndex);
        Util::getHash(*MLT.producer());
        ui->actionPaste->setEnabled(true);
    }
    QMutexLocker locker(&m_autosaveMutex);
    if (m_autosaveFile)
        setCurrentFile(m_autosaveFile->managedFileName());
    else if (!MLT.URL().isEmpty())
        setCurrentFile(MLT.URL());
    on_actionJack_triggered(ui->actionJack && ui->actionJack->isChecked());
}

void MainWindow::onProducerChanged()
{
    MLT.refreshConsumer();
    if (playlist() && MLT.producer() && MLT.producer()->is_valid()
        && MLT.producer()->get_int(kPlaylistIndexProperty))
        m_playlistDock->setUpdateButtonEnabled(true);
}

bool MainWindow::on_actionSave_triggered()
{
    if (m_currentFile.isEmpty()) {
        return on_actionSave_As_triggered();
    } else {
        if (Util::warnIfNotWritable(m_currentFile, this, tr("Save XML")))
            return false;
        bool success = saveXML(m_currentFile);
        QMutexLocker locker(&m_autosaveMutex);
        m_autosaveFile.reset(new AutoSaveFile(m_currentFile));
        setCurrentFile(m_currentFile);
        setWindowModified(false);
        if (success) {
            showStatusMessage(tr("Saved %1").arg(m_currentFile));
        } else {
            showSaveError();
        }
        m_undoStack->setClean();
        return true;
    }
}

bool MainWindow::on_actionSave_As_triggered()
{
    QString path = Settings.savePath();
    if (!m_currentFile.isEmpty())
        path = m_currentFile;
    QString caption = tr("Save XML");
    QString filename = QFileDialog::getSaveFileName(this, caption, path,
        tr("MLT XML (*.mlt)"), nullptr, Util::getFileDialogOptions());
    if (!filename.isEmpty()) {
        QFileInfo fi(filename);
        Settings.setSavePath(fi.path());
        if (fi.suffix() != "mlt")
            filename += ".mlt";

        if (Util::warnIfNotWritable(filename, this, caption))
            return false;
        newProject(filename);
    }
    return !filename.isEmpty();
}

bool MainWindow::continueModified()
{
    if (isWindowModified()) {
        QMessageBox dialog(QMessageBox::Warning,
                                     qApp->applicationName(),
                                     tr("The project has been modified.\n"
                                        "Do you want to save your changes?"),
                                     QMessageBox::No |
                                     QMessageBox::Cancel |
                                     QMessageBox::Yes,
                                     this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::Cancel);
        int r = dialog.exec();
        if (r == QMessageBox::Yes || r == QMessageBox::No) {
            if (r == QMessageBox::Yes) {
                return on_actionSave_triggered();
            } else {
                QMutexLocker locker(&m_autosaveMutex);
                m_autosaveFile.reset();
            }
        } else if (r == QMessageBox::Cancel) {
            return false;
        }
    }
    return true;
}

bool MainWindow::continueJobsRunning()
{
    if (JOBS.hasIncomplete()) {
        QMessageBox dialog(QMessageBox::Warning,
                                     qApp->applicationName(),
                                     tr("There are incomplete jobs.\n"
                                        "Do you want to still want to exit?"),
                                     QMessageBox::No |
                                     QMessageBox::Yes,
                                     this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        return (dialog.exec() == QMessageBox::Yes);
    }
    if (m_encodeDock->isExportInProgress()) {
        QMessageBox dialog(QMessageBox::Warning,
                                     qApp->applicationName(),
                                     tr("An export is in progress.\n"
                                        "Do you want to still want to exit?"),
                                     QMessageBox::No |
                                     QMessageBox::Yes,
                                     this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        return (dialog.exec() == QMessageBox::Yes);
    }
    return true;
}

QUndoStack* MainWindow::undoStack() const
{
    return m_undoStack;
}

void MainWindow::onEncodeTriggered(bool checked)
{
    if (checked) {
        m_encodeDock->show();
        m_encodeDock->raise();
    }
}

void MainWindow::onCaptureStateChanged(bool started)
{
    if (started && (MLT.resource().startsWith("x11grab:") ||
                    MLT.resource().startsWith("gdigrab:") ||
                    MLT.resource().startsWith("avfoundation"))
                && !MLT.producer()->get_int(kBackgroundCaptureProperty))
        showMinimized();
}

void MainWindow::onJobsDockTriggered(bool checked)
{
    if (checked) {
        m_jobsDock->show();
        m_jobsDock->raise();
    }
}

void MainWindow::onRecentDockTriggered(bool checked)
{
    if (checked) {
        m_recentDock->show();
        m_recentDock->raise();
    }
}

void MainWindow::onPropertiesDockTriggered(bool checked)
{
    if (checked) {
        m_propertiesDock->show();
        m_propertiesDock->raise();
    }
}

void MainWindow::onPlaylistDockTriggered(bool checked)
{
    if (checked) {
        m_playlistDock->show();
        m_playlistDock->raise();
    }
}

void MainWindow::onTimelineDockTriggered(bool checked)
{
    if (checked) {
        m_timelineDock->show();
        m_timelineDock->raise();
    }
}

void MainWindow::onHistoryDockTriggered(bool checked)
{
    if (checked) {
        m_historyDock->show();
        m_historyDock->raise();
    }
}

void MainWindow::onFiltersDockTriggered(bool checked)
{
    if (checked) {
        m_filtersDock->show();
        m_filtersDock->raise();
    }
}

void MainWindow::onKeyframesDockTriggered(bool checked)
{
    if (checked) {
        m_keyframesDock->show();
        m_keyframesDock->raise();
    }
}

void MainWindow::onPlaylistCreated()
{
    if (!playlist() || playlist()->count() == 0) return;
    m_player->enableTab(Player::ProjectTabIndex, true);
}

void MainWindow::onPlaylistLoaded()
{
    updateMarkers();
    m_player->enableTab(Player::ProjectTabIndex, true);
}

void MainWindow::onPlaylistCleared()
{
    m_player->onTabBarClicked(Player::SourceTabIndex);
    setWindowModified(true);
}

void MainWindow::onPlaylistClosed()
{
    closeProducer();
    setProfile(Settings.playerProfile());
    resetVideoModeMenu();
    setAudioChannels(Settings.playerAudioChannels());
    setCurrentFile("");
    setWindowModified(false);
    m_undoStack->clear();
    MLT.resetURL();
    QMutexLocker locker(&m_autosaveMutex);
    m_autosaveFile.reset(new AutoSaveFile(untitledFileName()));
    if (!isMultitrackValid())
        m_player->enableTab(Player::ProjectTabIndex, false);
}

void MainWindow::onPlaylistModified()
{
    setWindowModified(true);
    if (MLT.producer() && playlist() && (void*) MLT.producer()->get_producer() == (void*) playlist()->get_playlist())
        m_player->onDurationChanged();
    updateMarkers();
    m_player->enableTab(Player::ProjectTabIndex, true);
}

void MainWindow::onMultitrackCreated()
{
    m_player->enableTab(Player::ProjectTabIndex, true);
}

void MainWindow::onMultitrackClosed()
{
    setAudioChannels(Settings.playerAudioChannels());
    closeProducer();
    setProfile(Settings.playerProfile());
    resetVideoModeMenu();
    setCurrentFile("");
    setWindowModified(false);
    m_undoStack->clear();
    MLT.resetURL();
    QMutexLocker locker(&m_autosaveMutex);
    m_autosaveFile.reset(new AutoSaveFile(untitledFileName()));
    if (!playlist() || playlist()->count() == 0)
        m_player->enableTab(Player::ProjectTabIndex, false);
}

void MainWindow::onMultitrackModified()
{
    setWindowModified(true);

    // Reflect this playlist info onto the producer for keyframes dock.
    if (!m_timelineDock->selection().isEmpty()) {
        int trackIndex = m_timelineDock->selection().first().y();
        int clipIndex = m_timelineDock->selection().first().x();
        QScopedPointer<Mlt::ClipInfo> info(m_timelineDock->getClipInfo(trackIndex, clipIndex));
        if (info && info->producer && info->producer->is_valid()) {
            int expected = info->frame_in;
            QScopedPointer<Mlt::ClipInfo> info2(m_timelineDock->getClipInfo(trackIndex, clipIndex - 1));
            if (info2 && info2->producer && info2->producer->is_valid()
                      && info2->producer->get(kShotcutTransitionProperty)) {
                // Factor in a transition left of the clip.
                expected -= info2->frame_count;
                info->producer->set(kPlaylistStartProperty, info2->start);
            } else {
                info->producer->set(kPlaylistStartProperty, info->start);
            }
            if (expected != info->producer->get_int(kFilterInProperty)) {
                int delta = expected - info->producer->get_int(kFilterInProperty);
                info->producer->set(kFilterInProperty, expected);
                emit m_filtersDock->producerInChanged(delta);
            }
            expected = info->frame_out;
            info2.reset(m_timelineDock->getClipInfo(trackIndex, clipIndex + 1));
            if (info2 && info2->producer && info2->producer->is_valid()
                      && info2->producer->get(kShotcutTransitionProperty)) {
                // Factor in a transition right of the clip.
                expected += info2->frame_count;
            }
            if (expected != info->producer->get_int(kFilterOutProperty)) {
                int delta = expected - info->producer->get_int(kFilterOutProperty);
                info->producer->set(kFilterOutProperty, expected);
                emit m_filtersDock->producerOutChanged(delta);
            }
        }
    }
}

void MainWindow::onMultitrackDurationChanged()
{
    if (MLT.producer() && (void*) MLT.producer()->get_producer() == (void*) multitrack()->get_producer())
        m_player->onDurationChanged();
}

void MainWindow::onCutModified()
{
    if (!playlist() && !multitrack()) {
        setWindowModified(true);
    }
    if (playlist())
        m_playlistDock->setUpdateButtonEnabled(true);
}

void MainWindow::onProducerModified()
{
    setWindowModified(true);
}

void MainWindow::onFilterModelChanged()
{
    MLT.refreshConsumer();
    setWindowModified(true);
    if (playlist())
        m_playlistDock->setUpdateButtonEnabled(true);
}

void MainWindow::updateMarkers()
{
    if (playlist() && MLT.isPlaylist()) {
        QList<int> markers;
        int n = playlist()->count();
        for (int i = 0; i < n; i++)
            markers.append(playlist()->clip_start(i));
        m_player->setMarkers(markers);
    }
}

void MainWindow::updateThumbnails()
{
    if (Settings.playlistThumbnails() != "hidden")
        m_playlistDock->model()->refreshThumbnails();
}

void MainWindow::on_actionUndo_triggered()
{
    TimelineSelectionBlocker blocker(*m_timelineDock);
    m_undoStack->undo();
}

void MainWindow::on_actionRedo_triggered()
{
    TimelineSelectionBlocker blocker(*m_timelineDock);
    m_undoStack->redo();
}

void MainWindow::on_actionFAQ_triggered()
{
    QDesktopServices::openUrl(QUrl("https://www.shotcut.org/FAQ/"));
}

void MainWindow::on_actionForum_triggered()
{
    QDesktopServices::openUrl(QUrl("https://forum.shotcut.org/"));
}

bool MainWindow::saveXML(const QString &filename, bool withRelativePaths)
{
    bool result;
    if (m_timelineDock->model()->rowCount() > 0) {
        result = MLT.saveXML(filename, multitrack(), withRelativePaths);
    } else if (m_playlistDock->model()->rowCount() > 0) {
        int in = MLT.producer()->get_in();
        int out = MLT.producer()->get_out();
        MLT.producer()->set_in_and_out(0, MLT.producer()->get_length() - 1);
        result = MLT.saveXML(filename, playlist(), withRelativePaths);
        MLT.producer()->set_in_and_out(in, out);
    } else if (MLT.producer()) {
        result = MLT.saveXML(filename, (MLT.isMultitrack() || MLT.isPlaylist())? MLT.savedProducer() : 0, withRelativePaths);
    } else {
        // Save an empty playlist, which is accepted by both MLT and Shotcut.
        Mlt::Playlist playlist(MLT.profile());
        result = MLT.saveXML(filename, &playlist, withRelativePaths);
    }
    return result;
}

void MainWindow::changeTheme(const QString &theme)
{
    LOG_DEBUG() << "begin";
    if (theme == "dark") {
        QApplication::setStyle("Fusion");
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(50,50,50));
        palette.setColor(QPalette::WindowText, QColor(220,220,220));
        palette.setColor(QPalette::Base, QColor(30,30,30));
        palette.setColor(QPalette::AlternateBase, QColor(40,40,40));
        palette.setColor(QPalette::Highlight, QColor(23,92,118));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Highlight));
        palette.setColor(QPalette::ToolTipText, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Button, palette.color(QPalette::Window));
        palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Link, palette.color(QPalette::Highlight).lighter());
        palette.setColor(QPalette::LinkVisited, palette.color(QPalette::Highlight));
        palette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);
        QApplication::setPalette(palette);
        QIcon::setThemeName("dark");
        QMetaObject::invokeMethod(&MAIN, "on_actionShowTextUnderIcons_toggled", Qt::QueuedConnection, Q_ARG(bool, Settings.textUnderIcons()));
    } else if (theme == "light") {
        QStyle* style = QStyleFactory::create("Fusion");
        QApplication::setStyle(style);
        QApplication::setPalette(style->standardPalette());
        QIcon::setThemeName("light");
        QMetaObject::invokeMethod(&MAIN, "on_actionShowTextUnderIcons_toggled", Qt::QueuedConnection, Q_ARG(bool, Settings.textUnderIcons()));
    } else {
        QApplication::setStyle(qApp->property("system-style").toString());
        QIcon::setThemeName("oxygen");
    }
    emit QmlApplication::singleton().paletteChanged();
    LOG_DEBUG() << "end";
}

Mlt::Playlist* MainWindow::playlist() const
{
    return m_playlistDock->model()->playlist();
}

bool MainWindow::isPlaylistValid() const
{
    return m_playlistDock->model()->playlist()
        && m_playlistDock->model()->rowCount() > 0;
}

Mlt::Producer *MainWindow::multitrack() const
{
    return m_timelineDock->model()->tractor();
}

bool MainWindow::isMultitrackValid() const
{
    return m_timelineDock->model()->tractor()
       && !m_timelineDock->model()->trackList().empty();
}

QWidget *MainWindow::loadProducerWidget(Mlt::Producer* producer)
{
    QWidget* w = 0;
    QScrollArea* scrollArea = (QScrollArea*) m_propertiesDock->widget();

    if (!producer || !producer->is_valid()) {
        if (scrollArea->widget())
            scrollArea->widget()->deleteLater();
        return  w;
    } else {
        scrollArea->show();
    }

    QString service(producer->get("mlt_service"));
    QString resource = QString::fromUtf8(producer->get("resource"));
    QString shotcutProducer(producer->get(kShotcutProducerProperty));

    if (resource.startsWith("video4linux2:") || QString::fromUtf8(producer->get("resource1")).startsWith("video4linux2:"))
        w = new Video4LinuxWidget(this);
    else if (resource.startsWith("pulse:"))
        w = new PulseAudioWidget(this);
    else if (resource.startsWith("jack:"))
        w = new JackProducerWidget(this);
    else if (resource.startsWith("alsa:"))
        w = new AlsaWidget(this);
    else if (resource.startsWith("dshow:") || QString::fromUtf8(producer->get("resource1")).startsWith("dshow:"))
        w = new DirectShowVideoWidget(this);
    else if (resource.startsWith("avfoundation:"))
        w = new AvfoundationProducerWidget(this);
    else if (resource.startsWith("x11grab:"))
        w = new X11grabWidget(this);
    else if (resource.startsWith("gdigrab:"))
        w = new GDIgrabWidget(this);
    else if (service.startsWith("avformat") || shotcutProducer == "avformat")
        w = new AvformatProducerWidget(this);
    else if (MLT.isImageProducer(producer)) {
        w = new ImageProducerWidget(this);
        connect(m_player, SIGNAL(outChanged(int)), w, SLOT(updateDuration()));
    }
    else if (service == "decklink" || resource.contains("decklink"))
        w = new DecklinkProducerWidget(this);
    else if (service == "color")
        w = new ColorProducerWidget(this);
    else if (service == "noise")
        w = new NoiseWidget(this);
    else if (service == "frei0r.ising0r")
        w = new IsingWidget(this);
    else if (service == "frei0r.lissajous0r")
        w = new LissajousWidget(this);
    else if (service == "frei0r.plasma")
        w = new PlasmaWidget(this);
    else if (service == "frei0r.test_pat_B")
        w = new ColorBarsWidget(this);
    else if (service == "tone")
        w = new ToneProducerWidget(this);
    else if (service == "count")
        w = new CountProducerWidget(this);
    else if (service == "blipflash")
        w = new BlipProducerWidget(this);
    else if (producer->parent().get(kShotcutTransitionProperty)) {
        w = new LumaMixTransition(producer->parent(), this);
        scrollArea->setWidget(w);
        if (-1 != w->metaObject()->indexOfSignal("modified()"))
            connect(w, SIGNAL(modified()), SLOT(onProducerModified()));
        return w;
    } else if (playlist_type == producer->type()) {
        int trackIndex = m_timelineDock->currentTrack();
        bool isBottomVideo = m_timelineDock->model()->data(m_timelineDock->model()->index(trackIndex), MultitrackModel::IsBottomVideoRole).toBool();
        if (!isBottomVideo) {
            w = new TrackPropertiesWidget(*producer, this);
            scrollArea->setWidget(w);
            return w;
        }
    } else if (tractor_type == producer->type()) {
        w = new TimelinePropertiesWidget(*producer, this);
        scrollArea->setWidget(w);
        return w;
    }
    if (w) {
        dynamic_cast<AbstractProducerWidget*>(w)->setProducer(producer);
        if (-1 != w->metaObject()->indexOfSignal("producerChanged(Mlt::Producer*)")) {
            connect(w, SIGNAL(producerChanged(Mlt::Producer*)), SLOT(onProducerChanged()));
            connect(w, SIGNAL(producerChanged(Mlt::Producer*)), m_filterController, SLOT(setProducer(Mlt::Producer*)));
            connect(w, SIGNAL(producerChanged(Mlt::Producer*)), m_playlistDock, SLOT(onProducerChanged(Mlt::Producer*)));
            if (producer->get(kMultitrackItemProperty))
                connect(w, SIGNAL(producerChanged(Mlt::Producer*)), m_timelineDock, SLOT(onProducerChanged(Mlt::Producer*)));
        }
        if (-1 != w->metaObject()->indexOfSignal("modified()")) {
            connect(w, SIGNAL(modified()), SLOT(onProducerModified()));
            connect(w, SIGNAL(modified()), m_playlistDock, SLOT(onProducerModified()));
            connect(w, SIGNAL(modified()), m_timelineDock, SLOT(onProducerModified()));
            connect(w, SIGNAL(modified()), m_keyframesDock, SLOT(onProducerModified()));
            connect(w, SIGNAL(modified()), m_filterController, SLOT(onProducerChanged()));
        }
        if (-1 != w->metaObject()->indexOfSlot("updateDuration()")) {
            connect(m_timelineDock, SIGNAL(durationChanged()), w, SLOT(updateDuration()));
        }
        if (-1 != w->metaObject()->indexOfSlot("rename()")) {
            connect(this, SIGNAL(renameRequested()), w, SLOT(rename()));
        }
        scrollArea->setWidget(w);
        onProducerChanged();
    } else if (scrollArea->widget()) {
        scrollArea->widget()->deleteLater();
    }
    return w;
}

void MainWindow::on_actionEnter_Full_Screen_triggered()
{
#ifdef Q_OS_WIN
    bool isFull = isMaximized();
#else
    bool isFull = isFullScreen();
#endif
    if (isFull) {
        showNormal();
        ui->actionEnter_Full_Screen->setText(tr("Enter Full Screen"));
    } else {
#ifdef Q_OS_WIN
        showMaximized();
#else
        showFullScreen();
#endif
        ui->actionEnter_Full_Screen->setText(tr("Exit Full Screen"));
    }
}

void MainWindow::onGpuNotSupported()
{
    Settings.setPlayerGPU(false);
    if (ui->actionGPU) {
        ui->actionGPU->setChecked(false);
        ui->actionGPU->setDisabled(true);
    }
    LOG_WARNING() << "";
    QMessageBox::critical(this, qApp->applicationName(),
        tr("GPU effects are not supported"));
}

void MainWindow::stepLeftOneFrame()
{
    m_player->seek(m_player->position() - 1);
}

void MainWindow::stepRightOneFrame()
{
    m_player->seek(m_player->position() + 1);
}

void MainWindow::stepLeftOneSecond()
{
    stepLeftBySeconds(-1);
}

void MainWindow::stepRightOneSecond()
{
    stepLeftBySeconds(1);
}

void MainWindow::setInToCurrent(bool ripple)
{
    if (m_player->tabIndex() == Player::ProjectTabIndex && isMultitrackValid()) {
        m_timelineDock->trimClipAtPlayhead(TimelineDock::TrimInPoint, ripple);
    } else if (MLT.isSeekable() && MLT.isClip()) {
        m_player->setIn(m_player->position());
        int delta = m_player->position() - MLT.producer()->get_in();
        emit m_player->inChanged(delta);
    }
}

void MainWindow::setOutToCurrent(bool ripple)
{
    if (m_player->tabIndex() == Player::ProjectTabIndex && isMultitrackValid()) {
        m_timelineDock->trimClipAtPlayhead(TimelineDock::TrimOutPoint, ripple);
    } else if (MLT.isSeekable() && MLT.isClip()) {
        m_player->setOut(m_player->position());
        int delta = m_player->position() - MLT.producer()->get_out();
        emit m_player->outChanged(delta);
    }
}

void MainWindow::onShuttle(float x)
{
    if (x == 0) {
        m_player->pause();
    } else if (x > 0) {
        m_player->play(10.0 * x);
    } else {
        m_player->play(20.0 * x);
    }
}

void MainWindow::showUpgradePrompt()
{
    if (Settings.checkUpgradeAutomatic()) {
        showStatusMessage("Checking for upgrade...");
        m_network.get(QNetworkRequest(QUrl("https://check.shotcut.org/version.json")));
    } else {
        QAction* action = new QAction(tr("Click here to check for a new version of Shotcut."), 0);
        connect(action, SIGNAL(triggered(bool)), SLOT(on_actionUpgrade_triggered()));
        showStatusMessage(action, 15 /* seconds */);
    }
}

void MainWindow::on_actionRealtime_triggered(bool checked)
{
    Settings.setPlayerRealtime(checked);
    if (Settings.playerGPU())
        MLT.pause();
    if (MLT.consumer()) {
        MLT.restart();
    }

}

void MainWindow::on_actionProgressive_triggered(bool checked)
{
    MLT.videoWidget()->setProperty("progressive", checked);
    if (Settings.playerGPU())
        MLT.pause();
    if (MLT.consumer()) {
        MLT.profile().set_progressive(checked);
        MLT.updatePreviewProfile();
        MLT.restart();
    }
    Settings.setPlayerProgressive(checked);
}

void MainWindow::changeAudioChannels(bool checked, int channels)
{
    if( checked ) {
        Settings.setPlayerAudioChannels(channels);
        setAudioChannels(Settings.playerAudioChannels());
    }
}

void MainWindow::on_actionChannels1_triggered(bool checked)
{
    changeAudioChannels(checked, 1);
}

void MainWindow::on_actionChannels2_triggered(bool checked)
{
    changeAudioChannels(checked, 2);
}

void MainWindow::on_actionChannels6_triggered(bool checked)
{
    changeAudioChannels(checked, 6);
}

void MainWindow::changeDeinterlacer(bool checked, const char* method)
{
    if (checked) {
        MLT.videoWidget()->setProperty("deinterlace_method", method);
        if (MLT.consumer()) {
            MLT.consumer()->set("deinterlace_method", method);
            MLT.refreshConsumer();
        }
    }
    Settings.setPlayerDeinterlacer(method);
}

void MainWindow::on_actionOneField_triggered(bool checked)
{
    changeDeinterlacer(checked, "onefield");
}

void MainWindow::on_actionLinearBlend_triggered(bool checked)
{
    changeDeinterlacer(checked, "linearblend");
}

void MainWindow::on_actionYadifTemporal_triggered(bool checked)
{
    changeDeinterlacer(checked, "yadif-nospatial");
}

void MainWindow::on_actionYadifSpatial_triggered(bool checked)
{
    changeDeinterlacer(checked, "yadif");
}

void MainWindow::changeInterpolation(bool checked, const char* method)
{
    if (checked) {
        MLT.videoWidget()->setProperty("rescale", method);
        if (MLT.consumer()) {
            MLT.consumer()->set("rescale", method);
            MLT.refreshConsumer();
        }
    }
    Settings.setPlayerInterpolation(method);
}

void MainWindow::processMultipleFiles()
{
    if (m_multipleFiles.length() <= 0)
        return;
    QStringList multipleFiles = m_multipleFiles;
    m_multipleFiles.clear();
    int count = multipleFiles.length();
    if (count > 1) {
        LongUiTask longTask(tr("Open Files"));
        m_playlistDock->show();
        m_playlistDock->raise();
        for (int i = 0; i < count; i++) {
            QString filename = multipleFiles.takeFirst();
            LOG_DEBUG() << filename;
            longTask.reportProgress(QFileInfo(filename).fileName(), i, count);
            Mlt::Producer p(MLT.profile(), filename.toUtf8().constData());
            if (p.is_valid()) {
                // Convert avformat to avformat-novalidate so that XML loads faster.
                if (!qstrcmp(p.get("mlt_service"), "avformat")) {
                    p.set("mlt_service", "avformat-novalidate");
                    p.set("mute_on_pause", 0);
                }
                if (QDir::toNativeSeparators(filename) == QDir::toNativeSeparators(MAIN.fileName())) {
                    MAIN.showStatusMessage(QObject::tr("You cannot add a project to itself!"));
                    continue;
                }
                MLT.setImageDurationFromDefault(&p);
                MLT.lockCreationTime(&p);
                p.get_length_time(mlt_time_clock);
                Util::getHash(p);
                ProxyManager::generateIfNotExists(p);
                undoStack()->push(new Playlist::AppendCommand(*m_playlistDock->model(), MLT.XML(&p), false));
                m_recentDock->add(filename.toUtf8().constData());
            }
        }
        emit m_playlistDock->model()->modified();
    }
    if (m_isPlaylistLoaded && Settings.playerGPU()) {
        updateThumbnails();
        m_isPlaylistLoaded = false;
    }
}

void MainWindow::onLanguageTriggered(QAction* action)
{
    Settings.setLanguage(action->data().toString());
    QMessageBox dialog(QMessageBox::Information,
                       qApp->applicationName(),
                       tr("You must restart Shotcut to switch to the new language.\n"
                          "Do you want to restart now?"),
                       QMessageBox::No | QMessageBox::Yes,
                       this);
    dialog.setDefaultButton(QMessageBox::Yes);
    dialog.setEscapeButton(QMessageBox::No);
    dialog.setWindowModality(QmlApplication::dialogModality());
    if (dialog.exec() == QMessageBox::Yes) {
        m_exitCode = EXIT_RESTART;
        QApplication::closeAllWindows();
    }
}

void MainWindow::on_actionNearest_triggered(bool checked)
{
    changeInterpolation(checked, "nearest");
}

void MainWindow::on_actionBilinear_triggered(bool checked)
{
    changeInterpolation(checked, "bilinear");
}

void MainWindow::on_actionBicubic_triggered(bool checked)
{
    changeInterpolation(checked, "bicubic");
}

void MainWindow::on_actionHyper_triggered(bool checked)
{
    changeInterpolation(checked, "hyper");
}

void MainWindow::on_actionJack_triggered(bool checked)
{
    Settings.setPlayerJACK(checked);
    if (!MLT.enableJack(checked)) {
        if (ui->actionJack)
            ui->actionJack->setChecked(false);
        Settings.setPlayerJACK(false);
        QMessageBox::warning(this, qApp->applicationName(),
            tr("Failed to connect to JACK.\nPlease verify that JACK is installed and running."));
    }
}

void MainWindow::on_actionGPU_triggered(bool checked)
{
    if (checked) {
        QMessageBox dialog(QMessageBox::Warning,
                           qApp->applicationName(),
                           tr("GPU effects are experimental and may cause instability on some systems. "
                              "Some CPU effects are incompatible with GPU effects and will be disabled. "
                              "A project created with GPU effects can not be converted to a CPU only project later."
                              "\n\n"
                              "Do you want to enable GPU effects and restart Shotcut?"),
                           QMessageBox::No | QMessageBox::Yes,
                           this);
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        dialog.setWindowModality(QmlApplication::dialogModality());
        if (dialog.exec() == QMessageBox::Yes) {
            m_exitCode = EXIT_RESTART;
            QApplication::closeAllWindows();
        }
        else {
            ui->actionGPU->setChecked(false);
        }
    }
    else
    {
        QMessageBox dialog(QMessageBox::Information,
                           qApp->applicationName(),
                           tr("Shotcut must restart to disable GPU effects."
                              "\n\n"
                              "Disable GPU effects and restart?"),
                           QMessageBox::No | QMessageBox::Yes,
                           this);
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        dialog.setWindowModality(QmlApplication::dialogModality());
        if (dialog.exec() == QMessageBox::Yes) {
            m_exitCode = EXIT_RESTART;
            QApplication::closeAllWindows();
        }
        else {
            ui->actionGPU->setChecked(true);
        }
    }
}

void MainWindow::onExternalTriggered(QAction *action)
{
    LOG_DEBUG() << action->data().toString();
    bool isExternal = !action->data().toString().isEmpty();
    Settings.setPlayerExternal(action->data().toString());
    MLT.stop();
    bool ok = false;
    int screen = action->data().toInt(&ok);
    if (ok || action->data().toString().isEmpty()) {
        m_player->moveVideoToScreen(ok? screen : -2);
        isExternal = false;
        MLT.videoWidget()->setProperty("mlt_service", QVariant());
    } else {
        m_player->moveVideoToScreen(-2);
        MLT.videoWidget()->setProperty("mlt_service", action->data());
    }

    QString profile = Settings.playerProfile();
    // Automatic not permitted for SDI/HDMI
    if (isExternal && profile.isEmpty()) {
        profile = "atsc_720p_50";
        Settings.setPlayerProfile(profile);
        setProfile(profile);
        MLT.restart();
        foreach (QAction* a, m_profileGroup->actions()) {
            if (a->data() == profile) {
                a->setChecked(true);
                break;
            }
        }
    }
    else {
        MLT.consumerChanged();
    }
    // Automatic not permitted for SDI/HDMI
    m_profileGroup->actions().at(0)->setEnabled(!isExternal);

    // Disable progressive option when SDI/HDMI
    ui->actionProgressive->setEnabled(!isExternal);
    bool isProgressive = isExternal
            ? MLT.profile().progressive()
            : ui->actionProgressive->isChecked();
    MLT.videoWidget()->setProperty("progressive", isProgressive);
    if (MLT.consumer()) {
        MLT.consumer()->set("progressive", isProgressive);
        MLT.restart();
    }
    if (m_keyerMenu)
        m_keyerMenu->setEnabled(action->data().toString().startsWith("decklink"));

    // Preview scaling not permitted for SDI/HDMI
    if (isExternal) {
        setPreviewScale(0);
        m_previewScaleGroup->setEnabled(false);
    } else {
        setPreviewScale(Settings.playerPreviewScale());
        m_previewScaleGroup->setEnabled(true);
    }
}

void MainWindow::onKeyerTriggered(QAction *action)
{
    LOG_DEBUG() << action->data().toString();
    MLT.videoWidget()->setProperty("keyer", action->data());
    MLT.consumerChanged();
    Settings.setPlayerKeyerMode(action->data().toInt());
}

void MainWindow::onProfileTriggered(QAction *action)
{
    Settings.setPlayerProfile(action->data().toString());
    if (MLT.producer() && MLT.producer()->is_valid()) {
        // Save the XML to get correct in/out points before profile is changed.
        QString xml = MLT.XML();
        setProfile(action->data().toString());
        MLT.restart(xml);
        emit producerOpened(false);
    } else {
        setProfile(action->data().toString());
    }
}

void MainWindow::onProfileChanged()
{
    if (multitrack() && MLT.isMultitrack() &&
       (m_timelineDock->selection().isEmpty() || m_timelineDock->currentTrack() == -1)) {
        emit m_timelineDock->selected(multitrack());
    }
}

void MainWindow::on_actionAddCustomProfile_triggered()
{
    QString xml;
    if (MLT.producer() && MLT.producer()->is_valid()) {
        // Save the XML to get correct in/out points before profile is changed.
        xml = MLT.XML();
    }
    CustomProfileDialog dialog(this);
    dialog.setWindowModality(QmlApplication::dialogModality());
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.profileName();
        if (!name.isEmpty()) {
            addCustomProfile(name, customProfileMenu(), actionProfileRemove(), profileGroup());
        } else if (m_profileGroup->checkedAction()) {
            m_profileGroup->checkedAction()->setChecked(false);
        }
        // Use the new profile.
        emit profileChanged();
        if (!xml.isEmpty()) {
            MLT.restart(xml);
            emit producerOpened(false);
        }
    }
}

void MainWindow::on_actionSystemTheme_triggered()
{
    changeTheme("system");
    QApplication::setPalette(QApplication::style()->standardPalette());
    Settings.setTheme("system");
}

void MainWindow::on_actionFusionDark_triggered()
{
    changeTheme("dark");
    Settings.setTheme("dark");
    ui->mainToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
}

void MainWindow::on_actionFusionLight_triggered()
{
    changeTheme("light");
    Settings.setTheme("light");
    ui->mainToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
}

void MainWindow::on_actionTutorials_triggered()
{
    QDesktopServices::openUrl(QUrl("https://www.shotcut.org/tutorials/"));
}

void MainWindow::on_actionRestoreLayout_triggered()
{
    auto mode = Settings.layoutMode();
    if (mode != LayoutMode::Custom) {
        // Clear the saved layout for this mode
        Settings.setLayout(QString(kReservedLayoutPrefix).arg(mode), QByteArray(), QByteArray());
        // Reset the layout mode so the current layout is saved as custom when trigger action
        Settings.setLayoutMode();
    }
    switch (mode) {
    case LayoutMode::Custom:
        ui->actionLayoutEditing->setChecked(true);
        Q_FALLTHROUGH();
    case LayoutMode::Editing:
        on_actionLayoutEditing_triggered();
        break;
    case LayoutMode::Logging:
        on_actionLayoutLogging_triggered();
        break;
    case LayoutMode::Effects:
        on_actionLayoutEffects_triggered();
        break;
    case LayoutMode::Color:
        on_actionLayoutColor_triggered();
        break;
    case LayoutMode::Audio:
        on_actionLayoutAudio_triggered();
        break;
    case LayoutMode::PlayerOnly:
        on_actionLayoutPlayer_triggered();
        break;
    }
}

void MainWindow::on_actionShowTitleBars_triggered(bool checked)
{
    QList <QDockWidget *> docks = findChildren<QDockWidget *>();
    for (int i = 0; i < docks.count(); i++) {
        QDockWidget* dock = docks.at(i);
        if (checked) {
            dock->setTitleBarWidget(0);
        } else {
            if (!dock->isFloating()) {
                dock->setTitleBarWidget(new QWidget);
            }
        }
    }
    Settings.setShowTitleBars(checked);
}

void MainWindow::on_actionShowToolbar_triggered(bool checked)
{
    ui->mainToolBar->setVisible(checked);
}

void MainWindow::onToolbarVisibilityChanged(bool visible)
{
    ui->actionShowToolbar->setChecked(visible);
    Settings.setShowToolBar(visible);
}

void MainWindow::on_menuExternal_aboutToShow()
{
    foreach (QAction* action, m_externalGroup->actions()) {
        bool ok = false;
        int i = action->data().toInt(&ok);
        if (ok) {
            if (i == QApplication::desktop()->screenNumber(this)) {
                if (action->isChecked()) {
                    m_externalGroup->actions().first()->setChecked(true);
                    Settings.setPlayerExternal(QString());
                }
                action->setDisabled(true);
            }  else {
                action->setEnabled(true);
            }
        }
    }
}

void MainWindow::on_actionUpgrade_triggered()
{
    if (Settings.askUpgradeAutomatic()) {
        QMessageBox dialog(QMessageBox::Question,
           qApp->applicationName(),
           tr("Do you want to automatically check for updates in the future?"),
           QMessageBox::No |
           QMessageBox::Yes,
           this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        dialog.setCheckBox(new QCheckBox(tr("Do not show this anymore.", "Automatic upgrade check dialog")));
        Settings.setCheckUpgradeAutomatic(dialog.exec() == QMessageBox::Yes);
        if (dialog.checkBox()->isChecked())
            Settings.setAskUpgradeAutomatic(false);
    }
    showStatusMessage("Checking for upgrade...");
    m_network.get(QNetworkRequest(QUrl("https://check.shotcut.org/version.json")));
}

void MainWindow::on_actionOpenXML_triggered()
{
    QString path = Settings.openPath();
#ifdef Q_OS_MAC
    path.append("/*");
#endif
    QStringList filenames = QFileDialog::getOpenFileNames(this, tr("Open File"), path,
        tr("MLT XML (*.mlt);;All Files (*)"), nullptr, Util::getFileDialogOptions());
    if (filenames.length() > 0) {
        QString url = filenames.first();
        MltXmlChecker checker;
        if (checker.check(url)) {
            if (!isCompatibleWithGpuMode(checker))
                return;
            isXmlRepaired(checker, url);
            // Check if the locale usage differs.
            // Get current locale.
            QString localeName = QString(::setlocale(MLT_LC_CATEGORY, nullptr)).toUpper();
            // Test if it is C or POSIX.
            bool currentlyUsingLocale = (localeName != "" && localeName != "C" && localeName != "POSIX");
            if (currentlyUsingLocale != checker.usesLocale()) {
                // Show a warning dialog and cancel if requested.
                QMessageBox dialog(QMessageBox::Question,
                   qApp->applicationName(),
                   tr("The decimal point of the MLT XML file\nyou want to open is incompatible.\n\n"
                      "Do you want to continue to open this MLT XML file?"),
                   QMessageBox::No |
                   QMessageBox::Yes,
                   this);
                dialog.setWindowModality(QmlApplication::dialogModality());
                dialog.setDefaultButton(QMessageBox::No);
                dialog.setEscapeButton(QMessageBox::No);
                if (dialog.exec() != QMessageBox::Yes)
                    return;
            }
        }
        Settings.setOpenPath(QFileInfo(url).path());
        activateWindow();
        if (filenames.length() > 1)
            m_multipleFiles = filenames;
        if (!MLT.openXML(url)) {
            open(MLT.producer());
            m_recentDock->add(url);
            LOG_INFO() << url;
        }
        else {
            showStatusMessage(tr("Failed to open ") + url);
            emit openFailed(url);
        }
    }
}

void MainWindow::on_actionGammaSRGB_triggered(bool checked)
{
    Q_UNUSED(checked)
    Settings.setPlayerGamma("iec61966_2_1");
    MLT.restart();
    MLT.refreshConsumer();
}

void MainWindow::on_actionGammaRec709_triggered(bool checked)
{
    Q_UNUSED(checked)
    Settings.setPlayerGamma("bt709");
    MLT.restart();
    MLT.refreshConsumer();
}

void MainWindow::onFocusChanged(QWidget *, QWidget * ) const
{
    LOG_DEBUG() << "Focuswidget changed";
    LOG_DEBUG() << "Current focusWidget:" << QApplication::focusWidget();
    LOG_DEBUG() << "Current focusObject:" << QApplication::focusObject();
    LOG_DEBUG() << "Current focusWindow:" << QApplication::focusWindow();
}

void MainWindow::on_actionScrubAudio_triggered(bool checked)
{
    Settings.setPlayerScrubAudio(checked);
}

#if !defined(Q_OS_MAC)
void MainWindow::onDrawingMethodTriggered(QAction *action)
{
    Settings.setDrawMethod(action->data().toInt());
    QMessageBox dialog(QMessageBox::Information,
                       qApp->applicationName(),
                       tr("You must restart Shotcut to change the display method.\n"
                          "Do you want to restart now?"),
                       QMessageBox::No | QMessageBox::Yes,
                       this);
    dialog.setDefaultButton(QMessageBox::Yes);
    dialog.setEscapeButton(QMessageBox::No);
    dialog.setWindowModality(QmlApplication::dialogModality());
    if (dialog.exec() == QMessageBox::Yes) {
        m_exitCode = EXIT_RESTART;
        QApplication::closeAllWindows();
    }
}
#endif

void MainWindow::on_actionApplicationLog_triggered()
{
    TextViewerDialog dialog(this);
    QDir dir = Settings.appDataLocation();
    QFile logFile(dir.filePath("shotcut-log.txt"));
    logFile.open(QIODevice::ReadOnly | QIODevice::Text);
    dialog.setText(logFile.readAll());
    logFile.close();
    dialog.setWindowTitle(tr("Application Log"));
    dialog.exec();
}

void MainWindow::on_actionClose_triggered()
{
    if (continueModified()) {
        LOG_DEBUG() << "";
        MLT.setProjectFolder(QString());
        MLT.stop();
        if (multitrack())
            m_timelineDock->model()->close();
        if (playlist())
            m_playlistDock->model()->close();
        else
            onMultitrackClosed();
        m_player->enableTab(Player::SourceTabIndex, false);
        MLT.purgeMemoryPool();
        MLT.resetLocale();
    }
}

void MainWindow::onPlayerTabIndexChanged(int index)
{
    if (Player::SourceTabIndex == index)
        m_timelineDock->saveAndClearSelection();
    else
        m_timelineDock->restoreSelection();
}

void MainWindow::onUpgradeCheckFinished(QNetworkReply* reply)
{
    if (!reply->error()) {
        QByteArray response = reply->readAll();
        LOG_DEBUG() << "response: " << response;
        QJsonDocument json = QJsonDocument::fromJson(response);
        QString current = qApp->applicationVersion();

        if (!json.isNull() && json.object().value("version_string").type() == QJsonValue::String) {
            QString latest = json.object().value("version_string").toString();
            if (current != "adhoc" && QVersionNumber::fromString(current) < QVersionNumber::fromString(latest)) {
                QAction* action = new QAction(tr("Shotcut version %1 is available! Click here to get it.").arg(latest), 0);
                connect(action, SIGNAL(triggered(bool)), SLOT(onUpgradeTriggered()));
                if (!json.object().value("url").isUndefined())
                    m_upgradeUrl = json.object().value("url").toString();
                showStatusMessage(action, 15 /* seconds */);
            } else {
                showStatusMessage(tr("You are running the latest version of Shotcut."));
            }
            reply->deleteLater();
            return;
        } else {
            LOG_WARNING() << "failed to parse version.json";
        }
    } else {
        LOG_WARNING() << reply->errorString();
    }
    QAction* action = new QAction(tr("Failed to read version.json when checking. Click here to go to the Web site."), 0);
    connect(action, SIGNAL(triggered(bool)), SLOT(onUpgradeTriggered()));
    showStatusMessage(action);
    reply->deleteLater();
}

void MainWindow::onUpgradeTriggered()
{
    QDesktopServices::openUrl(QUrl(m_upgradeUrl));
}

void MainWindow::onTimelineSelectionChanged()
{
    bool enable = (m_timelineDock->selection().size() > 0);
    ui->actionCut->setEnabled(enable);
    ui->actionCopy->setEnabled(enable);
}

void MainWindow::on_actionCut_triggered()
{
    m_timelineDock->show();
    m_timelineDock->raise();
    m_timelineDock->removeSelection(true);
}

void MainWindow::on_actionCopy_triggered()
{
    m_timelineDock->show();
    m_timelineDock->raise();
    if (!m_timelineDock->selection().isEmpty())
        m_timelineDock->copyClip(m_timelineDock->selection().first().y(), m_timelineDock->selection().first().x());
}

void MainWindow::on_actionPaste_triggered()
{
    m_timelineDock->show();
    m_timelineDock->raise();
    m_timelineDock->insert(-1);
}

void MainWindow::onClipCopied()
{
    m_player->enableTab(Player::SourceTabIndex);
}

void MainWindow::on_actionExportEDL_triggered()
{
    // Dialog to get export file name.
    QString path = Settings.savePath();
    QString caption = tr("Export EDL");
    QString saveFileName = QFileDialog::getSaveFileName(this, caption, path,
        tr("EDL (*.edl);;All Files (*)"), nullptr, Util::getFileDialogOptions());
    if (!saveFileName.isEmpty()) {
        QFileInfo fi(saveFileName);
        if (fi.suffix() != "edl")
            saveFileName += ".edl";

        if (Util::warnIfNotWritable(saveFileName, this, caption))
            return;

        // Locate the JavaScript file in the filesystem.
        QDir qmlDir = QmlUtilities::qmlDir();
        qmlDir.cd("export-edl");
        QString jsFileName = qmlDir.absoluteFilePath("export-edl.js");
        QFile scriptFile(jsFileName);
        if (scriptFile.open(QIODevice::ReadOnly)) {
            // Read JavaScript into a string.
            QTextStream stream(&scriptFile);
            stream.setCodec("UTF-8");
            stream.setAutoDetectUnicode(true);
            QString contents = stream.readAll();
            scriptFile.close();

            // Evaluate JavaScript.
            QJSEngine jsEngine;
            QJSValue result = jsEngine.evaluate(contents, jsFileName);
            if (!result.isError()) {
                // Call the JavaScript main function.
                QJSValue options = jsEngine.newObject();
                options.setProperty("useBaseNameForReelName", true);
                options.setProperty("useBaseNameForClipComment", true);
                options.setProperty("channelsAV", "AA/V");
                QJSValueList args;
                args << MLT.XML(0, true, true) << options;
                result = result.call(args);
                if (!result.isError()) {
                    // Save the result with the export file name.
                    QFile f(saveFileName);
                    f.open(QIODevice::WriteOnly | QIODevice::Text);
                    f.write(result.toString().toLatin1());
                    f.close();
                }
            }
            if (result.isError()) {
                LOG_ERROR() << "Uncaught exception at line"
                            << result.property("lineNumber").toInt()
                            << ":" << result.toString();
                showStatusMessage(tr("A JavaScript error occurred during export."));
            }
        } else {
            showStatusMessage(tr("Failed to open export-edl.js"));
        }
    }
}

void MainWindow::on_actionExportFrame_triggered()
{
    if (Settings.playerGPU() || Settings.playerPreviewScale()) {
        Mlt::GLWidget* glw = qobject_cast<Mlt::GLWidget*>(MLT.videoWidget());
        connect(glw, SIGNAL(imageReady()), SLOT(onGLWidgetImageReady()));
        MLT.setPreviewScale(0);
        glw->requestImage();
        MLT.refreshConsumer();
    } else {
        onGLWidgetImageReady();
    }
}

void MainWindow::onGLWidgetImageReady()
{
    Mlt::GLWidget* glw = qobject_cast<Mlt::GLWidget*>(MLT.videoWidget());
    QImage image = glw->image();
    if (Settings.playerGPU() || Settings.playerPreviewScale()) {
        disconnect(glw, SIGNAL(imageReady()), this, 0);
        MLT.setPreviewScale(Settings.playerPreviewScale());
    }
    if (!image.isNull()) {
        QString path = Settings.savePath();
        QString caption = tr("Export Frame");
        QString nameFilter = tr("PNG (*.png);;BMP (*.bmp);;JPEG (*.jpg *.jpeg);;PPM (*.ppm);;TIFF (*.tif *.tiff);;WebP (*.webp);;All Files (*)");
        QString saveFileName = QFileDialog::getSaveFileName(this, caption, path, nameFilter,
            nullptr, Util::getFileDialogOptions());
        if (!saveFileName.isEmpty()) {
            QFileInfo fi(saveFileName);
            if (fi.suffix().isEmpty())
                saveFileName += ".png";
            if (Util::warnIfNotWritable(saveFileName, this, caption))
                return;
            // Convert to square pixels if needed.
            qreal aspectRatio = (qreal) image.width() / image.height();
            if (qFloor(aspectRatio * 1000) != qFloor(MLT.profile().dar() * 1000)) {
                image = image.scaled(qRound(image.height() * MLT.profile().dar()), image.height(),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            image.save(saveFileName, Q_NULLPTR,
                (QFileInfo(saveFileName).suffix() == "webp")? 80 : -1);
            Settings.setSavePath(fi.path());
            m_recentDock->add(saveFileName);
        }
    } else {
        showStatusMessage(tr("Unable to export frame."));
    }
}

void MainWindow::on_actionAppDataSet_triggered()
{
    QMessageBox dialog(QMessageBox::Information,
                       qApp->applicationName(),
                       tr("You must restart Shotcut to change the data directory.\n"
                          "Do you want to continue?"),
                       QMessageBox::No | QMessageBox::Yes,
                       this);
    dialog.setDefaultButton(QMessageBox::Yes);
    dialog.setEscapeButton(QMessageBox::No);
    dialog.setWindowModality(QmlApplication::dialogModality());
    if (dialog.exec() != QMessageBox::Yes) return;

    QString dirName = QFileDialog::getExistingDirectory(this, tr("Data Directory"), Settings.appDataLocation(),
        Util::getFileDialogOptions());
    if (!dirName.isEmpty()) {
        // Move the data files.
        QDirIterator it(Settings.appDataLocation());
        while (it.hasNext()) {
            if (!it.filePath().isEmpty() && it.fileName() != "." && it.fileName() != "..") {
                if (!QFile::exists(dirName + "/" + it.fileName())) {
                    if (it.fileInfo().isDir()) {
                        if (!QFile::rename(it.filePath(), dirName + "/" + it.fileName()))
                            LOG_WARNING() << "Failed to move" << it.filePath() << "to" << dirName + "/" + it.fileName();
                    } else {
                        if (!QFile::copy(it.filePath(), dirName + "/" + it.fileName()))
                            LOG_WARNING() << "Failed to copy" << it.filePath() << "to" << dirName + "/" + it.fileName();
                    }
                }
            }
            it.next();
        }
        writeSettings();
        Settings.setAppDataLocally(dirName);

        m_exitCode = EXIT_RESTART;
        QApplication::closeAllWindows();
    }
}

void MainWindow::on_actionAppDataShow_triggered()
{
    Util::showInFolder(Settings.appDataLocation());
}

void MainWindow::on_actionNew_triggered()
{
    on_actionClose_triggered();
}

void MainWindow::on_actionKeyboardShortcuts_triggered()
{
    QDesktopServices::openUrl(QUrl("https://www.shotcut.org/howtos/keyboard-shortcuts/"));
}

void MainWindow::on_actionLayoutLogging_triggered()
{
    Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
    Settings.setLayoutMode(LayoutMode::Logging);
    auto state = Settings.layoutState(QString(kReservedLayoutPrefix).arg(LayoutMode::Logging));
    if (state.isEmpty()) {
        restoreState(kLayoutLoggingDefault);
//        setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
//        setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
//        setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
//        setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
//        resizeDocks({m_playlistDock, m_propertiesDock},
//            {qFloor(width() * 0.25), qFloor(width() * 0.25)}, Qt::Horizontal);
    } else {
//        LOG_DEBUG() << state.toBase64();
        restoreState(state);
    }
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutEditing_triggered()
{
    Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
    Settings.setLayoutMode(LayoutMode::Editing);
    auto state = Settings.layoutState(QString(kReservedLayoutPrefix).arg(LayoutMode::Editing));
    if (state.isEmpty()) {
        restoreState(kLayoutEditingDefault);
//        resetDockCorners();
    } else {
//        LOG_DEBUG() << state.toBase64();
        restoreState(state);
    }
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutEffects_triggered()
{
    Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
    Settings.setLayoutMode(LayoutMode::Effects);
    auto state = Settings.layoutState(QString(kReservedLayoutPrefix).arg(LayoutMode::Effects));
    if (state.isEmpty()) {
        restoreState(kLayoutEffectsDefault);
//        resetDockCorners();
    } else {
//        LOG_DEBUG() << state.toBase64();
        restoreState(state);
    }
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutColor_triggered()
{
    Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
    Settings.setLayoutMode(LayoutMode::Color);
    auto state = Settings.layoutState(QString(kReservedLayoutPrefix).arg(LayoutMode::Color));
    if (state.isEmpty()) {
        restoreState(kLayoutColorDefault);
//        setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
//        setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
//        setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
//        setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    } else {
//        LOG_DEBUG() << state.toBase64();
        restoreState(state);
    }
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutAudio_triggered()
{
    Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
    Settings.setLayoutMode(LayoutMode::Audio);
    auto state = Settings.layoutState(QString(kReservedLayoutPrefix).arg(LayoutMode::Audio));
    if (state.isEmpty()) {
        restoreState(kLayoutAudioDefault);
//        setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
//        setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
//        setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
//        setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    } else {
//        LOG_DEBUG() << state.toBase64();
        restoreState(state);
    }
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutPlayer_triggered()
{
    Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
    Settings.setLayoutMode(LayoutMode::PlayerOnly);
    auto state = Settings.layoutState(QString(kReservedLayoutPrefix).arg(LayoutMode::PlayerOnly));
    if (state.isEmpty()) {
        restoreState(kLayoutPlayerDefault);
//        resetDockCorners();
    } else {
//        LOG_DEBUG() << state.toBase64();
        restoreState(state);
    }
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutPlaylist_triggered()
{
    if (Settings.layoutMode() != LayoutMode::Custom) {
        Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
        Settings.setLayoutMode(LayoutMode::Custom);
    }
    clearCurrentLayout();
    restoreState(Settings.windowStateDefault());
    m_recentDock->show();
    m_recentDock->raise();
    m_playlistDock->show();
    m_playlistDock->raise();
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutClip_triggered()
{
    if (Settings.layoutMode() != LayoutMode::Custom) {
        Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
        Settings.setLayoutMode(LayoutMode::Custom);
    }
    clearCurrentLayout();
    restoreState(Settings.windowStateDefault());
    m_recentDock->show();
    m_recentDock->raise();
    m_filtersDock->show();
    m_filtersDock->raise();
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionLayoutAdd_triggered()
{
    QInputDialog dialog(this);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setWindowTitle(tr("Add Custom Layout"));
    dialog.setLabelText(tr("Name"));
    dialog.setWindowModality(QmlApplication::dialogModality());
    auto result = dialog.exec();
    auto name = dialog.textValue();
    if (result == QDialog::Accepted && !name.isEmpty()) {
        if (Settings.setLayout(name, saveGeometry(), saveState())) {
            Settings.setLayoutMode();
            clearCurrentLayout();
            Settings.sync();
            if (Settings.layouts().size() == 1) {
                ui->menuLayout->addAction(ui->actionLayoutRemove);
                ui->menuLayout->addSeparator();
            }
            ui->menuLayout->addAction(addLayout(m_layoutGroup, name));
        }
    }
}

void MainWindow::onLayoutTriggered(QAction* action)
{
    if (Settings.layoutMode() != LayoutMode::Custom) {
        Settings.setLayout(QString(kReservedLayoutPrefix).arg(Settings.layoutMode()), QByteArray(), saveState());
        Settings.setLayoutMode(LayoutMode::Custom);
    }
    clearCurrentLayout();
    restoreState(Settings.layoutState(action->text()));
    Settings.setWindowState(saveState());
}

void MainWindow::on_actionProfileRemove_triggered()
{
    QDir dir(Settings.appDataLocation());
    if (dir.cd("profiles")) {
        // Setup the dialog.
        QStringList profiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
        ListSelectionDialog dialog(profiles, this);
        dialog.setWindowModality(QmlApplication::dialogModality());
        dialog.setWindowTitle(tr("Remove Video Mode"));

        // Show the dialog.
        if (dialog.exec() == QDialog::Accepted) {
            removeCustomProfiles(dialog.selection(), dir, customProfileMenu(), actionProfileRemove());
        }
    }
}

void MainWindow::on_actionLayoutRemove_triggered()
{
    // Setup the dialog.
    ListSelectionDialog dialog(Settings.layouts(), this);
    dialog.setWindowModality(QmlApplication::dialogModality());
    dialog.setWindowTitle(tr("Remove Layout"));

    // Show the dialog.
    if (dialog.exec() == QDialog::Accepted) {
        foreach(const QString& layout, dialog.selection()) {
            // Update the configuration.
            if (Settings.removeLayout(layout))
                Settings.sync();
            // Locate the menu item.
            foreach (QAction* action, ui->menuLayout->actions()) {
                if (action->text() == layout) {
                    // Remove the menu item.
                    delete action;
                    break;
                }
            }
        }
        // If no more custom layouts.
        if (Settings.layouts().size() == 0) {
            // Remove the Remove action and separator.
            ui->menuLayout->removeAction(ui->actionLayoutRemove);
            bool isSecondSeparator = false;
            foreach (QAction* action, ui->menuLayout->actions()) {
                if (action->isSeparator()) {
                    if (isSecondSeparator) {
                        delete action;
                        break;
                    } else {
                        isSecondSeparator = true;
                    }
                }
            }
        }
    }
}

void MainWindow::on_actionOpenOther2_triggered()
{
    ui->actionOpenOther2->menu()->popup(mapToGlobal(ui->mainToolBar->geometry().bottomLeft()) + QPoint(64, 0));
}

void MainWindow::onOpenOtherTriggered(QWidget* widget)
{
    QDialog dialog(this);
    dialog.resize(426, 288);
    QVBoxLayout vlayout(&dialog);
    vlayout.addWidget(widget);
    QDialogButtonBox buttonBox(&dialog);
    buttonBox.setOrientation(Qt::Horizontal);
    buttonBox.setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    vlayout.addWidget(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));
    QString name = widget->objectName();
    if (name == "NoiseWidget" || dialog.exec() == QDialog::Accepted) {
        open(dynamic_cast<AbstractProducerWidget*>(widget)->newProducer(MLT.profile()));
        if (name == "TextProducerWidget") {
            m_filtersDock->show();
            m_filtersDock->raise();
        } else {
            m_propertiesDock->show();
            m_propertiesDock->raise();
        }
    }
    delete widget;
}

void MainWindow::onOpenOtherTriggered()
{
    if (sender()->objectName() == "color")
        onOpenOtherTriggered(new ColorProducerWidget(this));
    else if (sender()->objectName() == "text")
        onOpenOtherTriggered(new TextProducerWidget(this));
    else if (sender()->objectName() == "noise")
        onOpenOtherTriggered(new NoiseWidget(this));
    else if (sender()->objectName() == "ising0r")
        onOpenOtherTriggered(new IsingWidget(this));
    else if (sender()->objectName() == "lissajous0r")
        onOpenOtherTriggered(new LissajousWidget(this));
    else if (sender()->objectName() == "plasma")
        onOpenOtherTriggered(new PlasmaWidget(this));
    else if (sender()->objectName() == "test_pat_B")
        onOpenOtherTriggered(new ColorBarsWidget(this));
    else if (sender()->objectName() == "tone")
        onOpenOtherTriggered(new ToneProducerWidget(this));
    else if (sender()->objectName() == "count")
        onOpenOtherTriggered(new CountProducerWidget(this));
    else if (sender()->objectName() == "blipflash")
        onOpenOtherTriggered(new BlipProducerWidget(this));
    else if (sender()->objectName() == "v4l2")
        onOpenOtherTriggered(new Video4LinuxWidget(this));
    else if (sender()->objectName() == "pulse")
        onOpenOtherTriggered(new PulseAudioWidget(this));
    else if (sender()->objectName() == "jack")
        onOpenOtherTriggered(new JackProducerWidget(this));
    else if (sender()->objectName() == "alsa")
        onOpenOtherTriggered(new AlsaWidget(this));
#if defined(Q_OS_MAC)
    else if (sender()->objectName() == "device")
        onOpenOtherTriggered(new AvfoundationProducerWidget(this));
#elif defined(Q_OS_WIN)
    else if (sender()->objectName() == "device")
        onOpenOtherTriggered(new DirectShowVideoWidget(this));
#endif
    else if (sender()->objectName() == "decklink")
        onOpenOtherTriggered(new DecklinkProducerWidget(this));
}

void MainWindow::on_actionClearRecentOnExit_toggled(bool arg1)
{
    Settings.setClearRecent(arg1);
    if (arg1)
        Settings.setRecent(QStringList());
}

void MainWindow::onSceneGraphInitialized()
{
    if (Settings.playerGPU() && Settings.playerWarnGPU()) {
        QMessageBox dialog(QMessageBox::Warning,
                           qApp->applicationName(),
                           tr("GPU effects are EXPERIMENTAL, UNSTABLE and UNSUPPORTED! Unsupported means do not report bugs about it.\n\n"
                              "Do you want to disable GPU effects and restart Shotcut?"),
                           QMessageBox::No | QMessageBox::Yes,
                           this);
        dialog.setDefaultButton(QMessageBox::Yes);
        dialog.setEscapeButton(QMessageBox::No);
        dialog.setWindowModality(QmlApplication::dialogModality());
        if (dialog.exec() == QMessageBox::Yes) {
            ui->actionGPU->setChecked(false);
            m_exitCode = EXIT_RESTART;
            QApplication::closeAllWindows();
        } else {
            ui->actionGPU->setVisible(true);
        }
    } else if (Settings.playerGPU()) {
        ui->actionGPU->setVisible(true);
    }
}

void MainWindow::on_actionShowTextUnderIcons_toggled(bool b)
{
    ui->mainToolBar->setToolButtonStyle(b? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly);
    Settings.setTextUnderIcons(b);
    updateLayoutSwitcher();
}

void MainWindow::on_actionShowSmallIcons_toggled(bool b)
{
    ui->mainToolBar->setIconSize(b? QSize(16, 16) : QSize());
    Settings.setSmallIcons(b);
    updateLayoutSwitcher();
}

void MainWindow::onPlaylistInChanged(int in)
{
    m_player->blockSignals(true);
    m_player->setIn(in);
    m_player->blockSignals(false);
}

void MainWindow::onPlaylistOutChanged(int out)
{
    m_player->blockSignals(true);
    m_player->setOut(out);
    m_player->blockSignals(false);
}

void MainWindow::on_actionPreviewNone_triggered(bool checked)
{
    if (checked) {
        Settings.setPlayerPreviewScale(0);
        setPreviewScale(0);
        m_player->showIdleStatus();
    }
}

void MainWindow::on_actionPreview360_triggered(bool checked)
{
    if (checked) {
        Settings.setPlayerPreviewScale(360);
        setPreviewScale(360);
        m_player->showIdleStatus();
    }
}

void MainWindow::on_actionPreview540_triggered(bool checked)
{
    if (checked) {
        Settings.setPlayerPreviewScale(540);
        setPreviewScale(540);
        m_player->showIdleStatus();
    }
}

void MainWindow::on_actionPreview720_triggered(bool checked)
{
    if (checked) {
        Settings.setPlayerPreviewScale(720);
        setPreviewScale(720);
        m_player->showIdleStatus();
    }
}

QUuid MainWindow::timelineClipUuid(int trackIndex, int clipIndex)
{
    QScopedPointer<Mlt::ClipInfo> info(m_timelineDock->getClipInfo(trackIndex, clipIndex));
    if (info && info->cut && info->cut->is_valid())
        return MLT.ensureHasUuid(*info->cut);
    return QUuid();
}

void MainWindow::replaceInTimeline(const QUuid& uuid, Mlt::Producer& producer)
{
    int trackIndex = -1;
    int clipIndex = -1;
    // lookup the current track and clip index by UUID
    QScopedPointer<Mlt::ClipInfo> info(MAIN.timelineClipInfoByUuid(uuid, trackIndex, clipIndex));

    if (trackIndex >= 0 && clipIndex >= 0) {
        Util::getHash(producer);
        Util::applyCustomProperties(producer, *info->producer, producer.get_in(), producer.get_out());
        m_timelineDock->replace(trackIndex, clipIndex, MLT.XML(&producer));
    }
}

Mlt::ClipInfo* MainWindow::timelineClipInfoByUuid(const QUuid& uuid, int& trackIndex, int& clipIndex)
{
    return m_timelineDock->model()->findClipByUuid(uuid, trackIndex, clipIndex);
}

void MainWindow::replaceAllByHash(const QString& hash, Mlt::Producer& producer, bool isProxy)
{
    Util::getHash(producer);
    if (!isProxy)
        m_recentDock->add(producer.get("resource"));
    if (MLT.isClip() && MLT.producer() && Util::getHash(*MLT.producer()) == hash) {
        Util::applyCustomProperties(producer, *MLT.producer(), MLT.producer()->get_in(), MLT.producer()->get_out());
        MLT.copyFilters(*MLT.producer(), producer);
        MLT.close();
        m_player->setPauseAfterOpen(true);
        open(new Mlt::Producer(MLT.profile(), "xml-string", MLT.XML(&producer).toUtf8().constData()));
    } else if (MLT.savedProducer() && Util::getHash(*MLT.savedProducer()) == hash) {
        Util::applyCustomProperties(producer, *MLT.savedProducer(), MLT.savedProducer()->get_in(), MLT.savedProducer()->get_out());
        MLT.copyFilters(*MLT.savedProducer(), producer);
        MLT.setSavedProducer(&producer);
    }
    if (playlist()) {
        if (isProxy) {
            m_playlistDock->replaceClipsWithHash(hash, producer);
        } else {
            // Append to playlist
            producer.set(kPlaylistIndexProperty, playlist()->count());
            MAIN.undoStack()->push(
                new Playlist::AppendCommand(*m_playlistDock->model(), MLT.XML(&producer)));
        }
    }
    if (isMultitrackValid()) {
        m_timelineDock->replaceClipsWithHash(hash, producer);
    }
}

void MainWindow::on_actionTopics_triggered()
{
    QDesktopServices::openUrl(QUrl("https://www.shotcut.org/howtos/"));
}

void MainWindow::on_actionSync_triggered()
{
    auto dialog = new SystemSyncDialog(this);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::on_actionUseProxy_triggered(bool checked)
{
    if (MLT.producer()) {
        QDir dir(m_currentFile.isEmpty()? QDir::tempPath() : QFileInfo(m_currentFile).dir());
        QScopedPointer<QTemporaryFile> tmp(new QTemporaryFile(dir.filePath("shotcut-XXXXXX.mlt")));
        tmp->open();
        tmp->close();
        QString fileName = tmp->fileName();
        tmp->remove();
        tmp.reset();
        LOG_DEBUG() << fileName;

        if (saveXML(fileName)) {
            MltXmlChecker checker;

            Settings.setProxyEnabled(checked);
            checker.check(fileName);
            if (!isXmlRepaired(checker, fileName)) {
                QFile::remove(fileName);
                return;
            }
            if (checker.isUpdated()) {
                QFile::remove(fileName);
                fileName = checker.tempFileName();
            }

            // Open the temporary file
            int result = 0;
            {
                LongUiTask longTask(checked? tr("Turn Proxy On") : tr("Turn Proxy Off"));
                QFuture<int> future = QtConcurrent::run([=]() {
                    return MLT.open(QDir::fromNativeSeparators(fileName), QDir::fromNativeSeparators(m_currentFile));
                });
                result = longTask.wait<int>(tr("Converting"), future);
            }
            if (!result) {
                auto position = m_player->position();
                m_undoStack->clear();
                m_player->stop();
                m_player->setPauseAfterOpen(true);
                open(MLT.producer());
                MLT.seek(m_player->position());
                m_player->seek(position);

                if (checked && (isPlaylistValid() || isMultitrackValid())) {
                    // Prompt user if they want to create missing proxies
                    QMessageBox dialog(QMessageBox::Question, qApp->applicationName(),
                       tr("Do you want to create missing proxies for every file in this project?\n\n"
                          "You must reopen your project after all proxy jobs are finished."),
                       QMessageBox::No | QMessageBox::Yes, this);
                    dialog.setWindowModality(QmlApplication::dialogModality());
                    dialog.setDefaultButton(QMessageBox::Yes);
                    dialog.setEscapeButton(QMessageBox::No);
                    if (dialog.exec() == QMessageBox::Yes) {
                        Mlt::Producer producer(playlist());
                        if (producer.is_valid()) {
                            ProxyManager::generateIfNotExistsAll(producer);
                        }
                        producer = multitrack();
                        if (producer.is_valid()) {
                            ProxyManager::generateIfNotExistsAll(producer);
                        }
                    }
                }
            } else if (fileName != untitledFileName()) {
                showStatusMessage(tr("Failed to open ") + fileName);
                emit openFailed(fileName);
            }
        } else {
            ui->actionUseProxy->setChecked(!checked);
            showSaveError();
        }
        QFile::remove(fileName);
    } else {
        Settings.setProxyEnabled(checked);
    }
    m_player->showIdleStatus();
}

void MainWindow::on_actionProxyStorageSet_triggered()
{
    // Present folder dialog just like App Data Directory
    QString dirName = QFileDialog::getExistingDirectory(this, tr("Proxy Folder"), Settings.proxyFolder(),
        Util::getFileDialogOptions());
    if (!dirName.isEmpty() && dirName != Settings.proxyFolder()) {
        auto oldFolder = Settings.proxyFolder();
        Settings.setProxyFolder(dirName);
        Settings.sync();

        // Get a count for the progress dialog
        auto oldDir = QDir(oldFolder);
        auto dirList = oldDir.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        auto count = dirList.size();

        if (count > 0) {
            // Prompt user if they want to create missing proxies
            QMessageBox dialog(QMessageBox::Question, qApp->applicationName(),
               tr("Do you want to move all files from the old folder to the new folder?"),
               QMessageBox::No | QMessageBox::Yes, this);
            dialog.setWindowModality(QmlApplication::dialogModality());
            dialog.setDefaultButton(QMessageBox::Yes);
            dialog.setEscapeButton(QMessageBox::No);
            if (dialog.exec() == QMessageBox::Yes) {
                // Move the existing files
                LongUiTask longTask(tr("Moving Files"));
                int i = 0;
                for (const auto& fileName : dirList) {
                    if (!fileName.isEmpty() && !QFile::exists(dirName + "/" + fileName)) {
                        LOG_DEBUG() << "moving" << oldDir.filePath(fileName) << "to" << dirName + "/" + fileName;
                        longTask.reportProgress(fileName, i++, count);
                        if (!QFile::rename(oldDir.filePath(fileName), dirName + "/" + fileName))
                            LOG_WARNING() << "Failed to move" << oldDir.filePath(fileName);
                    }
                }
            }
        }
    }
}

void MainWindow::on_actionProxyStorageShow_triggered()
{
    Util::showInFolder(ProxyManager::dir().path());
}

void MainWindow::on_actionProxyUseProjectFolder_triggered(bool checked)
{
    Settings.setProxyUseProjectFolder(checked);
}

void MainWindow::on_actionProxyUseHardware_triggered(bool checked)
{
    if (checked && Settings.encodeHardware().isEmpty()) {
        if (!m_encodeDock->detectHardwareEncoders())
            ui->actionProxyUseHardware->setChecked(false);
    }
    Settings.setProxyUseHardware(ui->actionProxyUseHardware->isChecked());
}

void MainWindow::on_actionProxyConfigureHardware_triggered()
{
    m_encodeDock->on_hwencodeButton_clicked();
    if (Settings.encodeHardware().isEmpty()) {
        ui->actionProxyUseHardware->setChecked(false);
        Settings.setProxyUseHardware(false);
    }
}

void MainWindow::updateLayoutSwitcher()
{
    if (Settings.textUnderIcons() && !Settings.smallIcons()) {
        auto layoutSwitcher = findChild<QWidget*>(kLayoutSwitcherName);
        if (layoutSwitcher) {
            layoutSwitcher->show();
            for (const auto& child : layoutSwitcher->findChildren<QToolButton*>()) {
                child->show();
            }
        } else {
            layoutSwitcher = new QWidget;
            layoutSwitcher->setObjectName(kLayoutSwitcherName);
            auto layoutGrid = new QGridLayout(layoutSwitcher);
            layoutGrid->setContentsMargins(0, 0, 0, 0);
            ui->mainToolBar->insertWidget(ui->dummyAction, layoutSwitcher);
            auto button = new QToolButton;
            button->setAutoRaise(true);
            button->setDefaultAction(ui->actionLayoutLogging);
            layoutGrid->addWidget(button, 0, 0, Qt::AlignCenter);
            button = new QToolButton;
            button->setAutoRaise(true);
            button->setDefaultAction(ui->actionLayoutEditing);
            layoutGrid->addWidget(button, 0, 1, Qt::AlignCenter);
            button = new QToolButton;
            button->setAutoRaise(true);
            button->setDefaultAction(ui->actionLayoutEffects);
            layoutGrid->addWidget(button, 0, 2, Qt::AlignCenter);
            button = new QToolButton;
            button->setAutoRaise(true);
            button->setDefaultAction(ui->actionLayoutColor);
            layoutGrid->addWidget(button, 1, 0, Qt::AlignCenter);
            button = new QToolButton;
            button->setAutoRaise(true);
            button->setDefaultAction(ui->actionLayoutAudio);
            layoutGrid->addWidget(button, 1, 1, Qt::AlignCenter);
            button = new QToolButton;
            button->setAutoRaise(true);
            button->setDefaultAction(ui->actionLayoutPlayer);
            layoutGrid->addWidget(button, 1, 2, Qt::AlignCenter);
        }
        ui->mainToolBar->removeAction(ui->actionLayoutLogging);
        ui->mainToolBar->removeAction(ui->actionLayoutEditing);
        ui->mainToolBar->removeAction(ui->actionLayoutEffects);
        ui->mainToolBar->removeAction(ui->actionLayoutColor);
        ui->mainToolBar->removeAction(ui->actionLayoutAudio);
        ui->mainToolBar->removeAction(ui->actionLayoutPlayer);
    } else {
        auto layoutSwitcher = findChild<QWidget*>(kLayoutSwitcherName);
        if (layoutSwitcher) {
            layoutSwitcher->hide();
            for (const auto& child : layoutSwitcher->findChildren<QToolButton*>()) {
                child->hide();
            }
            ui->mainToolBar->insertAction(ui->dummyAction, ui->actionLayoutLogging);
            ui->mainToolBar->insertAction(ui->dummyAction, ui->actionLayoutEditing);
            ui->mainToolBar->insertAction(ui->dummyAction, ui->actionLayoutEffects);
            ui->mainToolBar->insertAction(ui->dummyAction, ui->actionLayoutColor);
            ui->mainToolBar->insertAction(ui->dummyAction, ui->actionLayoutAudio);
            ui->mainToolBar->insertAction(ui->dummyAction, ui->actionLayoutPlayer);
        }
    }
}

void MainWindow::clearCurrentLayout()
{
    auto currentLayout = ui->actionLayoutLogging->actionGroup()->checkedAction();
    if (currentLayout) {
        currentLayout->setChecked(false);
    }
}

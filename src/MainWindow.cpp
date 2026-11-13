// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "MemoryFileNames.h"
#include "MainWindow.h"
#include "CalculatorWidget.h"
#include "InspectorPanel.h"
#include "BackWindow.h"
#include "CalcActions.h"
#include "KeyClick.h"
#include "MatrixEditor.h"
#include "ProgramEntry15.h"
#include "ProgramInfoDialog.h"
#include "WelcomeWidget.h"
#include "usb/UsbPlatform.h"
#ifndef Q_OS_WIN
#include "usb/HidrawTransport.h"
#endif
#include "usb/UsbLog.h"
#ifndef Q_OS_WIN
#include "usb/UsbPermissions.h"
#endif
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFrame>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifndef VOYAGERCALC_VERSION
#define VOYAGERCALC_VERSION "0.7.0"
#endif

namespace vc {

static const QString kStockLimits = QStringLiteral(
    "HP's original 12C firmware runs the keyboard and display only. These need the CE firmware's memory layout and are not available with it: "
    "Registers & Program, Open / Save Memory File, Program Info and documentation, the USB transfer of the calculator's memory to and from the "
    "emulator, the model switch, and keeping the calculator's memory between launches (it starts clear each time).");

static const QString kFontCredit = QStringLiteral(
    "Documentation PDF fonts, included with the app: key-cap font by Eric Rechlin; seven-segment display font by Torsten Manz "
    "(HP15C Simulator Font © 2005 Torsten Manz, Freeware). The font files are renamed copies (name table only) that keep their copyright notices. HP-15C and HP-16C calculator photographs (front and back) by Warren Young.");

// ---- pending action texts (AppModel.PendingAction) ---------------------------------------------------------------

QString PendingAction::title() const {
    switch (kind) {
    case SwitchModel: return QStringLiteral("Switch to the %1?").arg(Machine::variantName(variant));
    case MemoryReset: return QStringLiteral("Reset the calculator's memory?");
    case LoadMemory: return QStringLiteral("Load the memory from %1?").arg(source);
    case LoadMemoryAnyway: return QStringLiteral("The memory from %1 may not fit").arg(source);
    default: return QStringLiteral("The memory from %1 is for the HP-%2").arg(source, Machine::variantName(variant));
    }
}
QString PendingAction::message() const {
    switch (kind) {
    case SwitchModel: return keepsMemory
        ? QStringLiteral("Each mode keeps its own memory (programs, registers and settings). Switching brings back the other mode's memory.")
        : QStringLiteral("Switching model clears the calculator's memory (programs, registers and settings), as on the calculator. You can save a backup first.");
    case MemoryReset: return QStringLiteral("ON + − clears all programs, registers and settings (Pr Error). You can save a backup first.");
    case LoadMemory: return QStringLiteral("This replaces the emulator's memory (programs, registers and settings) with the one from %1. You can save a backup of the emulator's memory first.").arg(source);
    case LoadMemoryAnyway: return reason + QStringLiteral(" You can load it anyway; save a backup of the emulator's memory first if you want to keep it.");
    default: return reason;
    }
}
QString PendingAction::confirmTitle() const {
    switch (kind) {
    case MemoryReset: return QStringLiteral("Reset Memory");
    case SwitchModel: return QStringLiteral("Switch Model");
    case LoadMemory: return QStringLiteral("Load Memory");
    case LoadMemoryAnyway: return QStringLiteral("Load Anyway");
    default: return QStringLiteral("Switch to %1 and Load").arg(Machine::variantName(variant));
    }
}

QString PendingWrite::calculatorModel() const {
    return calculatorModels.empty() ? QString() : QStringLiteral("HP-") + Machine::variantName(calculatorModels.front()).section(QStringLiteral(" ("), 0, 0);
}
QString PendingWrite::title() const { return QStringLiteral("Write to the %1?").arg(calculatorModel().isEmpty() ? QStringLiteral("calculator") : calculatorModel()); }
QString PendingWrite::message() const {
    QString m;
    const QString c = calculatorModel();
    if (!c.isEmpty()) {
        m = matches() ? QStringLiteral("The connected %1 gets the emulator's %2 memory (programs, registers and settings).").arg(c, emulatorModel)
                      : QStringLiteral("The connected calculator holds an %1 memory, but the emulator runs an %2. The calculator will most likely show Pr Error and clear its memory.").arg(c, emulatorModel);
    } else {
        m = QStringLiteral("The connected calculator's memory does not look like a memory this firmware knows. It gets the emulator's %1 memory.").arg(emulatorModel);
    }
    if (needs152) m += QStringLiteral(" This memory uses the HP-15C (15.2) double memory: set the calculator to 15C (15.2) mode first (turn it off, hold g and ENTER, press ON, 4, then the key under 15.2), or it will show Error 10.");
    if (!layoutNote.isEmpty()) m += layoutNote;
    return m + QStringLiteral(" Its current memory was saved to “%1” in %2.").arg(QFileInfo(backup).fileName(), QFileInfo(backup).absolutePath());
}

// ---- window size memory ----------------------------------------------------------------------------------------------
// The flyout (Registers & Program) is part of the window. The saved geometry says whether it already included room for
// the flyout (geometryHasInspector); when the flyout appears as part of that restored layout the window is NOT grown
// again (it used to grow by the flyout's width at every launch). Only a toggle by the user later changes the width.

QRect MainWindow::screenAreaOverride;

QRect MainWindow::availableArea() const {
    if (!screenAreaOverride.isNull()) return screenAreaOverride;
    QScreen *sc = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!sc) sc = QGuiApplication::screenAt(geometry().center());
    if (!sc) sc = QGuiApplication::primaryScreen();
    return sc ? sc->availableGeometry() : QRect(0, 0, 1280, 800);
}

void MainWindow::restoreWindowGeometry() {
    QSettings st;
    const QRect fallbackArea = availableArea();
    auto useDefault = [&] {
        const int w = std::min(900, fallbackArea.width()), h = std::min(560, fallbackArea.height() - 30);
        resize(std::max(minimumWidth(), w), std::max(minimumHeight(), h));
        move(fallbackArea.center() - QPoint(width() / 2, height() / 2));
    };
    bool ok = st.contains(QStringLiteral("geometry")) && restoreGeometry(st.value(QStringLiteral("geometry")).toByteArray());
    // legacy settings (before the flag existed): the window had the flyout in it when it was open at the end
    savedHadInspector_ = st.contains(QStringLiteral("geometryHasInspector")) ? st.value(QStringLiteral("geometryHasInspector")).toBool() : showInspector_;
    if (!ok) { savedHadInspector_ = false; useDefault(); return; }
    if (isMaximized() || isFullScreen()) return;
    // off every screen: back to the default, centred
    bool onScreen = !screenAreaOverride.isNull() ? screenAreaOverride.contains(geometry().center()) : false;
    if (screenAreaOverride.isNull()) for (QScreen *sc : QGuiApplication::screens()) if (sc->availableGeometry().contains(geometry().center())) onScreen = true;
    if (!onScreen) { savedHadInspector_ = false; useDefault(); return; }
    fitWindowToScreen();
}

/// Keep the window (and its frame) inside the available screen area; smaller than the screen when it is larger.
void MainWindow::fitWindowToScreen() {
    if (isMaximized() || isFullScreen()) return;
    const QRect a = availableArea();
    const int fw = std::max(0, frameGeometry().width() - width()), fh = std::max(0, frameGeometry().height() - height());
    int w = std::max(std::min(width(), a.width() - fw), std::min(minimumWidth(), a.width()));
    int h = std::max(std::min(height(), a.height() - fh), std::min(minimumHeight(), a.height()));
    if (w != width() || h != height()) resize(w, h);
    int x = std::clamp(frameGeometry().x(), a.left(), std::max(a.left(), a.right() - (w + fw) + 1));
    int y = std::clamp(frameGeometry().y(), a.top(), std::max(a.top(), a.bottom() - (h + fh) + 1));
    if (x != frameGeometry().x() || y != frameGeometry().y()) move(x + (geometry().x() - frameGeometry().x()), y + (geometry().y() - frameGeometry().y()));
}

/// Change the window width by dw (the flyout appearing or disappearing); never past the screen; keeps the right edge on screen.
void MainWindow::resizeWindowBy(int dw) {
    if (isMaximized() || isFullScreen() || dw == 0) return;
    resize(std::max(minimumWidth(), width() + dw), height());
    fitWindowToScreen();
}

/// After a restore: when the calculator pane is clearly wider than its face photo needs (black bars left and right), take the
/// excess off the window width. Only ever narrows, only once per start, so the window never feels locked.
void MainWindow::trimSideBars(int sideSpace) {
    if (isMaximized() || isFullScreen()) return;
    const double aspect = calc_->faceAspect();
    if (aspect <= 0) return;
    const int paneH = height() - (menuBar() && menuBar()->isVisible() ? menuBar()->sizeHint().height() : 0);
    const int paneW = width() - sideSpace;
    const int need = int(std::lround(paneH * aspect));
    if (need > 0 && paneW > need * 1.08) resize(std::max(minimumWidth(), need + sideSpace), height());
}

void MainWindow::saveWindowSettings() {
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    // before the first layout after start the window is exactly as it was loaded: keep what it said
    s.setValue(QStringLiteral("geometryHasInspector"), restorePending_ ? savedHadInspector_ : inspector_->isVisible());
    if (inspector_->isVisible() && split_->sizes().size() == 2 && split_->sizes()[1] >= 260) inspectorWidth_ = split_->sizes()[1];
    s.setValue(QStringLiteral("inspectorWidth"), inspectorWidth_);
}

// ---- construction ------------------------------------------------------------------------------------------------

static PdfFonts &pdfFonts() { static PdfFonts f = PdfFonts::fromResources(); return f; }

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("VoyagerCalc"));
    setAcceptDrops(true);
    stack_ = new QStackedWidget(this);
    welcome_ = new WelcomeWidget(stack_);
    split_ = new QSplitter(Qt::Horizontal, stack_);
    split_->setChildrenCollapsible(false);
    calc_ = new CalculatorWidget(split_);
    inspector_ = new InspectorPanel(split_);
    inspector_->setMinimumWidth(260);
    inspector_->hide();
    split_->addWidget(calc_); split_->addWidget(inspector_);
    split_->setStretchFactor(0, 1); split_->setStretchFactor(1, 0);
    split_->setHandleWidth(3);
    stack_->addWidget(welcome_); stack_->addWidget(split_);
    setCentralWidget(stack_);
    resize(900, 560);
    setMinimumSize(520, 340);

    busy_ = new QLabel(stack_);
    busy_->setStyleSheet("background:rgba(40,40,40,0.92);color:white;border-radius:14px;padding:8px 16px;font-size:13px;");
    busy_->hide();
    importBar_ = new QFrame(stack_);
    importBar_->setObjectName(QStringLiteral("memoryImportStatus"));
    importBar_->setStyleSheet("QFrame#memoryImportStatus{background:rgba(28,23,20,0.94);border:1px solid rgba(255,255,255,0.28);border-radius:6px;} "
                              "QLabel{color:#f4efe6;font-size:11px;} QPushButton{color:#E3A63A;font-size:11px;font-weight:bold;border:none;padding:2px 6px;}");
    auto *importRow = new QHBoxLayout(importBar_);
    importRow->setContentsMargins(8, 4, 4, 4); importRow->setSpacing(6);
    importLabel_ = new QLabel(importBar_);
    importLabel_->setWordWrap(true); importLabel_->setMaximumWidth(460);
    undoLoad_ = new QPushButton(QStringLiteral("Undo Load"), importBar_);
    undoLoad_->setObjectName(QStringLiteral("undoLoad"));
    auto *dismiss = new QPushButton(QStringLiteral("×"), importBar_);
    importRow->addWidget(importLabel_, 1); importRow->addWidget(undoLoad_); importRow->addWidget(dismiss);
    importBar_->hide();
    connect(undoLoad_, &QPushButton::clicked, this, &MainWindow::undoMemoryImport);
    connect(dismiss, &QPushButton::clicked, this, [this] { memoryImportStatus_.clear(); refreshImportStatus(); });

    QSettings st;
    paper_ = st.value(QStringLiteral("pdfPaper"), QStringLiteral("a4")).toString() == QLatin1String("letter") ? Paper::Letter : Paper::A4;
    showInspector_ = st.value(QStringLiteral("showInspector"), false).toBool();
    inspectorWidth_ = std::clamp(st.value(QStringLiteral("inspectorWidth"), 340).toInt(), 260, 900);
    restoreWindowGeometry();
    connect(split_, &QSplitter::splitterMoved, this, [this] {
        if (inspector_->isVisible() && split_->sizes().size() == 2 && split_->sizes()[1] >= 260) inspectorWidth_ = split_->sizes()[1];
    });

    // default prompts: Qt's dialogs
    prompts.confirm = [this](const PendingAction &a) {
        QMessageBox box(QMessageBox::Warning, a.title(), a.message(), QMessageBox::NoButton, this);
        QPushButton *backup = box.addButton(QStringLiteral("Save Backup First…"), QMessageBox::ActionRole);
        QPushButton *go = box.addButton(a.confirmTitle(), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == go) return 1;
        if (box.clickedButton() == backup) return 2;
        return 0;
    };
    prompts.confirmWrite = [this](const PendingWrite &w) {
        QMessageBox box(QMessageBox::Warning, w.title(), w.message(), QMessageBox::NoButton, this);
        QPushButton *go = box.addButton(w.matches() ? QStringLiteral("Write to Calculator") : QStringLiteral("Write Anyway"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel); box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == go;
    };
    prompts.confirmImport = [this](const QString &t, const QString &m) {
        return QMessageBox::question(this, t, m, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok;
    };
    prompts.error = [this](const QString &m) { QMessageBox::warning(this, QStringLiteral("VoyagerCalc"), m); };
    prompts.notice = [this](const QString &m) { QMessageBox::information(this, QStringLiteral("Note"), m); };
    prompts.openFile = [this](const QString &kind, const QString &filter) {
        QSettings s;
        QString dir = s.value(QStringLiteral("lastDir"), QDir::homePath()).toString();
        QString p = QFileDialog::getOpenFileName(this, kind, dir, filter);
        if (!p.isEmpty()) s.setValue(QStringLiteral("lastDir"), QFileInfo(p).absolutePath());
        return p;
    };
    prompts.saveFile = [this](const QString &name, const QString &filter) {
        QSettings s;
        QString dir = s.value(QStringLiteral("lastDir"), QDir::homePath()).toString();
        QString p = QFileDialog::getSaveFileName(this, QStringLiteral("Save"), dir + QLatin1Char('/') + name, filter);
        if (!p.isEmpty()) s.setValue(QStringLiteral("lastDir"), QFileInfo(p).absolutePath());
        return p;
    };
    prompts.askFixPermissions = [this](const QString &msg) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("VoyagerCalc"), msg, QMessageBox::NoButton, this);
        QPushButton *fix = box.addButton(QStringLiteral("Fix USB Permissions"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        return box.clickedButton() == fix;
    };

    calc_->setKeysEnabled(false);
    connect(welcome_, &WelcomeWidget::chooseFirmware, this, &MainWindow::chooseFirmware);
    connect(calc_, &CalculatorWidget::keyPressed, this, &MainWindow::press);
    connect(calc_, &CalculatorWidget::keyReleased, this, &MainWindow::release);
    connect(calc_, &CalculatorWidget::legendPressed, this, &MainWindow::pressLegend);
    connect(calc_, &CalculatorWidget::latchToggled, this, &MainWindow::toggleLatch);
    connect(inspector_, &InspectorPanel::editComment, this, &MainWindow::editComment);
    connect(inspector_, &InspectorPanel::performKeys, this, [this](const QList<int> &k) { performKeys(std::vector<int>(k.begin(), k.end())); });
    calc_->router().press = [this](int c) { press(c); };
    calc_->router().release = [this](int c) { release(c); };
    calc_->router().pressShifted = [this](int shift, int c) { pressLegend(c, shift); };
    inspector_->setEmptyText(QStringLiteral("Choose a firmware file first."));

    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this] {
        if (linkStatus_.isEmpty()) return;
        ++linkTicket_; setBusy({});
        const QString last = UsbLog::lastLine();
        UsbLog::write(QStringLiteral("watchdog: the USB work did not finish within the time limit (the window is fine; the USB thread is still waiting)"));
        linkFailed(LinkError::hung(last.section(QStringLiteral("  "), 1)));
    });
    makeTransport = [] { return makeDefaultTransport(); };

    buildMenus();
    setShowKeyHelp(QSettings().value(QStringLiteral("showKeyHelp"), false).toBool());   // off by default (0.7.1: the hover help was intrusive)
    setLegendClicks(QSettings().value(QStringLiteral("legendClicks"), true).toBool());  // 0.7.1: a setting (on = as before)
    setKeyClick(QSettings().value(QStringLiteral("keyClick"), false).toBool());
    updateMenus();
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    stopEngine();
}

const Model &MainWindow::model() const { return Model::with(calc_->modelId()); }

QMenu *MainWindow::menu(const QString &title) const {
    for (QAction *a : menuBar()->actions()) if (a->menu() && a->text() == title) return a->menu();
    return nullptr;
}

void MainWindow::buildMenus() {
    auto act = [this](QMenu *m, const QString &text, auto fn, const QKeySequence &key = {}) {
        QAction *a = m->addAction(text, this, fn);
        if (!key.isEmpty()) a->setShortcut(key);
        return a;
    };
    fileMenu_ = menuBar()->addMenu(QStringLiteral("File"));
    actOpen_ = act(fileMenu_, QStringLiteral("Open Memory File…"), [this] { openMemory(); }, QKeySequence::Open);
    actSave_ = act(fileMenu_, QStringLiteral("Save Memory File…"), [this] { saveMemory(); }, QKeySequence::Save);
    fileMenu_->addSeparator();
    actInfo_ = act(fileMenu_, QStringLiteral("Program Info…"), [this] { openProgramInfo(); }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    actOpenNotes_ = act(fileMenu_, QStringLiteral("Open Program Notes…"), [this] { openNotes(); });
    actSaveNotes_ = act(fileMenu_, QStringLiteral("Save Program Notes…"), [this] { saveNotes(); });
    actPdf_ = act(fileMenu_, QStringLiteral("Export Program Documentation (PDF)…"), [this] { exportPdf(); }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    actText_ = act(fileMenu_, QStringLiteral("Export Listing as Text…"), [this] { exportText(); });
    fileMenu_->addSeparator();
    actImportPrg_ = act(fileMenu_, QStringLiteral("Import Program (.15c)…"), [this] { importManzProgram(); });
    actExportPrg_ = act(fileMenu_, QStringLiteral("Export Program (.15c)…"), [this] { exportManzProgram(); });
    fileMenu_->addSeparator();
    QMenu *paperMenu = fileMenu_->addMenu(QStringLiteral("PDF Paper Size"));
    auto *pg = new QActionGroup(this);
    actA4_ = paperMenu->addAction(QStringLiteral("A4"), this, [this] { setPaper(Paper::A4); });
    actLetter_ = paperMenu->addAction(QStringLiteral("US Letter"), this, [this] { setPaper(Paper::Letter); });
    for (QAction *a : {actA4_, actLetter_}) { a->setCheckable(true); pg->addAction(a); }
    actClear_ = act(fileMenu_, QStringLiteral("Clear Program Notes"), [this] { clearNotes(); });
    fileMenu_->addSeparator();
    act(fileMenu_, QStringLiteral("Choose Firmware…"), [this] { chooseFirmware(); });
    fileMenu_->addSeparator();
    act(fileMenu_, QStringLiteral("Quit"), [this] { close(); }, QKeySequence::Quit);

    QMenu *view = menuBar()->addMenu(QStringLiteral("View"));
    actInspector_ = act(view, QStringLiteral("Registers && Program"), [this] { setShowInspector(!showInspector_); }, QKeySequence(Qt::CTRL | Qt::Key_I));
    actKeyHelp_ = act(view, QStringLiteral("Show Key Help"), [this] { setShowKeyHelp(!calc_->showKeyHelp()); });
    actKeyHelp_->setCheckable(true);
    actKeyHelp_->setToolTip(QStringLiteral("Show each key's functions and keyboard shortcut when the pointer rests on it"));
    actInspector_->setCheckable(true);
    actBack_ = act(view, QStringLiteral("Show Calculator Back"), [this] { showCalculatorBack(); }, QKeySequence(Qt::CTRL | Qt::Key_B));
    actBack_->setToolTip(QStringLiteral("The back of the HP-15C or HP-16C, with its tables, in a separate window"));
    view->addSeparator();
    actLegend_ = act(view, QStringLiteral("Click f/g Labels"), [this] { setLegendClicks(!calc_->legendClicks()); });
    actLegend_->setCheckable(true);
    actLegend_->setToolTip(QStringLiteral("A click on a key's gold (f) or blue (g) label types f or g and the key; the label lights up under the pointer"));
    actClick_ = act(view, QStringLiteral("Key Click Sound"), [this] { setKeyClick(!keyClick_); });
    actClick_->setCheckable(true);
    actClick_->setToolTip(QStringLiteral("A short, deep click at every key press"));

    calcMenu_ = menuBar()->addMenu(QStringLiteral("Calculator"));
    // "ON / OFF   Esc": the text after the tab is shown at the right like a shortcut, but nothing is bound to it: the key itself is handled
    // in ONE place (the face widget's key path, which keeps ON held while Esc is down), so it can never fire twice (0.6.9).
    actPower_ = act(calcMenu_, QStringLiteral("ON / OFF\tEsc"), [this] { togglePower(); });
    calcMenu_->addSeparator();
    actReset_ = act(calcMenu_, QStringLiteral("Reset Calculator (clears memory)…"), [this] { resetCalculator(); });
    calcMenu_->addSeparator();
    actMatrix_ = act(calcMenu_, QStringLiteral("Matrix Editor…"), [this] { openMatrixEditor(); }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    actMatrix_->setToolTip(QStringLiteral("HP-15C: edit, paste or copy matrices A–E in the emulator's memory"));
    calcMenu_->addSeparator();
    usbMenu_ = calcMenu_->addMenu(QStringLiteral("Real Calculator (USB)"));
    actRead_ = act(usbMenu_, QStringLiteral("Read from Calculator"), [this] { readFromCalculator(); });
    actWrite_ = act(usbMenu_, QStringLiteral("Write to Calculator…"), [this] { writeToCalculator(); });
    actSaveCalc_ = act(usbMenu_, QStringLiteral("Save Calculator Memory to File…"), [this] { saveCalculatorMemoryToFile(); });
    usbMenu_->addSeparator();
    if (usbNeedsPermissionFix()) {          // Linux only: the udev rule. Windows needs nothing.
        actFix_ = act(usbMenu_, QStringLiteral("Fix USB Permissions…"), [this] { fixUsbPermissions(); });
        usbMenu_->addSeparator();
    }
    usbMenu_->addAction(QStringLiteral("USB: calculator off, hold f + ENTER, press ON"))->setEnabled(false);

    modelMenu_ = menuBar()->addMenu(QStringLiteral("Model"));
    keysMenu_ = menuBar()->addMenu(QStringLiteral("Keys"));
    QMenu *help = menuBar()->addMenu(QStringLiteral("Help"));
    act(help, QStringLiteral("Show USB Log"), [this] { showUsbLog(); });
    act(help, QStringLiteral("About VoyagerCalc"), [this] { showAbout(); });
    rebuildModelMenu();
    rebuildKeysMenu();
}

void MainWindow::rebuildModelMenu() {
    modelMenu_->clear();
    delete modelGroup_;
    modelGroup_ = new QActionGroup(this);
    if (fw_.images.empty()) { modelMenu_->addAction(QStringLiteral("No firmware"))->setEnabled(false); return; }
    for (int v : fw_.images) {
        QAction *a = modelMenu_->addAction(Machine::variantName(v), this, [this, v] { chooseModel(v); });
        a->setCheckable(true); a->setData(v); modelGroup_->addAction(a);
        a->setChecked(running_ && snap_.variant == v);
        a->setEnabled(fw_.images.size() > 1 && running_ && !switching_);
    }
    if (fw_.images.size() > 1) {
        modelMenu_->addSeparator();
        modelMenu_->addAction(switching_ ? QStringLiteral("Switching…")
            : (fw_.keepsModeMemory
               ? QStringLiteral("Each mode keeps its own memory")
               : QStringLiteral("Switching can clear the calculator's memory")))->setEnabled(false);
    }
    if (running_ && snap_.variant == 160) {
        modelMenu_->addSeparator();
        QMenu *decode = modelMenu_->addMenu(QStringLiteral("16C inspector layout"));
        auto *decodeGroup = new QActionGroup(decode);
        const int choices[] = {VC_MEM16_NONE, VC_MEM16_STD, VC_MEM16_LAYOUT206};      // 0.7.7: stock and 206 only
        for (int lay : choices) {
            const auto layout = Emulator::Memory16Layout(lay);
            QAction *a = decode->addAction(Emulator::layoutTitle(layout), this, [this, lay] { chooseInspectorLayout(lay); });
            a->setCheckable(true); a->setChecked(inspectorLayout_ == lay); decodeGroup->addAction(a);
        }
        decode->addSeparator();
        decode->addAction(QStringLiteral("Decoding and memory-import target; does not change firmware"))->setEnabled(false);
        QMenu *usbLay = modelMenu_->addMenu(QStringLiteral("USB 16C layout (physical self-test)"));
        auto *usbGroup = new QActionGroup(usbLay);
        for (int lay : choices) {
            const QString title = lay == VC_MEM16_NONE ? QStringLiteral("Unknown: ask on read; block write")
                                                       : Emulator::layoutTitle(Emulator::Memory16Layout(lay));
            QAction *a = usbLay->addAction(title, this, [this, lay] { setConnected16Layout(lay); });
            a->setCheckable(true); a->setChecked(connectedLayout_ == lay); usbGroup->addAction(a);
        }
    }
    if (isStock()) { modelMenu_->addSeparator(); modelMenu_->addAction(QStringLiteral("HP's original firmware has no model switch"))->setEnabled(false); }
}

void MainWindow::rebuildKeysMenu() {
    keysMenu_->clear();
    const int mdl = snap_.model ? snap_.model : calc_->modelId();
    for (const ChordInfo &c : allChords()) {
        if (!chordAvailable(c.chord, mdl, fw_.family)) continue;
        QAction *a = keysMenu_->addAction(c.title + (c.needsConfirmation ? QStringLiteral("…") : QString()) + QStringLiteral("\t") + c.written,
                                          this, [this, ch = c.chord] { perform(ch); });
        a->setToolTip(c.explanation); a->setStatusTip(c.explanation);
        a->setEnabled(running_ && !switching_);
    }
    keysMenu_->addSeparator();
    actRelease_ = keysMenu_->addAction(QStringLiteral("Release Held Keys"), this, [this] { releaseLatchedKeys(); });
    actRelease_->setEnabled(!latchedShown_.isEmpty());
    keysMenu_->addAction(QStringLiteral("Hold a key down: right-click it (or Ctrl-click)"))->setEnabled(false);
    keysMenu_->addAction(isStock() ? QStringLiteral("HP's original firmware: only the chords tried on it are listed")
                                   : QStringLiteral("f-ENTER-ON: no USB in the app, the calculator just turns on"))->setEnabled(false);
}

int MainWindow::currentModel() const { return snap_.model ? snap_.model : calc_->modelId(); }

void MainWindow::showCalculatorBack() {
    const int m = currentModel();
    if (!running_ || BackWindow::resourceFor(m).isEmpty()) return;
    if (!backWindow_) backWindow_ = new BackWindow(this);
    backWindow_->setModel(m);
    backWindow_->show(); backWindow_->raise(); backWindow_->activateWindow();
}

void MainWindow::performKeys(const std::vector<int> &keys) {
    if (!running_ || !worker_ || keys.empty()) return;
    releaseLatchedKeys();
    worker_->submit(this, [keys](Machine &m) { CalcActions::perform(m, keys); }, [this](const QString &err) { if (!err.isEmpty()) showNotice(err); });
}

void MainWindow::updateMenus() {
    const bool run = running_;
    actOpen_->setEnabled(run && hasMemoryFiles()); actSave_->setEnabled(run && hasMemoryFiles());
    for (QAction *a : {actInfo_, actOpenNotes_, actSaveNotes_, actPdf_, actText_, actClear_}) a->setEnabled(run && hasInspector());
    actInspector_->setEnabled(run && hasInspector());
    actInspector_->setChecked(showInspector_ && hasInspector());
    const bool is15 = run && hasMemoryFiles() && hasInspector() && (snap_.model ? snap_.model : calc_->modelId()) == 15 && !switching_;
    actMatrix_->setEnabled(is15);
    for (QAction *a : {actImportPrg_, actExportPrg_}) {
        a->setEnabled(is15 && !importingProgram_);
        a->setToolTip(run && !isStock() && (snap_.model ? snap_.model : calc_->modelId()) != 15
                          ? QStringLiteral("HP-15C only")
                          : QStringLiteral("HP-15C programs in the format of Torsten Manz's HP-15C Simulator"));
    }
    if (matrixEditor_ && matrixEditor_->property("vcAvailable").toBool() != is15) {
        matrixEditor_->setProperty("vcAvailable", is15);
        matrixEditor_->setAvailable(is15);
    }
    {
        const int m = currentModel();
        actBack_->setEnabled(run && !BackWindow::resourceFor(m).isEmpty());
        actBack_->setToolTip(actBack_->isEnabled() ? QStringLiteral("The back of the HP-%1C, with its tables, in a separate window").arg(m)
                                                   : QStringLiteral("HP-15C and HP-16C only"));
        if (backWindow_) backWindow_->setModel(run ? m : 0);
    }
    actA4_->setChecked(paper_ == Paper::A4); actLetter_->setChecked(paper_ == Paper::Letter);
    actPower_->setEnabled(run); actReset_->setEnabled(run);
    const bool busy = linkBusy();
    actRead_->setEnabled(run && !busy && hasMemoryFiles());
    actWrite_->setEnabled(run && !busy && hasMemoryFiles());
    actSaveCalc_->setEnabled(!busy);
    if (actRelease_) actRelease_->setEnabled(!latchedShown_.isEmpty());
    rebuildModelMenu();
    rebuildKeysMenu();
}

// ---- messages ------------------------------------------------------------------------------------------------------

void MainWindow::showError(const QString &m) { if (prompts.error) prompts.error(m); }
void MainWindow::showNotice(const QString &m) { if (prompts.notice) prompts.notice(m); }
void MainWindow::stockUnavailable(const QString &what) {
    showError(QStringLiteral("%1 is not available with HP's original 12C firmware. ").arg(what) + kStockLimits);
}

// ---- firmware and engine ---------------------------------------------------------------------------------------------

void MainWindow::launch() {
    if (running_ || starting_) return;
    if (auto fw = FirmwareStore::load()) startFirmware(fw->data, fw->name, false);
}

void MainWindow::chooseFirmware() {
    QString p = prompts.openFile(QStringLiteral("Choose the firmware file"), QStringLiteral("Calculator firmware (*.bin);;All files (*)"));
    if (!p.isEmpty()) openFirmwareFile(p);
}

bool MainWindow::openFirmwareFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { showError(QStringLiteral("Could not read “%1”: %2").arg(QFileInfo(path).fileName(), f.errorString())); return false; }
    QByteArray data = f.readAll();
    if (data.size() < 4096 || data.size() > 4 * 1024 * 1024) {
        showError(QStringLiteral("“%1” does not look like a calculator firmware file (it should be a .bin of about 112 KB).").arg(QFileInfo(path).fileName()));
        return false;
    }
    saveStateNow();                                   // keep the memory of the firmware that runs now
    startFirmware(data, QFileInfo(path).fileName(), true);
    return true;
}

void MainWindow::stopEngine() {
    if (thread_) {
        if (worker_) QMetaObject::invokeMethod(worker_, [w = worker_] { w->stop(); }, Qt::BlockingQueuedConnection);
        thread_->quit();
        thread_->wait();
        delete worker_;
        delete thread_;
        worker_ = nullptr; thread_ = nullptr;
    }
    running_ = false;
    pressed_.clear(); latchedShown_.clear(); latch_ = KeyLatch();
    calc_->setPressed({}); calc_->setLatched({}); calc_->setKeysEnabled(false);
    inspection_.reset(); inspector_->setSnapshot(nullptr);
}

void MainWindow::startFirmware(const QByteArray &data, const QString &name, bool remember) {
    stopEngine();
    starting_ = true; rememberAfterBoot_ = remember; pendingName_ = name; firmwareData_ = data;
    welcome_->setStarting(true);
    stack_->setCurrentWidget(welcome_);
    const QString id = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    if (!fw_.id.isEmpty() && fw_.id != id) { clearAllNotes(true); NotesStore::forget(id); }    // 0.6.9: another firmware: nothing of the old notes is left
    auto saved = StateStore::load(id);
    thread_ = new QThread(this);
    worker_ = new EngineWorker;
    worker_->moveToThread(thread_);
    connect(worker_, &EngineWorker::booted, this, &MainWindow::onBooted);
    connect(worker_, &EngineWorker::displayChanged, this, &MainWindow::onDisplay);
    connect(worker_, &EngineWorker::inspectorChanged, this, &MainWindow::onInspector);
    connect(worker_, &EngineWorker::fault, this, [this](const QString &m) { showError(m); });
    thread_->start();
    bool has = saved.has_value();
    SavedState st = saved.value_or(SavedState());
    worker_->post([w = worker_, data, has, st] { w->start(data, has, st); });
    updateMenus();
}

void MainWindow::onBooted(bool ok, const QString &message, const QString &note, const FirmwareDetails &d) {
    starting_ = false;
    welcome_->setStarting(false);
    if (!ok) {
        stopEngine();
        showError(message);
        updateMenus();
        return;
    }
    fw_ = d; firmwareName_ = pendingName_; running_ = true;
    inspectorLayout_ = VC_MEM16_NONE;
    connectedLayout_ = VC_MEM16_NONE;
    memoryUndo_.reset();
    memoryImportStatus_.clear();
    refreshImportStatus();
    if (rememberAfterBoot_) { QString err; FirmwareStore::remember({firmwareName_, firmwareData_}, &err); }
    calc_->setKeysEnabled(true);
    stack_->setCurrentWidget(split_);
    calc_->setFocus();
    memoryFileName_.clear(); lastProgramKeys_.clear(); notesPath_.clear(); exportNames_.forgetAll();
    doc_ = ProgramDocument();
    loadStoredNotes();
    worker_->post([w = worker_, ins = showInspector_ && hasInspector()] { w->setInspector(ins); });
    inspector_->setEmptyText(isStock() ? QStringLiteral("HP's original 12C firmware keeps its registers and program in a layout this panel cannot read, so Registers & Program is not available with it.")
                                       : QStringLiteral("The calculator is switching models."));
    updateInspectorVisibility();
    updateMenus();
    QString extra = note;
    if (isStock()) {
        QSettings s;
        const QString key = QStringLiteral("stockNoticeSeen-") + fw_.id;
        if (!s.value(key, false).toBool()) {
            s.setValue(key, true);
            extra = (extra.isEmpty() ? QString() : extra + QStringLiteral("\n\n")) + QStringLiteral("This is HP's original 12C firmware. The calculator runs, with the keyboard and the display.\n\n") + kStockLimits;
        }
    }
    if (!extra.isEmpty()) showNotice(extra);
}

void MainWindow::onDisplay(const DisplaySnapshot &s) {
    const bool modelChanged = s.model != snap_.model || s.variant != snap_.variant;
    snap_ = s;
    calc_->setDisplay(s);
    if (modelChanged) { updateMenus(); updateInspectorVisibility(); }
}

void MainWindow::onInspector(std::shared_ptr<const InspectorSnapshot> s) {
    inspection_ = s;
    inspector_->setSnapshot(s);
    if (s) programChanged(*s);
}

void MainWindow::updateInspectorVisibility() {
    const bool show = running_ && showInspector_ && hasInspector();
    const int handle = split_->handleWidth();
    if (running_ && restorePending_) {
        // first layout after the start: the window already is the restored size, which did or did not include the flyout
        restorePending_ = false;
        inspector_->setVisible(show);
        if (show) split_->setSizes({std::max(300, split_->width() - inspectorWidth_ - handle), inspectorWidth_});
        if (show && !savedHadInspector_) resizeWindowBy(inspectorWidth_ + handle);        // saved without flyout: make room once
        else if (!show && savedHadInspector_) resizeWindowBy(-(inspectorWidth_ + handle)); // saved with flyout, none now
        trimSideBars(show ? inspectorWidth_ + handle : 0);
        fitWindowToScreen();
        if (show) split_->setSizes({std::max(300, split_->width() - inspectorWidth_ - handle), inspectorWidth_});
    } else if (inspector_->isVisible() != show) {
        // the user opened/closed the flyout (or a model change made it appear): the window gets/gives its width
        int w = inspectorWidth_;
        if (!show && split_->sizes().size() == 2 && split_->sizes()[1] >= 260) w = inspectorWidth_ = split_->sizes()[1];
        inspector_->setVisible(show);
        if (show) split_->setSizes({std::max(300, split_->width() - w - handle), w});
        resizeWindowBy(show ? w + handle : -(w + handle));
        if (show) split_->setSizes({std::max(300, split_->width() - w - handle), w});
    }
    actInspector_->setChecked(show);
}

void MainWindow::setShowInspector(bool on) {
    showInspector_ = on;
    QSettings().setValue(QStringLiteral("showInspector"), on);
    if (worker_) worker_->post([w = worker_, v = on && hasInspector()] { w->setInspector(v); });
    updateInspectorVisibility();
}

void MainWindow::setShowKeyHelp(bool on) {
    calc_->setShowKeyHelp(on);
    if (actKeyHelp_) actKeyHelp_->setChecked(on);
    QSettings().setValue(QStringLiteral("showKeyHelp"), on);
}

void MainWindow::setLegendClicks(bool on) {
    calc_->setLegendClicks(on);
    if (actLegend_) actLegend_->setChecked(on);
    QSettings().setValue(QStringLiteral("legendClicks"), on);
}

void MainWindow::setKeyClick(bool on) {
    keyClick_ = on;
    if (actClick_) actClick_->setChecked(on);
    QSettings().setValue(QStringLiteral("keyClick"), on);
}

void MainWindow::openMatrixEditor() {
    if (!running_ || !hasMemoryFiles()) return;
    if ((snap_.model ? snap_.model : calc_->modelId()) != 15) return showError(QStringLiteral("The Matrix Editor is for the HP-15C. Switch the model to the 15C first."));
    if (!matrixEditor_) {
        MatrixEditor::Hooks h;
        h.fetch = [this]() -> std::optional<std::pair<QByteArray, int>> {
            if (!worker_ || !running_) return std::nullopt;
            QByteArray img; int var = 0;
            if (!worker_->performAndWait([&](Machine &m) { if (m.model() == 15) { img = m.rawMemory(); var = m.variant(); } })) return std::nullopt;
            if (img.size() != VC_MEMORY_SIZE || (var != 150 && var != 152)) return std::nullopt;
            return std::make_pair(img, var);
        };
        h.apply = [this](int m, int r, int c, std::vector<uint64_t> raw, std::function<void(const QString &)> done) {
            if (!worker_) return done(QStringLiteral("The calculator is not running."));
            worker_->submit(this, [m, r, c, raw](Machine &mc) { mc.writeMatrix15(m, r, c, raw); }, [this, done](const QString &e) {
                if (e.isEmpty()) saveStateNow(true);          // keep it across a restart, like a loaded memory file
                done(e);
            });
        };
        if (prompts.matrixHooks) prompts.matrixHooks(h);
        matrixEditor_ = new MatrixEditor(h, this);
        matrixEditor_->setProperty("vcAvailable", true);
    } else matrixEditor_->reload();
    matrixEditor_->show(); matrixEditor_->raise(); matrixEditor_->activateWindow();
}

void MainWindow::setPaper(Paper p) {
    paper_ = p; QSettings().setValue(QStringLiteral("pdfPaper"), p == Paper::A4 ? QStringLiteral("a4") : QStringLiteral("letter"));
    actA4_->setChecked(p == Paper::A4); actLetter_->setChecked(p == Paper::Letter);
}

void MainWindow::saveStateNow(bool turnBackOn) {
    if (!worker_ || !running_ || !hasMemoryFiles()) return;
    SavedState s;
    if (worker_->performAndWait([&](Machine &m) { s = m.savedState(turnBackOn); })) StateStore::save(s);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    saveWindowSettings();
    saveStateNow(false);
    stopEngine();
    QMainWindow::closeEvent(e);
}

void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    busy_->adjustSize();
    busy_->move((stack_->width() - busy_->width()) / 2, stack_->height() - busy_->height() - 16);
    refreshImportStatus();
}

// ---- keys ------------------------------------------------------------------------------------------------------------

void MainWindow::press(int code) {
    if (!running_) return;
    if (keyClick_) KeyClick::play();
    applyLatch(latch_.press(code));
}
void MainWindow::release(int code) { applyLatch(latch_.release(code)); }

void MainWindow::pressLegend(int code, int shift) {
    if (!running_) return;
    if (keyClick_) KeyClick::play();
    applyLatch(latch_.unlatchAll());
    pressed_.insert(shown(code)); calc_->setPressed(pressed_);
    worker_->post([w = worker_, shift, code] { w->pressShifted(shift, code); });
}

void MainWindow::toggleLatch(int code) {
    if (!running_) return;
    applyLatch(latch_.toggle(code));
}

void MainWindow::releaseLatchedKeys() { applyLatch(latch_.unlatchAll()); }

void MainWindow::applyLatch(const std::vector<KeyLatch::Action> &actions) {
    if (!worker_) return;
    for (const auto &a : actions) {
        switch (a.kind) {
        case KeyLatch::Action::Press:
            pressed_.insert(shown(a.keys[0]));
            worker_->post([w = worker_, k = a.keys[0]] { w->pressKey(k); });
            break;
        case KeyLatch::Action::Release:
            pressed_.remove(shown(a.keys[0]));
            worker_->post([w = worker_, k = a.keys[0]] { w->releaseKey(k); });
            break;
        case KeyLatch::Action::ReleaseLater: {
            QVector<int> ks;
            for (int k : a.keys) { pressed_.remove(shown(k)); ks << k; }
            worker_->post([w = worker_, ks] { w->releaseLater(ks, KeyLatch::kReleaseDelayMs); });
            break;
        }
        }
    }
    latchedShown_.clear();
    for (int k : latch_.latched()) latchedShown_.insert(shown(k));
    calc_->setPressed(pressed_); calc_->setLatched(latchedShown_);
    if (actRelease_) actRelease_->setEnabled(!latchedShown_.isEmpty());
}

void MainWindow::togglePower() { press(Machine::keyON); release(Machine::keyON); }

void MainWindow::perform(KeyChord c) {
    if (!running_) return;
    if (chordInfo(c).needsConfirmation) {
        PendingAction a; a.kind = PendingAction::MemoryReset;
        int r = prompts.confirm ? prompts.confirm(a) : 0;
        if (r == 2) backupThenConfirm(a); else if (r == 1) confirm(a);
        return;
    }
    releaseLatchedKeys();
    worker_->submit(this, [c](Machine &m) { m.perform(c); }, [this](const QString &err) { if (!err.isEmpty()) showError(err); });
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e) {
    if ((e->type() != QEvent::KeyPress && e->type() != QEvent::KeyRelease) || !running_) return false;
    if (auto *tw = qobject_cast<QWidget *>(obj); tw && tw->window() != this) return false;   // aimed at another window
    if (QApplication::activeWindow() != this || QApplication::activeModalWidget()) return false;
    QWidget *fw = QApplication::focusWidget();
    if (fw && fw->window() != this) return false;     // another window (the Matrix Editor) has the keyboard
    if (qobject_cast<QLineEdit *>(fw) || qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw) || qobject_cast<QAbstractSpinBox *>(fw)
        || qobject_cast<QComboBox *>(fw)) return false;
    if (stack_->currentWidget() != split_) return false;
    auto *ke = static_cast<QKeyEvent *>(e);
    const bool used = calc_->handleKey(ke);
    if (ke->key() == Qt::Key_Alt) return true;       // keep a lone Alt away from the menu bar
    return used;
}

// ---- drag and drop ------------------------------------------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() && !e->mimeData()->urls().isEmpty() && e->mimeData()->urls().first().isLocalFile()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e) {
    if (!e->mimeData()->hasUrls() || e->mimeData()->urls().isEmpty()) return;
    const QString path = e->mimeData()->urls().first().toLocalFile();
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    e->acceptProposedAction();
    if (ext == QLatin1String("voyagerdoc")) { if (running_) openNotesFile(path); return; }
    if (fi.size() == Emulator::kMemorySize || MemoryFileNames::modelHint(ext) != 0) {
        if (running_) openMemoryFile(path); else showError(QStringLiteral("Choose the firmware file first (the .bin of about 112 KB), then open memory files."));
        return;
    }
    openFirmwareFile(path);
}

// ---- model chooser, confirmations ------------------------------------------------------------------------------------

void MainWindow::chooseModel(int v) {
    if (isStock()) return stockUnavailable(QStringLiteral("Switching the model"));
    if (!(fw_.images.size() > 1 && running_ && !switching_) || std::find(fw_.images.begin(), fw_.images.end(), v) == fw_.images.end()) return;
    PendingAction a; a.kind = PendingAction::SwitchModel; a.variant = v;
    a.keepsMemory = fw_.keepsModeMemory;
    int r = prompts.confirm ? prompts.confirm(a) : 0;
    if (r == 2) backupThenConfirm(a); else if (r == 1) confirm(a);
}

void MainWindow::backupThenConfirm(const PendingAction &a) {
    saveMemory([this, a] { confirm(a); });
}

void MainWindow::confirm(const PendingAction &a) {
    switch (a.kind) {
    case PendingAction::MemoryReset:
        releaseLatchedKeys();
        clearAllNotes(true);                          // 0.6.9: a reset clears the notes too
        worker_->submit(this, [](Machine &m) { m.perform(KeyChord::MemoryReset); }, [this](const QString &e) { if (!e.isEmpty()) showError(e); });
        break;
    case PendingAction::SwitchModel:
        switchModel(a.variant);
        break;
    case PendingAction::LoadMemory:
    case PendingAction::LoadMemoryAnyway: {
        MemoryUndo undo;
        undo.variant = snap_.variant; undo.firmwareId = fw_.id; undo.doc = doc_; undo.notesPath = notesPath_; undo.memoryFile = memoryFileName_;
        QString err;
        const bool saved = worker_->performAndWait([&](Machine &m) { undo.data = m.memory(true); }, &err);
        if (!saved) { showError(QStringLiteral("Could not retain an undo backup; nothing was loaded: %1").arg(err)); return; }
        worker_->submit(this, [d = a.data](Machine &m) { m.putMemory(d); }, [this, undo](const QString &e) {
            if (!e.isEmpty()) showError(e);
            else {
                memoryUndo_ = undo;
                memoryLoaded();
                if (memoryImportStatus_.isEmpty()) memoryImportStatus_ = QStringLiteral("Memory loaded. Undo Load restores the previous emulator memory.");
                refreshImportStatus();
            }
        });
        break;
    }
    case PendingAction::SwitchThenLoad: {
        PendingAction load; load.kind = PendingAction::LoadMemory; load.data = a.data;
        switchModel(a.variant, [this, load] { confirm(load); });
        break;
    }
    }
}

void MainWindow::switchModel(int v, std::function<void()> then) {
    if (!worker_) return;
    releaseLatchedKeys();
    switching_ = true; updateMenus();
    worker_->submit(this, [v](Machine &m) { m.switchModel(v); }, [this, then](const QString &err) {
        switching_ = false;
        if (err.isEmpty()) {
            if (!fw_.keepsModeMemory) clearAllNotes(true);  // Multi/3-ROM and unknown versions: the switch can clear memory, so the notes go too
            saveStateNow(true); if (then) then();
        }
        else showError(err);
        updateMenus();
    });
}

void MainWindow::resetCalculator() {
    if (!running_) return;
    stopEngine();
    clearAllNotes(true);                              // 0.6.9: everything of the old program, notes included
    StateStore::clear(fw_.id);
    if (auto fw = FirmwareStore::load()) {
        // start without the remembered memory
        starting_ = true; rememberAfterBoot_ = false; pendingName_ = fw->name; firmwareData_ = fw->data;
        welcome_->setStarting(true); stack_->setCurrentWidget(welcome_);
        thread_ = new QThread(this); worker_ = new EngineWorker; worker_->moveToThread(thread_);
        connect(worker_, &EngineWorker::booted, this, &MainWindow::onBooted);
        connect(worker_, &EngineWorker::displayChanged, this, &MainWindow::onDisplay);
        connect(worker_, &EngineWorker::inspectorChanged, this, &MainWindow::onInspector);
        connect(worker_, &EngineWorker::fault, this, [this](const QString &m) { showError(m); });
        thread_->start();
        QByteArray data = fw->data;
        worker_->post([w = worker_, data] { w->start(data, false, SavedState()); });
    } else {
        stack_->setCurrentWidget(welcome_);
    }
    updateMenus();
}

// ---- memory files --------------------------------------------------------------------------------------------------------

void MainWindow::openMemory() {
    if (!hasMemoryFiles()) return stockUnavailable(QStringLiteral("Opening a memory file"));
    QString p = prompts.openFile(QStringLiteral("Open a memory file"), QStringLiteral("Calculator memory (*.bin12c *.bin15c *.bin16c *.12c *.15c *.16c *.12cmem *.15cmem *.16cmem *.bin);;All files (*)"));
    if (!p.isEmpty()) openMemoryFile(p);
}

void MainWindow::openMemoryFile(const QString &path) {
    if (!hasMemoryFiles()) return stockUnavailable(QStringLiteral("Opening a memory file"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { showError(QStringLiteral("Could not read “%1”: %2").arg(QFileInfo(path).fileName(), f.errorString())); return; }
    QByteArray data = f.readAll();
    const QString name = QFileInfo(path).fileName();
    // by content, never by the ending (0.6.9): any 2048-byte file opens; a text program gets its own message
    if (const QString why = MemoryFileNames::problem(data, name, Emulator::kMemorySize); !why.isEmpty()) { showError(why); return; }
    findSidecar(path);
    offerMemory(data, QStringLiteral("“%1”").arg(name));
}

void MainWindow::refreshImportStatus() {
    const bool on = !memoryImportStatus_.isEmpty();
    importLabel_->setText(memoryImportStatus_);
    importLabel_->setToolTip(memoryImportStatus_);
    undoLoad_->setVisible(canUndoMemoryImport());
    importBar_->setVisible(on);
    if (!on) return;
    importBar_->setMaximumWidth(std::min(560, std::max(280, stack_->width() - 24)));
    importBar_->adjustSize();
    importBar_->move(12, stack_->height() - importBar_->height() - 12);
    importBar_->raise();
}

bool MainWindow::canUndoMemoryImport() const {
    return memoryUndo_ && memoryUndo_->firmwareId == fw_.id && memoryUndo_->variant == snap_.variant;
}

void MainWindow::chooseInspectorLayout(int layout) {
    inspectorLayout_ = layout;
    if (!worker_ || !running_) return;
    worker_->submit(this, [layout](Machine &m) { m.calc.setMemory16Layout(Emulator::Memory16Layout(layout)); }, [](const QString &) {});
}

void MainWindow::setConnected16Layout(int layout) { connectedLayout_ = layout; }

void MainWindow::undoMemoryImport() {
    if (!canUndoMemoryImport() || !worker_) return;
    const MemoryUndo previous = *memoryUndo_;
    worker_->submit(this, [d = previous.data](Machine &m) { m.putMemory(d); }, [this, previous](const QString &err) {
        if (!err.isEmpty()) { showError(err); return; }
        doc_ = previous.doc;
        notesPath_ = previous.notesPath;
        memoryFileName_ = previous.memoryFile;
        memoryFileCandidate_.clear();
        pendingSidecar_.reset();
        lastProgramKeys_.clear();
        storeNotes();
        QMap<int, QString> comments;
        for (auto &c : doc_.comments) if (!c.orphaned) comments[c.step] = c.text;
        inspector_->setComments(comments);
        memoryUndo_.reset();
        memoryImportStatus_ = QStringLiteral("Previous emulator memory restored. Original import file unchanged.");
        refreshImportStatus();
    });
}

int MainWindow::memory16Target() const {
    // 0.7.7: an explicit choice in Model → 16C inspector layout (stock or 206) is the user's statement of the
    // running firmware's pool; it wins over the ROM-detected layout. An unsupported pool stays unsupported.
    const int chosen = inspectorLayout_;
    if (fw_.mem16Layout == VC_MEM16_UNSUPPORTED) return VC_MEM16_UNSUPPORTED;
    if (chosen == VC_MEM16_STD || chosen == VC_MEM16_LAYOUT206) return chosen;
    return fw_.mem16Layout;
}

void MainWindow::offerMemory(const QByteArray &data, const QString &source, int explicitLayout) {
    const int target = memory16Target();
    if (snap_.variant == 160 && target == VC_MEM16_UNSUPPORTED) {
        showError(QStringLiteral("This firmware's 16C memory is neither stock nor 206 slots, the only layouts VoyagerCalc supports. Host memory loading is off for it; keep a raw backup and update the calculator to EAEA, C9C9 or ECEC. Nothing was loaded."));
        return;
    }
    if (snap_.variant == 160 && target == VC_MEM16_UNDETERMINED) {
        showError(QStringLiteral("This extended-memory firmware is not a known build, so its 16C layout was not guessed. Choose Stock (29 slots) or 206 slots in Model → 16C inspector layout, then open the file again. Nothing was loaded."));
        return;
    }
    const bool overridden = target != fw_.mem16Layout;
    const QByteArray fw = firmwareData_;
    const int v = snap_.variant;
    const std::vector<int> imgs = fw_.images;
    const QString name = QStringLiteral("HP-") + model().name();
    QThread *t = QThread::create([this, data, fw, v, imgs, name, source, target, explicitLayout, overridden] {
        QByteArray img = data;
        QString src = source;
        QString note;
        if (v == 160 && data.size() == Emulator::kMemorySize) {
            try {
                std::optional<Emulator::Memory16Layout> known;
                if (explicitLayout == VC_MEM16_STD || explicitLayout == VC_MEM16_LAYOUT206)
                    known = Emulator::Memory16Layout(explicitLayout);
                const Emulator::Memory16Import prepared = Emulator::prepareMemory16(data, Emulator::Memory16Layout(target), known);
                img = prepared.data;
                note = prepared.note;
                if (overridden) note += QStringLiteral(" Target: %1 slots, as chosen in Model → 16C inspector layout.").arg(Emulator::memory16Slots(Emulator::Memory16Layout(target)));
                if (prepared.converted) {
                    const QString from = prepared.ambiguous ? QStringLiteral("compatible raw layouts") : QString::number(Emulator::memory16Slots(prepared.source));
                    src += QStringLiteral(" (converted %1 → %2; original file unchanged)").arg(from).arg(Emulator::memory16Slots(Emulator::Memory16Layout(target)));
                }
            } catch (const Error &e) {
                const QString message = e.message();
                QMetaObject::invokeMethod(qApp, [this, guard = QPointer<MainWindow>(this), message] { if (guard) showError(message); }, Qt::QueuedConnection);
                return;
            }
        }
        Machine::MemoryCheck verdict = Machine::checkMemory(img, fw, v);
        std::vector<int> others;
        if (!verdict.fits()) { std::vector<int> among; for (int i : imgs) if (i != v) among.push_back(i); others = Machine::identifyMemory(img, fw, among); }
        QMetaObject::invokeMethod(qApp, [this, guard = QPointer<MainWindow>(this), img, verdict, others, name, src, note] {
            if (!guard) return;
            if (!note.isEmpty()) { memoryImportStatus_ = note; refreshImportStatus(); }
            auto go = [&](const PendingAction &a) {
                int r = prompts.confirm ? prompts.confirm(a) : 0;
                if (r == 2) backupThenConfirm(a); else if (r == 1) confirm(a);
            };
            PendingAction a; a.data = img; a.source = src;
            switch (verdict.kind) {
            case Machine::MemoryCheck::Fits:
                a.kind = PendingAction::LoadMemory;
                go(a); break;
            case Machine::MemoryCheck::NeedsMoreMemory: {
                QString m = QStringLiteral("It uses more registers than the %1 mode this firmware is running has (g MEM gives Error 10, and DIM (i) cannot fix it). "
                                           "It is a memory from the HP-15C (15.2) double-memory mode.").arg(name);
                if (!others.empty()) { a.kind = PendingAction::SwitchThenLoad; a.variant = others.front(); a.reason = m + QStringLiteral(" Switch the model first (this clears the emulator's memory), then load it."); }
                else { a.kind = PendingAction::LoadMemoryAnyway; a.reason = m; }
                go(a); break;
            }
            case Machine::MemoryCheck::WrongSize:
                showError(QStringLiteral("The memory from %1 is %2 bytes; a calculator memory is 2048 bytes. Nothing was loaded.").arg(src).arg(verdict.bytes));
                break;
            default: {
                QString m = QStringLiteral("In a trial load on this firmware's %1").arg(name);
                if (verdict.kind == Machine::MemoryCheck::Rejected) m += QStringLiteral(", the calculator showed Pr Error with it (its memory would be cleared).");
                else if (verdict.kind == Machine::MemoryCheck::Changed) m += QStringLiteral(", the calculator started but changed %1 bytes of the registers / program area (it may have cleared them).").arg(verdict.bytes);
                else m += QStringLiteral(", the calculator did not show anything after ON.");
                if (!others.empty()) m += QStringLiteral(" It fits the HP-%1: choose that in the Model menu, then try again.").arg(Machine::variantName(others.front()));
                else m += QStringLiteral(" It may be from another model or firmware.");
                a.kind = PendingAction::LoadMemoryAnyway; a.reason = m; go(a);
            }
            }
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void MainWindow::saveDataAs(const QString &kind, const QString &defaultName, const QString &filter, const QByteArray &data, const QString &ext,
                            std::function<void(const QString &)> done) {
    QString name = exportNames_.name(kind, defaultName);
    QString path = prompts.saveFile(name + QLatin1Char('.') + ext, filter);
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + ext;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) { showError(QStringLiteral("Could not save “%1”: %2").arg(QFileInfo(path).fileName(), f.errorString())); return; }
    f.close();
    exportNames_.remember(kind, path);
    if (done) done(path);
}

void MainWindow::saveMemory(std::function<void()> after) {
    if (!hasMemoryFiles()) return stockUnavailable(QStringLiteral("Saving a memory file"));
    if (!running_) return;
    auto st = std::make_shared<SavedState>();
    worker_->submit(this, [st](Machine &m) { *st = m.savedState(true); }, [this, st, after](const QString &err) {
        if (!err.isEmpty()) return showError(err);
        const QString ext = memorySaveEnding();
        QString def = memoryFileName_.isEmpty() ? QStringLiteral("Calculator memory") : QFileInfo(memoryFileName_).completeBaseName();
        saveDataAs(QStringLiteral("memory"), def, QStringLiteral("Calculator memory (*.%1 *.bin12c *.bin15c *.bin16c);;All files (*)").arg(ext), st->memory, ext,
                   [after](const QString &) { if (after) after(); });
    });
}

void MainWindow::findSidecar(const QString &memoryPath) {
    memoryFileCandidate_ = QFileInfo(memoryPath).fileName();
    pendingSidecar_.reset();
    for (const QString &candidate : MemoryFileNames::sidecarCandidates(memoryPath)) {          // the name itself, then the other memory endings (0.6.9)
        QFile f(candidate);
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (auto d = ProgramDocument::decode(f.readAll())) { pendingSidecar_ = std::make_pair(*d, f.fileName()); break; }
    }
}

void MainWindow::memoryLoaded() {
    if (!memoryFileCandidate_.isEmpty()) { memoryFileName_ = memoryFileCandidate_; exportNames_.forgetAll(); }
    importedManz_.reset();
    memoryFileCandidate_.clear();
    lastProgramKeys_.clear();
    if (pendingSidecar_) { doc_ = pendingSidecar_->first; notesPath_ = pendingSidecar_->second; storeNotes(); }
    else if (doc_.isEmpty() && !memoryFileName_.isEmpty()) { doc_.title = QFileInfo(memoryFileName_).completeBaseName(); storeNotes(); }
    pendingSidecar_.reset();
    if (!inspector_->isVisible() && showInspector_) updateInspectorVisibility();
    inspector_->setComments([this] { QMap<int, QString> m; for (auto &c : doc_.comments) if (!c.orphaned) m[c.step] = c.text; return m; }());
}

// ---- program notes ---------------------------------------------------------------------------------------------------------

QString MainWindow::documentModelName() const { return ProgramDocumentInfo::modelNameFor(snap_.variant, snap_.model); }

// The ending offered in the save dialog: the one typed last time, else .bin12c / .bin15c / .bin16c. Only a suggestion (any ending can be typed).
QString MainWindow::memorySaveEnding() const {
    const QString last = exportNames_.ending(QStringLiteral("memory"));
    return last.isEmpty() ? MemoryFileNames::defaultExtension(model().id()) : last;
}

// A reset, a model switch or another firmware (0.6.9): nothing of the old program's notes is kept: all step comments, the orphans, title,
// author, description and the label / register / flag notes, the sidecar link, the stored copy and the remembered memory file name.
void MainWindow::clearAllNotes(bool forgetStored) {
    doc_ = ProgramDocument(); notesPath_.clear(); pendingSidecar_.reset(); memoryFileCandidate_.clear(); memoryFileName_.clear();
    importedManz_.reset();
    lastProgramKeys_.clear(); exportNames_.forgetAll();
    if (forgetStored) NotesStore::forget(fw_.id);
    if (inspector_) inspector_->setComments({});
}

void MainWindow::storeNotes() { NotesStore::save(fw_.id, doc_.encoded()); }
void MainWindow::loadStoredNotes() {
    if (auto d = ProgramDocument::decode(NotesStore::load(fw_.id))) doc_ = *d;
    inspector_->setComments([this] { QMap<int, QString> m; for (auto &c : doc_.comments) if (!c.orphaned) m[c.step] = c.text; return m; }());
}

void MainWindow::programChanged(const InspectorSnapshot &s) {
    if (!s.hasProgram || s.program.empty()) return;
    std::vector<QString> keys;
    for (auto &l : s.program) keys.push_back(l.keys);
    if (keys == lastProgramKeys_) return;
    const bool first = lastProgramKeys_.empty();
    lastProgramKeys_ = keys;
    bool stale = false;
    for (auto &c : doc_.comments) {
        auto it = std::find_if(s.program.begin(), s.program.end(), [&](const ProgramLine &l) { return l.line == c.step; });
        if (it == s.program.end() || it->keys != c.keys) { stale = true; break; }
    }
    if (!doc_.comments.empty() && (!first || stale)) {
        ProgramDocument d = doc_; d.rematch(s.program);
        if (d != doc_) { doc_ = d; storeNotes(); }
    }
    QMap<int, QString> m; for (auto &c : doc_.comments) if (!c.orphaned) m[c.step] = c.text;
    inspector_->setComments(m);
}

void MainWindow::fetchProgram(std::function<void(std::shared_ptr<const InspectorSnapshot>)> done) {
    if (!hasInspector()) { stockUnavailable(QStringLiteral("Program documentation")); return done(nullptr); }
    if (!running_) return done(nullptr);
    auto out = std::make_shared<std::shared_ptr<const InspectorSnapshot>>();
    worker_->submit(this, [out](Machine &m) { auto s = inspect(m.calc, true); if (s) *out = std::make_shared<const InspectorSnapshot>(*s); },
                    [done, out](const QString &) { done(*out); });
}

ProgramDocumentContent MainWindow::documentContent(const InspectorSnapshot &s) {
    ProgramDocument d = doc_;
    d.model = documentModelName();
    ProgramDocumentInfo info;
    info.modelName = documentModelName(); info.firmware = firmwareName_; info.memoryFile = memoryFileName_; info.model = s.model;
    return ProgramDocumentContent(d, info, s.hasProgram ? s.program : std::vector<ProgramLine>{}, s.pool, s.setFlags(), s.pool16);
}

void MainWindow::openProgramInfo() {
    fetchProgram([this](std::shared_ptr<const InspectorSnapshot> s) {
        if (!s) return;
        docLines_ = s->hasProgram ? s->program : std::vector<ProgramLine>{};
        ProgramDocument d = doc_;
        d.rematch(docLines_);
        d.prefill(ProgramAnalysis(docLines_, s->model));
        if (d.title.isEmpty() && !memoryFileName_.isEmpty()) d.title = QFileInfo(memoryFileName_).completeBaseName();
        d.model = documentModelName();
        ProgramInfoDialog dlg(d, docLines_, s->model, documentModelName(), notesPath_.isEmpty() ? QString() : QFileInfo(notesPath_).fileName(), this);
        if (dlg.exec() == QDialog::Accepted) {
            doc_ = dlg.document(); storeNotes();
            if (dlg.next() == ProgramInfoDialog::SaveNotes) saveNotes();
            else if (dlg.next() == ProgramInfoDialog::ExportPdf) exportPdf();
        }
    });
}

void MainWindow::openNotes() {
    if (!hasInspector()) return stockUnavailable(QStringLiteral("Program notes"));
    QString p = prompts.openFile(QStringLiteral("Open program notes"), QStringLiteral("Program notes (*.voyagerdoc *.json);;All files (*)"));
    if (!p.isEmpty()) openNotesFile(p);
}

void MainWindow::openNotesFile(const QString &path) {
    QFile f(path);
    auto doc = f.open(QIODevice::ReadOnly) ? ProgramDocument::decode(f.readAll()) : std::nullopt;
    if (!doc) { showError(QStringLiteral("“%1” is not a VoyagerCalc program notes file.").arg(QFileInfo(path).fileName())); return; }
    notesPath_ = path;
    fetchProgram([this, d = *doc, path](std::shared_ptr<const InspectorSnapshot> s) mutable {
        if (s && s->hasProgram && !s->program.empty()) {
            auto r = d.rematch(s->program);
            if (r.orphaned > 0)
                showNotice(QStringLiteral("%1 comment%2 in “%3” no longer match a step of the program in the calculator. They are kept and listed in Program Info.")
                               .arg(r.orphaned).arg(r.orphaned == 1 ? "" : "s").arg(QFileInfo(path).fileName()));
        }
        lastProgramKeys_.clear();
        if (s) for (auto &l : s->program) lastProgramKeys_.push_back(l.keys);
        doc_ = d; storeNotes();
        QMap<int, QString> m; for (auto &c : doc_.comments) if (!c.orphaned) m[c.step] = c.text;
        inspector_->setComments(m);
    });
}

void MainWindow::saveNotes() {
    if (!hasInspector()) return stockUnavailable(QStringLiteral("Program notes"));
    QString base = !notesPath_.isEmpty() ? QFileInfo(notesPath_).completeBaseName() : !memoryFileName_.isEmpty() ? memoryFileName_ : doc_.title.isEmpty() ? QStringLiteral("Program") : doc_.title;
    saveDataAs(QStringLiteral("notes"), base, QStringLiteral("Program notes (*.voyagerdoc);;All files (*)"), doc_.encoded(), QStringLiteral("voyagerdoc"),
               [this](const QString &p) { notesPath_ = p; });
}

void MainWindow::exportPdf() {
    const Paper paper = paper_;
    fetchProgram([this, paper](std::shared_ptr<const InspectorSnapshot> s) {
        if (!s) return;
        ProgramDocumentContent c = documentContent(*s);
        QByteArray pdf = renderProgramDocumentPdf(c, paper, pdfFonts());
        saveDataAs(QStringLiteral("pdf"), c.title() + QStringLiteral(" documentation"), QStringLiteral("PDF document (*.pdf)"), pdf, QStringLiteral("pdf"));
    });
}

void MainWindow::exportText() {
    fetchProgram([this](std::shared_ptr<const InspectorSnapshot> s) {
        if (!s) return;
        ProgramDocumentContent c = documentContent(*s);
        saveDataAs(QStringLiteral("text"), c.title() + QStringLiteral(" listing"), QStringLiteral("Text file (*.txt)"), c.plainText().toUtf8(), QStringLiteral("txt"));
    });
}

void MainWindow::clearNotes() { doc_ = ProgramDocument(); notesPath_.clear(); storeNotes(); inspector_->setComments({}); }

void MainWindow::setComment(const QString &text, int step) {
    const auto lines = inspection_ ? inspection_->program : docLines_;
    ProgramDocument d = doc_;
    d.setComment(text, step, lines);
    doc_ = d; storeNotes();
    QMap<int, QString> m; for (auto &c : doc_.comments) if (!c.orphaned) m[c.step] = c.text;
    inspector_->setComments(m);
}

void MainWindow::editComment(int step) {
    const int mdl = inspection_ ? inspection_->model : snap_.model;
    bool ok = false;
    QString text = QInputDialog::getText(this, QStringLiteral("Comment for step %1").arg(step, mdl == 12 ? 2 : 3, 10, QLatin1Char('0')),
                                         QStringLiteral("Shown in the listing and the exported documentation. It stays with this step when lines are inserted or deleted.\nEmpty removes the comment."),
                                         QLineEdit::Normal, doc_.comment(step), &ok);
    if (ok) setComment(text.trimmed(), step);
}

// ---- real calculator over USB ------------------------------------------------------------------------------------------------

void MainWindow::setBusy(const QString &status, int watchMs) {
    linkStatus_ = status;
    busy_->setText(status);
    busy_->setVisible(!status.isEmpty());
    busy_->adjustSize();
    busy_->move((stack_->width() - busy_->width()) / 2, stack_->height() - busy_->height() - 16);
    busy_->raise();
    if (status.isEmpty() || watchMs <= 0) watchdog_->stop(); else watchdog_->start(watchMs);
    updateMenus();
}

void MainWindow::linkFailedMessage(const QString &m, bool offerFix) {
    if (offerFix && prompts.askFixPermissions) { if (prompts.askFixPermissions(m)) fixUsbPermissions(); }
    else showError(m);
}

void MainWindow::linkFailed(const LinkError &e) { linkFailedMessage(e.message(), e.kind == LinkError::AccessDenied && usbNeedsPermissionFix()); }

/// Blocking USB work on its own thread, with a watchdog (the window never waits on it). `work` returns what to
/// do on the window's thread when it succeeded.
void MainWindow::usbRun(const QString &status, std::function<std::function<void()>(CalculatorLink &)> work) {
    const int ticket = ++linkTicket_;
    setBusy(status);
    UsbLog::write(status + QStringLiteral(" (requested; starting the USB thread)"));       // on the window's thread: proves the click arrived
    auto mk = makeTransport;
    QThread *t = QThread::create([this, ticket, mk, work, status, guard = QPointer<MainWindow>(this)] {
        std::function<void()> cont;
        std::optional<LinkError> le;
        QString err;
        try {
            UsbLog::write(QStringLiteral("USB thread running"));
            std::unique_ptr<HidTransport> tr = mk();
            CalculatorLink link(tr.get());
            if (usbFast) { link.commandPauseMs = 0; link.packetPauseMs = 0; link.readTimeoutMs = 400; link.retryAfterSilence = 50; }
            UsbLog::write(status + QStringLiteral(" (start)"));
            cont = work(link);
            UsbLog::write(status + QStringLiteral(" (done)"));
        } catch (const LinkError &e) { le = e; UsbLog::write(QStringLiteral("FAILED: ") + e.message()); }
        catch (const Error &e) { err = e.message(); UsbLog::write(QStringLiteral("FAILED: ") + err); }
        catch (const std::exception &e) { err = QString::fromUtf8(e.what()); UsbLog::write(QStringLiteral("FAILED: ") + err); }
        QMetaObject::invokeMethod(qApp, [this, ticket, cont, le, err, guard] {
            if (!guard || linkTicket_ != ticket) return;
            setBusy({});
            if (le) linkFailed(*le);
            else if (!err.isEmpty()) showError(err);
            else if (cont) cont();
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void MainWindow::readFromCalculator() {
    if (!hasMemoryFiles()) return stockUnavailable(QStringLiteral("Reading the real calculator's memory into the emulator"));
    if (!running_ || linkBusy()) return;
    usbRun(QStringLiteral("Reading the calculator's memory…"), [this](CalculatorLink &l) {
        QByteArray d = l.readMemory();
        return std::function<void()>([this, d] {
            memoryFileCandidate_.clear(); pendingSidecar_.reset();
            const int chosen = connectedLayout_;
            offerMemory(d, QStringLiteral("the calculator"), chosen == VC_MEM16_NONE ? -1 : chosen);
        });
    });
}

void MainWindow::saveCalculatorMemoryToFile() {
    if (linkBusy()) return;
    usbRun(QStringLiteral("Reading the calculator's memory…"), [this](CalculatorLink &l) {
        QByteArray d = l.readMemory();
        return std::function<void()>([this, d] {
            const QString ext = memorySaveEnding();
            saveDataAs(QStringLiteral("memory"), QStringLiteral("Calculator memory ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss")),
                       QStringLiteral("Calculator memory (*.%1 *.bin12c *.bin15c *.bin16c);;All files (*)").arg(ext), d, ext);
        });
    });
}

void MainWindow::writeToCalculator() {
    if (!hasMemoryFiles()) return stockUnavailable(QStringLiteral("Writing the emulator's memory to the real calculator"));
    if (!running_ || linkBusy()) return;
    const int sourceLayout = memory16Target(), targetLayout = connectedLayout_;
    auto physical = [](int l) { return l == VC_MEM16_STD || l == VC_MEM16_LAYOUT206; };
    if (snap_.variant == 160 && !physical(sourceLayout)) {
        showError(QStringLiteral("The emulator's 16C memory is neither stock nor 206 slots (an unsupported or unconfirmed firmware). Choose Stock or 206 slots in Model → 16C inspector layout first. Nothing was written."));
        return;
    }
    if (snap_.variant == 160 && !physical(targetLayout)) {
        showError(QStringLiteral("Before writing 16C memory, choose the connected calculator’s layout in Model → USB 16C layout, using its self-test code (EAEA/C9C9/ECEC: 206 slots; stock: 29). The USB protocol cannot report that version. Nothing was written."));
        return;
    }
    const QByteArray fw = firmwareData_;
    const std::vector<int> imgs = fw_.images.empty() ? std::vector<int>{snap_.variant} : fw_.images;
    const QString emu = QStringLiteral("HP-") + model().name();
    setBusy(QStringLiteral("Reading the emulator's memory…"));
    auto st = std::make_shared<SavedState>();
    worker_->submit(this, [st](Machine &m) { *st = m.savedState(true); }, [=](const QString &err) {
        setBusy({});
        if (!err.isEmpty()) return showError(err);
        QByteArray payload = st->memory;
        QString layoutNote;
        if (st->variant == 160) {
            if (sourceLayout != targetLayout) {          // stock <-> 206 (the only two layouts)
                const QByteArray converted = Emulator::convertPool(payload, Emulator::Memory16Layout(sourceLayout), Emulator::Memory16Layout(targetLayout));
                if (converted.isEmpty()) {
                    showError(QStringLiteral("Cannot safely convert the emulator’s memory to the connected calculator’s layout. High data may not fit, or the calculator is active. Nothing was written."));
                    return;
                }
                payload = converted;
            }
            layoutNote = QStringLiteral(" Layout: %1 → %2 slots; conversion if needed. Only the live mode is written.")
                             .arg(Emulator::memory16Slots(Emulator::Memory16Layout(sourceLayout)))
                             .arg(Emulator::memory16Slots(Emulator::Memory16Layout(targetLayout)));
        }
        usbRun(QStringLiteral("Backing up the calculator's memory…"), [=](CalculatorLink &l) {
            QByteArray old = l.readMemory();
            auto fits = Machine::identifyMemory(old, fw, imgs);
            bool big = Machine::needs152(st->memory, fw, imgs);
            QString why;
            QString backup = saveBackup(old, fits.empty() ? 0 : fits.front(), &why);
            if (backup.isEmpty()) throw Error(QStringLiteral("The calculator's current memory could not be backed up (%1), so nothing was written.").arg(why));
            return std::function<void()>([=] {
                PendingWrite w; w.memory = payload; w.emulatorModel = emu; w.calculatorModels = fits; w.backup = backup; w.needs152 = big; w.layoutNote = layoutNote;
                if (!prompts.confirmWrite || !prompts.confirmWrite(w)) return;
                usbRun(QStringLiteral("Writing to the calculator…"), [=](CalculatorLink &l2) {
                    try {
                        l2.writeMemory(w.memory, w.emulatorModel == QLatin1String("HP-15C") ? (CalculatorLink::flag8(w.memory) ? 1 : 0) : -1);
                    } catch (const LinkError &e) {
                        throw Error(e.message() + QStringLiteral(" The calculator's previous memory is in “%1”.").arg(QFileInfo(w.backup).fileName()));
                    }
                    if (usbSettleMs > 0) QThread::msleep(usbSettleMs);     // as VoyagerInterface: let the calculator settle
                    return std::function<void()>([=] {
                        showNotice(QStringLiteral("Done: the %1 memory was written to the calculator, read back and compared: the registers and program memory match "
                                                  "(the calculator kept its own USB-session state). Press ON on the calculator to leave USB mode, then ON again to use it.").arg(w.emulatorModel));
                    });
                });
            });
        });
    });
}

void MainWindow::fixUsbPermissions() {
#ifdef Q_OS_WIN
    showNotice(QStringLiteral("Windows needs no USB setup for the calculator."));
#else
    if (linkBusy()) return;
    const QString dev = HidrawTransport::findDevice();
    const int ticket = ++linkTicket_;
    setBusy(QStringLiteral("Waiting for the password prompt…"), 0);
    QThread *t = QThread::create([this, dev, ticket, guard = QPointer<MainWindow>(this)] {
        FixResult r = fixPermissions(dev);
        QMetaObject::invokeMethod(qApp, [this, r, ticket, guard] {
            if (!guard || linkTicket_ != ticket) return;
            setBusy({});
            if (r.noPkexec) {
                QMessageBox box(QMessageBox::Information, QStringLiteral("Fix USB Permissions"),
                                QStringLiteral("The password window could not be opened on this computer. Open a terminal, paste this one line and press Enter (it asks for the password there). Then unplug and plug in the calculator.\n\n%1").arg(manualCommand()), QMessageBox::Ok, this);
                QPushButton *copy = box.addButton(QStringLiteral("Copy the Line"), QMessageBox::ActionRole);
                box.exec();
                if (box.clickedButton() == copy) QApplication::clipboard()->setText(manualCommand());
            } else if (r.ok) {
                showNotice(QStringLiteral("Done. Linux now lets VoyagerCalc use the calculator over USB. If the calculator was already connected, unplug it, plug it in again, and put it in USB mode (turn it off, hold f and ENTER, press ON)."));
            } else showError(r.message);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
#endif
}

// ---- about --------------------------------------------------------------------------------------------------------------------

/// Help > Show USB Log: everything the USB link did (open, each report sent / received, errors); Copy or Save it and send it with a bug report.
void MainWindow::showUsbLog() {
    if (usbLogWindow_) { usbLogWindow_->show(); usbLogWindow_->raise(); usbLogWindow_->activateWindow(); return; }
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(QStringLiteral("USB Log"));
    d->resize(780, 460);
    auto *col = new QVBoxLayout(d);
    auto *info = new QLabel(QStringLiteral("Everything VoyagerCalc did on the USB link (also saved in %1). If reading fails, copy this and send it. "
                                           "For more detail run  voyagercalc --usb-selftest  in a terminal.").arg(UsbLog::filePath()), d);
    info->setWordWrap(true);
    col->addWidget(info);
    auto *view = new QPlainTextEdit(d);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(UsbLog::text());
    col->addWidget(view, 1);
    connect(&UsbLog::instance(), &UsbLog::lineAdded, view, [view](const QString &l) { view->appendPlainText(l); });
    auto *row = new QHBoxLayout;
    auto *copy = new QPushButton(QStringLiteral("Copy"), d);
    auto *save = new QPushButton(QStringLiteral("Save…"), d);
    auto *clear = new QPushButton(QStringLiteral("Clear"), d);
    auto *close = new QPushButton(QStringLiteral("Close"), d);
    row->addWidget(copy); row->addWidget(save); row->addWidget(clear); row->addStretch(1); row->addWidget(close);
    col->addLayout(row);
    connect(copy, &QPushButton::clicked, d, [view] { QApplication::clipboard()->setText(view->toPlainText()); });
    connect(save, &QPushButton::clicked, d, [this, view] {
        const QString path = prompts.saveFile ? prompts.saveFile(QStringLiteral("VoyagerCalc USB log.txt"), QStringLiteral("Text (*.txt);;All files (*)")) : QString();
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(view->toPlainText().toUtf8());
    });
    connect(clear, &QPushButton::clicked, d, [view] { UsbLog::clear(); view->clear(); });
    connect(close, &QPushButton::clicked, d, &QDialog::close);
    usbLogWindow_ = d;
    d->show();
}

void MainWindow::showAbout() {
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("About VoyagerCalc"));
    auto *col = new QVBoxLayout(&d);
    auto *t = new QLabel(QStringLiteral("<h2>VoyagerCalc</h2>"));
    col->addWidget(t);
    QString body = QStringLiteral("<p style=\"color:gray\">Version %1 (Linux)</p>"
                                  "<p>A calculator app that emulates the ARM chip of the CE calculators and runs the firmware file you supply.</p>").arg(QStringLiteral(VOYAGERCALC_VERSION));
    if (!firmwareName_.isEmpty()) {
        if (!fw_.identification.isEmpty())
            body += QStringLiteral("<p><b>%1</b><br><span style=\"color:gray\">%2</span></p>")
                        .arg(fw_.identification.toHtmlEscaped(), fw_.profileDetails.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        body += QStringLiteral("<p style=\"color:gray\">Firmware: %1<br>%2%3</p>").arg(firmwareName_.toHtmlEscaped(), fw_.info.toHtmlEscaped(),
                                                                                       fw_.checksum.isEmpty() ? QString() : QStringLiteral("<br>Checksum ") + fw_.checksum.toHtmlEscaped());
    }
    if (isStock()) body += QStringLiteral("<hr><p><b>HP's original 12C firmware</b></p><p style=\"color:gray\">%1</p>").arg(kStockLimits.toHtmlEscaped());
    body += QStringLiteral("<hr><p>Copyright © 2026 José González Divassón. MIT License.</p>"
                           "<p style=\"color:gray\">%1</p>"
                           "<p style=\"color:gray\">The program uses the Qt toolkit (LGPL v3), which is included as shared libraries.</p>"
                           "<p style=\"color:gray\">Not affiliated with, endorsed by or sponsored by HP Inc. Model numbers are used only to say which firmware the app works with.</p>")
                .arg(kFontCredit.toHtmlEscaped());
    auto *l = new QLabel(body);
    l->setWordWrap(true); l->setTextFormat(Qt::RichText); l->setMinimumWidth(440); l->setMaximumWidth(460);
    l->setObjectName(QStringLiteral("aboutText"));
    col->addWidget(l);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::accept);
    col->addWidget(bb);
    if (aboutHook) { aboutHook(l->text()); return; }
    d.exec();
}


// ---- 0.7.2: HP-15C Simulator program files (.15c) ---------------------------------------------------------------------

static const char *kNot15C = "Program files from the HP-15C Simulator are for the HP-15C. Switch the model to the 15C first.";

void MainWindow::importManzProgram() {
    if (!running_ || !hasInspector() || !hasMemoryFiles()) return stockUnavailable(QStringLiteral("Importing a program file"));
    if ((snap_.model ? snap_.model : calc_->modelId()) != 15) return showError(QString::fromUtf8(kNot15C));
    if (importingProgram_) return;
    const QString p = prompts.openFile(QStringLiteral("Import HP-15C Program"), QStringLiteral("HP-15C Simulator programs (*.15c *.15C *.txt);;All files (*)"));
    if (!p.isEmpty()) importManzFile(p);
}

void MainWindow::importManzFile(const QString &path) {
    const QString name = QFileInfo(path).fileName();
    if (!running_ || importingProgram_) return;
    if ((snap_.model ? snap_.model : calc_->modelId()) != 15) return showError(QString::fromUtf8(kNot15C));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return showError(QStringLiteral("“%1” cannot be read: %2").arg(name, f.errorString()));
    Manz15CProgram::ReadError err;
    auto program = Manz15CProgram::read(f.readAll(), &err);
    if (!program) return showError(QStringLiteral("“%1” cannot be imported. ").arg(name) + err.message());
    if (program->steps.empty()) return showError(QStringLiteral("“%1” has no program steps.").arg(name));
    const QString t = program->title.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(program->title);
    if (prompts.confirmImport && !prompts.confirmImport(QStringLiteral("Import “%1”?").arg(name),
            QStringLiteral("The program%1 has %2 steps. It replaces the program in the calculator and its notes; registers, matrices and settings are kept. "
                           "The steps are keyed in as on the calculator, which can take a little while.").arg(t).arg(program->steps.size())))
        return;
    importingProgram_ = true;
    updateMenus();
    setBusy(QStringLiteral("Importing %1…").arg(name), 0);
    auto report = std::make_shared<ProgramEntryReport>();
    auto prog = std::make_shared<Manz15CProgram>(*program);
    worker_->submit(this, [report, prog](Machine &m) { *report = enterProgram15(m, prog->steps); }, [this, report, prog, name](const QString &e) {
        importingProgram_ = false;
        setBusy(QString());
        updateMenus();
        if (!e.isEmpty()) return showError(QStringLiteral("“%1” was not imported. ").arg(name) + e);
        ProgramDocument doc;
        prog->apply(doc);
        if (doc.title.isEmpty()) doc.title = QFileInfo(name).completeBaseName();
        doc_ = doc; notesPath_.clear(); pendingSidecar_.reset(); memoryFileName_.clear(); memoryFileCandidate_.clear();
        lastProgramKeys_.clear(); exportNames_.forgetAll();
        importedManz_ = *prog;
        storeNotes();
        saveStateNow(true);
        inspector_->setComments({});
        if (report->mismatches.empty()) return showNotice(QStringLiteral("Imported “%1”: %2 steps.").arg(name).arg(report->steps));
        QStringList list;
        for (size_t i = 0; i < report->mismatches.size() && i < 5; ++i) {
            auto &mm = report->mismatches[i];
            list << QStringLiteral("%1 {%2} → {%3}").arg(mm.step, 3, 10, QLatin1Char('0')).arg(mm.file, mm.calculator);
        }
        const int n = int(report->mismatches.size());
        showError(QStringLiteral("“%1” was keyed in, but %2 step%3 in the calculator differ from the file: ").arg(name).arg(n).arg(n == 1 ? "" : "s")
                  + list.join(QStringLiteral("; ")) + (n > 5 ? QStringLiteral("; …") : QString()) + QStringLiteral(". Please check the program."));
    });
}

void MainWindow::exportManzProgram() {
    if (!running_ || !hasInspector() || !hasMemoryFiles()) return stockUnavailable(QStringLiteral("Exporting a program file"));
    if ((snap_.model ? snap_.model : calc_->modelId()) != 15) return showError(QString::fromUtf8(kNot15C));
    fetchProgram([this](std::shared_ptr<const InspectorSnapshot> s) {
        if (!s) return;
        if (s->model != 15) return showError(QString::fromUtf8(kNot15C));
        Manz15CProgram p;
        p.title = doc_.title.isEmpty() ? (memoryFileName_.isEmpty() ? QString() : QFileInfo(memoryFileName_).completeBaseName()) : doc_.title;
        p.description = doc_.description;
        p.labelNotes = Manz15CProgram::notes(doc_, importedManz_ ? importedManz_->labelNotes : std::vector<ManzNote>{});
        std::vector<QString> keys;
        if (s->hasProgram) for (auto &l : s->program) keys.push_back(l.keys);
        Manz15CProgram::ReadError err;
        auto steps = Manz15CProgram::stepsFromListing(keys, importedManz_ ? &importedManz_->steps : nullptr, &err);
        if (!steps) return showError(QStringLiteral("The program cannot be written as a .15c file: ") + err.message());
        p.steps = *steps;
        if (p.steps.empty()) return showError(QStringLiteral("There is no program in the calculator to export."));
        QString base = p.title.isEmpty() ? QStringLiteral("Program") : p.title;
        for (QChar c : QStringLiteral("/\\:*?\"<>|")) base.replace(c, QLatin1Char('-'));
        const int n = int(p.steps.size());
        saveDataAs(QStringLiteral("manz15c"), base, QStringLiteral("HP-15C Simulator program (*.15c);;All files (*)"), p.fileData(), QStringLiteral("15c"),
                   [this, n](const QString &path) { showNotice(QStringLiteral("Exported %1 steps to “%2”.").arg(n).arg(QFileInfo(path).fileName())); });
    });
}

}  // namespace vc

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// MainWindow.h - the window, its menus and everything the app does around the calculator (AppModel.swift,
// ProgramDocModel.swift, MainViews.swift, VoyagerCalcApp.swift): firmware, keys and latches, model chooser,
// memory files with the trial-load check and confirmations, program notes, PDF / text export, the USB link,
// "Fix USB Permissions", drag and drop. Every dialog goes through `Prompts`, so tests can answer them.
#pragma once
#include "EngineWorker.h"
#include "PdfExport.h"
#include "ProgramDocument.h"
#include "Stores.h"
#include "usb/CalculatorLink.h"
#include "usb/HidTransport.h"
#include <QMainWindow>
#include <QDialog>
#include <QPointer>
#include <QSet>
#include <functional>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QSplitter;
class QStackedWidget;
class QThread;
class QTimer;

namespace vc {

class CalculatorWidget;
class InspectorPanel;
class WelcomeWidget;

/// Something that clears the calculator's memory, waiting for the user's confirmation.
struct PendingAction {
    enum Kind { SwitchModel, MemoryReset, LoadMemory, LoadMemoryAnyway, SwitchThenLoad } kind = SwitchModel;
    int variant = 0;
    QByteArray data;
    QString source, reason;
    QString title() const;
    QString message() const;
    QString confirmTitle() const;
};

/// "Write to Calculator", after the calculator's own memory was read and backed up.
struct PendingWrite {
    QByteArray memory;
    QString emulatorModel;                // "HP-16C"
    std::vector<int> calculatorModels;    // variants the calculator's memory fits (empty: unknown)
    QString backup;                       // path of the backup file
    bool needs152 = false;
    QString calculatorModel() const;
    bool matches() const { return calculatorModel() == emulatorModel; }
    QString title() const;
    QString message() const;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// Every question and message of the app. Defaults use Qt's dialogs; tests replace them.
    struct Prompts {
        std::function<int(const PendingAction &)> confirm;            // 0 cancel, 1 go on, 2 save a backup first
        std::function<bool(const PendingWrite &)> confirmWrite;
        std::function<void(const QString &)> error, notice;
        std::function<QString(const QString &fileKind, const QString &filter)> openFile;
        std::function<QString(const QString &suggestedName, const QString &filter)> saveFile;
        std::function<bool(const QString &)> askFixPermissions;       // offered with the "no access" message
    };
    Prompts prompts;

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // firmware
    void launch();                                    // the remembered firmware, if any
    void chooseFirmware();
    bool openFirmwareFile(const QString &path);
    void startFirmware(const QByteArray &data, const QString &name, bool remember);

    // state
    bool isRunning() const { return running_; }
    bool isStarting() const { return starting_; }
    bool isSwitching() const { return switching_; }
    bool isStock() const { return fw_.stock(); }
    const FirmwareDetails &firmware() const { return fw_; }
    const DisplaySnapshot &snapshot() const { return snap_; }
    EngineWorker *worker() const { return worker_; }
    CalculatorWidget *calculator() const { return calc_; }
    InspectorPanel *inspector() const { return inspector_; }
    bool hasInspector() const { return fw_.capabilities & VC_CAP_INSPECT; }
    bool hasMemoryFiles() const { return fw_.capabilities & VC_CAP_MEMORY; }
    const ProgramDocument &programDoc() const { return doc_; }
    QString memoryFileName() const { return memoryFileName_; }
    QMenu *menu(const QString &title) const;
    Paper paper() const { return paper_; }

    // keys
    void press(int code);
    void release(int code);
    void pressLegend(int code, int shift);
    void toggleLatch(int code);
    void releaseLatchedKeys();
    void togglePower();
    void perform(KeyChord c);
    const QSet<int> &pressedKeys() const { return pressed_; }
    const QSet<int> &latchedKeys() const { return latchedShown_; }

    // model, memory, notes
    void chooseModel(int variant);
    void confirm(const PendingAction &a);
    void openMemory();
    void openMemoryFile(const QString &path);
    void saveMemory(std::function<void()> after = nullptr);
    void offerMemory(const QByteArray &data, const QString &source);
    void resetCalculator();
    void openProgramInfo();
    void openNotes();
    void openNotesFile(const QString &path);
    void saveNotes();
    void exportPdf();
    void exportText();
    void clearNotes();
    void clearAllNotes(bool forgetStored);       // 0.6.9: reset / model switch / another firmware
    QString memorySaveEnding() const;
    void setComment(const QString &text, int step);
    void fetchProgram(std::function<void(std::shared_ptr<const InspectorSnapshot>)> done);
    ProgramDocumentContent documentContent(const InspectorSnapshot &s);

    // USB
    void readFromCalculator();
    void writeToCalculator();
    void saveCalculatorMemoryToFile();
    void fixUsbPermissions();
    void showUsbLog();
    bool linkBusy() const { return !linkStatus_.isEmpty(); }
    /// Tests: no long pauses on the fake calculator, no settle time after a write, and the About text instead of a dialog.
    bool usbFast = false;
    int usbSettleMs = 5000;
    std::function<void(const QString &)> aboutHook;
    /// Replace the transport (tests use a fake calculator).
    std::function<std::unique_ptr<HidTransport>()> makeTransport;

    void showAbout();
    void saveStateNow(bool turnBackOn = false);
    void setShowInspector(bool on);
    void setTurbo(bool on);
    void setPaper(Paper p);
    void editComment(int step);

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void buildMenus();
    void updateMenus();
    void rebuildModelMenu();
    void rebuildKeysMenu();
    void stopEngine();
    void onBooted(bool ok, const QString &message, const QString &note, const FirmwareDetails &d);
    void onDisplay(const DisplaySnapshot &s);
    void onInspector(std::shared_ptr<const InspectorSnapshot> s);
    void applyLatch(const std::vector<KeyLatch::Action> &actions);
    void switchModel(int v, std::function<void()> then = nullptr);
    void backupThenConfirm(const PendingAction &a);
    void showError(const QString &m);
    void showNotice(const QString &m);
    void stockUnavailable(const QString &what);
    void programChanged(const InspectorSnapshot &s);
    void findSidecar(const QString &memoryPath);
    void memoryLoaded();
    void saveDataAs(const QString &kind, const QString &defaultName, const QString &filter, const QByteArray &data, const QString &defaultExt,
                    std::function<void(const QString &)> done = nullptr);
    QString documentModelName() const;
    void storeNotes();
    void loadStoredNotes();
    void setBusy(const QString &status, int watchMs = 25000);
    void usbRun(const QString &status, std::function<std::function<void()>(CalculatorLink &)> work);
    void linkFailed(const LinkError &e);
    void linkFailedMessage(const QString &m, bool offerFix);
    void updateInspectorVisibility();
    int shown(int code) const { return code == 35 ? 25 : code; }
    const Model &model() const;

    // widgets
    QStackedWidget *stack_;
    WelcomeWidget *welcome_;
    QSplitter *split_;
    CalculatorWidget *calc_;
    InspectorPanel *inspector_;
    QLabel *busy_;
    QMenu *fileMenu_ = nullptr, *calcMenu_ = nullptr, *modelMenu_ = nullptr, *keysMenu_ = nullptr, *usbMenu_ = nullptr;
    QAction *actOpen_, *actSave_, *actInfo_, *actOpenNotes_, *actSaveNotes_, *actPdf_, *actText_, *actClear_, *actInspector_, *actTurbo_,
            *actPower_, *actReset_, *actRead_, *actWrite_, *actSaveCalc_, *actFix_, *actA4_, *actLetter_, *actRelease_;
    QActionGroup *modelGroup_ = nullptr;

    // engine
    QThread *thread_ = nullptr;
    EngineWorker *worker_ = nullptr;
    bool running_ = false, starting_ = false, switching_ = false, rememberAfterBoot_ = false;
    QString pendingName_;
    FirmwareDetails fw_;
    QString firmwareName_;
    QByteArray firmwareData_;
    DisplaySnapshot snap_;
    std::shared_ptr<const InspectorSnapshot> inspection_;
    QSet<int> pressed_, latchedShown_;
    KeyLatch latch_;
    int lastModelSeen_ = 0;

    // notes
    ProgramDocument doc_;
    QString notesPath_, memoryFileName_, memoryFileCandidate_;
    std::optional<std::pair<ProgramDocument, QString>> pendingSidecar_;
    std::vector<QString> lastProgramKeys_;
    std::vector<ProgramLine> docLines_;
    ExportNameMemory exportNames_;
    QPointer<QDialog> usbLogWindow_;
    Paper paper_ = Paper::A4;
    bool showInspector_ = false, turbo_ = false;

    // usb
    QString linkStatus_;
    int linkTicket_ = 0;
    QTimer *watchdog_ = nullptr;
    std::function<void()> afterBackup_;
};

}  // namespace vc

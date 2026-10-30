// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// InspectorPanel.h - the flyout: registers and program listing, read-only, updated live (InspectorView.swift,
// Pool15Views.swift): model / mode / flag header, a Both / Registers / Program switch, the stack (imaginary part
// in complex mode), the HP-15C memory pool bar with its g MEM line, matrices A-E, storage registers with a
// "Non-zero only" toggle, and the program with the pointer line, step comments (double-click to edit).
#pragma once
#include "Inspector.h"
#include <QMap>
#include <QRect>
#include <QSet>
#include <QWidget>
#include <memory>

class QLabel;
class QPainter;
class QFont;
class QRect;
class QListView;
class QCheckBox;
class QButtonGroup;
class QStackedLayout;

namespace vc {

class RowModel;
struct Pool15;
/// The HP-15C pool bar (segments, g MEM caption, legend) into rc; 74 px high looks right.
void paintPool15Bar(QPainter *p, const QRect &rc, const Pool15 &pool, const QFont &base);

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget *parent = nullptr);
    ~InspectorPanel() override;
    /// nullptr: nothing to inspect (shows `emptyText`).
    void setSnapshot(std::shared_ptr<const InspectorSnapshot> s);
    void setEmptyText(const QString &t);
    void setComments(const QMap<int, QString> &c);
    const InspectorSnapshot *snapshot() const { return snap_.get(); }
    enum Pane { Both, Registers, Program };
    void setPane(Pane p);
    Pane pane() const { return pane_; }
    // for tests
    QStringList registerRowTexts() const;
    QStringList programRowTexts() const;
    QString headerText() const;
    void setHideZeros(bool on);
    /// The section header pinned at the top of the register / program list: its rectangle in that list's viewport (empty when none is pinned) and text.
    QRect pinnedHeaderRect(Pane which) const;
    QString pinnedHeaderText(Pane which) const;
    QListView *listView(Pane which) const;
    /// The context menu's items for a register row / program line (tests): titles, "-" for a separator, disabled ones in [ ].
    QStringList registerMenuTexts(const QString &name) const;
    QStringList programMenuTexts(int step) const;
    /// Trigger a context-menu item by title as if chosen (tests); false when it is not there or disabled.
    bool triggerRegisterItem(const QString &name, const QString &title);
    bool triggerProgramItem(int step, const QString &title);

signals:
    void editComment(int step);
    /// A context-menu item (GTO nnn, RCL / STO / x≷ …): the keys to press on the calculator.
    void performKeys(const QList<int> &keys);

private:
    void rebuild();
    std::shared_ptr<const InspectorSnapshot> snap_;
    QMap<int, QString> comments_;
    Pane pane_ = Both;
    bool hideZeros_ = false;
    int lastLine_ = -2;
    QSet<int> expanded_;
    QLabel *header_, *empty_;
    QWidget *content_;
    QListView *regView_, *progView_;
    RowModel *regModel_, *progModel_;
    QCheckBox *zeros_;
    QButtonGroup *paneButtons_;
};

}  // namespace vc

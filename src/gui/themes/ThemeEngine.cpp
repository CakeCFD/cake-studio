/*---------------------------------------------------------------------------*\
    Copyright (C) 2026 CakeCFD Contributors

    This file is part of CakeCFD.

    CakeCFD is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    CakeCFD is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along
    with CakeCFD. If not, see <https://www.gnu.org/licenses/>.
\*---------------------------------------------------------------------------*/

#include "ThemeEngine.h"
#include <QStyleFactory>
#include <QPalette>

ThemeEngine& ThemeEngine::instance() {
    static ThemeEngine inst;
    return inst;
}

ThemeColors ThemeEngine::colorsFor(Theme theme) {
    switch (theme) {
    case Theme::Light:
        return { true,
                 QColor("#f3f4f6"), QColor("#ebedf0"), QColor("#ffffff"), QColor("#ffffff"),
                 QColor("#d0d4da"), QColor("#b3b9c2"),
                 QColor("#1f2328"), QColor("#57606a"), QColor("#8c959f"),
                 QColor("#00458c"), QColor("#0a5bb5"), QColor("#ffffff"), QColor("#dbe7f5"),
                 QColor("#e8ecf1"), QColor("#ffffff"), QColor("#e3e6ea"),
                 QColor("#1a7f37"), QColor("#9a6700"), QColor("#cf222e"), QColor("#fff4d6") };
    case Theme::Navy:
        return { false,
                 QColor("#0d1b2e"), QColor("#0f2038"), QColor("#0a1626"), QColor("#16304f"),
                 QColor("#1d3557"), QColor("#2b4a73"),
                 QColor("#dbe6f3"), QColor("#8aa4c4"), QColor("#5c7699"),
                 QColor("#2a7de1"), QColor("#4d95ec"), QColor("#ffffff"), QColor("#1e4a80"),
                 QColor("#152a47"), QColor("#0a1626"), QColor("#1a2f4d"),
                 QColor("#3fb950"), QColor("#e3b341"), QColor("#ff6b6b"), QColor("#2e2a12") };
    case Theme::Graphite:
    default:
        return { false,
                 QColor("#1b1d21"), QColor("#202328"), QColor("#16181b"), QColor("#2a2e34"),
                 QColor("#33373e"), QColor("#454b54"),
                 QColor("#e6e8eb"), QColor("#9aa1ab"), QColor("#6b727c"),
                 QColor("#3d8bfd"), QColor("#66a3ff"), QColor("#ffffff"), QColor("#24406b"),
                 QColor("#2a2d33"), QColor("#16181b"), QColor("#2c3036"),
                 QColor("#3fb950"), QColor("#d29922"), QColor("#f85149"), QColor("#3a2f12") };
    }
}

QString ThemeEngine::key(Theme theme) {
    switch (theme) {
    case Theme::Light: return "light";
    case Theme::Navy:  return "navy";
    default:           return "graphite";
    }
}

QString ThemeEngine::displayName(Theme theme) {
    switch (theme) {
    case Theme::Light: return "Engineering Light";
    case Theme::Navy:  return "Deep Navy";
    default:           return "Graphite";
    }
}

Theme ThemeEngine::fromKey(const QString& key) {
    if (key == "light") return Theme::Light;
    if (key == "navy")  return Theme::Navy;
    return Theme::Graphite;
}

void ThemeEngine::apply(Theme theme, QApplication& app) {
    m_current = theme;
    m_colors = colorsFor(theme);
    const ThemeColors& c = m_colors;
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window,          c.window);
    p.setColor(QPalette::WindowText,      c.text);
    p.setColor(QPalette::Base,            c.base);
    p.setColor(QPalette::AlternateBase,   c.panel);
    p.setColor(QPalette::Text,            c.text);
    p.setColor(QPalette::Button,          c.raised);
    p.setColor(QPalette::ButtonText,      c.text);
    p.setColor(QPalette::Highlight,       c.selection);
    p.setColor(QPalette::HighlightedText, c.text);
    p.setColor(QPalette::Link,            c.accent);
    p.setColor(QPalette::Mid,             c.borderStrong);
    p.setColor(QPalette::Dark,            c.panel);
    p.setColor(QPalette::Shadow,          c.base);
    p.setColor(QPalette::ToolTipBase,     c.raised);
    p.setColor(QPalette::ToolTipText,     c.text);
    p.setColor(QPalette::PlaceholderText, c.textDim);
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        p.setColor(QPalette::Disabled, role, c.textDim);
    app.setPalette(p);
    app.setStyleSheet(stylesheet(c));
}

QString ThemeEngine::stylesheet(const ThemeColors& c) {
    QString s = R"(
        QMainWindow, QWidget          { background-color: @window; color: @text; }
        QMenuBar                      { background-color: @panel; color: @text; }
        QMenuBar::item:selected       { background-color: @raised; }
        QMenu                         { background-color: @window; border: 1px solid @border; }
        QMenu::item:selected          { background-color: @selection; }
        QDockWidget                   { background-color: @panel; color: @text; }
        QDockWidget::title            { background-color: @panel; padding: 4px; font-weight: bold; }
        QToolBar                      { background-color: @panel; border: none; border-bottom: 1px solid @border; spacing: 2px; }
        QStatusBar                    { background-color: @panel; color: @textMuted; border-top: 1px solid @border; }
        QSplitter::handle             { background-color: @border; }
        QPushButton                   { background-color: @raised; color: @text; border: 1px solid @border; padding: 4px 12px; border-radius: 4px; }
        QPushButton:hover             { border-color: @borderStrong; background-color: @selection; }
        QPushButton:pressed           { background-color: @borderStrong; }
        QPushButton:disabled          { color: @textDim; border-color: @border; }
        QToolButton                   { background: transparent; color: @text; border: none; border-radius: 3px; padding: 3px 10px; }
        QToolButton:hover             { background-color: @raised; }
        QToolButton:pressed, QToolButton:checked { background-color: @selection; }

        QLineEdit, QPlainTextEdit, QTextEdit { background-color: @base; color: @text; border: 1px solid @border; border-radius: 3px; padding: 2px 4px; }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
        QComboBox                     { background-color: @raised; color: @text; border: 1px solid @border; border-radius: 3px; padding: 2px 8px; }
        QComboBox::drop-down          { border: none; width: 0; }
        QComboBox::down-arrow         { image: none; }
        QComboBox QAbstractItemView   { background-color: @window; color: @text; selection-background-color: @selection; }
        QCheckBox::indicator          { width: 14px; height: 14px; border: 1px solid @borderStrong; border-radius: 3px; background-color: @base; }
        QCheckBox::indicator:hover    { border-color: @accent; }
        QCheckBox::indicator:checked  { background-color: @accent; border-color: @accent; image: none; }
        QCheckBox::indicator:checked:hover { background-color: @accentHover; border-color: @accentHover; }
        QGroupBox                     { border: 1px solid @border; border-radius: 4px; margin-top: 8px; padding-top: 4px; }
        QGroupBox::title              { subcontrol-origin: margin; left: 8px; color: @accent; }
        QScrollBar:vertical           { background: @panel; width: 8px; border: none; }
        QScrollBar::handle:vertical   { background: @borderStrong; border-radius: 4px; min-height: 20px; }
        QScrollBar:horizontal         { background: @panel; height: 8px; border: none; }
        QScrollBar::handle:horizontal { background: @borderStrong; border-radius: 4px; min-width: 20px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        #viewportControlBar           { background-color: @panel; border-bottom: 1px solid @border; }
        QTreeWidget, QListWidget, QTableWidget { background-color: @base; border: 1px solid @border; }
        QTreeWidget::item:selected, QListWidget::item:selected { background-color: @selection; color: @text; }
        QHeaderView::section          { background-color: @panel; border: none; padding: 4px; }
        QTabBar::tab                  { background: @panel; color: @textMuted; padding: 4px 12px; border: none; }
        QTabBar::tab:selected         { color: @text; border-bottom: 2px solid @accent; }
        QDoubleSpinBox, QSpinBox      { background-color: @base; color: @text; border: 1px solid @border; border-radius: 3px; padding: 2px 4px; }
        QDoubleSpinBox::up-button, QSpinBox::up-button     { width: 0; border: none; }
        QDoubleSpinBox::down-button, QSpinBox::down-button { width: 0; border: none; }
        QProgressBar                  { background-color: @raised; border: none; border-radius: 3px; }
        QProgressBar::chunk           { background-color: @accent; border-radius: 3px; }

        *[cakeRole="muted"]                    { color: @textMuted; }
        *[cakeRole="accentText"]               { color: @accent; }
        *[cakeRole="warnText"]                 { color: @warning; }
        QLabel[cakeRole="toast"]               { background: @raised; color: @success; border: 1px solid @success; border-radius: 6px; padding: 4px 16px; font-size: 12px; }
        QPushButton[cakeRole="toggle"]:checked { background-color: @accent; color: @onAccent; border-color: @accent; }
        QPushButton[cakeRole="primary"]        { background-color: @accent; color: @onAccent; border-color: @accent; font-weight: bold; }
        QPushButton[cakeRole="primary"]:hover  { background-color: @accentHover; border-color: @accentHover; }
        QPushButton[cakeRole="primary"]:disabled { background-color: @raised; color: @textDim; border-color: @border; }
        QPushButton[cakeRole="copyWarn"], QPushButton[cakeRole="copyErr"] { background: transparent; font-size: 10px; padding: 2px 8px; }
        QPushButton[cakeRole="copyWarn"]       { color: @warning; border: 1px solid @warning; }
        QPushButton[cakeRole="copyErr"]        { color: @danger; border: 1px solid @danger; }
        QPushButton[cakeRole="copyWarn"]:hover { background: @warning; color: @onAccent; }
        QPushButton[cakeRole="copyErr"]:hover  { background: @danger; color: @onAccent; }
        QFrame[cakeRole="warnBanner"]          { background: @warningBg; border: 1px solid @warning; border-radius: 4px; }
        QFrame[cakeRole="warnBanner"] QLabel   { background: transparent; border: none; color: @warning; }
        QFrame[cakeRole="warnBanner"] QPushButton { background: transparent; color: @warning; border: 1px solid @warning; font-size: 10px; padding: 2px 8px; }
        QFrame[cakeRole="warnBanner"] QPushButton:hover { background: @warning; color: @onAccent; }
    )";
    const QList<QPair<QString, QColor>> tokens = {
        {"@accentHover", c.accentHover}, {"@borderStrong", c.borderStrong}, {"@warningBg", c.warningBg},
        {"@textMuted", c.textMuted}, {"@selection", c.selection}, {"@onAccent", c.onAccent},
        {"@textDim", c.textDim}, {"@warning", c.warning}, {"@window", c.window}, {"@accent", c.accent},
        {"@border", c.border}, {"@raised", c.raised}, {"@danger", c.danger}, {"@panel", c.panel},
        {"@success", c.success}, {"@base", c.base}, {"@text", c.text},
    };
    for (const auto& t : tokens) s.replace(t.first, t.second.name());
    return s;
}

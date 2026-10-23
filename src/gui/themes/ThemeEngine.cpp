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
#include <QFile>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>

ThemeEngine& ThemeEngine::instance() {
    static ThemeEngine inst;
    return inst;
}

void ThemeEngine::apply(Theme theme, QApplication& app) {
    m_current = theme;
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    if (theme == Theme::Dark) {
        p.setColor(QPalette::Window,          QColor("#1e1e2e"));
        p.setColor(QPalette::WindowText,      QColor("#cdd6f4"));
        p.setColor(QPalette::Base,            QColor("#12121c"));
        p.setColor(QPalette::AlternateBase,   QColor("#181825"));
        p.setColor(QPalette::Text,            QColor("#cdd6f4"));
        p.setColor(QPalette::Button,          QColor("#313244"));
        p.setColor(QPalette::ButtonText,      QColor("#cdd6f4"));
        p.setColor(QPalette::Highlight,       QColor("#313244"));
        p.setColor(QPalette::HighlightedText, QColor("#cdd6f4"));
        p.setColor(QPalette::Link,            QColor("#89b4fa"));
        p.setColor(QPalette::Mid,             QColor("#45475a"));
        p.setColor(QPalette::Dark,            QColor("#181825"));
        p.setColor(QPalette::Shadow,          QColor("#11111b"));
        p.setColor(QPalette::ToolTipBase,     QColor("#313244"));
        p.setColor(QPalette::ToolTipText,     QColor("#cdd6f4"));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#585b70"));
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#585b70"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#585b70"));
    } else {
        p.setColor(QPalette::Window,          QColor("#2b2b2b"));
        p.setColor(QPalette::WindowText,      QColor("#e0e0e0"));
        p.setColor(QPalette::Base,            QColor("#1f1f1f"));
        p.setColor(QPalette::AlternateBase,   QColor("#252525"));
        p.setColor(QPalette::Text,            QColor("#e0e0e0"));
        p.setColor(QPalette::Button,          QColor("#3a3a3a"));
        p.setColor(QPalette::ButtonText,      QColor("#e0e0e0"));
        p.setColor(QPalette::Highlight,       QColor("#4a4a4a"));
        p.setColor(QPalette::HighlightedText, QColor("#e0e0e0"));
        p.setColor(QPalette::Link,            QColor("#f5c500"));
        p.setColor(QPalette::Mid,             QColor("#555555"));
        p.setColor(QPalette::Dark,            QColor("#1f1f1f"));
        p.setColor(QPalette::Shadow,          QColor("#111111"));
        p.setColor(QPalette::ToolTipBase,     QColor("#3a3a3a"));
        p.setColor(QPalette::ToolTipText,     QColor("#e0e0e0"));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#555555"));
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#555555"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#555555"));
    }
    app.setPalette(p);
    app.setStyleSheet(loadStylesheet(theme));
}

QString ThemeEngine::loadStylesheet(Theme theme) {
    if (theme == Theme::Dark) {
        return R"(
            QMainWindow, QWidget          { background-color: #1e1e2e; color: #cdd6f4; }
            QMenuBar                      { background-color: #181825; color: #cdd6f4; }
            QMenuBar::item:selected       { background-color: #313244; }
            QMenu                         { background-color: #1e1e2e; border: 1px solid #45475a; }
            QMenu::item:selected          { background-color: #313244; }
            QDockWidget                   { background-color: #181825; color: #cdd6f4; }
            QDockWidget::title            { background-color: #181825; padding: 4px; font-weight: bold; }
            QToolBar                      { background-color: #181825; border: none; spacing: 2px; }
            QStatusBar                    { background-color: #181825; color: #6c7086; }
            QSplitter::handle             { background-color: #313244; }
            QPushButton                   { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 4px 12px; border-radius: 4px; }
            QPushButton:hover             { background-color: #45475a; }
            QPushButton:pressed           { background-color: #585b70; }
            QPushButton:disabled          { color: #585b70; border-color: #313244; }
            QToolButton                   { background: transparent; color: #cdd6f4; border: none; border-radius: 3px; padding: 3px 10px; }
            QToolButton:hover             { background-color: #313244; }
            QToolButton:pressed           { background-color: #585b70; }

            QLineEdit, QPlainTextEdit, QTextEdit { background-color: #12121c; color: #cdd6f4; border: 1px solid #45475a; border-radius: 3px; padding: 2px 4px; }
            QLineEdit:focus               { border-color: #89b4fa; }
            QComboBox                     { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 3px; padding: 2px 8px; }
            QComboBox::drop-down          { border: none; width: 0; }
            QComboBox::down-arrow         { image: none; }
            QComboBox QAbstractItemView   { background-color: #1e1e2e; color: #cdd6f4; selection-background-color: #313244; }
            QCheckBox::indicator          { width: 14px; height: 14px; border: 1px solid #45475a; border-radius: 3px; background-color: palette(base); }
            QCheckBox::indicator:hover    { border-color: #89b4fa; }
            QCheckBox::indicator:checked  { background-color: #89b4fa; border-color: #89b4fa; image: none; }
            QCheckBox::indicator:checked:hover { background-color: #b4d0ff; border-color: #b4d0ff; }
            QGroupBox                     { border: 1px solid #313244; border-radius: 4px; margin-top: 8px; padding-top: 4px; }
            QGroupBox::title              { subcontrol-origin: margin; left: 8px; color: #89b4fa; }
            QScrollBar:vertical           { background: #181825; width: 8px; border: none; }
            QScrollBar::handle:vertical   { background: #45475a; border-radius: 4px; min-height: 20px; }
            QScrollBar:horizontal         { background: #181825; height: 8px; border: none; }
            QScrollBar::handle:horizontal { background: #45475a; border-radius: 4px; min-width: 20px; }
            QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
            #viewportControlBar           { background-color: #252535; border-bottom: 1px solid #45475a; }
            QTreeWidget                   { background-color: #12121c; border: 1px solid #313244; }
            QTreeWidget::item:selected    { background-color: #313244; }
            QHeaderView::section          { background-color: #181825; border: none; padding: 4px; }
            QTabBar::tab                  { background: #181825; color: #6c7086; padding: 4px 12px; border: none; }
            QTabBar::tab:selected         { color: #cdd6f4; border-bottom: 2px solid #89b4fa; }
            QDoubleSpinBox, QSpinBox      { background-color: #12121c; color: #cdd6f4; border: 1px solid #45475a; border-radius: 3px; padding: 2px 4px; }
            QDoubleSpinBox::up-button, QSpinBox::up-button     { width: 0; border: none; }
            QDoubleSpinBox::down-button, QSpinBox::down-button { width: 0; border: none; }
            QProgressBar                  { background-color: #313244; border: none; border-radius: 3px; }
            QProgressBar::chunk          { background-color: #89b4fa; border-radius: 3px; }
        )";
    } else {
        // Blueprint: dark charcoal + gold
        return R"(
            QMainWindow, QWidget          { background-color: #2b2b2b; color: #e0e0e0; }
            QMenuBar                      { background-color: #1f1f1f; color: #e0e0e0; }
            QMenuBar::item:selected       { background-color: #3a3a3a; }
            QMenu                         { background-color: #2b2b2b; border: 1px solid #555; }
            QMenu::item:selected          { background-color: #3a3a3a; color: #f5c500; }
            QDockWidget                   { background-color: #252525; color: #e0e0e0; }
            QDockWidget::title            { background-color: #1f1f1f; padding: 4px; font-weight: bold; color: #f5c500; border-bottom: 1px solid #f5c500; }
            QToolBar                      { background-color: #1f1f1f; border-bottom: 2px solid #f5c500; spacing: 2px; }
            QStatusBar                    { background-color: #1f1f1f; color: #888; border-top: 1px solid #3a3a3a; }
            QSplitter::handle             { background-color: #3a3a3a; }
            QPushButton                   { background-color: #3a3a3a; color: #e0e0e0; border: 1px solid #555; padding: 4px 12px; border-radius: 2px; }
            QPushButton:hover             { background-color: #4a4a4a; border-color: #f5c500; color: #f5c500; }
            QPushButton:pressed           { background-color: #f5c500; color: #1f1f1f; }
            QPushButton:disabled          { color: #555; border-color: #3a3a3a; }
            QToolButton                   { background: transparent; color: #e0e0e0; border: none; border-radius: 2px; padding: 3px 10px; }
            QToolButton:hover             { background-color: #3a3a3a; color: #f5c500; }
            QToolButton:pressed           { background-color: #f5c500; color: #1f1f1f; }

            QLineEdit, QPlainTextEdit, QTextEdit { background-color: #1f1f1f; color: #e0e0e0; border: 1px solid #555; border-radius: 2px; padding: 2px 4px; }
            QLineEdit:focus               { border-color: #f5c500; }
            QComboBox                     { background-color: #3a3a3a; color: #e0e0e0; border: 1px solid #555; border-radius: 2px; padding: 2px 8px; }
            QComboBox::drop-down          { border: none; width: 0; }
            QComboBox::down-arrow         { image: none; }
            QComboBox QAbstractItemView   { background-color: #2b2b2b; color: #e0e0e0; selection-background-color: #3a3a3a; }
            QCheckBox::indicator          { width: 14px; height: 14px; border: 1px solid #555; border-radius: 2px; background-color: palette(base); }
            QCheckBox::indicator:hover    { border-color: #f5c500; }
            QCheckBox::indicator:checked  { background-color: #f5c500; border-color: #f5c500; image: none; }
            QCheckBox::indicator:checked:hover { background-color: #ffd740; border-color: #ffd740; }
            QGroupBox                     { border: 1px solid #3a3a3a; border-radius: 2px; margin-top: 8px; padding-top: 4px; }
            QGroupBox::title              { subcontrol-origin: margin; left: 8px; color: #f5c500; }
            QScrollBar:vertical           { background: #1f1f1f; width: 8px; border: none; }
            QScrollBar::handle:vertical   { background: #555; border-radius: 4px; min-height: 20px; }
            QScrollBar:horizontal         { background: #1f1f1f; height: 8px; border: none; }
            QScrollBar::handle:horizontal { background: #555; border-radius: 4px; min-width: 20px; }
            QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
            #viewportControlBar           { background-color: #2b2b2b; border-bottom: 1px solid #555; }
            QTreeWidget                   { background-color: #1f1f1f; border: 1px solid #3a3a3a; }
            QTreeWidget::item:selected    { background-color: #3a3a3a; color: #f5c500; }
            QHeaderView::section          { background-color: #1f1f1f; border: none; padding: 4px; color: #f5c500; }
            QTabBar::tab                  { background: #1f1f1f; color: #888; padding: 4px 12px; border: none; }
            QTabBar::tab:selected         { color: #f5c500; border-bottom: 2px solid #f5c500; }
            QDoubleSpinBox, QSpinBox      { background-color: #1f1f1f; color: #e0e0e0; border: 1px solid #555; border-radius: 2px; padding: 2px 4px; }
            QDoubleSpinBox::up-button, QSpinBox::up-button     { width: 0; border: none; }
            QDoubleSpinBox::down-button, QSpinBox::down-button { width: 0; border: none; }
            QProgressBar                  { background-color: #3a3a3a; border: none; border-radius: 2px; }
            QProgressBar::chunk          { background-color: #f5c500; border-radius: 2px; }
        )";
    }
}

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

#pragma once
#include <QWidget>
#include <QScrollArea>
#include <functional>
#include "CaseTreePanel.h"

class QVBoxLayout;
class QFormLayout;
class QLabel;

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

signals:
    void loadDomainRequested(const QString& caseDir, const QString& stlPath);
    void domainPreviewChanged(double x0, double y0, double z0,
                              double x1, double y1, double z1,
                              const QString& flowAxis);

public slots:
    void showItem(int itemType, const QString& path, const QString& caseDir);
    void clear();
    void onThemeChanged();

private:
    void addSection(const QString& title);
    // Read-only display
    void addRow(const QString& label, const QString& value, bool mono = false);
    void addBadge(const QString& text, bool ok);
    // Editable rows call onCommit with the new value when the user commits
    void addEditableRow(const QString& label, const QString& value,
                        std::function<void(const QString&)> onCommit);
    void addComboRow(const QString& label, const QStringList& options, const QString& current,
                     std::function<void(const QString&)> onCommit);
    // Full-width action button
    void addButton(const QString& label, std::function<void()> onClick);

    void initContainer(); // creates a clean empty scroll container
    void buildBlankUI();  // initContainer + placeholder content
    void populateCase(const QString& caseDir);
    void populateMesh(const QString& caseDir);
    void populateGeo(const QString& path, const QString& caseDir);
    void populateResult(const QString& caseDir, const QString& timeStep);
    void populateDict(const QString& path);

    QScrollArea* m_scroll   = nullptr;
    QWidget*     m_content  = nullptr;
    QVBoxLayout* m_vlay     = nullptr;
    QFormLayout* m_form     = nullptr;
    QString      m_caseDir;

    // Last shown state, used by onThemeChanged() to rebuild in the new theme
    int     m_lastType    = -1;
    QString m_lastPath;
    QString m_lastCaseDir;
};

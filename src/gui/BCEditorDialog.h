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
#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QMap>
#include <QSet>
#include <QLabel>
#include <QPushButton>

struct BCPatch {
    QString name;
    QString type;
    QString value;
    bool    nonUniform = false;
};

class BCEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit BCEditorDialog(const QString& caseDir, QWidget* parent = nullptr);

private slots:
    void onApply();

private:
    struct FieldData {
        QString       path;
        QString       originalText;
        QTableWidget* table    = nullptr;
        bool          isVector = false;
    };

    void            discoverFields();
    void            loadField(const QString& fieldName);
    QList<BCPatch>  parsePatches(const QString& text);
    bool            writeField(FieldData& fd, const QList<BCPatch>& patches, QString& errOut);
    void            buildTab(const QString& fieldName, FieldData& fd);
    void            syncValueWidget(QWidget* w, const QString& type, bool isVector);

    QString      m_caseDir;
    QTabWidget*  m_tabs      = nullptr;
    QLabel*      m_statusLbl = nullptr;
    QMap<QString, FieldData> m_fields;

    static const QSet<QString> NO_VALUE_TYPES;
    static const QStringList   BC_TYPES;
};

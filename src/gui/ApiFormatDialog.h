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
#include <QSettings>
#include "CitationManager.h"

class QPlainTextEdit;
class QLineEdit;
class QLabel;

class ApiFormatDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApiFormatDialog(QSettings& settings, QWidget* parent = nullptr);

    QString jsonPath() const;
    QStringList componentKeys() const;
    CitationManager::Citation citationData() const;

private slots:
    void onTest();
    void onReset();

private:
    QVector<double> parseResponse(const QByteArray& json,
                                  const QString& path,
                                  const QStringList& keys) const;

    QSettings&       m_settings;
    QLineEdit*       m_pathEdit;
    QLineEdit*       m_keysEdit;
    QPlainTextEdit*  m_sampleEdit;
    QPlainTextEdit*  m_resultEdit;

    // Citation tab fields
    QLineEdit*       m_citeAuthors;
    QLineEdit*       m_citeTitle;
    QLineEdit*       m_citeJournal;
    QLineEdit*       m_citeYear;
    QLineEdit*       m_citeVolume;
    QLineEdit*       m_citePages;
    QLineEdit*       m_citeDoi;
};
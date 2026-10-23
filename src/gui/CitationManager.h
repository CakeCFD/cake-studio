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
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

class CitationManager
{
public:
    struct Citation {
        QString key;
        QString label;
        QString authors;
        QString title;
        QString journal;
        QString year;
        QString volume;
        QString pages;
        QString doi;
    };

    static CitationManager& instance();

    void cite(const QString& key, const QString& caseDir = {});

    void registerCustom(const QString& key, const Citation& c);

    bool hasCitation(const QString& key) const { return m_registry.contains(key); }

    QStringList allCitedKeys() const { return QStringList(m_cited.begin(), m_cited.end()); }

    QString formatByKey(const QString& key) const;

    QString markdownRef(const QString& key) const;

    const Citation* citationByKey(const QString& key) const;

    static QString turbModelKey(const QString& ofModelName);

    QStringList consumePending();

    void writeBib(const QString& caseDir) const;

    QString format(const Citation& c) const;

private:
    CitationManager();
    QMap<QString, Citation> m_registry;
    QSet<QString>           m_cited;
    QSet<QString>           m_printed;
};
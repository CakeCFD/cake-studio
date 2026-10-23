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

#include "CitationManager.h"
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QRegularExpression>

CitationManager& CitationManager::instance()
{
    static CitationManager inst;
    return inst;
}

CitationManager::CitationManager()
{
    m_registry["teno5"] = {
        "Fu2016", "TENO5",
        "Fu, L., Hu, X. Y., & Adams, N. A.",
        "A family of high-order targeted ENO schemes for compressible-fluid simulations",
        "Journal of Computational Physics", "2016", "305", "333--359",
        "10.1016/j.jcp.2015.10.037"
    };
    m_registry["komegasst"] = {
        "Menter1994", "k-omega SST",
        "Menter, F. R.",
        "Two-equation eddy-viscosity turbulence models for engineering applications",
        "AIAA Journal", "1994", "32", "1598--1605",
        "10.2514/3.12149"
    };
    m_registry["kepsilon"] = {
        "Launder1974", "k-epsilon",
        "Launder, B. E., & Spalding, D. B.",
        "The numerical computation of turbulent flows",
        "Computer Methods in Applied Mechanics and Engineering", "1974", "3", "269--289",
        "10.1016/0045-7825(74)90029-2"
    };
    m_registry["smagorinsky"] = {
        "Smagorinsky1963", "Smagorinsky SGS",
        "Smagorinsky, J.",
        "General circulation experiments with the primitive equations",
        "Monthly Weather Review", "1963", "91", "99--164",
        "10.1175/1520-0493(1963)091<0099:GCEWTP>2.3.CO;2"
    };
    m_registry["dynamickEqn"] = {
        "Kim1995", "dynamicKEqn SGS",
        "Kim, W. W., & Menon, S.",
        "A new dynamic one-equation subgrid-scale model for large eddy simulations",
        "AIAA Paper 95-0356", "1995", "", "",
        "10.2514/6.1995-356"
    };
    m_registry["wale"] = {
        "Nicoud1999", "WALE SGS",
        "Nicoud, F., & Ducros, F.",
        "Subgrid-scale stress modelling based on the square of the velocity gradient tensor",
        "Flow, Turbulence and Combustion", "1999", "62", "183--200",
        "10.1023/A:1009995426001"
    };
    m_registry["dynamiclagrangian"] = {
        "Meneveau1996", "Dynamic Lagrangian SGS",
        "Meneveau, C., Lund, T. S., & Cabot, W. H.",
        "A Lagrangian dynamic subgrid-scale model of turbulence",
        "Journal of Fluid Mechanics", "1996", "319", "353--385",
        "10.1017/S0022112096007379"
    };
    m_registry["openfoam"] = {
        "Weller1998", "OpenFOAM",
        "Weller, H. G., Tabor, G., Jasak, H., & Fureby, C.",
        "A tensorial approach to computational continuum mechanics using object-oriented techniques",
        "Computers in Physics", "1998", "12", "620--631",
        "10.1063/1.168744"
    };
    m_registry["spalartallmaras"] = {
        "Spalart1992", "Spalart-Allmaras",
        "Spalart, P. R., & Allmaras, S. R.",
        "A one-equation turbulence model for aerodynamic flows",
        "AIAA Paper 92-0439", "1992", "", "",
        "10.2514/6.1992-439"
    };
    m_registry["komega"] = {
        "Wilcox1988", "k-omega",
        "Wilcox, D. C.",
        "Reassessment of the scale-determining equation for advanced turbulence models",
        "AIAA Journal", "1988", "26", "1299--1310",
        "10.2514/3.10041"
    };
    m_registry["realizablekepsilon"] = {
        "Shih1995", "Realizable k-epsilon",
        "Shih, T.-H., Liou, W. W., Shabbir, A., Yang, Z., & Zhu, J.",
        "A new k-epsilon eddy viscosity model for high Reynolds number turbulent flows",
        "Computers & Fluids", "1995", "24", "227--238",
        "10.1016/0045-7930(94)00032-T"
    };
    m_registry["rngkepsilon"] = {
        "Yakhot1992", "RNG k-epsilon",
        "Yakhot, V., Orszag, S. A., Thangam, S., Gatski, T. B., & Speziale, C. G.",
        "Development of turbulence models for shear flows by a double expansion technique",
        "Physics of Fluids A", "1992", "4", "1510--1520",
        "10.1063/1.858424"
    };
    m_registry["dynamicsmagorinsky"] = {
        "Germano1991", "Dynamic Smagorinsky SGS",
        "Germano, M., Piomelli, U., Moin, P., & Cabot, W. H.",
        "A dynamic subgrid-scale eddy viscosity model",
        "Physics of Fluids A", "1991", "3", "1760--1765",
        "10.1063/1.857955"
    };
    m_registry["keqn"] = {
        "Schumann1975", "k-equation SGS",
        "Schumann, U.",
        "Subgrid scale model for finite difference simulations of turbulent flows in plane channels and annuli",
        "Journal of Computational Physics", "1975", "18", "376--404",
        "10.1016/0021-9991(75)90093-5"
    };
    m_registry["deardorff"] = {
        "Deardorff1973", "Deardorff SGS",
        "Deardorff, J. W.",
        "The use of subgrid transport equations in a three-dimensional model of atmospheric turbulence",
        "Journal of Fluids Engineering", "1973", "95", "429--438",
        "10.1115/1.3447047"
    };
    m_registry["v2f"] = {
        "Durbin1995", "v2-f",
        "Durbin, P. A.",
        "Separated flow computations with the k-epsilon-v2 model",
        "AIAA Journal", "1995", "33", "659--664",
        "10.2514/3.12628"
    };
}

QString CitationManager::format(const Citation& c) const
{
    return QString(
        "\n"
        "  +-- Reference [%1]\n"
        "  |   %2\n"
        "  |   \"%3\"\n"
        "  |   %4 %5, pp. %6\n"
        "  |   https://doi.org/%7\n"
        "  +-----------------------------------------------\n"
    ).arg(c.label, c.authors, c.title, c.journal, c.year, c.pages, c.doi);
}

void CitationManager::registerCustom(const QString& key, const Citation& c)
{
    m_registry[key] = c;
}

QString CitationManager::formatByKey(const QString& key) const
{
    auto it = m_registry.find(key);
    return it != m_registry.end() ? format(it.value()) : QString{};
}

const CitationManager::Citation* CitationManager::citationByKey(const QString& key) const
{
    auto it = m_registry.find(key);
    return it != m_registry.end() ? &it.value() : nullptr;
}

QString CitationManager::markdownRef(const QString& key) const
{
    const Citation* c = citationByKey(key);
    if (!c) return {};
    QString s = c->authors + " (" + c->year + "). " + c->title + ".";
    if (!c->journal.isEmpty()) s += " *" + c->journal + "*";
    if (!c->volume.isEmpty())  s += ", " + c->volume;
    if (!c->pages.isEmpty())   s += ", pp. " + c->pages;
    s += ".";
    if (!c->doi.isEmpty()) s += " https://doi.org/" + c->doi;
    return s;
}

QString CitationManager::turbModelKey(const QString& ofModelName)
{
    static const QMap<QString, QString> table = {
        {"kOmegaSST",          "komegasst"},
        {"kOmega",             "komega"},
        {"kEpsilon",           "kepsilon"},
        {"realizableKE",       "realizablekepsilon"},
        {"RNGkEpsilon",        "rngkepsilon"},
        {"SpalartAllmaras",    "spalartallmaras"},
        {"v2f",                "v2f"},
        {"Smagorinsky",        "smagorinsky"},
        {"dynamicKEqn",        "dynamickEqn"},
        {"kEqn",               "keqn"},
        {"WALE",               "wale"},
        {"dynamicLagrangian",  "dynamiclagrangian"},
        {"dynamicSmagorinsky", "dynamicsmagorinsky"},
        {"Deardorff",          "deardorff"},
    };
    const QString name = ofModelName.trimmed();
    for (auto it = table.begin(); it != table.end(); ++it)
        if (name.compare(it.key(), Qt::CaseInsensitive) == 0)
            return it.value();

    QString best;
    int bestLen = 0;
    for (auto it = table.begin(); it != table.end(); ++it)
        if (it.key().size() > bestLen && name.contains(it.key(), Qt::CaseInsensitive)) {
            best    = it.value();
            bestLen = it.key().size();
        }
    return best;
}

void CitationManager::cite(const QString& key, const QString& caseDir)
{
    if (!m_registry.contains(key)) return;
    m_cited.insert(key);
    if (!caseDir.isEmpty())
        writeBib(caseDir);
}

QStringList CitationManager::consumePending()
{
    QStringList msgs;
    for (const QString& key : m_cited) {
        if (m_printed.contains(key)) continue;
        auto it = m_registry.find(key);
        if (it != m_registry.end()) {
            msgs << format(it.value());
            m_printed.insert(key);
        }
    }
    return msgs;
}

static QString bibEscape(const QString& s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
            case '\\': out += "\\textbackslash{}"; break;
            case '&': case '%': case '$': case '#': case '_': case '{': case '}':
                out += '\\'; out += c; break;
            case '~': out += "\\textasciitilde{}"; break;
            case '^': out += "\\textasciicircum{}"; break;
            default:  out += c;
        }
    }
    return out;
}

static QString bibAuthors(const QString& apa)
{
    if (apa.contains(" and ")) return apa;
    QString s = apa;
    s.replace(QRegularExpression(R"(,?\s*&\s*)"), ", ");
    QStringList names = s.split(QRegularExpression(R"((?<=\.)\s*,\s*)"), Qt::SkipEmptyParts);
    for (QString& n : names) n = n.trimmed();
    return names.join(" and ");
}

void CitationManager::writeBib(const QString& caseDir) const
{
    QDir dir(caseDir);
    if (!dir.exists()) return;

    QFile f(dir.filePath("citations.bib"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&f);
    out << "% Cake CFD -- auto-generated citations\n\n";
    for (const QString& key : m_cited) {
        auto it = m_registry.find(key);
        if (it == m_registry.end()) continue;
        const Citation& c = it.value();
        out << "@article{" << c.key << ",\n"
            << "  author  = {" << bibEscape(bibAuthors(c.authors)) << "},\n"
            << "  title   = {" << bibEscape(c.title) << "},\n"
            << "  journal = {" << bibEscape(c.journal) << "},\n"
            << "  year    = {" << bibEscape(c.year) << "},\n"
            << "  volume  = {" << bibEscape(c.volume) << "},\n"
            << "  pages   = {" << c.pages << "},\n"
            << "  doi     = {" << c.doi << "}\n"
            << "}\n\n";
    }
}
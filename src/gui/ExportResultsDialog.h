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
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QListWidget>

class ExportResultsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportResultsDialog(const QString& caseDir, QWidget* parent = nullptr);

private slots:
    void onBrowseOutput();
    void onExport();
    void onOpenDir();
    void onBrowseReports();
    void onGenerateReports();
    void refreshRunsList();

private:
    struct ResidualRow {
        int    step;
        double time;
        QMap<QString, double> initResiduals;
    };

    struct ForceRow {
        double time;
        // Pressure (wave/form drag) + viscous (skin friction) components
        double fp_x, fp_y, fp_z;   // pressure forces
        double fv_x, fv_y, fv_z;   // viscous forces
        double mp_x, mp_y, mp_z;   // pressure moments
        double mv_x, mv_y, mv_z;   // viscous moments
        // Convenience totals
        double fx()  const { return fp_x + fv_x; }
        double fy()  const { return fp_y + fv_y; }
        double fz()  const { return fp_z + fv_z; }
        double mx()  const { return mp_x + mv_x; }
        double my()  const { return mp_y + mv_y; }
        double mz()  const { return mp_z + mv_z; }
    };

    QList<ResidualRow> parseResiduals() const;
    QList<ForceRow>    parseForces()    const;
    QString            solverLogPath()  const;
    QString            detectSolver()   const;

    bool writeResidualsCsv(const QString& path, const QList<ResidualRow>& rows) const;
    bool writeForcesCsv   (const QString& path, const QList<ForceRow>&    rows) const;
    bool writeReport      (const QString& path,
                           const QList<ResidualRow>& residuals,
                           const QList<ForceRow>&    forces)   const;
    bool writeSummaryJson (const QString& path,
                           const QList<ResidualRow>& residuals,
                           const QList<ForceRow>&    forces)   const;
    void log(const QString& msg) const;

    QString              readDictValue(const QString& filePath, const QString& key) const;
    QString              readInletU()    const;
    QMap<QString,QString> parseMeshStats() const;
    QStringList          citations(const QString& turbModel) const;

    void   loadCakeProperties();
    void   saveCakeProperties() const;

    QString      m_caseDir;

    // Export tab
    QTabWidget*     m_tabs         = nullptr;
    QLineEdit*      m_outputDir    = nullptr;
    QCheckBox*      m_chkResiduals = nullptr;
    QCheckBox*      m_chkForces    = nullptr;
    QCheckBox*      m_chkReport    = nullptr;
    QCheckBox*      m_chkSummary   = nullptr;
    QPushButton*    m_btnExport    = nullptr;
    QPushButton*    m_btnOpenDir   = nullptr;
    QTextEdit*      m_log          = nullptr;
    QLabel*         m_status       = nullptr;
    QString         m_lastExportDir;

    // Reference quantities
    QDoubleSpinBox* m_spRho    = nullptr;   // density [kg/m³]
    QDoubleSpinBox* m_spAref   = nullptr;   // reference area [m²]
    QDoubleSpinBox* m_spLref   = nullptr;   // reference length [m]
    QDoubleSpinBox* m_spUref   = nullptr;   // reference velocity [m/s] (0 = auto from inlet)
    QDoubleSpinBox* m_spAoa    = nullptr;   // angle of attack [deg] (auto-detected from 0/U)

    // Runs tab
    QListWidget* m_runsList        = nullptr;
    QLineEdit*   m_reportsDir      = nullptr;
    QLabel*      m_runsStatus      = nullptr;
    QPushButton* m_btnOpenReports  = nullptr;
    QString      m_lastReportsDir;
};

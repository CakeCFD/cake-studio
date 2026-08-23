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
#include <QSettings>
#include "DatasetClient.h"
#include "ApiFormatDialog.h"

class QLineEdit;
class QComboBox;
class QTextEdit;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QDoubleSpinBox;
class QRadioButton;
class QGroupBox;
class ResultCanvas;

namespace CFD { struct DatasetInfo; }

class DatasetPanel : public QWidget {
    Q_OBJECT
public:
    explicit DatasetPanel(QWidget* parent = nullptr);
    void setCaseDir(const QString& dir);

signals:

private slots:
    void onSaveSettings();
    void onRefreshDatasets();
    void onDatasetListReady(const QList<CFD::DatasetInfo>& datasets);
    void onDatasetChanged(int idx);
    void onTimeModeChanged();
    void onTimeRangeChanged();
    void onQueryClicked();
    void onCancelClicked();
    void onQueryFinished(const QString& dataset, const QString& field,
                         double time, const QVector<double>& result);
    void onQueryRangeFinished(const QString& dataset, const QString& field,
                              const QVector<double>& times,
                              const QList<QVector<double>>& results);
    void onQueryRangeProgress(int step, int total);
    void onExportToBoundary();
    void onExportRansBcs();
    void onCopyPython();
    void onViewRequest();
    void onLoadPatchPoints(bool append);
    void onAutowrap();
    void onError(const QString& msg);
    void onEditApiFormat();

private:
    void buildUI();
    void log(const QString& msg);
    void updateTurbStats();
    void saveResults();
    void restoreResults();
    bool writeBoundaryData(const QString& caseDir, const QString& patch,
                           const QVector<double>& points,
                           const QList<QVector<double>>& valuesByTime,
                           const QVector<double>& times,
                           int components);
    bool patchInletBC(const QString& caseDir, const QString& patch,
                      const QString& field);
    bool writeRansBcs(const QString& caseDir, const CFD::TurbStats& stats);
    QStringList getPatchNames(const QString& caseDir);
    QVector<double> readPatchFaceCenters(const QString& caseDir,
                                          const QString& patch);
    QString generatePythonCode() const;

    CFD::DatasetClient* m_client;
    QSettings           m_settings;
    QString             m_activeCaseDir;

    // API
    QLineEdit*   m_endpointEdit   = nullptr;
    QLineEdit*   m_tokenEdit      = nullptr;
    QComboBox*   m_spatialInterp  = nullptr;
    QComboBox*   m_temporalInterp = nullptr;

    // Dataset
    QComboBox*      m_datasetCombo  = nullptr;
    QLabel*         m_datasetDesc   = nullptr;
    QComboBox*      m_fieldCombo    = nullptr;
    QRadioButton*   m_rbSingle      = nullptr;
    QRadioButton*   m_rbRange       = nullptr;
    QDoubleSpinBox* m_timeSpin      = nullptr;
    QLabel*         m_timeRangeHint = nullptr;
    QDoubleSpinBox* m_tStart        = nullptr;
    QDoubleSpinBox* m_tEnd          = nullptr;
    QDoubleSpinBox* m_tStep         = nullptr;
    QLabel*         m_tStepCount    = nullptr;
    QWidget*        m_singleWidget  = nullptr;
    QWidget*        m_rangeWidget   = nullptr;

    // Points
    QComboBox*      m_patchCombo    = nullptr;
    QPlainTextEdit* m_pointsEdit    = nullptr;
    QLabel*         m_ptCountLbl    = nullptr;
    QPushButton*    m_autowrapBtn   = nullptr;

    // Controls
    QPushButton*    m_queryBtn      = nullptr;
    QPushButton*    m_cancelBtn     = nullptr;
    QPushButton*    m_exportBtn     = nullptr;
    QPushButton*    m_exportRansBtn = nullptr;
    QPushButton*    m_copyPyBtn     = nullptr;
    QPushButton*    m_viewReqBtn    = nullptr;
    QProgressBar*   m_progress      = nullptr;

    // Output
    ResultCanvas*   m_canvas        = nullptr;
    QLabel*         m_statsLabel    = nullptr;
    QTextEdit*      m_log           = nullptr;

    QList<CFD::DatasetInfo>  m_datasets;

    // Last query state (for export + Python generation)
    struct LastQuery {
        QString dataset, field, endpoint;
        double  time = 0;
        QVector<double> times, points;
        int spatialInterp  = 4;
        int temporalInterp = 0;
        bool isRange = false;
    } m_lastQuery;

    QVector<double>           m_lastPoints;
    QList<QVector<double>>    m_lastRangeResults;
    QVector<double>           m_lastRangeTimes;
    QVector<double>           m_lastSingleResult;
    QString                   m_lastField;
    double                    m_lastTime       = 0;
    int                       m_lastComponents = 3;
    CFD::TurbStats            m_lastStats;
};

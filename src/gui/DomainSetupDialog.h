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
#include <QString>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;

class DomainSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit DomainSetupDialog(const QString& stlPath,
                               const QString& caseDir,
                               QWidget* parent = nullptr);

signals:
    void filesGenerated(const QString& caseDir);
    void loadRequested(const QString& caseDir, const QString& stlPath);
    void previewChanged(double x0, double y0, double z0,
                        double x1, double y1, double z1,
                        const QString& flowAxis);

private slots:
    void onGenerate();
    void updatePreview();

private:
    bool computeBounds();
    void getDomainBounds(double& x0, double& x1, double& y0, double& y1,
                         double& z0, double& z1) const;
    bool writeBlockMeshDict();
    bool writeSnappyHexMeshDict();
    bool writeSurfaceFeatureExtractDict();

    QString m_stlPath;
    QString m_caseDir;
    QString m_geoName;

    double m_gx0, m_gx1, m_gy0, m_gy1, m_gz0, m_gz1;
    double m_Lc;

    QDoubleSpinBox* m_upstreamMult   = nullptr;
    QDoubleSpinBox* m_downstreamMult = nullptr;
    QDoubleSpinBox* m_sideMult       = nullptr;
    QDoubleSpinBox* m_cellSize       = nullptr;
    QComboBox*      m_refinement     = nullptr;
    QComboBox*      m_flowAxis       = nullptr;
    QDoubleSpinBox* m_aoa            = nullptr;
    QCheckBox*      m_addLayers      = nullptr;
    QSpinBox*       m_nLayers        = nullptr;

    QLabel* m_domainLabel = nullptr;
    QLabel* m_cellsLabel  = nullptr;
};

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
#include <QFrame>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include "themes/ThemeEngine.h"

class SystemConfigPanel : public QWidget {
    Q_OBJECT
public:
    explicit SystemConfigPanel(QWidget* parent = nullptr);
    void setCaseDir(const QString& dir);
    void refreshControlDict();  // re-reads endTime/writeInterval from disk (no same-case guard)
    void onThemeChanged();

signals:
    void stepsChanged(int endTime);

public slots:
    void onApply();

private:
    void detect();
    void writeDecomposeParDict();

    QString      m_caseDir;
    int          m_physicalCores = 1;
    int          m_logicalCores  = 1;

    QFrame*    m_block       = nullptr;
    QLabel*    m_cpuLabel    = nullptr;
    QLabel*    m_gpuLabel    = nullptr;
    QSpinBox*  m_coresSpin   = nullptr;
    QComboBox* m_methodCombo = nullptr;  // decomposition method
    QSpinBox*  m_nx = nullptr, *m_ny = nullptr, *m_nz = nullptr; // simple/hierarchical
    QWidget*   m_xyzWidget   = nullptr;
    QCheckBox* m_gpuCheck    = nullptr;
    QSpinBox*  m_vramSpin    = nullptr;  // % of GPU VRAM to allow (10-100)
    QLabel*    m_vramMbLbl   = nullptr;  // shows resolved MB
    QLabel*    m_statusLbl   = nullptr;
    QDoubleSpinBox* m_stepsSpin = nullptr;
    QSpinBox*  m_writeEvery  = nullptr;

    struct GpuInfo { QString name; int vramMB; };
    QList<GpuInfo> m_gpus;
};

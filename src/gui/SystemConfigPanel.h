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
#include <limits>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QValidator>
#include <QRegularExpression>
#include "themes/ThemeEngine.h"

// Double spinbox that shows values in shortest 'g' form (0.0005, 1e-05, 200)
// instead of a fixed number of decimals, so small transient times survive.
class GDoubleSpinBox : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double v) const override { return QString::number(v, 'g', 10); }
    double  valueFromText(const QString& t) const override { return t.trimmed().toDouble(); }
    QValidator::State validate(QString& t, int&) const override {
        const QString s = t.trimmed();
        if (s.isEmpty()) return QValidator::Intermediate;
        bool ok; const double v = s.toDouble(&ok);
        if (ok) return (v >= minimum() && v <= maximum()) ? QValidator::Acceptable
                                                          : QValidator::Intermediate;
        static const QRegularExpression partial(R"(^\+?\d*\.?\d*(?:[eE][-+]?\d*)?$)");
        return partial.match(s).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
    }
};

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
    void loadControlDict();          // read endTime/writeInterval into the spinboxes
    void writeControlDictIfEdited(); // write only values the user changed since the last load
    void resyncXyz();                // make nx*ny*nz match the core count

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
    QDoubleSpinBox* m_stepsSpin  = nullptr;
    QDoubleSpinBox* m_writeEvery = nullptr;
    // Values last read from / written to controlDict: anything equal to these was
    // not edited by the user and must not be written back
    double     m_syncedEndTime       = std::numeric_limits<double>::quiet_NaN();
    double     m_syncedWriteInterval = std::numeric_limits<double>::quiet_NaN();

    struct GpuInfo { QString name; int vramMB; };
    QList<GpuInfo> m_gpus;
};

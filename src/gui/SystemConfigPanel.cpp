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

#include "SystemConfigPanel.h"
#include <cmath>
#include <algorithm>
#include <QHBoxLayout>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFrame>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QLabel>
#include <QRegularExpression>
#include <QMessageBox>
#include <QSettings>
#include <QFontMetrics>
#include <QSizePolicy>

static bool writeDictVal(const QString& path, const QString& key, const QString& value);

static QFrame* makeSep()
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::VLine);
    f->setFrameShadow(QFrame::Sunken);
    f->setFixedWidth(1);
    return f;
}

SystemConfigPanel::SystemConfigPanel(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(52);

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);

    m_block = new QFrame;
    m_block->setFrameShape(QFrame::NoFrame);
    outer->addWidget(m_block);

    auto* lay = new QHBoxLayout(m_block);
    lay->setContentsMargins(12, 4, 12, 4);
    lay->setSpacing(14);

    auto* cpuIcon = new QLabel("⚙");
    cpuIcon->setStyleSheet("font-size:15px;");
    lay->addWidget(cpuIcon);

    m_cpuLabel = new QLabel("Detecting CPU...");
    m_cpuLabel->setStyleSheet("font-size:12px;");
    m_cpuLabel->setMaximumWidth(180);
    m_cpuLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    lay->addWidget(m_cpuLabel);

    auto* coresLbl = new QLabel("Cores:");
    coresLbl->setStyleSheet("font-size:11px;");
    lay->addWidget(coresLbl);
    m_coresSpin = new QSpinBox;
    m_coresSpin->setRange(1, 256);
    m_coresSpin->setValue(4);
    m_coresSpin->setFixedWidth(58);
    m_coresSpin->setToolTip("MPI processes; decomposeParDict numberOfSubdomains");
    lay->addWidget(m_coresSpin);

    auto* methodLbl = new QLabel("Method:");
    methodLbl->setStyleSheet("font-size:11px;");
    lay->addWidget(methodLbl);
    m_methodCombo = new QComboBox;
    m_methodCombo->addItems({"scotch", "simple", "hierarchical", "metis", "ptscotch"});
    m_methodCombo->setFixedWidth(110);
    m_methodCombo->setToolTip(
        "scotch/ptscotch: automatic graph partitioning (best quality)\n"
        "simple/hierarchical: geometric, specify nx x ny x nz\n"
        "metis: graph partitioning (requires libmetis)");
    lay->addWidget(m_methodCombo);

    m_xyzWidget = new QWidget;
    m_xyzWidget->setStyleSheet("background: transparent;");
    auto* xyzLay = new QHBoxLayout(m_xyzWidget);
    xyzLay->setContentsMargins(0, 0, 0, 0);
    xyzLay->setSpacing(6);
    auto makeXyz = [](int def) {
        auto* s = new QSpinBox;
        s->setRange(1, 256);
        s->setValue(def);
        s->setFixedWidth(52);
        return s;
    };
    m_nx = makeXyz(2); m_ny = makeXyz(2); m_nz = makeXyz(1);
    auto addXyz = [&](const QString& lbl, QSpinBox* s) {
        auto* l = new QLabel(lbl);
        l->setStyleSheet("font-size:11px;");
        xyzLay->addWidget(l);
        xyzLay->addWidget(s);
    };
    addXyz("nx", m_nx); addXyz("ny", m_ny); addXyz("nz", m_nz);
    m_xyzWidget->hide();
    lay->addWidget(m_xyzWidget);

    auto* stepsLbl = new QLabel("Steps:");
    stepsLbl->setStyleSheet("font-size:11px;");
    lay->addWidget(stepsLbl);
    m_stepsSpin = new GDoubleSpinBox;
    m_stepsSpin->setRange(0.0, 1e9);
    m_stepsSpin->setDecimals(10);
    m_stepsSpin->setSingleStep(0.5);
    m_stepsSpin->setValue(200.0);
    m_stepsSpin->setFixedWidth(80);
    m_stepsSpin->setToolTip("endTime: iterations for steady-state, seconds for transient (icoFoam/pimpleFoam)");
    lay->addWidget(m_stepsSpin);
    connect(m_stepsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ emit stepsChanged(static_cast<int>(v)); });

    connect(m_stepsSpin, &QAbstractSpinBox::editingFinished,
            this, &SystemConfigPanel::writeControlDictIfEdited);

    auto* writeEveryLbl = new QLabel("Save every:");
    writeEveryLbl->setStyleSheet("font-size:11px;");
    lay->addWidget(writeEveryLbl);
    m_writeEvery = new GDoubleSpinBox;
    m_writeEvery->setRange(1e-9, 1e9);
    m_writeEvery->setDecimals(10);
    m_writeEvery->setValue(100);
    m_writeEvery->setSingleStep(50);
    m_writeEvery->setFixedWidth(64);
    m_writeEvery->setToolTip("controlDict writeInterval: write a result snapshot every N steps "
                             "(seconds for transient runTime control)");
    lay->addWidget(m_writeEvery);

    connect(m_writeEvery, &QAbstractSpinBox::editingFinished,
            this, &SystemConfigPanel::writeControlDictIfEdited);

    connect(m_methodCombo, &QComboBox::currentTextChanged, this,
            [this](const QString& m) {
                const bool geometric = (m == "simple" || m == "hierarchical");
                m_xyzWidget->setVisible(geometric);
                if (geometric) resyncXyz();
            });
    connect(m_coresSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { resyncXyz(); });

    lay->addWidget(makeSep());

    auto* gpuIcon = new QLabel("◈");
    gpuIcon->setStyleSheet("font-size:14px;");
    lay->addWidget(gpuIcon);

    m_gpuLabel = new QLabel("Detecting GPU...");
    m_gpuLabel->setStyleSheet("font-size:12px;");
    m_gpuLabel->setMaximumWidth(160);
    m_gpuLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    lay->addWidget(m_gpuLabel);

    m_gpuCheck = new QCheckBox("GPU solver");
    m_gpuCheck->setStyleSheet("background: transparent;");
    m_gpuCheck->setToolTip(
        "Route linear solvers through GPU via AmgX/PETSc backend.\n"
        "CakeCFD must be built with GPU support enabled.");
    m_gpuCheck->setEnabled(false);
    lay->addWidget(m_gpuCheck);

    auto* vramLabel = new QLabel("VRAM:");
    vramLabel->setStyleSheet("font-size:12px;");
    lay->addWidget(vramLabel);

    m_vramSpin = new QSpinBox;
    m_vramSpin->setRange(10, 100);
    m_vramSpin->setSingleStep(5);
    m_vramSpin->setValue(80);
    m_vramSpin->setSuffix("%");
    m_vramSpin->setFixedWidth(62);
    m_vramSpin->setToolTip("Maximum VRAM the GPU solver may use.\n"
                           "Keep below 100% to leave headroom for the OS and display driver.");
    m_vramSpin->setEnabled(false);
    lay->addWidget(m_vramSpin);

    m_vramMbLbl = new QLabel;
    m_vramMbLbl->setStyleSheet("font-size:11px; color: palette(mid);");
    m_vramMbLbl->setVisible(false);
    lay->addWidget(m_vramMbLbl);

    connect(m_gpuCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            auto res = QMessageBox::question(this, "Enable GPU Solver",
                "Enable GPU-accelerated linear solvers via AmgX/PETSc?\n\n"
                "CakeCFD must be compiled with GPU support.\n"
                "Click Apply to write the updated decomposeParDict.",
                QMessageBox::Yes | QMessageBox::Cancel);
            if (res != QMessageBox::Yes) {
                QSignalBlocker b(m_gpuCheck);
                m_gpuCheck->setChecked(false);
                return;
            }
            m_statusLbl->setText("GPU solver enabled: click Apply.");
        } else {
            m_statusLbl->setText("GPU solver disabled: click Apply.");
        }
        if (m_vramSpin) m_vramSpin->setEnabled(checked);
        onThemeChanged();
    });

    connect(m_vramSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int pct) {
        if (!m_gpus.isEmpty() && m_vramMbLbl) {
            int mb = m_gpus[0].vramMB * pct / 100;
            m_vramMbLbl->setText(QString("(%1 MB)").arg(mb));
        }
    });

    lay->addWidget(makeSep());

    auto* btnApply = new QPushButton("Apply");
    btnApply->setFixedWidth(64);
    btnApply->setToolTip("Write system/decomposeParDict");
    lay->addWidget(btnApply);

    m_statusLbl = new QLabel;
    m_statusLbl->setStyleSheet("font-size:11px;");
    lay->addWidget(m_statusLbl);
    lay->addStretch(1);

    connect(btnApply, &QPushButton::clicked, this, &SystemConfigPanel::onApply);

    m_block->setStyleSheet(m_block->styleSheet() +
        "QFrame { border-bottom: 2px solid rgba(0,0,0,120); }");

    detect();
    onThemeChanged();
}

void SystemConfigPanel::onThemeChanged()
{
    const ThemeColors& c = ThemeEngine::instance().colors();

    setStyleSheet(QString("SystemConfigPanel { background:%1; }").arg(c.window.name()));

    if (m_block) {
        m_block->setStyleSheet(QString(
            "QFrame { background:%1; border:1px solid %2; border-radius:4px; }"
            "QLabel { background:transparent; border:none; }")
            .arg(c.raised.name(), c.border.name()));
    }

    if (m_gpuLabel && !m_gpus.isEmpty())
        m_gpuLabel->setStyleSheet(QString("font-size:12px; color:%1;").arg(c.success.name()));
}

void SystemConfigPanel::detect()
{
    QFile cpuinfo("/proc/cpuinfo");
    if (cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = QString::fromUtf8(cpuinfo.readAll());
        QRegularExpression modelRe(R"(model name\s*:\s*(.+))");
        auto mm = modelRe.match(txt);
        QString model = mm.hasMatch() ? mm.captured(1).trimmed() : "Unknown CPU";
        model.remove(QRegularExpression(R"(\(R\)|\(TM\)|CPU\s+)"));
        model = model.simplified();

        m_logicalCores  = txt.count("processor\t:");
        QRegularExpression coreRe(R"(cpu cores\s*:\s*(\d+))");
        auto cm = coreRe.match(txt);
        m_physicalCores = cm.hasMatch() ? cm.captured(1).toInt() : m_logicalCores;

        QString fullCpu = QString("%1  %2c/%3t").arg(model).arg(m_physicalCores).arg(m_logicalCores);
        m_cpuLabel->setToolTip(fullCpu);
        QFontMetrics fm(m_cpuLabel->font());
        m_cpuLabel->setText(fm.elidedText(fullCpu, Qt::ElideRight, 180));
        m_coresSpin->setMaximum(m_logicalCores);
        m_coresSpin->setValue(qMax(1, m_physicalCores));
    }

    QProcess smi;
    smi.start("nvidia-smi", {"--query-gpu=name,memory.total", "--format=csv,noheader,nounits"});
    const bool started  = smi.waitForStarted(2000);
    const bool finished = started && smi.waitForFinished(2000);
    if (started && !finished) { smi.kill(); smi.waitForFinished(500); }
    if (finished && smi.exitStatus() == QProcess::NormalExit && smi.exitCode() == 0) {
        for (const QString& line :
             QString::fromUtf8(smi.readAllStandardOutput()).trimmed().split('\n', Qt::SkipEmptyParts)) {
            QStringList p = line.split(',');
            if (p.size() >= 2) m_gpus.append({p[0].trimmed(), p[1].trimmed().toInt()});
        }
        if (!m_gpus.isEmpty()) {
            QString fullGpu = QString("%1  %2 GB")
                .arg(m_gpus[0].name).arg(m_gpus[0].vramMB / 1024.0, 0, 'f', 1);
            m_gpuLabel->setToolTip(fullGpu);
            QFontMetrics fmg(m_gpuLabel->font());
            m_gpuLabel->setText(fmg.elidedText(fullGpu, Qt::ElideRight, 160));
            m_gpuCheck->setEnabled(true);
            if (m_vramMbLbl) {
                int mb = m_gpus[0].vramMB * (m_vramSpin ? m_vramSpin->value() : 80) / 100;
                m_vramMbLbl->setText(QString("(%1 MB)").arg(mb));
                m_vramMbLbl->setVisible(true);
            }
            onThemeChanged();
        } else {
            m_gpuLabel->setText("No GPU detected");
        }
    } else {
        m_gpuLabel->setText("No GPU detected");
    }
}

static QString readDictVal(const QString& path, const QString& key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString txt = QString::fromUtf8(f.readAll());
    QRegularExpression re(QRegularExpression::escape(key) + R"(\s+([^\s;]+)\s*;)");
    auto m = re.match(txt);
    return m.hasMatch() ? m.captured(1) : QString{};
}

static bool writeDictVal(const QString& path, const QString& key, const QString& value)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QString txt = QString::fromUtf8(f.readAll());
    f.close();
    QRegularExpression re(QRegularExpression::escape(key) + R"(\s+[^;]+;)");
    auto m = re.match(txt);
    if (!m.hasMatch()) return false;
    txt = txt.left(m.capturedStart()) + key + "  " + value + ";" + txt.mid(m.capturedEnd());
    QFile fw(path);
    if (!fw.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    fw.write(txt.toUtf8());
    return true;
}

void SystemConfigPanel::setCaseDir(const QString& dir)
{
    bool newCase = (dir != m_caseDir);
    m_caseDir = dir;
    m_statusLbl->clear();

    if (newCase) loadControlDict();
}

void SystemConfigPanel::refreshControlDict()
{
    if (m_caseDir.isEmpty()) return;
    loadControlDict();
}

void SystemConfigPanel::loadControlDict()
{
    QString cd = m_caseDir + "/system/controlDict";
    QString et = readDictVal(cd, "endTime");
    QString wi = readDictVal(cd, "writeInterval");

    if (!et.isEmpty() && m_stepsSpin) {
        bool ok; double v = et.toDouble(&ok);
        if (ok) m_stepsSpin->setValue(v);
    }
    if (!wi.isEmpty() && m_writeEvery) {
        bool ok; double v = wi.toDouble(&ok);
        if (ok) m_writeEvery->setValue(v);
    }
    if (m_stepsSpin)  m_syncedEndTime       = m_stepsSpin->value();
    if (m_writeEvery) m_syncedWriteInterval = m_writeEvery->value();
}

void SystemConfigPanel::writeControlDictIfEdited()
{
    if (m_caseDir.isEmpty()) return;
    const QString cd = m_caseDir + "/system/controlDict";
    auto same = [](double a, double b) {
        return !std::isnan(b) && std::abs(a - b) <= 1e-12 * std::max(std::abs(a), std::abs(b));
    };
    if (m_stepsSpin && !same(m_stepsSpin->value(), m_syncedEndTime)) {
        const double v = m_stepsSpin->value();
        if (writeDictVal(cd, "endTime", QString::number(v, 'g', 10))) m_syncedEndTime = v;
    }
    if (m_writeEvery && !same(m_writeEvery->value(), m_syncedWriteInterval)) {
        const double v = m_writeEvery->value();
        if (writeDictVal(cd, "writeInterval", QString::number(v, 'g', 10))) m_syncedWriteInterval = v;
    }
}

void SystemConfigPanel::onApply()
{
    writeDecomposeParDict();
    writeControlDictIfEdited();
    onThemeChanged();
}

void SystemConfigPanel::resyncXyz()
{
    const QString m = m_methodCombo->currentText();
    if (m != "simple" && m != "hierarchical") return;
    const int n = m_coresSpin->value();
    if (m_nx->value() * m_ny->value() * m_nz->value() == n) return;
    m_nx->setValue(n); m_ny->setValue(1); m_nz->setValue(1);
}

void SystemConfigPanel::writeDecomposeParDict()
{
    if (m_caseDir.isEmpty()) { m_statusLbl->setText("No case open."); return; }

    QDir().mkpath(m_caseDir + "/system");
    int     n      = m_coresSpin->value();
    QString method = m_methodCombo->currentText();

    QString methodBlock;
    if (method == "simple" || method == "hierarchical") {
        int nx = m_nx->value(), ny = m_ny->value(), nz = m_nz->value();
        if (nx * ny * nz != n) {
            m_statusLbl->setText(QString("nx*ny*nz = %1 must equal Cores (%2): not written.")
                                     .arg(nx * ny * nz).arg(n));
            return;
        }
        methodBlock = QString("method  %1;\n\n%1Coeffs\n{\n    n     ( %2 %3 %4 );\n    delta 0.001;\n    order xyz;\n}\n")
                          .arg(method).arg(nx).arg(ny).arg(nz);
        m_statusLbl->setText(QString("Written: %1 cores  %2  %3x%4x%5").arg(n).arg(method).arg(nx).arg(ny).arg(nz));
    } else if (method == "metis") {
        methodBlock = QString("method  metis;\n\nmetisCoeffs\n{\n    processorWeights ( %1);\n}\n")
                          .arg(QString("1 ").repeated(n));
        m_statusLbl->setText(QString("Written: %1 cores  metis").arg(n));
    } else {
        methodBlock = QString("method  %1;\n\n%1Coeffs\n{\n    processorWeights ( %2);\n}\n")
                          .arg(method).arg(QString("1 ").repeated(n));
        m_statusLbl->setText(QString("Written: %1 cores  %2").arg(n).arg(method));
    }

    QString content =
        "FoamFile\n{\n    version 2.0;\n    format ascii;\n"
        "    class dictionary;\n    object decomposeParDict;\n}\n\n"
        + QString("numberOfSubdomains  %1;\n\n").arg(n)
        + methodBlock;

    const bool gpuOn = m_gpuCheck && m_gpuCheck->isChecked();
    const int  vramPct = (m_vramSpin ? m_vramSpin->value() : 80);
    content += gpuOn
        ? QString("\nuseGPUSolver      true;\ngpuVramFraction   %1;\n").arg(vramPct / 100.0, 0, 'f', 2)
        : "\nuseGPUSolver      false;\ngpuVramFraction   0.80;\n";

    QFile f(m_caseDir + "/system/decomposeParDict");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { m_statusLbl->setText("Write failed."); return; }
    f.write(content.toUtf8());
}


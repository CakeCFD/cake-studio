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

#include "FileBrowserDialog.h"
#include "ConsolePanel.h"
#include "CitationManager.h"
#include "ThermalPropertiesDialog.h"
#include "SolverConfigDialog.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDir>
#include <QKeyEvent>
#include <QStringList>
#include <QSplitter>
#include <QClipboard>
#include <QApplication>
#include <QToolTip>
#include <QMessageBox>
#include <QRegularExpression>

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent) {
    m_runner = new SolverRunner(this);
    setupUI();

    connect(m_runner, &SolverRunner::outputLine,     this,   &ConsolePanel::appendLine);
    connect(m_runner, &SolverRunner::solverStarted,  this,   &ConsolePanel::onSolverStarted);
    connect(m_runner, &SolverRunner::solverFinished, this,   &ConsolePanel::onSolverFinished);
    connect(m_runner, &SolverRunner::solverError,    this,   &ConsolePanel::appendLine);
    connect(m_runner, &SolverRunner::residualParsed, m_plot, &ResidualPlot::addResidual);
}

void ConsolePanel::setupUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // Controls row
    auto* controls = new QHBoxLayout();

    m_solverCombo = new QComboBox;
    m_solverCombo->addItems({"simpleFoam", "icoFoam", "pimpleFoam", "rhoSimpleFoam",
                              "buoyantSimpleFoam", "buoyantPimpleFoam", "rhoCentralFoam"});
    m_solverCombo->setFixedWidth(160);

    m_casePathEdit = new QLineEdit();
    m_casePathEdit->setPlaceholderText("Case directory...");

    auto* browseBtn = new QPushButton("Browse");
    connect(browseBtn, &QPushButton::clicked, this, [this](){
        QString start = m_casePathEdit->text().trimmed();
        if (start.isEmpty()) start = QDir::homePath();
        QString dir = FileBrowserDialog::getExistingDirectory(
            this, "Select CakeCFD Case", start);
        if (!dir.isEmpty()) m_casePathEdit->setText(dir);
    });

    m_runBtn  = new QPushButton("Run");
    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setEnabled(false);
    m_statusLabel = new QLabel("Idle");

    auto* resetRunBtn = new QPushButton("Reset & Run");
    connect(resetRunBtn, &QPushButton::clicked, this, [this](){
        QString caseDir = m_casePathEdit->text().trimmed();
        if (caseDir.isEmpty()) { appendLine("[ERROR] Set case directory first."); return; }
        // Remove all numeric time directories (old solution data)
        QDir d(caseDir);
        for (const QString& entry : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok; int n = entry.toInt(&ok);
            if (ok && n > 0) QDir(d.filePath(entry)).removeRecursively();
        }
        appendLine("  Cleared old time directories - starting fresh...");
        onRunClicked();
    });
    connect(m_runBtn,  &QPushButton::clicked, this, &ConsolePanel::onRunClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &ConsolePanel::onStopClicked);

    controls->addWidget(new QLabel("Solver:"));
    controls->addWidget(m_solverCombo);
    controls->addWidget(m_casePathEdit);
    controls->addWidget(browseBtn);
    controls->addWidget(resetRunBtn);
    controls->addWidget(m_runBtn);
    controls->addWidget(m_stopBtn);
    controls->addWidget(m_statusLabel);
    controls->addStretch();

    m_solverConfigBtn = new QPushButton("Solver Config");
    m_solverConfigBtn->setToolTip("Configure fvSchemes, fvSolution, and initial fields for this solver.\n"
                                  "Click before running to write all required case files.");
    controls->addWidget(m_solverConfigBtn);

    connect(m_solverConfigBtn, &QPushButton::clicked, this, [this]() {
        QString caseDir = m_casePathEdit->text().trimmed();
        if (caseDir.isEmpty()) {
            caseDir = FileBrowserDialog::getExistingDirectory(
                this, "Select case directory", QDir::homePath());
            if (caseDir.isEmpty()) return;
            m_casePathEdit->setText(caseDir);
        }
        SolverConfigDialog dlg(m_solverCombo->currentText(), caseDir, this);
        dlg.exec();
        emit caseFilesChanged(caseDir);
        hideSolverBanner();
    });

    connect(m_solverCombo, &QComboBox::currentTextChanged,
            this, &ConsolePanel::showSolverBanner);

    // Solver-change warning banner (hidden until solver is switched)
    m_solverBanner = new QFrame;
    m_solverBanner->setFrameShape(QFrame::StyledPanel);
    m_solverBanner->setObjectName("solverBanner");
    m_solverBanner->setStyleSheet(
        "QFrame#solverBanner {"
        "  background: #3d3000;"
        "  border: 1px solid #a87d00;"
        "  border-radius: 4px;"
        "  padding: 0px;"
        "}");
    {
        auto* bl = new QHBoxLayout(m_solverBanner);
        bl->setContentsMargins(8, 5, 8, 5);
        bl->setSpacing(8);

        auto* icon = new QLabel("⚠");
        icon->setStyleSheet("color:#f9e2af; font-size:14px;");
        icon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        m_bannerLabel = new QLabel;
        m_bannerLabel->setStyleSheet("color:#f9e2af; font-size:11px;");
        m_bannerLabel->setWordWrap(false);

        auto* openBtn = new QPushButton("Open Solver Config");
        openBtn->setStyleSheet(
            "QPushButton { font-size:10px; padding:2px 10px;"
            "  color:#f9e2af; border:1px solid #a87d00;"
            "  border-radius:3px; background:transparent; }"
            "QPushButton:hover { background:#a87d00; color:#1e1e2e; }");
        openBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(openBtn, &QPushButton::clicked, m_solverConfigBtn, &QPushButton::click);

        auto* dismissBtn = new QPushButton("✕");
        dismissBtn->setFixedSize(20, 20);
        dismissBtn->setStyleSheet(
            "QPushButton { font-size:10px; color:#a87d00; border:none; background:transparent; }"
            "QPushButton:hover { color:#f9e2af; }");
        connect(dismissBtn, &QPushButton::clicked, this, &ConsolePanel::hideSolverBanner);

        bl->addWidget(icon);
        bl->addWidget(m_bannerLabel, 1);
        bl->addWidget(openBtn);
        bl->addWidget(dismissBtn);
    }
    m_solverBanner->setVisible(false);

    // Splitter: plot on top, text output below
    auto* splitter = new QSplitter(Qt::Vertical);

    m_plot = new ResidualPlot();
    splitter->addWidget(m_plot);

    m_output = new QPlainTextEdit();
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(9);
    m_output->setFont(mono);
    splitter->addWidget(m_output);

    splitter->setStretchFactor(0, 2);   // plot gets 2/5
    splitter->setStretchFactor(1, 3);   // log text gets 3/5

    root->addLayout(controls);
    root->addWidget(m_solverBanner);
    root->addWidget(splitter);
    // Terminal input bar
    auto* termRow = new QHBoxLayout;
    termRow->setSpacing(4);

    auto* prompt = new QLabel("$");
    prompt->setStyleSheet("color:#6af; font-family:monospace; font-weight:bold;");

    m_cmdEdit = new QLineEdit;
    m_cmdEdit->setFont(QFont("Monospace", 9));
    m_cmdEdit->setPlaceholderText("CakeCFD command... (Tab to complete, up/down history)");
    m_cmdEdit->installEventFilter(this);

    auto* runCmdBtn = new QPushButton("Run");
    runCmdBtn->setFixedWidth(48);

    auto makeCopyBtn = [this](const QString& label, const QString& color,
                               const QStringList& keywords) {
        auto* btn = new QPushButton(label);
        btn->setFixedWidth(110);
        btn->setStyleSheet(QString(
            "QPushButton { font-size:10px; padding:2px 6px; color:%1; border:1px solid %1; "
            "border-radius:3px; background:transparent; }"
            "QPushButton:hover { background:%1; color:#1e1e2e; }").arg(color));
        connect(btn, &QPushButton::clicked, this, [this, keywords, btn]() {
            // Capture full blocks: from the matching line until the next blank line,
            // so multi-line FOAM errors are copied in their entirety.
            QStringList lines = m_output->toPlainText().split('\n');
            QStringList blocks;
            bool inBlock = false;
            QStringList currentBlock;
            for (const QString& line : lines) {
                bool matches = false;
                for (const QString& kw : keywords)
                    if (line.contains(kw, Qt::CaseInsensitive)) { matches = true; break; }
                if (matches) {
                    if (!currentBlock.isEmpty()) blocks << currentBlock.join('\n') << "";
                    currentBlock.clear();
                    inBlock = true;
                }
                if (inBlock) {
                    if (line.trimmed().isEmpty() && !currentBlock.isEmpty()) {
                        blocks << currentBlock.join('\n') << "";
                        currentBlock.clear();
                        inBlock = false;
                    } else {
                        currentBlock << line;
                    }
                }
            }
            if (inBlock && !currentBlock.isEmpty()) blocks << currentBlock.join('\n');

            if (blocks.isEmpty()) {
                QToolTip::showText(btn->mapToGlobal(btn->rect().center()), "Nothing found", btn);
                return;
            }
            QApplication::clipboard()->setText(blocks.join('\n').trimmed());
            int count = std::count_if(blocks.begin(), blocks.end(), [](const QString& s){ return s.isEmpty(); }) + 1;
            QToolTip::showText(btn->mapToGlobal(btn->rect().center()),
                               QString("Copied %1 block(s)").arg(count), btn);
        });
        return btn;
    };

    auto* copyWarnBtn = makeCopyBtn("⚠ Copy Warnings", "#f9e2af",
                                    {"Warning", "WARNING", "WARN", "warning"});
    auto* copyErrBtn  = makeCopyBtn("✕ Copy Errors",   "#f38ba8",
                                    {"Error", "ERROR", "FATAL", "FAILED", "error:", "fatal"});

    // Completer with OF commands
    QStringList ofCmds = {
        // Solvers
        "simpleFoam","icoFoam","pimpleFoam","pisoFoam",
        "rhoSimpleFoam","rhoCentralFoam","buoyantSimpleFoam","buoyantPimpleFoam",
        "sonicFoam","laplacianFoam","potentialFoam","SRFSimpleFoam",
        // Mesh
        "blockMesh","snappyHexMesh","surfaceFeatureExtract","checkMesh",
        "extrudeMesh","refineMesh","renumberMesh","decomposePar","reconstructPar",
        "foamCleanPolyMesh","transformPoints","mirrorMesh","createPatch",
        // Post
        "foamToVTK","paraFoam","foamListTimes","postProcess",
        "sample","probeLocations","streamlines",
        // Utilities
        "foamInfo","foamDictionary","setFields","mapFields",
        "changeDictionary","topoSet","createBaffles",
        // Surface
        "surfaceCheck","surfaceConvert","surfaceFeatureConvert",
        "surfaceSplitByTopology","surfaceOrient",
    };
    m_suggModel = new QStringListModel(ofCmds, this);
    m_completer = new QCompleter(m_suggModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_cmdEdit->setCompleter(m_completer);

    connect(runCmdBtn, &QPushButton::clicked, this, [this](){
        execCommand(m_cmdEdit->text().trimmed());
    });
    connect(m_cmdEdit, &QLineEdit::returnPressed, this, [this](){
        execCommand(m_cmdEdit->text().trimmed());
    });

    termRow->addWidget(prompt);
    termRow->addWidget(m_cmdEdit, 1);
    termRow->addWidget(runCmdBtn);
    termRow->addWidget(copyWarnBtn);
    termRow->addWidget(copyErrBtn);
    root->addLayout(termRow);

}

void ConsolePanel::appendLine(const QString& line) {
    m_output->appendPlainText(line);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void ConsolePanel::onSolverStarted() {
    hideSolverBanner();
    m_statusLabel->setText("Running...");
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_plot->clear();
    appendLine("=== Solver started ===");
    detectAndCiteSchemes();
}

void ConsolePanel::detectAndCiteSchemes() {
    QString caseDir = m_casePathEdit->text().trimmed();
    if (caseDir.isEmpty()) return;

    auto& cit = CitationManager::instance();

    // Detect TENO5 from system/fvSchemes
    QFile fvSchemes(caseDir + "/system/fvSchemes");
    if (fvSchemes.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = fvSchemes.readAll();
        if (txt.contains("TENO5", Qt::CaseInsensitive))
            cit.cite("teno5", caseDir);
        fvSchemes.close();
    }

    // Detect turbulence model from constant/momentumTransport or turbulenceProperties
    for (const QString& fname : {caseDir + "/constant/momentumTransport",
                                  caseDir + "/constant/turbulenceProperties"}) {
        QFile tf(fname);
        if (!tf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString txt = tf.readAll();
        if (txt.contains("kOmegaSST", Qt::CaseSensitive))
            cit.cite("komegasst", caseDir);
        else if (txt.contains("kEpsilon", Qt::CaseSensitive))
            cit.cite("kepsilon", caseDir);
        tf.close();
        break;
    }

    // Print any newly cited references
    QStringList msgs = cit.consumePending();
    if (!msgs.isEmpty()) {
        appendLine("\n  [Cake] Active numerical methods; please cite:");
        for (const QString& m : msgs)
            appendLine(m);
        appendLine("  [Cake] citations.bib written to case directory.\n");
    }
}

void ConsolePanel::onSolverFinished(int exitCode) {
    m_statusLabel->setText(exitCode == 0 ? "Finished" : "Failed (code " + QString::number(exitCode) + ")");
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    appendLine("=== Solver finished (exit " + QString::number(exitCode) + ") ===");

    QString caseDir = m_casePathEdit->text().trimmed();

    if (!m_currentRunDir.isEmpty()) {
        if (exitCode == 0) {
            RunLogger::finishRun(m_currentRunDir, caseDir, true);
            appendLine(QString("[Run] Saved: %1").arg(QFileInfo(m_currentRunDir).fileName()));
        } else {
            // Solver crashed: discard the run log, don't pollute run history
            QDir(m_currentRunDir).removeRecursively();
            appendLine("[Run] Solver exited with errors: run not saved.");
        }
        m_currentRunDir.clear();
    }

    // Always try to show whatever time steps exist: useful for failed/diverged runs too.
    if (!caseDir.isEmpty()) {
        QDir d(caseDir);
        static QRegularExpression numRe("^\\d+(\\.\\d+)?$");
        bool hasResults = false;
        for (const QString& e : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok; double v = e.toDouble(&ok);
            if (ok && v > 0) { hasResults = true; break; }
        }
        if (hasResults)
            emit resultsReady(caseDir);
    }
}

void ConsolePanel::showSolverBanner(const QString& solver)
{
    if (!m_solverBanner || !m_bannerLabel) return;
    m_bannerLabel->setText(
        "Solver changed to <b>" + solver + "</b> - "
        "click <b>Open Solver Config</b> to write required case files before running.");
    m_solverBanner->setVisible(true);
}

void ConsolePanel::hideSolverBanner()
{
    if (m_solverBanner) m_solverBanner->setVisible(false);
}

QString ConsolePanel::caseDir() const { return m_casePathEdit->text().trimmed(); }

void ConsolePanel::setCaseDir(const QString& dir)
{
    m_casePathEdit->setText(dir);
    appendLine("[Case] " + dir);

    // Auto-detect solver from controlDict so the combo stays in sync with the case
    QFile ctlFile(dir + "/system/controlDict");
    if (ctlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = ctlFile.readAll();
        QRegularExpression re(R"(^\s*application\s+(\w+)\s*;)", QRegularExpression::MultilineOption);
        auto m = re.match(txt);
        if (m.hasMatch()) {
            QSignalBlocker b(m_solverCombo);
            int idx = m_solverCombo->findText(m.captured(1));
            if (idx >= 0) m_solverCombo->setCurrentIndex(idx);
        }
    }
}

void ConsolePanel::setSolver(const QString& solver)
{
    int idx = m_solverCombo->findText(solver);
    if (idx >= 0) m_solverCombo->setCurrentIndex(idx);
}

void ConsolePanel::onRunClicked() {
    QString caseDir = m_casePathEdit->text().trimmed();
    QString solver  = m_solverCombo->currentText();
    if (caseDir.isEmpty() || solver.isEmpty()) {
        appendLine("[ERROR] Set solver and case directory first.");
        return;
    }
    emit aboutToRun();  // flush PhysicsPanel + SystemConfig settings to disk before starting

    // Detect if the selected solver doesn't match the case's controlDict application field.
    // When they differ, files written by a previous Solver Config are wrong - require the
    // user to re-run it so the case is properly configured for the new solver.
    {
        QString currentApp;
        QFile ctl(caseDir + "/system/controlDict");
        if (ctl.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QRegularExpression re(R"(^\s*application\s+(\w+)\s*;)", QRegularExpression::MultilineOption);
            auto m = re.match(ctl.readAll());
            if (m.hasMatch()) currentApp = m.captured(1);
        }
        if (!currentApp.isEmpty() && currentApp != solver) {
            auto btn = QMessageBox::question(this, "Solver Mismatch",
                "The case is configured for <b>" + currentApp + "</b> but you selected <b>" + solver + "</b>.<br><br>"
                "The existing fvSchemes, fvSolution, and 0/ fields will not work with " + solver + ".<br>"
                "Run Solver Config now to rewrite them?",
                QMessageBox::Yes | QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;
            SolverConfigDialog dlg(solver, caseDir, this);
            dlg.exec();
            emit caseFilesChanged(caseDir);
            // Re-verify: if Write was done, application is now updated to solver
            QString newApp;
            QFile ctl2(caseDir + "/system/controlDict");
            if (ctl2.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QRegularExpression re2(R"(^\s*application\s+(\w+)\s*;)", QRegularExpression::MultilineOption);
                auto m2 = re2.match(ctl2.readAll());
                if (m2.hasMatch()) newApp = m2.captured(1);
            }
            if (newApp != solver) {
                appendLine("[Run] Solver Config not written for " + solver + " - aborting run.");
                return;
            }
        }
    }

    // Compressible solvers: if SolverConfigDialog hasn't been run yet, prompt the user
    const bool isCompressible = (solver == "rhoSimpleFoam"     || solver == "rhoPimpleFoam"
                                  || solver == "rhoCentralFoam"
                                  || solver == "buoyantSimpleFoam" || solver == "buoyantPimpleFoam");
    if (isCompressible && !QFile::exists(caseDir + "/constant/thermophysicalProperties")) {
        auto btn = QMessageBox::question(this, "Solver Config Required",
            solver + " requires thermophysical properties and initial fields.\n\n"
            "Open Solver Config to write them now?",
            QMessageBox::Yes | QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;
        SolverConfigDialog dlg(solver, caseDir, this);
        if (dlg.exec() != QDialog::Accepted) return;
        emit caseFilesChanged(caseDir);
    }

    // Incompressible solvers: restore kinematic p and incompressible fvSchemes if case was compressible
    const bool isIncompressible = (solver == "simpleFoam" || solver == "icoFoam"
                                    || solver == "pimpleFoam" || solver == "pisoFoam");
    if (isIncompressible) {
        QString pPath = caseDir + "/0/p";
        QFile pf(pPath);
        if (pf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString txt = pf.readAll();
            pf.close();
            if (txt.contains("[1 -1 -2") || txt.contains("uniform 101325")) {
                // Detect div scheme to preserve TENO
                QString divUScheme = "Gauss linearUpwind grad(U)";
                {
                    QFile sf(caseDir + "/system/fvSchemes");
                    if (sf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QRegularExpression re(R"(div\(phi,U\)\s+(Gauss\s+\S+(?:\s+\S+)?)\s*;)");
                        auto m = re.match(sf.readAll());
                        if (m.hasMatch()) divUScheme = m.captured(1).trimmed();
                    }
                }

                // Restore 0/p to kinematic pressure
                struct PI { QString name, type; };
                QList<PI> patches;
                {
                    QFile bf(caseDir + "/constant/polyMesh/boundary");
                    if (bf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QRegularExpression re(R"((\w+)\s*\{[^}]*\btype\s+(\w+)\s*;[^}]*\})",
                                             QRegularExpression::DotMatchesEverythingOption);
                        auto it = re.globalMatch(bf.readAll());
                        while (it.hasNext()) {
                            auto m = it.next();
                            if (m.captured(1) != "FoamFile") patches.append({m.captured(1), m.captured(2)});
                        }
                    }
                }
                QFile::remove(pPath);
                QFile wf(pPath);
                if (wf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream o(&wf);
                    o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object p; }\n\n"
                      << "dimensions [0 2 -2 0 0 0 0];\n"
                      << "internalField uniform 0;\n"
                      << "boundaryField\n{\n";
                    if (patches.isEmpty()) {
                        o << "    inlet  { type zeroGradient; }\n"
                          << "    outlet { type fixedValue; value uniform 0; }\n";
                    } else {
                        for (const auto& p : patches) {
                            if (p.type == "symmetry" || p.type == "symmetryPlane" || p.type == "empty")
                                o << "    " << p.name << " { type " << p.type << "; }\n";
                            else if (p.name == "outlet")
                                o << "    outlet { type fixedValue; value uniform 0; }\n";
                            else
                                o << "    " << p.name << " { type zeroGradient; }\n";
                        }
                    }
                    o << "}\n";
                    appendLine("[Run] Restored 0/p to kinematic pressure for " + solver + ".");
                }

                // Restore fvSchemes to incompressible if they contain compressible stress term
                QString schPath = caseDir + "/system/fvSchemes";
                {
                    QFile sf(schPath);
                    if (sf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QString st = sf.readAll();
                        sf.close();
                        if (st.contains("rho*nuEff")) {
                            const bool transient = solver.contains("pimple", Qt::CaseInsensitive)
                                                || solver.contains("piso",   Qt::CaseInsensitive);
                            QFile::remove(schPath);
                            QFile ws(schPath);
                            if (ws.open(QIODevice::WriteOnly | QIODevice::Text)) {
                                QTextStream o(&ws);
                                o << "FoamFile\n{ version 2.0; format ascii; class dictionary; object fvSchemes; }\n\n"
                                  << "ddtSchemes      { default " << (transient ? "backward" : "steadyState") << "; }\n\n"
                                  << "gradSchemes     { default Gauss linear; }\n\n"
                                  << "divSchemes\n{\n"
                                  << "    default          none;\n"
                                  << "    div(phi,U)       " << divUScheme << ";\n"
                                  << "    turbulence       bounded Gauss upwind;\n"
                                  << "    div(phi,k)       $turbulence;\n"
                                  << "    div(phi,omega)   $turbulence;\n"
                                  << "    div(phi,epsilon) $turbulence;\n"
                                  << "    div(phi,nuTilda) $turbulence;\n"
                                  << "    div((nuEff*dev2(T(grad(U))))) Gauss linear;\n"
                                  << "}\n\n"
                                  << "laplacianSchemes  { default Gauss linear corrected; }\n\n"
                                  << "interpolationSchemes { default linear; }\n\n"
                                  << "snGradSchemes     { default corrected; }\n\n"
                                  << "fluxRequired      { default no; p; }\n\n"
                                  << "wallDist          { method meshWave; }\n";
                                appendLine("[Run] Restored system/fvSchemes for " + solver + " (scheme: " + divUScheme + ").");
                            }
                        }
                    }
                }
            }
        }
    }

    // Read GPU flags that onApply() wrote to decomposeParDict
    bool useGpu = false;
    double vramFraction = 0.80;
    QFile dpd(caseDir + "/system/decomposeParDict");
    if (dpd.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString dpdTxt = dpd.readAll();
        dpd.close();
        useGpu = dpdTxt.contains("useGPUSolver      true");
        QRegularExpression vramRe(R"(gpuVramFraction\s+([\d.]+))");
        auto vm = vramRe.match(dpdTxt);
        if (vm.hasMatch()) vramFraction = vm.captured(1).toDouble();
    }

    m_output->clear();
    m_currentRunDir = RunLogger::startRun(caseDir, solver);
    appendLine(QString("[Run] Logging to %1").arg(QFileInfo(m_currentRunDir).fileName()));
    if (useGpu) appendLine(QString("[Run] GPU solver enabled (VRAM limit: %1%).").arg(qRound(vramFraction * 100)));
    m_runner->run(solver, caseDir, {}, useGpu, vramFraction);
}

void ConsolePanel::onStopClicked() {
    m_runner->stop();
    m_statusLabel->setText("Stopped");
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
}

void ConsolePanel::execCommand(const QString& cmd)
{
    if (cmd.isEmpty()) return;
    pushHistory(cmd);
    m_cmdEdit->clear();

    QString caseDir = m_casePathEdit->text().trimmed();
    appendLine("<span style=\'color:#6af;\'>$ " + cmd + "</span>");

    if (m_cmdProcess && m_cmdProcess->state() != QProcess::NotRunning) {
        appendLine(QString("[busy: previous command still running]"));
        return;
    }

    delete m_cmdProcess;
    m_cmdProcess = new QProcess(this);
    if (!caseDir.isEmpty()) m_cmdProcess->setWorkingDirectory(caseDir);
    m_cmdProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_cmdProcess, &QProcess::readyReadStandardOutput, this, [this](){
        QString out = m_cmdProcess->readAllStandardOutput();
        for (const QString& l : out.split("\n"))
            if (!l.trimmed().isEmpty()) appendLine(l);
    });
    connect(m_cmdProcess, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code){
        appendLine(code == 0 ? "[done]" :
                   "[exit " + QString::number(code) + "]");
    });

    QString full = "source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null && " + cmd;
    m_cmdProcess->start("bash", {"-c", full});
}

void ConsolePanel::pushHistory(const QString& cmd)
{
    if (!m_cmdHistory.isEmpty() && m_cmdHistory.last() == cmd) {
        m_historyIdx = m_cmdHistory.size();
        return;
    }
    m_cmdHistory.append(cmd);
    m_historyIdx = m_cmdHistory.size();
}

bool ConsolePanel::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_cmdEdit && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Up) {
            if (m_historyIdx > 0) {
                --m_historyIdx;
                m_cmdEdit->setText(m_cmdHistory.value(m_historyIdx));
            }
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            if (m_historyIdx < m_cmdHistory.size() - 1) {
                ++m_historyIdx;
                m_cmdEdit->setText(m_cmdHistory.value(m_historyIdx));
            } else {
                m_historyIdx = m_cmdHistory.size();
                m_cmdEdit->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

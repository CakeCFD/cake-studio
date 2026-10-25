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

#include "ViewportWidget.h"
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QJsonArray>
#include <QMessageBox>
#include <QFile>
#include <algorithm>

#include <vtkSTLReader.h>
#include <vtkOBJReader.h>
#include <vtkOpenFOAMReader.h>
#include <vtkPolyDataMapper.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkCamera.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkLookupTable.h>
#include <vtkCubeSource.h>
#include <vtkConeSource.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkCompositeDataGeometryFilter.h>
#include <vtkCompositeDataIterator.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPolyDataNormals.h>
#include <vtkCellDataToPointData.h>
#include <vtkArrayCalculator.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkTextProperty.h>
#include <vtkInformation.h>
#include <vtkStreamingDemandDrivenPipeline.h>
#include <vtkQuadricDecimation.h>
#include <vtkContourFilter.h>
#include <vtkStreamTracer.h>
#include <vtkTubeFilter.h>
#include <vtkLineSource.h>
#include <vtkPlaneSource.h>
#include <vtkAppendPolyData.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkPolyDataConnectivityFilter.h>

ViewportWidget::ViewportWidget(QWidget* parent, bool studioMode) : QWidget(parent) {
    setAcceptDrops(true);
    setupPipeline();
    if (studioMode) setupResultsToolbar();
    else            setupMeshToolbar();
}

static QString resolveDropUrl(const QUrl& url)
{
    QString p = url.toLocalFile();
    if (p.isEmpty()) p = url.path();
    static QRegularExpression driveRe("^([A-Za-z]):[/\\\\](.*)$");
    auto m = driveRe.match(p);
    if (m.hasMatch())
        return "/mnt/" + m.captured(1).toLower() + "/" + m.captured(2).replace('\\', '/');
    return p;
}

static bool isGeoUrl(const QUrl& u) {
    QString p = resolveDropUrl(u);
    return p.endsWith(".stl", Qt::CaseInsensitive) ||
           p.endsWith(".obj", Qt::CaseInsensitive);
}

void ViewportWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (isGeoUrl(u)) { e->acceptProposedAction(); return; }
}

void ViewportWidget::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls()) {
        QString p = resolveDropUrl(u);
        if (p.endsWith(".stl", Qt::CaseInsensitive) || p.endsWith(".obj", Qt::CaseInsensitive)) {
            if (!m_projectDir.isEmpty()) {
                QString destDir = m_projectDir + "/constant/triSurface";
                QDir().mkpath(destDir);
                QString dest = destDir + "/" + QFileInfo(p).fileName();
                if (dest != p) {
                    QFile::remove(dest);
                    QFile::copy(p, dest);
                }
                p = dest;
            }
            if (p.endsWith(".stl", Qt::CaseInsensitive)) loadSTL(p);
            else                                          loadOBJ(p);
        }
    }
    e->acceptProposedAction();
}

bool ViewportWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_vtkHost && m_vtkWidget && event->type() == QEvent::Resize) {
        const QSize host = m_vtkHost->size(), view = m_vtkWidget->size();
        if (view.width() <= 1 || !m_vtkHost->isVisible()
            || host.width() < view.width() || host.height() < view.height())
            m_vtkWidget->setGeometry(m_vtkHost->rect());
        else
            m_resizeTimer->start();
    }
    if (m_vtkWidget && (obj == m_vtkWidget || obj == m_vtkWidget->embeddedOpenGLWindow())) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            dragEnterEvent(static_cast<QDragEnterEvent*>(event));
            return true;
        }
        if (event->type() == QEvent::Drop) {
            dropEvent(static_cast<QDropEvent*>(event));
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ViewportWidget::setupPipeline() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_vtkHost = new QWidget(this);
    m_vtkHost->setObjectName("viewportHost");
    m_vtkHost->installEventFilter(this);
    layout->addWidget(m_vtkHost, 1);
    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(80);
    connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
        m_vtkWidget->setGeometry(m_vtkHost->rect());
    });

    m_vtkWidget = new QVTKOpenGLStereoWidget(m_vtkHost);
    m_vtkWidget->setAcceptDrops(true);
    m_vtkWidget->installEventFilter(this);
    m_vtkWidget->embeddedOpenGLWindow()->installEventFilter(this);

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    const QColor vbg = ThemeEngine::instance().colors().viewport;
    m_renderer->SetBackground(vbg.redF(), vbg.greenF(), vbg.blueF());
    renderWindow->AddRenderer(m_renderer);

    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    m_vtkWidget->interactor()->SetInteractorStyle(style);
    m_vtkWidget->interactor()->SetDesiredUpdateRate(10.0);
    m_vtkWidget->interactor()->SetStillUpdateRate(0.5);
    renderWindow->SetDesiredUpdateRate(10.0);
    renderWindow->SetMultiSamples(0);

    auto axes = vtkSmartPointer<vtkAxesActor>::New();
    m_axesWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_axesWidget->SetOrientationMarker(axes);
    m_axesWidget->SetInteractor(m_vtkWidget->interactor());
    m_axesWidget->SetViewport(0.0, 0.0, 0.22, 0.22);
    m_axesWidget->SetEnabled(1);
    m_axesWidget->InteractiveOff();

    m_lut = vtkSmartPointer<vtkLookupTable>::New();
    m_lut->SetHueRange(0.667, 0.0);
    m_lut->SetNumberOfTableValues(256);
    m_lut->Build();

    m_meshActor   = vtkSmartPointer<vtkActor>::New();
    m_resultActor = vtkSmartPointer<vtkActor>::New();
    m_domainActor = vtkSmartPointer<vtkActor>::New();
    m_isoActor    = vtkSmartPointer<vtkActor>::New();
    m_streamActor = vtkSmartPointer<vtkActor>::New();
    m_domainActor->GetProperty()->SetRepresentationToWireframe();
    m_domainActor->GetProperty()->SetColor(0.3, 0.3, 0.35);
    m_domainActor->GetProperty()->SetOpacity(0.25);
    m_domainActor->SetVisibility(0);
    m_isoActor->SetVisibility(0);
    m_streamActor->SetVisibility(0);
    m_renderer->AddActor(m_meshActor);
    m_renderer->AddActor(m_resultActor);
    m_renderer->AddActor(m_domainActor);
    m_renderer->AddActor(m_isoActor);
    m_renderer->AddActor(m_streamActor);
    m_particles = std::make_unique<FlowParticles>();
    m_renderer->AddActor(m_particles->actor());
    m_particleTimer = new QTimer(this);
    m_particleTimer->setInterval(33);
    connect(m_particleTimer, &QTimer::timeout, this, [this]() {
        double dt = std::min(m_particleClock.restart() / 1000.0, 0.1);
        if (!isVisible() || m_particles->empty()) return;
        m_particles->advance(dt);
        m_vtkWidget->renderWindow()->Render();
    });

    m_domainCubeSource = vtkSmartPointer<vtkCubeSource>::New();
    auto cubeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    cubeMapper->SetInputConnection(m_domainCubeSource->GetOutputPort());
    m_domainPreviewActor = vtkSmartPointer<vtkActor>::New();
    m_domainPreviewActor->SetMapper(cubeMapper);
    m_domainPreviewActor->GetProperty()->SetRepresentationToWireframe();
    m_domainPreviewActor->GetProperty()->SetColor(0.2, 0.8, 1.0);
    m_domainPreviewActor->GetProperty()->SetLineWidth(1.5);
    m_domainPreviewActor->GetProperty()->SetOpacity(0.7);
    m_domainPreviewActor->SetVisibility(0);
    m_renderer->AddActor(m_domainPreviewActor);

    m_flowConeSource = vtkSmartPointer<vtkConeSource>::New();
    m_flowConeSource->SetResolution(16);
    auto coneMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    coneMapper->SetInputConnection(m_flowConeSource->GetOutputPort());
    m_flowConeActor = vtkSmartPointer<vtkActor>::New();
    m_flowConeActor->SetMapper(coneMapper);
    m_flowConeActor->GetProperty()->SetColor(0.2, 1.0, 0.4);
    m_flowConeActor->GetProperty()->SetOpacity(0.85);
    m_flowConeActor->SetVisibility(0);
    m_renderer->AddActor(m_flowConeActor);
}

void ViewportWidget::setupResultsToolbar() {
    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());

    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("viewportControlBar");
    auto* barLayout = new QHBoxLayout(m_controlBar);
    barLayout->setContentsMargins(8, 3, 8, 3);
    barLayout->setSpacing(6);

    barLayout->addWidget(new QLabel("Field:"));
    m_fieldCombo = new QComboBox;
    m_fieldCombo->setMinimumWidth(140);
    m_fieldCombo->setEnabled(false);
    barLayout->addWidget(m_fieldCombo);

    barLayout->addWidget(new QLabel("Step:"));
    m_timeSlider = new QSlider(Qt::Horizontal);
    m_timeSlider->setMinimum(0);
    m_timeSlider->setMaximum(1000);
    m_timeSlider->setSingleStep(5);
    m_timeSlider->setPageStep(50);
    m_timeSlider->setEnabled(false);
    m_timeSlider->setFixedWidth(180);
    barLayout->addWidget(m_timeSlider);

    m_timeLabel = new QLabel("-");
    m_timeLabel->setFixedWidth(40);
    barLayout->addWidget(m_timeLabel);

    m_btnHideMesh = new QPushButton("Hide Mesh");
    m_btnHideMesh->setCheckable(true);
    m_btnHideMesh->setFixedWidth(80);
    m_btnHideMesh->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 6px; }");
    ThemeEngine::setRole(m_btnHideMesh, "toggle");
    barLayout->addWidget(m_btnHideMesh);
    connect(m_btnHideMesh, &QPushButton::toggled, this, [this](bool hidden) {
        m_meshActor->SetVisibility(hidden ? 0 : 1);
        m_btnHideMesh->setText(hidden ? "Show Mesh" : "Hide Mesh");
        m_vtkWidget->renderWindow()->Render();
    });

    m_btnEdges = new QPushButton("Edges");
    m_btnEdges->setCheckable(true);
    m_btnEdges->setChecked(false);
    m_btnEdges->setFixedWidth(52);
    m_btnEdges->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    ThemeEngine::setRole(m_btnEdges, "toggle");
    barLayout->addWidget(m_btnEdges);
    connect(m_btnEdges, &QPushButton::toggled, this, [this](bool on) {
        m_meshActor->GetProperty()->SetEdgeVisibility(on ? 1 : 0);
        m_meshActor->GetProperty()->SetEdgeColor(0.2, 0.2, 0.3);
        m_meshActor->GetProperty()->SetLineWidth(0.5);
        m_vtkWidget->renderWindow()->Render();
    });

    auto* btnCells = new QPushButton("Cells");
    btnCells->setCheckable(true);
    btnCells->setFixedWidth(48);
    btnCells->setToolTip("Show CFD mesh cells on wall surface");
    btnCells->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    ThemeEngine::setRole(btnCells, "toggle");
    barLayout->addWidget(btnCells);
    connect(btnCells, &QPushButton::toggled, this, [this](bool on) {
        m_resultActor->GetProperty()->SetEdgeVisibility(on ? 1 : 0);
        m_resultActor->GetProperty()->SetEdgeColor(1.0, 1.0, 1.0);
        m_resultActor->GetProperty()->SetLineWidth(0.4);
        m_vtkWidget->renderWindow()->Render();
    });

    m_btnIso = new QPushButton("Iso");
    m_btnIso->setCheckable(true);
    m_btnIso->setFixedWidth(48);
    m_btnIso->setToolTip("Toggle iso-surface on current scalar field");
    m_btnIso->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    ThemeEngine::setRole(m_btnIso, "toggle");
    barLayout->addWidget(m_btnIso);
    connect(m_btnIso, &QPushButton::toggled, this, &ViewportWidget::toggleIsoSurface);

    m_btnStream = new QPushButton("Flow");
    m_btnStream->setCheckable(true);
    m_btnStream->setFixedWidth(48);
    m_btnStream->setToolTip("Toggle streamlines on velocity field");
    m_btnStream->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    ThemeEngine::setRole(m_btnStream, "toggle");
    barLayout->addWidget(m_btnStream);
    connect(m_btnStream, &QPushButton::toggled, this, &ViewportWidget::toggleStreamlines);

    auto* btnStreamCfg = new QPushButton("⚙");
    btnStreamCfg->setFixedWidth(22);
    btnStreamCfg->setToolTip("Streamline and particle settings");
    btnStreamCfg->setStyleSheet("QPushButton { font-size: 11px; padding: 1px 2px; }");
    barLayout->addWidget(btnStreamCfg);
    connect(btnStreamCfg, &QPushButton::clicked, this, &ViewportWidget::showStreamSettings);

    m_btnParticles = new QPushButton("Particles");
    m_btnParticles->setCheckable(true);
    m_btnParticles->setFixedWidth(64);
    m_btnParticles->setToolTip("Animate particles moving with the flow");
    m_btnParticles->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    ThemeEngine::setRole(m_btnParticles, "toggle");
    barLayout->addWidget(m_btnParticles);
    connect(m_btnParticles, &QPushButton::toggled, this, &ViewportWidget::toggleParticles);

    barLayout->addStretch();

    m_fieldLabel = new QLabel("-");
    m_fieldLabel->setStyleSheet("font-size: 11px;");
    barLayout->addWidget(m_fieldLabel);

    m_colorBar = new ColorBarWidget(m_controlBar);
    barLayout->addWidget(m_colorBar);
    m_colorBar->setVisible(false);

    connect(m_fieldCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ViewportWidget::onFieldChanged);
    connect(m_timeSlider, &QSlider::sliderReleased, this, [this](){
        onTimeStepChanged(m_timeSlider->value());
    });
    connect(m_timeSlider, &QSlider::valueChanged, this, [this](int v){
        if (!m_timeSlider->isSliderDown()) onTimeStepChanged(v);
    });

    layout->insertWidget(0, m_controlBar);
}

void ViewportWidget::setupMeshToolbar()
{
    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
    auto* bar    = new QWidget(this);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 3, 8, 3);
    barLayout->setSpacing(6);

    m_btnEdges = new QPushButton("Edges");
    m_btnEdges->setCheckable(true);
    m_btnEdges->setChecked(false);
    m_btnEdges->setFixedWidth(52);
    m_btnEdges->setToolTip("Toggle wireframe edge overlay (expensive on dense meshes)");
    m_btnEdges->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    ThemeEngine::setRole(m_btnEdges, "toggle");
    connect(m_btnEdges, &QPushButton::toggled, this, [this](bool on) {
        m_meshActor->GetProperty()->SetEdgeVisibility(on ? 1 : 0);
        m_meshActor->GetProperty()->SetEdgeColor(0.2, 0.2, 0.3);
        m_meshActor->GetProperty()->SetLineWidth(0.5);
        m_vtkWidget->renderWindow()->Render();
    });

    barLayout->addStretch();
    barLayout->addWidget(m_btnEdges);
    layout->insertWidget(0, bar);
}

void ViewportWidget::clearMeshData()
{
    m_meshActor->VisibilityOff();
    m_resultActor->VisibilityOff();
    m_stlPath.clear();
    m_timeSteps.clear();
    m_availableFields.clear();
    if (m_fieldCombo)  m_fieldCombo->clear();
    if (m_timeSlider)  { m_timeSlider->setRange(0, 0); m_timeSlider->setValue(0); }
    if (m_timeLabel)   m_timeLabel->setText("-");
    if (m_btnHideMesh) { m_btnHideMesh->setChecked(false); m_btnHideMesh->setText("Hide Mesh"); }
    if (m_btnIso)    { m_btnIso->setChecked(false); }
    if (m_btnStream) { m_btnStream->setChecked(false); }
    stopParticles();
    m_isoActor->VisibilityOff();
    m_streamActor->VisibilityOff();
    m_lastDataSet = nullptr;
    m_vtkWidget->renderWindow()->Render();

    if (!m_projectDir.isEmpty()) {
        QDir triSurf(m_projectDir + "/constant/triSurface");
        if (triSurf.exists()) {
            for (const QString& f : triSurf.entryList({"*.stl","*.STL","*.obj","*.OBJ"}, QDir::Files))
                triSurf.remove(f);
        }
    }
}

void ViewportWidget::toggleIsoSurface(bool on)
{
    m_isoActor->SetVisibility(on ? 1 : 0);

    if (on && !m_volumeDataSet && !m_caseDir.isEmpty() && !m_timeSteps.isEmpty()) {
        QString foamFile = m_caseDir + "/case.foam";
        auto volReader = vtkSmartPointer<vtkOpenFOAMReader>::New();
        volReader->SetFileName(foamFile.toStdString().c_str());
        volReader->DecomposePolyhedraOn();
        volReader->UpdateInformation();
        volReader->EnableAllPatchArrays();
        auto* volInfo = volReader->GetOutputInformation(0);
        if (m_lastTimeStepIndex < m_timeSteps.size())
            volInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(),
                         m_timeSteps[m_lastTimeStepIndex].toDouble());
        volReader->Update();
        auto* mbd = vtkMultiBlockDataSet::SafeDownCast(volReader->GetOutput());
        if (mbd) {
            vtkIdType bestCells = 0;
            auto iter = vtkSmartPointer<vtkCompositeDataIterator>::Take(mbd->NewIterator());
            if (iter) {
                iter->SkipEmptyNodesOn();
                for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem()) {
                    auto* ug = vtkUnstructuredGrid::SafeDownCast(iter->GetCurrentDataObject());
                    if (ug && ug->GetNumberOfCells() > bestCells) {
                        bestCells = ug->GetNumberOfCells();
                        auto copy = vtkSmartPointer<vtkUnstructuredGrid>::New();
                        copy->ShallowCopy(ug);
                        m_volumeDataSet = copy;
                    }
                }
            }
        }
        if (!m_volumeDataSet) {
            qWarning() << "IsoSurface: could not extract internal mesh from" << foamFile;
            m_isoActor->SetVisibility(0);
            m_vtkWidget->renderWindow()->Render();
            return;
        }
    }

    if (on && m_volumeDataSet) {
        QString field = m_fieldCombo ? m_fieldCombo->currentText() : QString();

        auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
        c2p->SetInputDataObject(m_volumeDataSet);
        c2p->PassCellDataOn();
        c2p->Update();

        vtkSmartPointer<vtkDataSet> contourInput;
        QString scalarField = field;
        if (field == "|U|") {
            auto calc = vtkSmartPointer<vtkArrayCalculator>::New();
            calc->SetInputConnection(c2p->GetOutputPort());
            calc->AddVectorArrayName("U");
            calc->SetFunction("mag(U)");
            calc->SetResultArrayName("|U|");
            calc->Update();
            contourInput = vtkDataSet::SafeDownCast(calc->GetOutput());
        } else {
            contourInput = vtkDataSet::SafeDownCast(c2p->GetOutput());
        }

        if (!contourInput) { m_isoActor->SetVisibility(0); m_vtkWidget->renderWindow()->Render(); return; }

        vtkDataArray* arr = contourInput->GetPointData()->GetArray(scalarField.toStdString().c_str());
        if (!arr) arr = contourInput->GetPointData()->GetScalars();
        if (!arr) { m_isoActor->SetVisibility(0); m_vtkWidget->renderWindow()->Render(); return; }

        double range[2];
        arr->GetRange(range);
        if (range[0] >= range[1] - 1e-10) { m_isoActor->SetVisibility(0); m_vtkWidget->renderWindow()->Render(); return; }

        double isoVal = range[0] + (range[1] - range[0]) * 0.3;

        auto contour = vtkSmartPointer<vtkContourFilter>::New();
        contour->SetInputDataObject(contourInput);
        contour->SetValue(0, isoVal);
        contour->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(contour->GetOutputPort());
        mapper->SetLookupTable(m_lut);
        mapper->SetScalarRange(range[0], range[1]);
        m_isoActor->SetMapper(mapper);
        m_isoActor->GetProperty()->SetOpacity(0.6);
    }
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::toggleStreamlines(bool on)
{
    if (m_flowRenderingBlocked) {
        m_streamActor->VisibilityOff();
        m_vtkWidget->renderWindow()->Render();
        return;
    }

    if (on) {
        auto emptyMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        emptyMapper->SetInputData(vtkSmartPointer<vtkPolyData>::New());
        m_streamActor->SetMapper(emptyMapper);
        m_streamPaths = nullptr;
    }
    m_streamActor->SetVisibility(on && m_btnStream && m_btnStream->isChecked() ? 1 : 0);
    if (on && m_caseDir.isEmpty()) {
        m_streamActor->SetVisibility(0);
        m_vtkWidget->renderWindow()->Render();
        return;
    }
    if (on && !m_volumeDataSet && !m_caseDir.isEmpty() && !m_timeSteps.isEmpty()) {
        QString foamFile = m_caseDir + "/case.foam";
        auto volReader = vtkSmartPointer<vtkOpenFOAMReader>::New();
        volReader->SetFileName(foamFile.toStdString().c_str());
        volReader->DecomposePolyhedraOn();
        volReader->UpdateInformation();
        volReader->EnableAllPatchArrays();
        auto* volInfo = volReader->GetOutputInformation(0);
        if (m_lastTimeStepIndex < m_timeSteps.size()) {
            volInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(),
                         m_timeSteps[m_lastTimeStepIndex].toDouble());
        }
        volReader->Update();
        auto* mbd = vtkMultiBlockDataSet::SafeDownCast(volReader->GetOutput());
        if (mbd) {
            vtkIdType bestCells = 0;
            auto iter = vtkSmartPointer<vtkCompositeDataIterator>::Take(mbd->NewIterator());
            if (iter) {
                iter->SkipEmptyNodesOn();
                for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem()) {
                    auto* ug2 = vtkUnstructuredGrid::SafeDownCast(iter->GetCurrentDataObject());
                    if (ug2 && ug2->GetNumberOfCells() > bestCells) {
                        bestCells = ug2->GetNumberOfCells();
                        auto copy = vtkSmartPointer<vtkUnstructuredGrid>::New();
                        copy->ShallowCopy(ug2);
                        m_volumeDataSet = copy;
                    }
                }
            }
        }
        if (!m_volumeDataSet) {
            qWarning() << "Streamlines: could not extract internal mesh from" << foamFile;
            m_streamActor->SetVisibility(0);
            m_vtkWidget->renderWindow()->Render();
            return;
        }
    }
    if (on && m_volumeDataSet) {
        if (!m_streamPointCache) {
            auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
            c2p->SetInputDataObject(m_volumeDataSet);
            c2p->PassCellDataOff();
            c2p->Update();
            auto cached = vtkSmartPointer<vtkUnstructuredGrid>::New();
            cached->ShallowCopy(c2p->GetOutput());
            m_streamPointCache = cached;
        }
        vtkSmartPointer<vtkDataSet> pd = m_streamPointCache;

        vtkDataArray* velArr = nullptr;
        const char* velName  = nullptr;
        for (const char* name : {"U", "velocity", "Velocity", "u"}) {
            if (pd->GetPointData())
                velArr = pd->GetPointData()->GetArray(name);
            if (!velArr && m_volumeDataSet && m_volumeDataSet->GetCellData())
                velArr = m_volumeDataSet->GetCellData()->GetArray(name);
            if (velArr && velArr->GetNumberOfComponents() == 3) { velName = name; break; }
            velArr = nullptr;
        }
        if (!velArr) {
            auto* pd2 = pd->GetPointData();
            for (int i = 0; pd2 && i < pd2->GetNumberOfArrays(); ++i) {
                auto* a = pd2->GetArray(i);
                if (a && a->GetNumberOfComponents() == 3) { velArr = a; velName = a->GetName(); break; }
            }
        }
        if (!velArr) {
            qWarning() << "Streamlines: no 3-component velocity array found in" << m_caseDir;
            m_streamActor->SetVisibility(0);
            m_vtkWidget->renderWindow()->Render();
            return;
        }

        {
            double vRange[2];
            velArr->GetRange(vRange, -1);
            if (vRange[1] > 1e4) {
                qWarning() << "Streamlines: velocity field diverged (max |U| =" << vRange[1]
                           << "). Refusing to trace streamlines into garbage data.";
                m_streamActor->SetVisibility(0);
                m_vtkWidget->renderWindow()->Render();
                emit streamlinesDiverged();
                return;
            }
        }

        pd->GetPointData()->SetActiveVectors(velName);

        double bounds[6];
        pd->GetBounds(bounds);
        double domLen = bounds[1] - bounds[0];

        double surf[6] = {bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]};
        if (m_lastDataSet) m_lastDataSet->GetBounds(surf);

        double aircraftH = surf[3] - surf[2];
        double aircraftW = surf[5] - surf[4];
        double padY = aircraftH * m_streamProximity;
        double padZ = aircraftW * m_streamProximity;

        double meanUx = 0.0;
        {
            vtkIdType n = velArr->GetNumberOfTuples();
            vtkIdType stride = qMax((vtkIdType)1, n / 5000);
            vtkIdType cnt = 0;
            for (vtkIdType i = 0; i < n; i += stride) {
                meanUx += velArr->GetComponent(i, 0);
                ++cnt;
            }
            if (cnt) meanUx /= cnt;
        }
        double uRef = (m_inletSpeed > 1e-6) ? m_inletSpeed : std::fabs(meanUx);
        double sX = (meanUx < -0.05 * uRef)
            ? bounds[1] - domLen * 0.04
            : bounds[0] + domLen * 0.04;
        double sY0 = surf[2] - padY;
        double sY1 = surf[3] + padY;
        double sZ0 = surf[4] - padZ;
        double sZ1 = surf[5] + padZ;

        sY0 = qMax(sY0, bounds[2]);
        sY1 = qMin(sY1, bounds[3]);
        sZ0 = qMax(sZ0, bounds[4]);
        sZ1 = qMin(sZ1, bounds[5]);

        int res = qMax(2, (int)std::sqrt((double)m_streamDensity));

        auto planeUp = vtkSmartPointer<vtkPlaneSource>::New();
        planeUp->SetOrigin(sX, sY0, sZ0);
        planeUp->SetPoint1(sX, sY1, sZ0);
        planeUp->SetPoint2(sX, sY0, sZ1);
        planeUp->SetResolution(res, res);
        planeUp->Update();

        double zCenter = 0.5 * (surf[4] + surf[5]);
        auto lineCenter = vtkSmartPointer<vtkLineSource>::New();
        lineCenter->SetPoint1(sX, sY0, zCenter);
        lineCenter->SetPoint2(sX, sY1, zCenter);
        lineCenter->SetResolution(res * 2);
        lineCenter->Update();

        auto allSeeds = vtkSmartPointer<vtkAppendPolyData>::New();
        allSeeds->AddInputData(planeUp->GetOutput());
        allSeeds->AddInputData(lineCenter->GetOutput());
        allSeeds->Update();

        auto tracer = vtkSmartPointer<vtkStreamTracer>::New();
        tracer->SetInputDataObject(pd);
        tracer->SetSourceData(allSeeds->GetOutput());
        double domDiag = std::sqrt(
            std::pow(bounds[1]-bounds[0], 2) +
            std::pow(bounds[3]-bounds[2], 2) +
            std::pow(bounds[5]-bounds[4], 2));
        tracer->SetMaximumPropagation(domLen * 1.1);
        tracer->SetInitialIntegrationStep(domDiag * 1e-4);
        tracer->SetMaximumIntegrationStep(domDiag * 2e-3);
        tracer->SetIntegrationDirectionToForward();
        tracer->SetIntegratorTypeToRungeKutta45();
        tracer->SetMaximumError(1e-5);
        tracer->SetMaximumNumberOfSteps(50000);
        tracer->SetTerminalSpeed(m_inletSpeed > 1e-6 ? m_inletSpeed * 1e-4 : 1e-6);

        double velRange[2] = {0.0, 1.0};
        velArr->GetRange(velRange, -1);
        {
            double uInf = m_inletSpeed;
            if (uInf < 1e-6) {
                for (const QString& uPath : {m_caseDir + "/0/U",
                                              m_caseDir + "/0.orig/U",
                                              m_caseDir + "/0.org/U"}) {
                    QFile uFile(uPath);
                    if (!uFile.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                    QString txt = uFile.readAll();
                    QRegularExpression re(R"(uniform\s*\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
                    auto it2 = re.globalMatch(txt);
                    while (it2.hasNext()) {
                        auto mm = it2.next();
                        double ux = mm.captured(1).toDouble(), uy = mm.captured(2).toDouble(), uz = mm.captured(3).toDouble();
                        uInf = std::max(uInf, std::sqrt(ux*ux + uy*uy + uz*uz));
                    }
                    if (uInf > 1e-6) break;
                }
            }
            if (uInf > 1.0) velRange[1] = uInf * 1.05;
        }
        velRange[0] = 0.0;

        auto streamLut = vtkSmartPointer<vtkLookupTable>::New();
        streamLut->SetTableRange(velRange[0], velRange[1]);
        streamLut->SetHueRange(0.0, 0.667);
        streamLut->SetSaturationRange(1.0, 1.0);
        streamLut->SetValueRange(1.0, 1.0);
        streamLut->SetVectorModeToMagnitude();
        streamLut->Build();

        tracer->Update();

        auto tracerOut = vtkSmartPointer<vtkPolyData>::New();
        tracerOut->ShallowCopy(tracer->GetOutput());

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(tracerOut);
        mapper->SetLookupTable(streamLut);
        mapper->SetScalarRange(velRange[0], velRange[1]);
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray(velName);
        mapper->SetColorModeToMapScalars();

        m_streamActor->SetMapper(mapper);
        m_streamActor->GetProperty()->SetLineWidth(1.8f * static_cast<float>(m_streamTubeScale));
        m_streamActor->GetProperty()->SetOpacity(0.9);

        m_streamPaths   = tracerOut;
        m_streamLut     = streamLut;
        m_streamVelName = velName;
        if (m_btnParticles && m_btnParticles->isChecked()
            && !m_particles->setPaths(m_streamPaths, velName, m_streamLut))
            stopParticles();
    }
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::toggleParticles(bool on)
{
    if (!on) { stopParticles(); return; }
    if (!m_streamPaths) toggleStreamlines(true);
    if (!m_streamPaths || !m_particles->setPaths(m_streamPaths, m_streamVelName.c_str(), m_streamLut)) {
        stopParticles();
        return;
    }
    m_particles->actor()->SetVisibility(1);
    m_particleClock.start();
    m_particleTimer->start();
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::stopParticles()
{
    if (m_btnParticles) { QSignalBlocker b(m_btnParticles); m_btnParticles->setChecked(false); }
    if (m_particleTimer) m_particleTimer->stop();
    if (!m_particles) return;
    m_particles->actor()->SetVisibility(0);
    m_particles->clear();
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::loadSTL(const QString& path) {
    if (!m_stlPath.isEmpty() && QFileInfo(m_stlPath).canonicalFilePath() != QFileInfo(path).canonicalFilePath()) {
        auto btn = QMessageBox::question(this, "Load New Mesh",
            "Loading a new mesh will clear the current mesh and any loaded results.\n"
            "The aerodynamic domain will be kept. Continue?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
        clearMeshData();
    }
    m_stlPath = path;
    auto reader = vtkSmartPointer<vtkSTLReader>::New();
    reader->SetFileName(path.toStdString().c_str());
    reader->Update();

    vtkIdType nFaces = reader->GetOutput()->GetNumberOfPolys();
    vtkSmartPointer<vtkPolyData> displayMesh;
    if (nFaces > 50000) {
        double ratio = 50000.0 / nFaces;
        auto decimate = vtkSmartPointer<vtkQuadricDecimation>::New();
        decimate->SetInputConnection(reader->GetOutputPort());
        decimate->SetTargetReduction(1.0 - ratio);
        decimate->Update();
        displayMesh = decimate->GetOutput();
    } else {
        displayMesh = reader->GetOutput();
    }

    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(displayMesh);
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->SplittingOff();
    normals->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(normals->GetOutput());
    mapper->StaticOn();

    m_meshActor->SetMapper(mapper);
    m_meshActor->SetVisibility(1);
    m_meshActor->GetProperty()->SetColor(0.6, 0.75, 0.9);
    m_meshActor->GetProperty()->SetOpacity(0.85);
    m_meshActor->GetProperty()->EdgeVisibilityOff();
    m_meshActor->GetProperty()->BackfaceCullingOn();
    resetCamera();
}

void ViewportWidget::loadOBJ(const QString& path) {
    if (!m_stlPath.isEmpty() && QFileInfo(m_stlPath).canonicalFilePath() != QFileInfo(path).canonicalFilePath()) {
        auto btn = QMessageBox::question(this, "Load New Mesh",
            "Loading a new mesh will clear the current mesh and any loaded results.\n"
            "The aerodynamic domain will be kept. Continue?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
        clearMeshData();
    }
    auto reader = vtkSmartPointer<vtkOBJReader>::New();
    reader->SetFileName(path.toStdString().c_str());
    reader->Update();

    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(reader->GetOutput());
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->SplittingOff();
    normals->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(normals->GetOutput());
    mapper->StaticOn();

    m_meshActor->SetMapper(mapper);
    m_meshActor->SetVisibility(1);
    m_meshActor->GetProperty()->SetColor(0.6, 0.75, 0.9);
    m_meshActor->GetProperty()->EdgeVisibilityOff();
    m_meshActor->GetProperty()->BackfaceCullingOn();
    resetCamera();
}

void ViewportWidget::loadOFResults(const QString& caseDir)
{
    const bool caseChanged = (caseDir != m_caseDir);
    m_caseDir = caseDir;

    m_inletSpeed = 0.0;
    for (const QString& uPath : {caseDir + "/0/U",
                                  caseDir + "/0.orig/U",
                                  caseDir + "/0.org/U"}) {
        QFile uFile(uPath);
        if (!uFile.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString txt = uFile.readAll();
        static QRegularExpression reU(
            R"(uniform\s*\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
        auto it = reU.globalMatch(txt);
        while (it.hasNext()) {
            auto mm = it.next();
            double ux = mm.captured(1).toDouble();
            double uy = mm.captured(2).toDouble();
            double uz = mm.captured(3).toDouble();
            m_inletSpeed = std::max(m_inletSpeed, std::sqrt(ux*ux + uy*uy + uz*uz));
        }
        if (m_inletSpeed > 1e-6) break;
    }

    QDir dir(caseDir);
    QStringList timeSteps;
    static QRegularExpression numRe("^\\d+(\\.\\d+)?$");
    for (const QString& e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok; double v = e.toDouble(&ok);
        if (ok && v > 0) timeSteps << e;
    }
    std::sort(timeSteps.begin(), timeSteps.end(),
              [](const QString& a, const QString& b){ return a.toDouble() < b.toDouble(); });
    if (caseChanged || timeSteps.isEmpty()) {
        m_resultActor->VisibilityOff();
        m_isoActor->VisibilityOff();
        m_streamActor->VisibilityOff();
        m_lastDataSet      = nullptr;
        m_volumeDataSet    = nullptr;
        m_streamPointCache = nullptr;
        m_timeSteps.clear();
        m_availableFields.clear();
        if (m_fieldCombo)  { QSignalBlocker b(m_fieldCombo); m_fieldCombo->clear(); }
        if (m_timeSlider)  { QSignalBlocker b(m_timeSlider); m_timeSlider->setRange(0, 0); m_timeSlider->setEnabled(false); }
        if (m_timeLabel)   m_timeLabel->setText("-");
        if (m_btnIso)      { QSignalBlocker b(m_btnIso);    m_btnIso->setChecked(false); }
        if (m_btnStream)   { QSignalBlocker b(m_btnStream); m_btnStream->setChecked(false); }
        m_streamPaths = nullptr;
        stopParticles();
        if (m_colorBar)    m_colorBar->setVisible(false);
        if (m_fieldLabel)  m_fieldLabel->setText("-");
    }
    if (timeSteps.isEmpty()) {
        m_vtkWidget->renderWindow()->Render();
        return;
    }
    m_timeSteps = timeSteps;

    QString foamFile = caseDir + "/case.foam";
    {
        QFile f(foamFile);
        f.open(QIODevice::WriteOnly);
    }

    auto reader = vtkSmartPointer<vtkOpenFOAMReader>::New();
    reader->SetFileName(foamFile.toStdString().c_str());
    reader->ReadZonesOn();
    reader->DecomposePolyhedraOn();
    reader->Update();

    m_availableFields.clear();
    auto* mb = reader->GetOutput();
    if (mb && mb->GetNumberOfBlocks() > 0) {
        auto* block = mb->GetBlock(0);
        if (block) {
            auto* pd = block->GetFieldData();
            auto geom = vtkSmartPointer<vtkCompositeDataGeometryFilter>::New();
            geom->SetInputConnection(reader->GetOutputPort());
            geom->Update();
            auto* poly = geom->GetOutput();
            if (poly) {
                auto* pointData = poly->GetPointData();
                auto* cellData  = poly->GetCellData();
                for (int i = 0; i < pointData->GetNumberOfArrays(); ++i)
                    m_availableFields << QString::fromStdString(pointData->GetArrayName(i));
                for (int i = 0; i < cellData->GetNumberOfArrays(); ++i) {
                    QString n = QString::fromStdString(cellData->GetArrayName(i));
                    if (!m_availableFields.contains(n)) m_availableFields << n;
                }
            }
        }
    }

    if (m_availableFields.contains("U") && !m_availableFields.contains("|U|"))
        m_availableFields.prepend("|U|");
    m_fieldCombo->blockSignals(true);
    m_fieldCombo->clear();
    m_fieldCombo->addItems(m_availableFields);
    m_fieldCombo->setEnabled(!m_availableFields.isEmpty());
    m_fieldCombo->blockSignals(false);

    int lastIdx = m_timeSteps.size() - 1;
    m_timeSlider->blockSignals(true);
    m_timeSlider->setMinimum(0);
    m_timeSlider->setMaximum(lastIdx);
    m_timeSlider->setValue(lastIdx);
    m_timeSlider->setEnabled(lastIdx > 0);
    m_timeSlider->blockSignals(false);
    if (m_timeLabel) m_timeLabel->setText(m_timeSteps.last());

    m_volumeDataSet = nullptr;
    m_streamPointCache = nullptr;
    m_streamPaths = nullptr;
    applyField(m_fieldCombo->currentText(), m_timeSteps.size() - 1);
    resetCamera();
}

void ViewportWidget::applyField(const QString& field, int timeStep)
{
    if (m_caseDir.isEmpty() || m_timeSteps.isEmpty()) return;

    QString foamFile = m_caseDir + "/case.foam";

    auto reader = vtkSmartPointer<vtkOpenFOAMReader>::New();
    reader->SetFileName(foamFile.toStdString().c_str());
    reader->DecomposePolyhedraOn();
    reader->UpdateInformation();

    QStringList wallPatches, domainPatches;
    {
        QFile bf(m_caseDir + "/constant/polyMesh/boundary");
        bf.open(QIODevice::ReadOnly | QIODevice::Text);
        if (bf.isOpen()) {
            QString txt = bf.readAll();
            QRegularExpression re(R"((\w+)\s*\{\s*type\s+(\w+)\s*;)",
                                  QRegularExpression::MultilineOption);
            auto it = re.globalMatch(txt);
            while (it.hasNext()) {
                auto m = it.next();
                QString name = m.captured(1), type = m.captured(2);
                if (type == "wall") wallPatches << name;
                else if (name != "defaultFaces") domainPatches << name;
            }
            bf.close();
        }
    }

    reader->DisableAllPatchArrays();
    int nPatches = reader->GetNumberOfPatchArrays();
    auto isInternal = [](const QString& n) {
        return n.compare("internalMesh", Qt::CaseInsensitive) == 0;
    };

    int enabledWallCount = 0;
    if (!wallPatches.isEmpty()) {
        for (int i = 0; i < nPatches; ++i) {
            QString pname = QString::fromStdString(reader->GetPatchArrayName(i));
            if (isInternal(pname)) continue;
            QString bare = pname.startsWith("patch/") ? pname.mid(6) : pname;
            if (wallPatches.contains(bare) || wallPatches.contains(pname)) {
                reader->SetPatchArrayStatus(reader->GetPatchArrayName(i), 1);
                ++enabledWallCount;
            }
        }
    }

    if (enabledWallCount == 0) {
        for (int i = 0; i < nPatches; ++i) {
            QString pname = QString::fromStdString(reader->GetPatchArrayName(i));
            if (!isInternal(pname))
                reader->SetPatchArrayStatus(reader->GetPatchArrayName(i), 1);
        }
    }

    auto* info = reader->GetOutputInformation(0);
    if (timeStep < m_timeSteps.size()) {
        double t = m_timeSteps[timeStep].toDouble();
        info->Set(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(), t);
    }
    reader->Update();

    auto geom = vtkSmartPointer<vtkCompositeDataGeometryFilter>::New();
    geom->SetInputConnection(reader->GetOutputPort());
    geom->Update();

    auto* poly = geom->GetOutput();
    if (!poly || poly->GetNumberOfPoints() == 0) return;

    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetFeatureAngle(20.0);
    normals->SplittingOn();
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();

    vtkSmartPointer<vtkDataSetMapper> mapper = vtkSmartPointer<vtkDataSetMapper>::New();

    QString actualField = field;
    if (field == "|U|") {
        auto calc = vtkSmartPointer<vtkArrayCalculator>::New();
        calc->SetInputData(poly);
        calc->AddVectorArrayName("U");
        calc->SetFunction("mag(U)");
        calc->SetResultArrayName("|U|");
        calc->Update();
        auto* calcPoly = vtkPolyData::SafeDownCast(calc->GetOutput());
        normals->SetInputData(calcPoly ? calcPoly : poly);
    } else {
        normals->SetInputData(poly);
    }
    normals->Update();
    auto* normalsPoly = vtkPolyData::SafeDownCast(normals->GetOutput());
    mapper->SetInputData(normalsPoly ? normalsPoly : poly);

    mapper->SelectColorArray(actualField.toStdString().c_str());
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SetLookupTable(m_lut);
    mapper->ScalarVisibilityOn();

    double range[2] = {0, 1};
    auto* rangePoly = normalsPoly ? normalsPoly : poly;
    auto* arr = rangePoly->GetPointData()->GetArray(actualField.toStdString().c_str());
    if (!arr) arr = rangePoly->GetCellData()->GetArray(actualField.toStdString().c_str());
    if (!arr) arr = poly->GetPointData()->GetArray(actualField.toStdString().c_str());
    if (!arr) arr = poly->GetCellData()->GetArray(actualField.toStdString().c_str());
    if (arr) arr->GetRange(range);
    m_lut->SetTableRange(range[0], range[1]);
    mapper->SetScalarRange(range[0], range[1]);

    m_resultActor->SetMapper(mapper);
    m_resultActor->SetVisibility(1);   // loadOFResults() hides it on every case change
    m_resultActor->GetProperty()->SetOpacity(1.0);
    m_resultActor->GetProperty()->SetInterpolationToGouraud();
    m_meshActor->SetVisibility(0);

    m_lastDataSet = vtkSmartPointer<vtkPolyData>::New();
    vtkPolyData::SafeDownCast(m_lastDataSet)->ShallowCopy(poly);

    if (field == "|U|" && range[0] >= range[1] - 1e-10) {
        double uInf = m_inletSpeed;
        if (uInf < 1e-6) {
            for (const QString& uPath : {m_caseDir + "/0/U",
                                          m_caseDir + "/0.orig/U",
                                          m_caseDir + "/0.org/U"}) {
                QFile uFile(uPath);
                if (!uFile.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                QString txt = uFile.readAll();
                QRegularExpression re(R"(uniform\s*\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
                auto it = re.globalMatch(txt);
                while (it.hasNext()) {
                    auto mm = it.next();
                    double ux = mm.captured(1).toDouble();
                    double uy = mm.captured(2).toDouble();
                    double uz = mm.captured(3).toDouble();
                    uInf = std::max(uInf, std::sqrt(ux*ux + uy*uy + uz*uz));
                }
                if (uInf > 1e-6) break;
            }
        }
        if (uInf > 1e-6) {
            range[0] = 0.0;
            range[1] = uInf;
            m_lut->SetTableRange(range[0], range[1]);
            mapper->SetScalarRange(range[0], range[1]);
        }
    }

    if (timeStep != m_lastTimeStepIndex) {
        m_volumeDataSet = nullptr;
        m_streamPointCache = nullptr;
        m_streamPaths = nullptr;
    }
    m_lastTimeStepIndex = timeStep;

    if (m_btnIso    && m_btnIso->isChecked())    toggleIsoSurface(true);
    if (m_btnStream && m_btnStream->isChecked()) toggleStreamlines(true);
    else if (m_btnParticles && m_btnParticles->isChecked() && !m_streamPaths) toggleParticles(true);

    if (!domainPatches.isEmpty()) {
        auto reader2 = vtkSmartPointer<vtkOpenFOAMReader>::New();
        reader2->SetFileName(foamFile.toStdString().c_str());
        reader2->DecomposePolyhedraOn();
        reader2->UpdateInformation();
        reader2->DisableAllPatchArrays();
        for (const QString& p : domainPatches) {
            reader2->SetPatchArrayStatus(p.toStdString().c_str(), 1);
            reader2->SetPatchArrayStatus(("patch/" + p).toStdString().c_str(), 1);
        }
        auto* info2 = reader2->GetOutputInformation(0);
        if (timeStep < m_timeSteps.size())
            info2->Set(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(),
                       m_timeSteps[timeStep].toDouble());
        reader2->Update();
        auto geom2 = vtkSmartPointer<vtkCompositeDataGeometryFilter>::New();
        geom2->SetInputConnection(reader2->GetOutputPort());
        geom2->Update();
        auto mapper2 = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper2->SetInputData(geom2->GetOutput());
        mapper2->ScalarVisibilityOff();
        m_domainActor->SetMapper(mapper2);
        m_domainActor->SetVisibility(1);
    } else {
        m_domainActor->SetVisibility(0);
    }

    updateScalarBar(field, range[0], range[1]);
}

void ViewportWidget::updateScalarBar(const QString& label, double mn, double mx)
{
    if (m_colorBar) {
        m_colorBar->setRange(mn, mx, label);
        m_colorBar->setVisible(true);
    }
    if (m_fieldLabel) m_fieldLabel->setText(label);
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::showStreamSettings()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Streamline Settings");
    dlg->setFixedWidth(300);
    auto* form = new QFormLayout(dlg);
    form->setSpacing(10);
    form->setContentsMargins(14, 14, 14, 14);

    double origProx  = m_streamProximity;
    int    origDens  = m_streamDensity;
    double origScale = m_streamTubeScale;

    auto* proxSpin = new QDoubleSpinBox;
    proxSpin->setRange(0.0, 1.0);
    proxSpin->setSingleStep(0.05);
    proxSpin->setDecimals(2);
    proxSpin->setValue(m_streamProximity);
    proxSpin->setToolTip("0.05 = right next to surface, 0.5 = half bbox width away\n"
                         "Increase for wide-body / large aircraft");
    form->addRow("Proximity (0-1):", proxSpin);

    auto* densSpin = new QSpinBox;
    densSpin->setRange(1, 512);
    densSpin->setSingleStep(8);
    densSpin->setValue(m_streamDensity);
    densSpin->setToolTip("Total seed points: more = denser streamlines, slower");
    form->addRow("Density (lines):", densSpin);

    auto* scaleSpin = new QDoubleSpinBox;
    scaleSpin->setRange(0.1, 10.0);
    scaleSpin->setSingleStep(0.1);
    scaleSpin->setDecimals(1);
    scaleSpin->setValue(m_streamTubeScale);
    scaleSpin->setToolTip("1.0 = default  |  0.5 = thinner  |  3.0 = thicker");
    form->addRow("Tube radius scale:", scaleSpin);

    double origSpeed = m_particles->speed();
    auto* speedSpin = new QDoubleSpinBox;
    speedSpin->setRange(0.1, 5.0);
    speedSpin->setSingleStep(0.1);
    speedSpin->setDecimals(1);
    speedSpin->setValue(origSpeed);
    speedSpin->setToolTip("Particle animation speed: 1.0 = default");
    form->addRow("Particle speed:", speedSpin);
    connect(speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ m_particles->setSpeed(v); });

    connect(proxSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ m_streamProximity = v; });
    connect(densSpin,  QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ m_streamDensity = v; });
    connect(scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ m_streamTubeScale = v; });

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    int result = dlg->exec();
    dlg->deleteLater();

    if (result == QDialog::Accepted) {
        if (m_btnStream && m_btnStream->isChecked())
            toggleStreamlines(true);
        else if (m_btnParticles && m_btnParticles->isChecked()) {
            m_streamPaths = nullptr;
            toggleParticles(true);
        }
    } else {
        m_particles->setSpeed(origSpeed);
        m_streamProximity = origProx;
        m_streamDensity   = origDens;
        m_streamTubeScale = origScale;
    }
}

void ViewportWidget::setSliderMax(int endTime)
{
    if (!m_timeSlider) return;
    QSignalBlocker b(m_timeSlider);
    m_timeSlider->setMaximum(endTime);
    m_timeSlider->setValue(endTime);
    if (m_timeLabel) m_timeLabel->setText(QString::number(endTime));
}

void ViewportWidget::onFieldChanged(int)
{
    onTimeStepChanged(m_timeSlider->value());
}

void ViewportWidget::onTimeStepChanged(int sliderVal)
{
    if (m_timeSteps.isEmpty()) return;
    int idx = std::clamp(sliderVal, 0, (int)m_timeSteps.size() - 1);
    m_timeLabel->setText(m_timeSteps[idx]);
    applyField(m_fieldCombo->currentText(), idx);
}

void ViewportWidget::resetCamera() {
    double b[6];
    bool hasBounds = false;
    for (vtkActor* actor : {m_meshActor.Get(), m_resultActor.Get()}) {
        if (actor->GetVisibility() && actor->GetMapper() &&
            actor->GetMapper()->GetInput()) {
            actor->GetBounds(b);
            if (b[0] < b[1]) { m_renderer->ResetCamera(b); hasBounds = true; break; }
        }
    }
    if (!hasBounds) m_renderer->ResetCamera();
    m_vtkWidget->renderWindow()->Render();
}

QJsonObject ViewportWidget::getCameraState() const
{
    auto* cam = m_renderer->GetActiveCamera();
    double pos[3], fp[3], up[3];
    cam->GetPosition(pos);
    cam->GetFocalPoint(fp);
    cam->GetViewUp(up);
    return QJsonObject{
        {"position",    QJsonArray{pos[0], pos[1], pos[2]}},
        {"focal_point", QJsonArray{fp[0],  fp[1],  fp[2]}},
        {"view_up",     QJsonArray{up[0],  up[1],  up[2]}},
        {"parallel_scale", cam->GetParallelScale()},
    };
}

void ViewportWidget::setCameraState(const QJsonObject& obj)
{
    auto get = [&](const QString& key, int i) {
        return obj[key].toArray().at(i).toDouble();
    };
    auto* cam = m_renderer->GetActiveCamera();
    cam->SetPosition   (get("position",    0), get("position",    1), get("position",    2));
    cam->SetFocalPoint (get("focal_point", 0), get("focal_point", 1), get("focal_point", 2));
    cam->SetViewUp     (get("view_up",     0), get("view_up",     1), get("view_up",     2));
    if (obj.contains("parallel_scale"))
        cam->SetParallelScale(obj["parallel_scale"].toDouble());
    m_vtkWidget->renderWindow()->Render();
}

QString ViewportWidget::activeField() const
{
    return m_fieldCombo ? m_fieldCombo->currentText() : QString();
}

int ViewportWidget::activeTimeStep() const
{
    return m_timeSlider ? m_timeSlider->value() : 0;
}

void ViewportWidget::setActiveField(const QString& field)
{
    if (m_fieldCombo) m_fieldCombo->setCurrentText(field);
}

void ViewportWidget::setActiveTimeStep(int step)
{
    if (m_timeSlider) m_timeSlider->setValue(step);
}

void ViewportWidget::setFlowRenderingBlocked(bool blocked)
{
    m_flowRenderingBlocked = blocked;
    if (!blocked) {
        if (m_btnStream) m_btnStream->setEnabled(true);
        return;
    }

    // This is so that the user doesnt get annoyed at the cached streamlines.
    auto emptyStreamMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    emptyStreamMapper->SetInputData(vtkSmartPointer<vtkPolyData>::New());
    m_streamActor->SetMapper(emptyStreamMapper);
    m_streamActor->VisibilityOff();
    if (m_btnStream) {
        QSignalBlocker blocker(m_btnStream);
        m_btnStream->setChecked(false);
        m_btnStream->setEnabled(false);
    }
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::clearScene()
{
    m_meshActor->VisibilityOff();
    m_resultActor->VisibilityOff();
    m_isoActor->VisibilityOff();
    m_streamActor->VisibilityOff();
    m_domainActor->VisibilityOff();
    m_domainPreviewActor->VisibilityOff();
    m_flowConeActor->VisibilityOff();
    m_lastDataSet   = nullptr;
    m_volumeDataSet = nullptr;
    m_streamPointCache = nullptr;
    m_streamPaths   = nullptr;
    stopParticles();
    m_inletSpeed    = 0.0;
    m_caseDir.clear();
    m_stlPath.clear();
    m_timeSteps.clear();
    m_availableFields.clear();
    if (m_fieldCombo)  { m_fieldCombo->blockSignals(true); m_fieldCombo->clear(); m_fieldCombo->blockSignals(false); }
    if (m_timeSlider)  { m_timeSlider->setRange(0, 0); m_timeSlider->setValue(0); m_timeSlider->setEnabled(false); }
    if (m_timeLabel)   m_timeLabel->setText("-");
    if (m_btnHideMesh) { m_btnHideMesh->setChecked(false); m_btnHideMesh->setText("Hide Mesh"); }
    if (m_btnIso)      { QSignalBlocker b(m_btnIso);    m_btnIso->setChecked(false); }
    if (m_btnStream)   { QSignalBlocker b(m_btnStream); m_btnStream->setChecked(false); }
    if (m_colorBar)    m_colorBar->setVisible(false);
    if (m_fieldLabel)  m_fieldLabel->setText("-");
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::setMeshHidden(bool hidden)
{
    if (!m_btnHideMesh) return;
    m_btnHideMesh->setChecked(hidden);
    m_meshActor->SetVisibility(hidden ? 0 : 1);
    m_btnHideMesh->setText(hidden ? "Show Mesh" : "Hide Mesh");
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::loadAtStep(int stepIndex)
{
    if (!m_timeSteps.isEmpty() && stepIndex >= 0 && stepIndex < m_timeSteps.size())
        applyField(m_fieldCombo->currentText(), stepIndex);
}

void ViewportWidget::setBackgroundColor(double r, double g, double b) {
    m_renderer->SetBackground(r, g, b);
    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::setTheme(Theme t)
{
    const QColor bg = ThemeEngine::colorsFor(t).viewport;
    m_renderer->SetBackground(bg.redF(), bg.greenF(), bg.blueF());
    if (m_vtkHost)
        m_vtkHost->setStyleSheet(QString("QWidget#viewportHost { background-color: %1; }").arg(bg.name()));

    if (m_controlBar)
        m_controlBar->setStyleSheet(QString());

    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::updateDomainPreview(double x0, double y0, double z0,
                                          double x1, double y1, double z1,
                                          const QString& flowAxis)
{
    double cx = (x0+x1)/2, cy = (y0+y1)/2, cz = (z0+z1)/2;
    double lx = x1-x0, ly = y1-y0, lz = z1-z0;
    m_domainCubeSource->SetCenter(cx, cy, cz);
    m_domainCubeSource->SetXLength(lx);
    m_domainCubeSource->SetYLength(ly);
    m_domainCubeSource->SetZLength(lz);
    m_domainCubeSource->Update();
    m_domainPreviewActor->SetVisibility(1);

    double coneLen = std::min({lx, ly, lz}) * 0.18;
    double coneRad = coneLen * 0.35;
    m_flowConeSource->SetHeight(coneLen);
    m_flowConeSource->SetRadius(coneRad);

    m_flowConeActor->SetOrientation(0, 0, 0);
    if (flowAxis == "+X") {
        m_flowConeSource->SetDirection(1, 0, 0);
        m_flowConeActor->SetPosition(x0 + coneLen*0.5, cy, cz);
    } else if (flowAxis == "-X") {
        m_flowConeSource->SetDirection(-1, 0, 0);
        m_flowConeActor->SetPosition(x1 - coneLen*0.5, cy, cz);
    } else if (flowAxis == "+Y") {
        m_flowConeSource->SetDirection(0, 1, 0);
        m_flowConeActor->SetPosition(cx, y0 + coneLen*0.5, cz);
    } else if (flowAxis == "-Y") {
        m_flowConeSource->SetDirection(0, -1, 0);
        m_flowConeActor->SetPosition(cx, y1 - coneLen*0.5, cz);
    } else if (flowAxis == "+Z") {
        m_flowConeSource->SetDirection(0, 0, 1);
        m_flowConeActor->SetPosition(cx, cy, z0 + coneLen*0.5);
    } else {
        m_flowConeSource->SetDirection(0, 0, -1);
        m_flowConeActor->SetPosition(cx, cy, z1 - coneLen*0.5);
    }
    m_flowConeSource->Update();
    m_flowConeActor->SetVisibility(1);

    m_vtkWidget->renderWindow()->Render();
}

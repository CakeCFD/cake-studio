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

// Resolve drop URL to a Linux path. Windows Explorer drops arrive as C:/...;
// convert drive letter to the WSL /mnt/<drive>/ mount point.
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
            // Copy into project's constant/triSurface/ if we have a project dir
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

// The QVTKOpenGLNativeWidget child fills the entire visible area, so drag events
// land on it, not on this parent widget. Forward them here.
bool ViewportWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_vtkWidget) {
        if (event->type() == QEvent::DragEnter) {
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

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setAcceptDrops(true);
    m_vtkWidget->installEventFilter(this);
    layout->addWidget(m_vtkWidget, 1);

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.12, 0.12, 0.18);
    renderWindow->AddRenderer(m_renderer);

    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    m_vtkWidget->interactor()->SetInteractorStyle(style);
    m_vtkWidget->interactor()->SetDesiredUpdateRate(10.0);
    m_vtkWidget->interactor()->SetStillUpdateRate(0.5);
    renderWindow->SetDesiredUpdateRate(10.0);
    renderWindow->SetMultiSamples(0);  // disable MSAA: reduces framebuffer bandwidth on WSLg

    auto axes = vtkSmartPointer<vtkAxesActor>::New();
    m_axesWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_axesWidget->SetOrientationMarker(axes);
    m_axesWidget->SetInteractor(m_vtkWidget->interactor());
    m_axesWidget->SetViewport(0.0, 0.0, 0.22, 0.22);
    m_axesWidget->SetEnabled(1);
    m_axesWidget->InteractiveOff();

    // Lookup table: cool-to-warm (blue → red)
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

    // Live domain preview: wireframe box
    m_domainCubeSource = vtkSmartPointer<vtkCubeSource>::New();
    auto cubeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    cubeMapper->SetInputConnection(m_domainCubeSource->GetOutputPort());
    m_domainPreviewActor = vtkSmartPointer<vtkActor>::New();
    m_domainPreviewActor->SetMapper(cubeMapper);
    m_domainPreviewActor->GetProperty()->SetRepresentationToWireframe();
    m_domainPreviewActor->GetProperty()->SetColor(0.2, 0.8, 1.0);   // cyan
    m_domainPreviewActor->GetProperty()->SetLineWidth(1.5);
    m_domainPreviewActor->GetProperty()->SetOpacity(0.7);
    m_domainPreviewActor->SetVisibility(0);
    m_renderer->AddActor(m_domainPreviewActor);

    // Live domain preview: flow direction cone (green, points in flow direction)
    m_flowConeSource = vtkSmartPointer<vtkConeSource>::New();
    m_flowConeSource->SetResolution(16);
    auto coneMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    coneMapper->SetInputConnection(m_flowConeSource->GetOutputPort());
    m_flowConeActor = vtkSmartPointer<vtkActor>::New();
    m_flowConeActor->SetMapper(coneMapper);
    m_flowConeActor->GetProperty()->SetColor(0.2, 1.0, 0.4);        // green
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
    m_btnHideMesh->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 2px 6px; }"
        "QPushButton:checked { background: #585b70; color: #cdd6f4; }");
    barLayout->addWidget(m_btnHideMesh);
    connect(m_btnHideMesh, &QPushButton::toggled, this, [this](bool hidden) {
        m_meshActor->SetVisibility(hidden ? 0 : 1);
        m_btnHideMesh->setText(hidden ? "Show Mesh" : "Hide Mesh");
        m_vtkWidget->renderWindow()->Render();
    });

    // Edge visibility toggle: default off (expensive on dense meshes)
    m_btnEdges = new QPushButton("Edges");
    m_btnEdges->setCheckable(true);
    m_btnEdges->setChecked(false);
    m_btnEdges->setFixedWidth(52);
    m_btnEdges->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 2px 4px; }"
        "QPushButton:checked { background: #cba6f7; color: #1e1e2e; }");
    barLayout->addWidget(m_btnEdges);
    connect(m_btnEdges, &QPushButton::toggled, this, [this](bool on) {
        m_meshActor->GetProperty()->SetEdgeVisibility(on ? 1 : 0);
        m_meshActor->GetProperty()->SetEdgeColor(0.2, 0.2, 0.3);
        m_meshActor->GetProperty()->SetLineWidth(0.5);
        m_vtkWidget->renderWindow()->Render();
    });

    // CFD wall mesh wireframe overlay: shows actual computational cell faces on surface
    auto* btnCells = new QPushButton("Cells");
    btnCells->setCheckable(true);
    btnCells->setFixedWidth(48);
    btnCells->setToolTip("Show CFD mesh cells on wall surface");
    btnCells->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 2px 4px; }"
        "QPushButton:checked { background: #f38ba8; color: #1e1e2e; }");
    barLayout->addWidget(btnCells);
    connect(btnCells, &QPushButton::toggled, this, [this](bool on) {
        m_resultActor->GetProperty()->SetEdgeVisibility(on ? 1 : 0);
        m_resultActor->GetProperty()->SetEdgeColor(1.0, 1.0, 1.0);
        m_resultActor->GetProperty()->SetLineWidth(0.4);
        m_vtkWidget->renderWindow()->Render();
    });

    // Iso-surface toggle
    m_btnIso = new QPushButton("Iso");
    m_btnIso->setCheckable(true);
    m_btnIso->setFixedWidth(48);
    m_btnIso->setToolTip("Toggle iso-surface on current scalar field");
    m_btnIso->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 2px 4px; }"
        "QPushButton:checked { background: #89dceb; color: #1e1e2e; }");
    barLayout->addWidget(m_btnIso);
    connect(m_btnIso, &QPushButton::toggled, this, &ViewportWidget::toggleIsoSurface);

    // Streamlines toggle
    m_btnStream = new QPushButton("Flow");
    m_btnStream->setCheckable(true);
    m_btnStream->setFixedWidth(48);
    m_btnStream->setToolTip("Toggle streamlines on velocity field");
    m_btnStream->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 2px 4px; }"
        "QPushButton:checked { background: #a6e3a1; color: #1e1e2e; }");
    barLayout->addWidget(m_btnStream);
    connect(m_btnStream, &QPushButton::toggled, this, &ViewportWidget::toggleStreamlines);

    auto* btnStreamCfg = new QPushButton("⚙");
    btnStreamCfg->setFixedWidth(22);
    btnStreamCfg->setToolTip("Streamline settings: proximity, density");
    btnStreamCfg->setStyleSheet("QPushButton { font-size: 11px; padding: 1px 2px; }");
    barLayout->addWidget(btnStreamCfg);
    connect(btnStreamCfg, &QPushButton::clicked, this, &ViewportWidget::showStreamSettings);

    barLayout->addStretch();

    // Compact Qt colorbar -- field name + gradient strip + min/max
    m_fieldLabel = new QLabel("-");
    m_fieldLabel->setStyleSheet("font-size: 11px;");
    barLayout->addWidget(m_fieldLabel);

    m_colorBar = new ColorBarWidget(m_controlBar);
    barLayout->addWidget(m_colorBar);
    m_colorBar->setVisible(false);

    connect(m_fieldCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ViewportWidget::onFieldChanged);
    // Only re-render on release: avoids triggering the full VTK pipeline on
    // every drag tick (which causes multi-second freezes on large meshes).
    connect(m_timeSlider, &QSlider::sliderReleased, this, [this](){
        onTimeStepChanged(m_timeSlider->value());
    });
    // Keyboard / programmatic changes still need to respond
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
    m_btnEdges->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 2px 4px; }"
        "QPushButton:checked { background: #cba6f7; color: #1e1e2e; }");
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
    m_isoActor->VisibilityOff();
    m_streamActor->VisibilityOff();
    m_lastDataSet = nullptr;
    m_vtkWidget->renderWindow()->Render();

    // Wipe existing geometry files so only one mesh lives in the project at a time
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

    // Lazy-load the volumetric mesh: same logic as toggleStreamlines.
    // Without this, clicking Iso before Flow always sees m_volumeDataSet == null.
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

        // OF data is cell-centred: convert to point data for vtkContourFilter
        auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
        c2p->SetInputDataObject(m_volumeDataSet);
        c2p->PassCellDataOn();
        c2p->Update();

        // For |U|, compute magnitude on the volume before contouring
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

        // Iso at 30% of range: for pressure this captures the suction surface,
        // for |U| it captures the boundary layer edge.
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
    // Always clear the actor's geometry first so stale data from a previous run
    // never bleeds through if the new run fails to build streamlines.
    if (on) {
        auto emptyMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        emptyMapper->SetInputData(vtkSmartPointer<vtkPolyData>::New());
        m_streamActor->SetMapper(emptyMapper);
    }
    m_streamActor->SetVisibility(on ? 1 : 0);
    if (on && m_caseDir.isEmpty()) {
        m_streamActor->SetVisibility(0);
        m_vtkWidget->renderWindow()->Render();
        return;
    }
    // Lazy-load the volumetric internal mesh: only done here, not in applyField,
    // because loading millions of cells on every field/step change kills performance.
    if (on && !m_volumeDataSet && !m_caseDir.isEmpty() && !m_timeSteps.isEmpty()) {
        QString foamFile = m_caseDir + "/case.foam";
        auto volReader = vtkSmartPointer<vtkOpenFOAMReader>::New();
        volReader->SetFileName(foamFile.toStdString().c_str());
        volReader->DecomposePolyhedraOn();
        volReader->UpdateInformation();
        // Enable ALL patches so we definitely get the internal mesh regardless of
        // how vtkOpenFOAMReader names it across VTK versions ("internalMesh" vs other).
        volReader->EnableAllPatchArrays();
        auto* volInfo = volReader->GetOutputInformation(0);
        if (m_lastTimeStepIndex < m_timeSteps.size()) {
            volInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(),
                         m_timeSteps[m_lastTimeStepIndex].toDouble());
        }
        volReader->Update();
        auto* mbd = vtkMultiBlockDataSet::SafeDownCast(volReader->GetOutput());
        if (mbd) {
            // Walk all blocks and pick the UnstructuredGrid with the most cells :
            // the internal mesh is always the largest, regardless of block index or name.
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
        // OF fields are cell-centred: convert to point data for vtkStreamTracer.
        // Cached - this is the single most expensive step and the result only
        // changes when m_volumeDataSet is reloaded.
        if (!m_streamPointCache) {
            auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
            c2p->SetInputDataObject(m_volumeDataSet);
            c2p->PassCellDataOff();   // tracer needs point data only
            c2p->Update();
            auto cached = vtkSmartPointer<vtkUnstructuredGrid>::New();
            cached->ShallowCopy(c2p->GetOutput());
            m_streamPointCache = cached;
        }
        vtkSmartPointer<vtkDataSet> pd = m_streamPointCache;

        // Find velocity array: check point data first (after c2p), then cell data fallback
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
            // Last resort: any 3-component array in the dataset
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

        // Divergence guard: if the velocity magnitude exceeds a physical limit
        // (incompressible RANS: Mach ~0.3 max ≈ 100 m/s; give 10× headroom)
        // the run has blown up. Refuse to trace streamlines into garbage data.
        {
            double vRange[2];
            velArr->GetRange(vRange, -1);  // component=-1 → magnitude range
            if (vRange[1] > 1e4) {
                qWarning() << "Streamlines: velocity field diverged (max |U| =" << vRange[1]
                           << "). Refusing to trace streamlines into garbage data.";
                m_streamActor->SetVisibility(0);
                m_vtkWidget->renderWindow()->Render();
                emit streamlinesDiverged();  // signal for UI warning if connected
                return;
            }
        }

        pd->GetPointData()->SetActiveVectors(velName);

        double bounds[6];
        pd->GetBounds(bounds);
        double domLen = bounds[1] - bounds[0];

        // Aircraft surface bounds: used to size seed plane around the aircraft
        double surf[6] = {bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]};
        if (m_lastDataSet) m_lastDataSet->GetBounds(surf);

        // Seed plane: upstream inlet face, sized to aircraft Y-Z footprint + proximity margin.
        // Using domain-sized seed planes wastes almost all seeds on freestream far from aircraft.
        double aircraftH = surf[3] - surf[2];  // Y span of aircraft
        double aircraftW = surf[5] - surf[4];  // Z span of aircraft
        double padY = aircraftH * m_streamProximity;
        double padZ = aircraftW * m_streamProximity;

        // Detect actual flow direction from the solved field rather than trusting
        // the configured axis: average U_x over a sample of points. Positive mean
        // → flow runs +X, so the inlet is the x-min face; negative → x-max face.
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
        // Dead band: the raw mean includes the stagnant far field, the wake and the
        // no-slip body, so a bare "< 0" test flips the seed face at random on cases
        // whose mean U_x sits near zero. Require a clear fraction of freestream.
        double uRef = (m_inletSpeed > 1e-6) ? m_inletSpeed : std::fabs(meanUx);
        double sX = (meanUx < -0.05 * uRef)
            ? bounds[1] - domLen * 0.04    // -X flow: inlet is the x-max face
            : bounds[0] + domLen * 0.04;   // +X flow: inlet is the x-min face
        double sY0 = surf[2] - padY;
        double sY1 = surf[3] + padY;
        double sZ0 = surf[4] - padZ;
        double sZ1 = surf[5] + padZ;

        // Clamp to domain extents so seeds don't land outside the mesh
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

        // Center-plane seeds: a vertical line at the aircraft's Z midpoint so
        // streamlines pass through the symmetry plane over/under the body
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
        // Cap at ~1.1× the flow-axis length so wake vortex streamlines
        // don't spiral indefinitely before reaching max propagation.
        tracer->SetMaximumPropagation(domLen * 1.1);
        tracer->SetInitialIntegrationStep(domDiag * 1e-4);
        tracer->SetMaximumIntegrationStep(domDiag * 2e-3);
        tracer->SetIntegrationDirectionToForward();
        tracer->SetIntegratorTypeToRungeKutta45();
        tracer->SetMaximumError(1e-5);
        // Safety net only - MaximumPropagation is the intended length limit. RK45
        // shrinks its step through refined cells near the body, so a low cap here
        // truncates every line at the same distance from the seed plane.
        tracer->SetMaximumNumberOfSteps(50000);
        // Stop lines that stagnate (separation bubbles, near-wall recirculation).
        // Must be a fraction of the INLET speed, never of the field max: on a diverged
        // run the max can be 20x freestream, putting the threshold above freestream
        // and killing every line at its seed point.
        // 0.5% of freestream kills lines the moment they reach slow wake fluid, so the
        // region behind the body renders empty. Use a much smaller floor so lines can
        // push through the velocity deficit and trace the wake.
        tracer->SetTerminalSpeed(m_inletSpeed > 1e-6 ? m_inletSpeed * 1e-4 : 1e-6);

        // Velocity range: 0 (stagnation) → U_inf (free stream)
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
        // Streamlines run low = red -> high = blue, so freestream reads blue and
        // slow/stagnant regions read red. Deliberately the reverse of m_lut.
        streamLut->SetHueRange(0.0, 0.667);
        streamLut->SetSaturationRange(1.0, 1.0);
        streamLut->SetValueRange(1.0, 1.0);
        // U is a 3-component array: colour by |U|, not by component 0. On -X flow
        // Ux is negative, falls below the [0, U_inf] range and clamps every line
        // to the bottom of the table, painting the whole field one colour.
        streamLut->SetVectorModeToMagnitude();
        streamLut->Build();

        // A vtkPolyDataConnectivityFilter used to run here purely as a side effect to
        // force the tracer to execute; its output was never read. Update the tracer
        // directly instead and skip a full connectivity pass over every streamline.
        tracer->Update();

        // Post-filters disabled: every traced line is rendered as-is. Previously two
        // filters dropped whole streamlines - arc length < 5% of the domain diagonal,
        // and net displacement / arc length < 0.08 ("looping spiral") - which removed
        // legitimate lines that slowed or curved tightly around the body and made the
        // field look chopped up.
        auto tracerOut = vtkSmartPointer<vtkPolyData>::New();
        tracerOut->ShallowCopy(tracer->GetOutput());

        // Render as thick lines: same visual result as tubes at 1/50th the geometry cost.
        // vtkTubeFilter at 128 seeds × 3000 steps × 4 sides ≈ 1.5M triangles; lines ≈ 0.
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(tracerOut);
        mapper->SetLookupTable(streamLut);
        mapper->SetScalarRange(velRange[0], velRange[1]);
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray(velName);
        mapper->SetColorModeToMapScalars();   // run values through streamLut

        m_streamActor->SetMapper(mapper);
        m_streamActor->GetProperty()->SetLineWidth(1.8f * static_cast<float>(m_streamTubeScale));
        m_streamActor->GetProperty()->SetOpacity(0.9);
    }
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

    // Compute smooth normals once; breaks live pipeline so VTK skips MTime checks each frame.
    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(displayMesh);
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->SplittingOff();
    normals->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(normals->GetOutput());  // static snapshot, no pipeline re-eval
    mapper->StaticOn();

    m_meshActor->SetMapper(mapper);
    // clearMeshData() turns the actor off when swapping geometry; nothing else
    // turns it back on, so loading a second STL would render nothing.
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
    m_meshActor->SetVisibility(1);   // see loadSTL: clearMeshData() left it off
    m_meshActor->GetProperty()->SetColor(0.6, 0.75, 0.9);
    m_meshActor->GetProperty()->EdgeVisibilityOff();
    m_meshActor->GetProperty()->BackfaceCullingOn();
    resetCamera();
}

void ViewportWidget::loadOFResults(const QString& caseDir)
{
    m_caseDir = caseDir;

    // Cache the inlet speed from 0/U. Nothing else assigns m_inletSpeed, so without
    // this it stays 0.0 forever and every consumer (streamline terminal speed, the
    // |U| colour range, the seed-face dead band) silently falls back.
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

    // Find numeric time directories (> 0)
    QDir dir(caseDir);
    QStringList timeSteps;
    static QRegularExpression numRe("^\\d+(\\.\\d+)?$");
    for (const QString& e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok; double v = e.toDouble(&ok);
        if (ok && v > 0) timeSteps << e;
    }
    std::sort(timeSteps.begin(), timeSteps.end(),
              [](const QString& a, const QString& b){ return a.toDouble() < b.toDouble(); });
    if (timeSteps.isEmpty()) {
        return;
    }
    m_timeSteps = timeSteps;

    // Touch the .foam file so vtkOpenFOAMReader sees a new mtime and re-reads all
    // time directories: otherwise VTK caches the old time set between runs.
    QString foamFile = caseDir + "/case.foam";
    {
        QFile f(foamFile);
        f.open(QIODevice::WriteOnly);
    }

    // Read with VTK's OpenFOAM reader
    auto reader = vtkSmartPointer<vtkOpenFOAMReader>::New();
    reader->SetFileName(foamFile.toStdString().c_str());
    reader->ReadZonesOn();
    reader->DecomposePolyhedraOn();
    reader->Update();

    // Collect available field names from the last time step
    m_availableFields.clear();
    auto* mb = reader->GetOutput();
    if (mb && mb->GetNumberOfBlocks() > 0) {
        auto* block = mb->GetBlock(0);
        if (block) {
            auto* pd = block->GetFieldData();
            // Check point data arrays from geometry filter
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

    // Add U magnitude as a synthetic option
    if (m_availableFields.contains("U") && !m_availableFields.contains("|U|"))
        m_availableFields.prepend("|U|");
    m_fieldCombo->blockSignals(true);
    m_fieldCombo->clear();
    m_fieldCombo->addItems(m_availableFields);
    m_fieldCombo->setEnabled(!m_availableFields.isEmpty());
    m_fieldCombo->blockSignals(false);

    // Slider range = 0..N-1 (index-based): works for both integer RANS steps and
    // fractional LES times where int(lastTime) would truncate to 0.
    int lastIdx = m_timeSteps.size() - 1;
    m_timeSlider->blockSignals(true);
    m_timeSlider->setMinimum(0);
    m_timeSlider->setMaximum(lastIdx);
    m_timeSlider->setValue(lastIdx);
    m_timeSlider->setEnabled(lastIdx > 0);
    m_timeSlider->blockSignals(false);
    if (m_timeLabel) m_timeLabel->setText(m_timeSteps.last());

    // Always invalidate cached volume so streamlines/iso reload from new run results,
    // even when the last timestep index hasn't changed between runs
    m_volumeDataSet = nullptr;
    m_streamPointCache = nullptr;
    applyField(m_fieldCombo->currentText(), m_timeSteps.size() - 1);
    resetCamera();  // only on initial results load, not on field/step changes
}

void ViewportWidget::applyField(const QString& field, int timeStep)
{
    if (m_caseDir.isEmpty() || m_timeSteps.isEmpty()) return;

    QString foamFile = m_caseDir + "/case.foam";

    auto reader = vtkSmartPointer<vtkOpenFOAMReader>::New();
    reader->SetFileName(foamFile.toStdString().c_str());
    reader->DecomposePolyhedraOn();
    reader->UpdateInformation();

    // Parse boundary file to find wall patches vs domain faces.
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

    // Enumerate all available patches from reader (after UpdateInformation).
    // Build a lookup so we can enable by exact VTK name: avoids the
    // EnableAllPatchArrays+disable-by-name approach which silently fails when
    // the name doesn't match exactly, letting internalMesh bleed through.
    reader->DisableAllPatchArrays();
    int nPatches = reader->GetNumberOfPatchArrays();
    auto isInternal = [](const QString& n) {
        return n.compare("internalMesh", Qt::CaseInsensitive) == 0;
    };

    int enabledWallCount = 0;
    if (!wallPatches.isEmpty()) {
        // Enable only the wall patches found in the boundary file
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
        // No wall patch names matched (LES case, non-standard naming, or boundary file missing).
        // Fall back to enabling all non-internal patches so the mesh renders and the
        // colorbar becomes visible, rather than bailing out silently.
        for (int i = 0; i < nPatches; ++i) {
            QString pname = QString::fromStdString(reader->GetPatchArrayName(i));
            if (!isInternal(pname))
                reader->SetPatchArrayStatus(reader->GetPatchArrayName(i), 1);
        }
    }

    // Set time step
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

    // Smooth normals: removes flat-shading faceting from low-poly STL surfaces.
    // FeatureAngle 45° preserves sharp edges (wing LE, fin roots) while smoothing curved surfaces.
    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetFeatureAngle(20.0);
    normals->SplittingOn();
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();

    // Handle |U| magnitude via calculator
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

    // Auto-range: for |U| the magnitude array lives in normalsPoly (post-calculator),
    // not in the original poly which only has the raw U vector.
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
    m_resultActor->GetProperty()->SetOpacity(1.0);
    m_resultActor->GetProperty()->SetInterpolationToGouraud();
    m_meshActor->SetVisibility(0);

    // Cache dataset for iso-surface and streamlines
    m_lastDataSet = vtkSmartPointer<vtkPolyData>::New();
    vtkPolyData::SafeDownCast(m_lastDataSet)->ShallowCopy(poly);

    // Colorbar range fallback: wall surface has U=0 everywhere (no-slip) so the
    // surface-only range for |U| is degenerate [0,0]. Read inlet velocity from 0/U
    // as a lightweight substitute: avoids loading the full volume mesh here.
    if (field == "|U|" && range[0] >= range[1] - 1e-10) {
        double uInf = m_inletSpeed;  // pre-loaded from run metadata (most reliable)
        if (uInf < 1e-6) {
            // Scan 0/U: match any "uniform (x y z)" regardless of keyword prefix
            // so fixedValue, inletOutlet, freestream, pressureInletOutletVelocity etc. all work.
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

    // Invalidate cached volume data whenever the time step changes so streamlines
    // reload from the correct time directory on next toggle.
    if (timeStep != m_lastTimeStepIndex)
        m_volumeDataSet = nullptr;
        m_streamPointCache = nullptr;
    m_lastTimeStepIndex = timeStep;

    // Refresh overlays if active
    if (m_btnIso    && m_btnIso->isChecked())    toggleIsoSurface(true);
    if (m_btnStream && m_btnStream->isChecked()) toggleStreamlines(true);

    // Domain outline: wireframe of non-wall patches
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
    // Don't reset camera here: caller handles it for geometry loads.
    // updateScalarBar already triggers Render().
}

void ViewportWidget::updateScalarBar(const QString& label, double mn, double mx)
{
    if (m_colorBar) {
        m_colorBar->setRange(mn, mx, label);
        m_colorBar->setVisible(true);
    }
    if (m_fieldLabel) m_fieldLabel->setText(label);
    m_vtkWidget->renderWindow()->Render();  // single render per applyField() call
}

void ViewportWidget::showStreamSettings()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Streamline Settings");
    dlg->setFixedWidth(300);
    auto* form = new QFormLayout(dlg);
    form->setSpacing(10);
    form->setContentsMargins(14, 14, 14, 14);

    // Snapshot originals so Cancel can restore them
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

    // Live-update member vars on every spinbox change so values are never lost
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
    } else {
        // Restore originals on Cancel
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
    // Re-use snapping logic from onTimeStepChanged
    onTimeStepChanged(m_timeSlider->value());
}

void ViewportWidget::onTimeStepChanged(int sliderVal)
{
    if (m_timeSteps.isEmpty()) return;
    // Slider is index-based (0..N-1) after loadOFResults - clamp and look up directly.
    int idx = std::clamp(sliderVal, 0, (int)m_timeSteps.size() - 1);
    m_timeLabel->setText(m_timeSteps[idx]);
    applyField(m_fieldCombo->currentText(), idx);
}

void ViewportWidget::resetCamera() {
    // Reset to mesh/result actor bounds so the domain preview box doesn't
    // shrink the geometry to a speck in the corner.
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
    m_inletSpeed    = 0.0;   // stale value would skew the next case's colour range
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
    if (t == Theme::Blueprint)
        m_renderer->SetBackground(0.17, 0.17, 0.17);
    else
        m_renderer->SetBackground(0.12, 0.12, 0.18);

    // No local stylesheet: ThemeEngine's global QSS handles everything.
    // A local setStyleSheet here would override palette derivation for all
    // child widgets (including the Field combobox), causing Fusion to paint
    // drop-down buttons with unexpected palette colors.
    if (m_controlBar)
        m_controlBar->setStyleSheet(QString());

    m_vtkWidget->renderWindow()->Render();
}

void ViewportWidget::updateDomainPreview(double x0, double y0, double z0,
                                          double x1, double y1, double z1,
                                          const QString& flowAxis)
{
    // Update wireframe domain box
    double cx = (x0+x1)/2, cy = (y0+y1)/2, cz = (z0+z1)/2;
    double lx = x1-x0, ly = y1-y0, lz = z1-z0;
    m_domainCubeSource->SetCenter(cx, cy, cz);
    m_domainCubeSource->SetXLength(lx);
    m_domainCubeSource->SetYLength(ly);
    m_domainCubeSource->SetZLength(lz);
    m_domainCubeSource->Update();
    m_domainPreviewActor->SetVisibility(1);

    // Cone: place it at the inlet face, point in the flow direction
    double coneLen = std::min({lx, ly, lz}) * 0.18;
    double coneRad = coneLen * 0.35;
    m_flowConeSource->SetHeight(coneLen);
    m_flowConeSource->SetRadius(coneRad);

    // vtkConeSource points along +X by default; rotate and position for each axis
    m_flowConeActor->SetOrientation(0, 0, 0);
    if (flowAxis == "+X") {
        // Cone tip points in +X (flow goes right); place at x-min face
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
    } else {   // -Z
        m_flowConeSource->SetDirection(0, 0, -1);
        m_flowConeActor->SetPosition(cx, cy, z1 - coneLen*0.5);
    }
    m_flowConeSource->Update();
    m_flowConeActor->SetVisibility(1);

    m_vtkWidget->renderWindow()->Render();
}

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
#include <QPainter>
#include <QLinearGradient>
#include <QPushButton>
#include <QString>
#include <QJsonObject>
#include "themes/ThemeEngine.h"
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkActor.h>
#include <vtkLookupTable.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkCubeSource.h>
#include <vtkConeSource.h>
#include <vtkActor.h>
#include <QVTKOpenGLStereoWidget.h>
#include <QElapsedTimer>
#include <memory>
#include <string>
#include "FlowParticles.h"
class QTimer;
class vtkPolyData;

class QComboBox;
class QSlider;
class QLabel;

class ColorBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorBarWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(140, 18);
    }
    void setRange(double mn, double mx, const QString& field) {
        m_min = mn; m_max = mx; m_field = field; update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0.0,  QColor(0,   0,   255));
        g.setColorAt(0.25, QColor(0,   255, 255));
        g.setColorAt(0.5,  QColor(0,   255, 0  ));
        g.setColorAt(0.75, QColor(255, 255, 0  ));
        g.setColorAt(1.0,  QColor(255, 0,   0  ));
        p.fillRect(rect(), g);
        QFont f = font(); f.setPointSize(7); f.setBold(true); p.setFont(f);
        QFontMetrics fm(f);
        QString minS = QString::number(m_min, 'g', 3);
        QString maxS = QString::number(m_max, 'g', 3);
        auto drawOutlined = [&](const QString& txt, int x, int y) {
            p.setPen(QColor(0, 0, 0, 180));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx || dy) p.drawText(x + dx, y + dy, txt);
            p.setPen(Qt::white);
            p.drawText(x, y, txt);
        };
        drawOutlined(minS, 2, height() - 3);
        drawOutlined(maxS, width() - fm.horizontalAdvance(maxS) - 2, height() - 3);
    }
private:
    double  m_min = 0, m_max = 1;
    QString m_field;
};

class ViewportWidget : public QWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr, bool studioMode = true);

    void loadSTL(const QString& path);
    void loadOBJ(const QString& path);
    void loadOFResults(const QString& caseDir);
    void loadAtStep(int stepIndex);
    void resetCamera();
    void setBackgroundColor(double r, double g, double b);
    void setTheme(Theme t);

    void updateDomainPreview(double x0, double y0, double z0,
                             double x1, double y1, double z1,
                             const QString& flowAxis);

    QJsonObject getCameraState() const;
    void        setCameraState(const QJsonObject& obj);
    QString     activeField() const;
    int         activeTimeStep() const;
    void        setActiveField(const QString& field);
    void        setActiveTimeStep(int step);
    void        setFlowRenderingBlocked(bool blocked);
    QString     stlPath() const { return m_stlPath; }
    bool        meshHidden() const { return m_btnHideMesh && m_btnHideMesh->isChecked(); }
    void        setMeshHidden(bool hidden);
    void        clearScene();
    void        setProjectDir(const QString& dir) { m_projectDir = dir; }
    void        setSliderMax(int endTime);

    double streamProximity() const  { return m_streamProximity; }
    int    streamDensity()   const  { return m_streamDensity;   }
    double streamTubeScale() const  { return m_streamTubeScale; }
    void   setStreamProximity(double v) { m_streamProximity = v; }
    void   setStreamDensity(int v)      { m_streamDensity   = v; }
    void   setStreamTubeScale(double v) { m_streamTubeScale = v; }

signals:
    void streamlinesDiverged();
    void streamlinesAxisMismatch(const QString& configured, const QString& actual);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onFieldChanged(int idx);
    void onTimeStepChanged(int step);

private:
    void setupPipeline();
    void setupResultsToolbar();
    void setupMeshToolbar();
    void clearMeshData();
    void applyField(const QString& field, int timeStep);
    void updateScalarBar(const QString& label, double mn, double mx);

    QVTKOpenGLStereoWidget*       m_vtkWidget   = nullptr;
    QWidget*                      m_vtkHost     = nullptr;
    QTimer*                       m_resizeTimer = nullptr;
    vtkSmartPointer<vtkRenderer>  m_renderer;
    vtkSmartPointer<vtkActor>     m_meshActor;
    vtkSmartPointer<vtkActor>     m_resultActor;
    vtkSmartPointer<vtkActor>     m_domainActor;
    vtkSmartPointer<vtkLookupTable> m_lut;

    vtkSmartPointer<vtkOrientationMarkerWidget> m_axesWidget;
    vtkSmartPointer<vtkCubeSource>  m_domainCubeSource;
    vtkSmartPointer<vtkActor>       m_domainPreviewActor;
    vtkSmartPointer<vtkConeSource>  m_flowConeSource;
    vtkSmartPointer<vtkActor>       m_flowConeActor;

    QString      m_caseDir;
    QString      m_projectDir;
    QString      m_stlPath;
    QStringList  m_timeSteps;
    QStringList  m_availableFields;

    QComboBox*      m_fieldCombo  = nullptr;
    QSlider*        m_timeSlider  = nullptr;
    QLabel*         m_timeLabel   = nullptr;
    QWidget*        m_controlBar  = nullptr;
    ColorBarWidget* m_colorBar    = nullptr;
    QLabel*         m_fieldLabel  = nullptr;
    QPushButton*    m_btnHideMesh   = nullptr;
    QPushButton*    m_btnEdges      = nullptr;
    QPushButton*    m_btnIso        = nullptr;
    QPushButton*    m_btnStream     = nullptr;
    QPushButton*    m_btnParticles  = nullptr;

    vtkSmartPointer<vtkActor> m_isoActor;
    vtkSmartPointer<vtkActor> m_streamActor;

    vtkSmartPointer<vtkDataSet> m_lastDataSet;
    vtkSmartPointer<vtkDataSet> m_volumeDataSet;
    vtkSmartPointer<vtkDataSet> m_streamPointCache;
    int  m_lastTimeStepIndex    = 0;
    double  m_inletSpeed           = 0.0;
    QString m_flowAxis             = "+X";

    double m_streamProximity  = 0.10;
    int    m_streamDensity    = 256;
    double m_streamTubeScale  = 1.0;
    bool   m_flowRenderingBlocked = false;

    vtkSmartPointer<vtkPolyData>    m_streamPaths;
    vtkSmartPointer<vtkLookupTable> m_streamLut;
    std::string                     m_streamVelName;

    std::unique_ptr<FlowParticles> m_particles;
    QTimer*        m_particleTimer = nullptr;
    QElapsedTimer  m_particleClock;

    void toggleIsoSurface(bool on);
    void toggleStreamlines(bool on);
    void toggleParticles(bool on);
    void stopParticles();
    void showStreamSettings();
};

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
#include <QMap>
#include <QVector>
#include <QString>
#include <QTimer>
#include "../solver/SolverRunner.h"
#include "themes/ThemeEngine.h"

class ResidualPlot : public QWidget {
    Q_OBJECT
public:
    explicit ResidualPlot(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {600, 200}; }
    QSize minimumSizeHint() const override { return {200, 120}; }

public slots:
    void addResidual(const Residual& r);
    void clear();
    void setTheme(Theme t) { m_theme = t; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Series {
        QVector<double> values;
        QColor color;
    };

    QMap<QString, Series> m_series;
    int    m_maxIter     = 0;
    Theme  m_theme       = Theme::Dark;
    QTimer m_coalesce;   // batches rapid addResidual() calls into one repaint

    static const QColor SERIES_COLORS[];
    QColor colorForField(const QString& field);
    int m_colorIdx = 0;

    void drawGrid(QPainter&, const QRectF& plot, double yMin, double yMax) const;
    void drawAxes(QPainter&, const QRectF& plot, double yMin, double yMax) const;
    void drawSeries(QPainter&, const QRectF& plot, double yMin, double yMax) const;
    void drawLegend(QPainter&, const QRectF& plot) const;
};

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

#include "ResidualPlot.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

const QColor ResidualPlot::SERIES_COLORS[] = {
    {0x64, 0xb5, 0xf6},
    {0xef, 0x9a, 0x9a},
    {0xa5, 0xd6, 0xa7},
    {0xff, 0xe0, 0x82},
    {0xce, 0x93, 0xd8},
    {0x80, 0xde, 0xea},
    {0xff, 0xcc, 0x80},
};

ResidualPlot::ResidualPlot(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(200);
    connect(&m_coalesce, &QTimer::timeout, this, [this]{ update(); });
}

void ResidualPlot::addResidual(const Residual& r) {
    if (!m_series.contains(r.field))
        m_series[r.field] = {QVector<double>{}, colorForField(r.field)};

    Series& s = m_series[r.field];
    while (s.values.size() < r.iteration - 1)
        s.values.append(std::numeric_limits<double>::quiet_NaN());
    s.values.append(r.initial > 0 ? r.initial : 1e-20);

    m_maxIter = std::max(m_maxIter, r.iteration);
    if (!m_coalesce.isActive()) m_coalesce.start();
}

void ResidualPlot::clear() {
    m_series.clear();
    m_maxIter = 0;
    update();
}

QColor ResidualPlot::colorForField(const QString&) {
    constexpr int N = int(sizeof(SERIES_COLORS) / sizeof(SERIES_COLORS[0]));
    return SERIES_COLORS[m_colorIdx++ % N];
}

void ResidualPlot::paintEvent(QPaintEvent*) {
    // Theme-aware palette
    const bool blueprint =(m_theme == Theme::Blueprint);
    const QColor bgColor      = blueprint ? QColor(0x1f, 0x1f, 0x1f) : QColor(0x16, 0x16, 0x22);
    const QColor gridColor    = blueprint ? QColor(0x3a, 0x3a, 0x3a) : QColor(0x30, 0x30, 0x44);
    const QColor axisColor    = blueprint ? QColor(0x88, 0x88, 0x88) : QColor(0x88, 0x88, 0x99);
    const QColor borderColor  = blueprint ? QColor(0x44, 0x44, 0x44) : QColor(0x55, 0x55, 0x66);
    const QColor legendBg     = blueprint ? QColor(0x2b, 0x2b, 0x2b, 200) : QColor(0x16, 0x16, 0x22, 200);
    const QColor legendBorder = blueprint ? QColor(0x55, 0x55, 0x55) : QColor(0x33, 0x33, 0x44);
    // Store in members so draw helpers can use them
    // (pass via paint-local struct instead)
    struct Pal { QColor bg, grid, axis, border, legBg, legBdr; };
    Pal pal{bgColor, gridColor, axisColor, borderColor, legendBg, legendBorder};

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), pal.bg);

    if (m_series.isEmpty() || m_maxIter == 0) {
        p.setPen(pal.border);
        p.drawText(rect(), Qt::AlignCenter, "No residuals yet");
        return;
    }

    const int ML = 72, MR = 12, MT = 10, MB = 28;
    QRectF plot(ML, MT, width() - ML - MR, height() - MT - MB);
    if (plot.width() < 10 || plot.height() < 10) return;

    double yMin = 0.0, yMax = -20.0;
    for (auto it = m_series.cbegin(); it != m_series.cend(); ++it) {
        const Series& s = it.value();
        for (double v : s.values) {
            if (!std::isnan(v) && v > 0) {
                double lv = std::log10(v);
                yMin = std::min(yMin, lv);
                yMax = std::max(yMax, lv);
            }
        }
    }
    yMin = std::floor(yMin) - 0.5;
    yMax = std::ceil(yMax)  + 0.5;
    if (yMax - yMin < 1.0) yMax = yMin + 2.0;

    drawGrid(p, plot, yMin, yMax);
    drawAxes(p, plot, yMin, yMax);
    p.setClipRect(plot);
    drawSeries(p, plot, yMin, yMax);
    p.setClipping(false);
    drawLegend(p, plot);
}

void ResidualPlot::drawGrid(QPainter& p, const QRectF& plot,
                             double yMin, double yMax) const
{
    QColor gc = (m_theme == Theme::Blueprint) ? QColor(0x3a,0x3a,0x3a) : QColor(0x30,0x30,0x44);
    p.setPen(QPen(gc, 1, Qt::DotLine));
    int iMin = (int)std::floor(yMin), iMax = (int)std::ceil(yMax);
    for (int decade = iMin; decade <= iMax; ++decade) {
        double t = (decade - yMin) / (yMax - yMin);
        double y = plot.bottom() - t * plot.height();
        if (y >= plot.top() && y <= plot.bottom())
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
}

void ResidualPlot::drawAxes(QPainter& p, const QRectF& plot,
                              double yMin, double yMax) const
{
    const bool blueprint =(m_theme == Theme::Blueprint);
    QColor axCol = blueprint ? QColor(0x88,0x88,0x88) : QColor(0x88,0x88,0x99);
    QColor brCol = blueprint ? QColor(0x44,0x44,0x44) : QColor(0x55,0x55,0x66);

    p.setPen(axCol);
    QFont f = font(); f.setPointSize(7); p.setFont(f);
    QFontMetrics fm(f);

    int iMin = (int)std::floor(yMin), iMax = (int)std::ceil(yMax);
    for (int decade = iMin; decade <= iMax; ++decade) {
        double t = (decade - yMin) / (yMax - yMin);
        double y = plot.bottom() - t * plot.height();
        if (y < plot.top() - 2 || y > plot.bottom() + 2) continue;
        QString lbl = QString("1e%1").arg(decade);
        p.drawText(QRectF(0, y - 8, plot.left() - 3, 16),
                   Qt::AlignRight | Qt::AlignVCenter, lbl);
        p.drawLine(QPointF(plot.left() - 3, y), QPointF(plot.left(), y));
    }

    p.setPen(axCol);
    int step = std::max(1, m_maxIter / 5);
    for (int it = 1; it <= m_maxIter; it += step) {
        double x = plot.left() + (it - 1.0) / (m_maxIter - 1.0 + 1e-9) * plot.width();
        p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 3));
        p.drawText(QRectF(x - 20, plot.bottom() + 4, 40, 14),
                   Qt::AlignHCenter, QString::number(it));
    }

    p.setPen(brCol);
    p.drawRect(plot);
}

void ResidualPlot::drawSeries(QPainter& p, const QRectF& plot,
                                double yMin, double yMax) const
{
    const bool sparse = (m_maxIter <= 30);  // show dots when data is sparse (e.g. pimpleFoam)
    for (auto it = m_series.cbegin(); it != m_series.cend(); ++it) {
        const Series& s = it.value();
        if (s.values.isEmpty()) continue;
        p.setPen(QPen(s.color, 1.5));
        QPainterPath path;
        QVector<QPointF> dots;
        bool started = false;
        for (int i = 0; i < s.values.size(); ++i) {
            double v = s.values[i];
            if (std::isnan(v) || v <= 0) { started = false; continue; }
            double lv = std::log10(v);
            double x = plot.left() + (double)i / (m_maxIter - 1.0 + 1e-9) * plot.width();
            double t  = (lv - yMin) / (yMax - yMin);
            double y  = plot.bottom() - t * plot.height();
            y = std::clamp(y, plot.top(), plot.bottom());
            if (!started) { path.moveTo(x, y); started = true; }
            else           path.lineTo(x, y);
            if (sparse) dots.append({x, y});
        }
        p.drawPath(path);
        if (sparse) {
            p.setPen(Qt::NoPen);
            p.setBrush(s.color);
            for (const QPointF& pt : dots)
                p.drawEllipse(pt, 3.0, 3.0);
            p.setBrush(Qt::NoBrush);
        }
    }
}

void ResidualPlot::drawLegend(QPainter& p, const QRectF& plot) const
{
    QFont f = font(); f.setPointSize(9); p.setFont(f);
    QFontMetrics fm(f);

    const int lineH  = fm.height() + 4;
    const int swatchW = 16, swatchH = 3, pad = 8;

    // Measure widest entry to size the background box
    int maxW = 0;
    for (auto it = m_series.cbegin(); it != m_series.cend(); ++it) {
        double last = 0;
        for (int i = it.value().values.size()-1; i >= 0; --i) {
            if (!std::isnan(it.value().values[i]) && it.value().values[i] > 0)
                { last = it.value().values[i]; break; }
        }
        QString txt = QString("%1  %2").arg(it.key()).arg(last, 0, 'e', 2);
        maxW = std::max(maxW, fm.horizontalAdvance(txt));
    }

    int n   = m_series.size();
    int boxW = swatchW + 6 + maxW + pad * 2;
    int boxH = n * lineH + pad;

    // Position: top-right inside plot
    double bx = plot.right() - boxW - 6;
    double by = plot.top() + 6;

    const bool blueprintLeg =(m_theme == Theme::Blueprint);
    p.fillRect(QRectF(bx, by, boxW, boxH),
               blueprintLeg ? QColor(0x2b,0x2b,0x2b,200) : QColor(0x16,0x16,0x22,200));
    p.setPen(blueprintLeg ? QColor(0x55,0x55,0x55) : QColor(0x33,0x33,0x44));
    p.drawRect(QRectF(bx, by, boxW, boxH));

    double ly = by + pad / 2.0;
    for (auto it = m_series.cbegin(); it != m_series.cend(); ++it) {
        const Series& s = it.value();

        double last = 0;
        for (int i = s.values.size()-1; i >= 0; --i) {
            if (!std::isnan(s.values[i]) && s.values[i] > 0)
                { last = s.values[i]; break; }
        }

        // Color swatch
        p.fillRect(QRectF(bx + pad, ly + (lineH - swatchH) / 2.0, swatchW, swatchH), s.color);

        // Label
        QString txt = QString("%1  %2").arg(it.key()).arg(last, 0, 'e', 2);
        p.setPen(s.color);
        p.drawText(QPointF(bx + pad + swatchW + 6, ly + fm.ascent()), txt);
        ly += lineH;
    }
}

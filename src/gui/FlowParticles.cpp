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

#include "FlowParticles.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkFloatArray.h>
#include <vtkUnsignedCharArray.h>
#include <vtkCellArray.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkLookupTable.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

namespace {
constexpr int    kTail        = 10;     // samples per comet tail
constexpr size_t kMaxParts    = 6000;
constexpr double kLapSeconds  = 5.0;    // wall time for a median line at speed 1
constexpr double kPerLap      = 8.0;    // particles spaced along a median line
constexpr double kTailFrac    = 0.08;   // tail length as a fraction of a median lap
}

FlowParticles::FlowParticles()
{
    m_points = vtkSmartPointer<vtkPoints>::New();
    m_points->SetDataTypeToFloat();
    m_colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    m_colors->SetNumberOfComponents(4);
    m_colors->SetName("rgba");
    m_poly = vtkSmartPointer<vtkPolyData>::New();
    m_poly->SetPoints(m_points);
    m_poly->GetPointData()->SetScalars(m_colors);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(m_poly);
    mapper->SetColorModeToDirectScalars();
    mapper->SetScalarModeToUsePointData();
    mapper->ScalarVisibilityOn();

    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(mapper);
    m_actor->GetProperty()->LightingOff();
    m_actor->GetProperty()->SetLineWidth(3.0f);
    m_actor->GetProperty()->SetPointSize(5.0f);
    m_actor->GetProperty()->RenderPointsAsSpheresOn();
    m_actor->SetPickable(0);
    m_actor->SetVisibility(0);
}

void FlowParticles::clear()
{
    m_pos.clear(); m_time.clear(); m_rgb.clear();
    m_lines.clear(); m_parts.clear();
    m_points->SetNumberOfPoints(0);
    m_colors->SetNumberOfTuples(0);
    m_poly->SetLines(vtkSmartPointer<vtkCellArray>::New());
    m_poly->SetVerts(vtkSmartPointer<vtkCellArray>::New());
    m_poly->Modified();
}

bool FlowParticles::setPaths(vtkPolyData* paths, const char* velName, vtkLookupTable* lut)
{
    clear();
    if (!paths || !paths->GetLines() || !lut) return false;
    vtkDataArray* vel   = velName ? paths->GetPointData()->GetArray(velName) : nullptr;
    vtkDataArray* itime = paths->GetPointData()->GetArray("IntegrationTime");

    vtkCellArray* cells = paths->GetLines();
    cells->InitTraversal();
    vtkIdType n = 0;
    const vtkIdType* ids = nullptr;
    while (cells->GetNextCell(n, ids)) {
        if (n < 3) continue;
        Line l{ m_time.size(), 0, 0.0 };
        double t = 0.0, prev[3] = {0, 0, 0};
        for (vtkIdType i = 0; i < n; ++i) {
            double p[3];
            paths->GetPoint(ids[i], p);
            double speed = 0.0;
            if (vel) {
                double u[3];
                vel->GetTuple(ids[i], u);
                speed = std::sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
            }
            if (itime) {
                t = itime->GetTuple1(ids[i]);
            } else if (i > 0) {
                // Without tracer time, step by arc length over local speed
                double ds = std::sqrt((p[0]-prev[0])*(p[0]-prev[0]) + (p[1]-prev[1])*(p[1]-prev[1])
                                      + (p[2]-prev[2])*(p[2]-prev[2]));
                t += ds / std::max(speed, 1e-6);
            }
            if (i > 0 && t < m_time.back()) t = m_time.back();
            unsigned char c[3];
            const unsigned char* lc = lut->MapValue(speed);
            c[0] = lc[0]; c[1] = lc[1]; c[2] = lc[2];
            m_pos.insert(m_pos.end(), { float(p[0]), float(p[1]), float(p[2]) });
            m_time.push_back(t);
            m_rgb.insert(m_rgb.end(), { c[0], c[1], c[2] });
            prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
        }
        l.count = m_time.size() - l.start;
        l.duration = m_time.back() - m_time[l.start];
        if (l.duration > 0.0) m_lines.push_back(l);
    }
    if (m_lines.empty()) { clear(); return false; }

    std::vector<double> durs;
    for (const auto& l : m_lines) durs.push_back(l.duration);
    std::nth_element(durs.begin(), durs.begin() + durs.size() / 2, durs.end());
    double median = durs[durs.size() / 2];
    m_rate = median / kLapSeconds;
    m_tail = median * kTailFrac;

    // Spread particles evenly in time so long wake loops get more of them
    double spacing = median / kPerLap;
    size_t total = 0;
    for (const auto& l : m_lines) total += std::max<size_t>(1, size_t(l.duration / spacing));
    if (total > kMaxParts) spacing *= double(total) / kMaxParts;

    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> jitter(0.0, 1.0);
    for (int li = 0; li < int(m_lines.size()); ++li) {
        size_t k = std::max<size_t>(1, size_t(m_lines[li].duration / spacing));
        for (size_t j = 0; j < k; ++j)
            m_parts.push_back({ li, (double(j) + jitter(rng)) * m_lines[li].duration / double(k) });
    }

    const vtkIdType np = vtkIdType(m_parts.size());
    m_points->SetNumberOfPoints(np * kTail);
    m_colors->SetNumberOfTuples(np * kTail);
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    auto verts = vtkSmartPointer<vtkCellArray>::New();
    vtkIdType seg[kTail];
    for (vtkIdType p = 0; p < np; ++p) {
        for (int s = 0; s < kTail; ++s) seg[s] = p * kTail + s;
        lines->InsertNextCell(kTail, seg);
        verts->InsertNextCell(1, &seg[0]);
    }
    m_poly->SetLines(lines);
    m_poly->SetVerts(verts);
    m_clock = 0.0;
    writeFrame();
    return true;
}

void FlowParticles::sample(const Line& l, double t, float* xyz, unsigned char* rgb) const
{
    const double* tb = m_time.data() + l.start;
    const double* te = tb + l.count;
    t = std::min(std::max(t, tb[0]), te[-1]);
    const double* hi = std::upper_bound(tb, te, t);
    if (hi == te) hi = te - 1;
    if (hi == tb) hi = tb + 1;
    size_t i1 = size_t(hi - m_time.data());
    size_t i0 = i1 - 1;
    double dt = m_time[i1] - m_time[i0];
    double w = dt > 0.0 ? (t - m_time[i0]) / dt : 0.0;
    for (int c = 0; c < 3; ++c) {
        xyz[c] = float(m_pos[3*i0 + c] + w * (m_pos[3*i1 + c] - m_pos[3*i0 + c]));
        rgb[c] = (unsigned char)(m_rgb[3*i0 + c] + w * (int(m_rgb[3*i1 + c]) - int(m_rgb[3*i0 + c])));
    }
}

void FlowParticles::advance(double wallSeconds)
{
    if (m_parts.empty()) return;
    m_clock += wallSeconds * m_rate * m_speed;
    writeFrame();
}

void FlowParticles::writeFrame()
{
    auto* pts = vtkFloatArray::SafeDownCast(m_points->GetData());
    if (!pts) return;
    float* xyz = pts->GetPointer(0);
    unsigned char* rgba = m_colors->GetPointer(0);
    for (size_t p = 0; p < m_parts.size(); ++p) {
        const Particle& part = m_parts[p];
        const Line& l = m_lines[part.line];
        double t0 = m_time[l.start];
        double head = t0 + std::fmod(m_clock + part.offset, l.duration);
        for (int s = 0; s < kTail; ++s) {
            double t = std::max(t0, head - m_tail * s / (kTail - 1));
            size_t k = p * kTail + s;
            sample(l, t, xyz + 3*k, rgba + 4*k);
            // Head brightened towards white, tail fades out
            double f = 1.0 - double(s) / (kTail - 1);
            if (s == 0)
                for (int c = 0; c < 3; ++c) rgba[4*k + c] = (unsigned char)(rgba[4*k + c] + (255 - rgba[4*k + c]) * 0.35);
            rgba[4*k + 3] = (unsigned char)(255.0 * f);
        }
    }
    m_points->Modified();
    m_colors->Modified();
    m_poly->Modified();
}

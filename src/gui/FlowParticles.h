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
#include <vector>
#include <vtkSmartPointer.h>
#include <vtkActor.h>
class vtkPolyData;
class vtkPoints;
class vtkUnsignedCharArray;
class vtkLookupTable;

class FlowParticles {
public:
    FlowParticles();

    vtkActor* actor() const { return m_actor; }
    bool setPaths(vtkPolyData* paths, const char* velName, vtkLookupTable* lut);
    void clear();
    bool empty() const { return m_parts.empty(); }
    void advance(double wallSeconds);
    void setSpeed(double s) { m_speed = s; }
    double speed() const { return m_speed; }

private:
    struct Line { size_t start; size_t count; double duration; };
    struct Particle { int line; double offset; };

    void sample(const Line& l, double t, float* xyz, unsigned char* rgb) const;
    void writeFrame();

    std::vector<float>         m_pos;
    std::vector<double>        m_time;
    std::vector<unsigned char> m_rgb;
    std::vector<Line>          m_lines;
    std::vector<Particle>      m_parts;

    double m_clock = 0.0;
    double m_rate  = 1.0;
    double m_tail  = 0.0;
    double m_speed = 1.0;

    vtkSmartPointer<vtkPolyData>          m_poly;
    vtkSmartPointer<vtkPoints>            m_points;
    vtkSmartPointer<vtkUnsignedCharArray> m_colors;
    vtkSmartPointer<vtkActor>             m_actor;
};

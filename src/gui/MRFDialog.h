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
#include <QDialog>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QList>

struct MRFZone {
    QString name          = "MRF1";
    QString cellZone      = "rotorZone";
    double  originX       = 0.0;
    double  originY       = 0.0;
    double  originZ       = 0.0;
    double  axisX         = 0.0;
    double  axisY         = 0.0;
    double  axisZ         = 1.0;
    double  rpm           = 1000.0;
    QString nonRotPatches;   // space-separated patch names
};

class MRFDialog : public QDialog {
    Q_OBJECT
public:
    explicit MRFDialog(const QString& caseDir, QWidget* parent = nullptr);

private slots:
    void onAdd();
    void onRemove();
    void onSelectionChanged();
    void onApply();

private:
    void loadFromFile();
    void saveToDetail(int idx);
    void loadToDetail(int idx);
    void writeZones();
    QList<MRFZone> parseFile(const QString& text);
    QString buildFile() const;

    QString m_caseDir;
    QList<MRFZone> m_zones;
    int m_currentIdx = -1;

    QListWidget*    m_list       = nullptr;
    QGroupBox*      m_detailBox  = nullptr;
    QLineEdit*      m_nameEdit   = nullptr;
    QLineEdit*      m_zoneEdit   = nullptr;
    QDoubleSpinBox* m_ox = nullptr, *m_oy = nullptr, *m_oz = nullptr;
    QDoubleSpinBox* m_ax = nullptr, *m_ay = nullptr, *m_az = nullptr;
    QDoubleSpinBox* m_rpm        = nullptr;
    QLineEdit*      m_noRotEdit  = nullptr;
    QLabel*         m_status     = nullptr;
};

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
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QString>
#include <QJsonObject>

class PhysicsPanel : public QDialog {
    Q_OBJECT
public:
    explicit PhysicsPanel(const QString& caseDir, QWidget* parent = nullptr);

    // Serialize / restore physics settings to/from a JSON object (for .cake project files)
    static QJsonObject snapshotFromCase(const QString& caseDir);
    static void        restoreToCase   (const QString& caseDir, const QJsonObject& obj);

    // Move libtenoScheme.so from fvSchemes → controlDict if misplaced (silent migration)
    static void migrateTenolibsToControlDict(const QString& caseDir);

signals:
    void solverRecommended(const QString& solver);

private slots:
    void onApply();
    void onSolverChanged(int idx);
    void onSimTypeChanged(int idx);
    void onModelChanged(int idx);
    void updateRe();

private:
    void loadFromCase();
    QString readDictValue(const QString& filePath, const QString& key) const;
    bool writeMomentumTransport();
    bool writeTransportProperties();
    bool writeInletVelocity();
    bool writeDivScheme();
    bool writeControlDictTimeSettings();
    bool isLES() const;
    static QString buildFvSchemes(const QString& solver, const QString& scheme);

    QString m_caseDir;

    // Solver / sim type
    QComboBox*      m_solverCombo;    // simpleFoam | buoyantSimpleFoam | rhoSimpleFoam | pimpleFoam | buoyantPimpleFoam
    QLabel*         m_thermalNote;    // hint shown when thermal solver selected
    QComboBox*      m_simTypeCombo;   // RANS | LES
    QComboBox*      m_modelCombo;
    QComboBox*      m_divSchemeCombo; // linearUpwind | teno | teno6

    QGroupBox*      m_turbBox;
    QDoubleSpinBox* m_kSpin;
    QDoubleSpinBox* m_omegaSpin;
    QDoubleSpinBox* m_epsilonSpin;
    QLabel*         m_omegaLabel;
    QLabel*         m_epsilonLabel;

    QDoubleSpinBox* m_nuSpin;
    QDoubleSpinBox* m_rhoSpin;
    QDoubleSpinBox* m_uxSpin, *m_uySpin, *m_uzSpin;
    QDoubleSpinBox* m_lengthSpin;
    QDoubleSpinBox* m_arefSpin;
    QLabel*         m_reLabel;

    void writeCakeProperties() const;
    void loadCakeProperties();
};

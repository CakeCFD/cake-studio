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
#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

namespace CFD {

struct DatasetInfo {
    QString id;
    QString description;
    QStringList fields;
    double tMin = 0, tMax = 0, dt = 0;
};

struct TurbStats {
    double k       = 0;   // turbulent kinetic energy [m²/s²]
    double I       = 0;   // turbulence intensity
    double epsilon = 0;   // dissipation rate estimate [m²/s³]
    double omega   = 0;   // specific dissipation rate [1/s]
    double Umag    = 0;   // mean velocity magnitude [m/s]
    QVector<double> Umean; // mean velocity vector [3 components]
};

class DatasetClient : public QObject {
    Q_OBJECT
public:
    enum class SpatialInterp  { None=0, Lag4=4, Lag6=6, Lag8=8 };
    enum class TemporalInterp { None=0, PCHIP=1 };

    explicit DatasetClient(QObject* parent = nullptr);

    void setEndpoint(const QString& url);
    void setToken(const QString& token);
    void setSpatialInterp(SpatialInterp s)   { m_spatialInterp  = s; }
    void setTemporalInterp(TemporalInterp t) { m_temporalInterp = t; }

    void fetchDatasetList();

    // Single-time batch query (SOAP or REST depending on endpoint)
    void queryField(const QString& dataset, const QString& field,
                    double time, const QVector<double>& points);

    // Multi-time range: fires sequential requests
    void queryFieldRange(const QString& dataset, const QString& field,
                         const QVector<double>& times,
                         const QVector<double>& points);

    // Cancels the running range job and aborts any in-flight query (single
    // or range); replies that still arrive afterwards are ignored
    void cancelRange();

    // Custom REST/JSON response parser shared with ApiFormatDialog's Test.
    // path: dot-separated, numeric segments index arrays ("data.0.items").
    // keys: per-item component keys, each may be dotted/indexed ("vel.x").
    static QVector<double> parseCustomJson(const QByteArray& raw, const QString& path,
                                           const QStringList& keys,
                                           QString* errOut = nullptr);

    // JHTDB SOAP vocabulary (also used by DatasetPanel's request viewer)
    static QString soapNamespace(const QString& endpoint);
    static QString soapMethod(const QString& field);
    static QString spatialInterpName(SpatialInterp s, const QString& method);
    static QString temporalInterpName(TemporalInterp t);

    // Compute turbulence statistics from spatial velocity samples (3-component)
    static TurbStats computeTurbStats(const QVector<double>& velocities,
                                      double Lref = 1.0,
                                      double Cmu  = 0.09);

    static int componentCount(const QString& field);

signals:
    void datasetListReady(const QList<DatasetInfo>& datasets);
    void queryFinished(const QString& dataset, const QString& field,
                       double time, const QVector<double>& result);
    void queryRangeProgress(int step, int total);
    void queryRangeFinished(const QString& dataset, const QString& field,
                            const QVector<double>& times,
                            const QList<QVector<double>>& results);
    void error(const QString& msg);

private slots:
    void onListReply();
    void onQueryReply();

private:
    QByteArray buildSoapBody(const QString& method, const QString& dataset,
                             double time, const QVector<double>& points) const;
    void querySoap(const QString& dataset, const QString& field,
                   double time, const QVector<double>& points);
    void queryCustom(const QString& dataset, const QString& field,
                     double time, const QVector<double>& points);
    void queryGraphQL(const QString& dataset, const QString& field,
                      double time, const QVector<double>& points);
    QByteArray buildGraphQLBody(const QString& field, const QString& dataset,
                                double time, const QVector<double>& points) const;
    void fireNextRangeStep();
    bool isSoap()     const;  // true when endpoint looks like a SOAP service (.asmx)
    bool isGraphQL()  const;  // true when endpoint contains "graphql"

    void track(QNetworkReply* reply);   // tag with m_generation, remember for abort
    void clearRangeJob();

    QNetworkAccessManager* m_nam;
    quint64  m_generation = 0;          // bumped by cancelRange()
    QList<QPointer<QNetworkReply>> m_inflight;
    QString  m_endpoint;
    QString  m_token;
    SpatialInterp  m_spatialInterp  = SpatialInterp::Lag4;
    TemporalInterp m_temporalInterp = TemporalInterp::None;

    struct RangeJob {
        QString dataset, field;
        QVector<double> times, points;
        QList<QVector<double>> results;
        int step = 0;
        bool cancelled = false;
    };
    RangeJob* m_rangeJob = nullptr;
};

} // namespace CFD

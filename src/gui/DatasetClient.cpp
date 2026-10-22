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

#include "DatasetClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <cmath>

namespace CFD {

// field metadata

struct FieldMeta {
    QString method;
    QStringList resultTags;  // XML element names to collect (in order)
    int components;
};

static const QMap<QString, FieldMeta> k_fieldMeta = {
    { "velocity",          { "GetVelocity",         {"x","y","z"},   3 } },
    { "pressure",          { "GetPressure",          {"p"},           1 } },
    { "vorticity",         { "GetVorticity",         {"x","y","z"},   3 } },
    { "magnetic",          { "GetMagneticField",     {"x","y","z"},   3 } },
    { "temperature",       { "GetTemperature",       {"p"},           1 } },
    { "velocity_gradient", { "GetVelocityGradient",
      {"duxdx","duxdy","duxdz","duydx","duydy","duydz","duzdx","duzdy","duzdz"}, 9 } },
    { "pressure_gradient", { "GetPressureGradient",  {"x","y","z"},   3 } },
};

// construction

DatasetClient::DatasetClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // JHTDB SSL handshake is slow from WSL (~20s); set a generous timeout
    m_nam->setTransferTimeout(120 * 1000);
}

bool DatasetClient::isSoap() const
{
    return m_endpoint.endsWith(".asmx", Qt::CaseInsensitive);
}

bool DatasetClient::isGraphQL() const
{
    return m_endpoint.contains("graphql", Qt::CaseInsensitive);
}

void DatasetClient::setEndpoint(const QString& url)
{
    m_endpoint = url.trimmed();
    if (m_endpoint.endsWith('/')) m_endpoint.chop(1);
}

void DatasetClient::setToken(const QString& token) { m_token = token.trimmed(); }

int DatasetClient::componentCount(const QString& field)
{
    return k_fieldMeta.value(field, FieldMeta{"", {}, 3}).components;
}

// public API

void DatasetClient::fetchDatasetList()
{
    if (m_endpoint.isEmpty()) { emit error("No endpoint configured."); return; }
    if (isSoap()) { emit error("SOAP endpoints don't expose a dataset list: enter datasets manually."); return; }

    if (isGraphQL()) {
        static const QByteArray kListQuery =
            R"({"query":"{ getQueryOptions { datasets { code name description physicalVariables { code } } } }"})";
        QNetworkRequest req{QUrl{m_endpoint}};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!m_token.isEmpty())
            req.setRawHeader("Authorization", ("Token " + m_token).toUtf8());
        auto* reply = m_nam->post(req, kListQuery);
        reply->setProperty("isGraphQL", true);
        connect(reply, &QNetworkReply::finished, this, &DatasetClient::onListReply);
        return;
    }

    QNetworkRequest req(QUrl(m_endpoint + "/datasets"));
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Token " + m_token).toUtf8());
    connect(m_nam->get(req), &QNetworkReply::finished, this, &DatasetClient::onListReply);
}

void DatasetClient::queryField(const QString& dataset, const QString& field,
                                double time, const QVector<double>& points)
{
    if (m_token.isEmpty()) { emit error("No API token set."); return; }
    if (points.size() < 3 || points.size() % 3 != 0) {
        emit error("Points must be a multiple of 3 (x,y,z triples)."); return;
    }
    if (isSoap())         querySoap(dataset, field, time, points);
    else if (isGraphQL()) queryGraphQL(dataset, field, time, points);
    else                  queryCustom(dataset, field, time, points);
}

void DatasetClient::queryFieldRange(const QString& dataset, const QString& field,
                                     const QVector<double>& times,
                                     const QVector<double>& points)
{
    if (m_rangeJob) { emit error("A range query is already running."); return; }
    if (times.isEmpty()) { emit error("No times specified."); return; }
    m_rangeJob = new RangeJob;
    m_rangeJob->dataset = dataset;
    m_rangeJob->field   = field;
    m_rangeJob->times   = times;
    m_rangeJob->points  = points;
    m_rangeJob->step    = 0;
    fireNextRangeStep();
}

void DatasetClient::cancelRange()
{
    // New generation first so the aborted replies' finished() is ignored
    ++m_generation;
    const auto inflight = m_inflight;
    m_inflight.clear();
    for (const QPointer<QNetworkReply>& r : inflight)
        if (r) r->abort();
    clearRangeJob();
}

void DatasetClient::track(QNetworkReply* reply)
{
    reply->setProperty("generation", m_generation);
    m_inflight.removeAll(QPointer<QNetworkReply>());
    m_inflight << reply;
}

void DatasetClient::clearRangeJob()
{
    delete m_rangeJob;
    m_rangeJob = nullptr;
}

// SOAP batch

QString DatasetClient::soapNamespace(const QString& endpoint)
{
    // JHTDB's WSDL targetNamespace, independent of the server's URL path.
    // Other SOAP servers keep the folder-derived namespace.
    static const QString kJhtdbNs = "http://turbulence.pha.jhu.edu/";
    const QUrl url(endpoint);
    if (url.host().endsWith("jhu.edu", Qt::CaseInsensitive))
        return kJhtdbNs;
    return url.adjusted(QUrl::RemoveFilename | QUrl::RemoveQuery).toString();
}

QString DatasetClient::soapMethod(const QString& field)
{
    return k_fieldMeta.value(field, FieldMeta{"GetVelocity", {"x","y","z"}, 3}).method;
}

QString DatasetClient::spatialInterpName(SpatialInterp s, const QString& method)
{
    // Gradient methods take the finite-difference members of JHTDB's
    // SpatialInterpolation enum (None_Fd4, Fd4Lag4, None_Fd6, None_Fd8)
    if (method.endsWith("Gradient")) {
        switch (s) {
        case SpatialInterp::Lag4: return "Fd4Lag4";
        case SpatialInterp::Lag6: return "None_Fd6";
        case SpatialInterp::Lag8: return "None_Fd8";
        default:                  return "None_Fd4";
        }
    }
    switch (s) {
    case SpatialInterp::Lag4: return "Lag4";
    case SpatialInterp::Lag6: return "Lag6";
    case SpatialInterp::Lag8: return "Lag8";
    default:                  return "None";
    }
}

QString DatasetClient::temporalInterpName(TemporalInterp t)
{
    return t == TemporalInterp::PCHIP ? "PCHIP" : "None";
}

QByteArray DatasetClient::buildSoapBody(const QString& method, const QString& dataset,
                                         double time, const QVector<double>& points) const
{
    QString pts;
    for (int i = 0; i + 2 < points.size(); i += 3)
        pts += QString("<Point3><x>%1</x><y>%2</y><z>%3</z></Point3>")
               .arg(points[i],   0, 'g', 15)
               .arg(points[i+1], 0, 'g', 15)
               .arg(points[i+2], 0, 'g', 15);

    // Element names / enum values follow the JHTDB turbulence.asmx WSDL
    return QString(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">"
        "<soap:Body>"
        "<%1 xmlns=\"%8\">"
        "<authToken>%2</authToken>"
        "<dataset>%3</dataset>"
        "<time>%4</time>"
        "<spatialInterpolation>%5</spatialInterpolation>"
        "<temporalInterpolation>%6</temporalInterpolation>"
        "<points>%7</points>"
        "</%1>"
        "</soap:Body>"
        "</soap:Envelope>")
        .arg(method)
        .arg(m_token.toHtmlEscaped())
        .arg(dataset.toHtmlEscaped())
        .arg(time, 0, 'g', 15)
        .arg(spatialInterpName(m_spatialInterp, method))
        .arg(temporalInterpName(m_temporalInterp))
        .arg(pts)
        .arg(soapNamespace(m_endpoint).toHtmlEscaped())
        .toUtf8();
}

void DatasetClient::querySoap(const QString& dataset, const QString& field,
                               double time, const QVector<double>& points)
{
    auto meta = k_fieldMeta.value(field, FieldMeta{"GetVelocity", {"x","y","z"}, 3});

    QNetworkRequest req{QUrl{m_endpoint}};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=utf-8");
    req.setRawHeader("SOAPAction",
        QString("\"%1%2\"").arg(soapNamespace(m_endpoint), meta.method).toUtf8());
    req.setRawHeader("User-Agent", "CakeCFD/1.0");

    auto* reply = m_nam->post(req, buildSoapBody(meta.method, dataset, time, points));
    reply->setProperty("dataset", dataset);
    reply->setProperty("field",   field);
    reply->setProperty("time",    time);
    reply->setProperty("isSoap", true);
    track(reply);
    connect(reply, &QNetworkReply::finished, this, &DatasetClient::onQueryReply);
}

void DatasetClient::queryCustom(const QString& dataset, const QString& field,
                                 double time, const QVector<double>& points)
{
    QJsonArray pts;
    for (int i = 0; i + 2 < points.size(); i += 3) {
        QJsonObject pt;
        pt["x"] = points[i]; pt["y"] = points[i+1]; pt["z"] = points[i+2];
        pts.append(pt);
    }
    QJsonObject body;
    body["token"]   = m_token;
    body["dataset"] = dataset;
    body["field"]   = field;
    body["time"]    = time;
    body["points"]  = pts;

    QNetworkRequest req(QUrl(m_endpoint + "/query"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Token " + m_token).toUtf8());

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson());
    reply->setProperty("dataset", dataset);
    reply->setProperty("field",   field);
    reply->setProperty("time",    time);
    reply->setProperty("isSoap", false);
    track(reply);
    connect(reply, &QNetworkReply::finished, this, &DatasetClient::onQueryReply);
}

static QString gqlSInterp(DatasetClient::SpatialInterp s)
{
    switch (s) {
    case DatasetClient::SpatialInterp::Lag4: return "lag4";
    case DatasetClient::SpatialInterp::Lag6: return "lag6";
    case DatasetClient::SpatialInterp::Lag8: return "lag8";
    default:                                 return "none";
    }
}
static QString gqlTInterp(DatasetClient::TemporalInterp t)
{
    return t == DatasetClient::TemporalInterp::PCHIP ? "pchip" : "none";
}

QByteArray DatasetClient::buildGraphQLBody(const QString& field, const QString& dataset,
                                           double time, const QVector<double>& points) const
{
    QString q = "{";
    int n = points.size() / 3;
    for (int i = 0; i < n; ++i) {
        q += QString(" p%1:getQueryPoints(queryParams:{"
                     "var:\"%2\",dataset:\"%3\","
                     "x:%4,y:%5,z:%6,t:%7,"
                     "sint:\"%8\",sop:\"field\",tint:\"%9\"})")
                 .arg(i)
                 .arg(field, dataset)
                 .arg(points[i*3+0], 0, 'g', 10)
                 .arg(points[i*3+1], 0, 'g', 10)
                 .arg(points[i*3+2], 0, 'g', 10)
                 .arg(time,          0, 'g', 10)
                 .arg(gqlSInterp(m_spatialInterp))
                 .arg(gqlTInterp(m_temporalInterp));
    }
    q += " }";
    QJsonObject body;
    body["query"] = q;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void DatasetClient::queryGraphQL(const QString& dataset, const QString& field,
                                 double time, const QVector<double>& points)
{
    QNetworkRequest req{QUrl{m_endpoint}};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Token " + m_token).toUtf8());
    req.setRawHeader("User-Agent", "CakeCFD/1.0");

    auto* reply = m_nam->post(req, buildGraphQLBody(field, dataset, time, points));
    reply->setProperty("dataset",   dataset);
    reply->setProperty("field",     field);
    reply->setProperty("time",      time);
    reply->setProperty("isSoap",    false);
    reply->setProperty("isGraphQL", true);
    reply->setProperty("nPoints",   points.size() / 3);
    track(reply);
    connect(reply, &QNetworkReply::finished, this, &DatasetClient::onQueryReply);
}

void DatasetClient::fireNextRangeStep()
{
    if (!m_rangeJob || m_rangeJob->cancelled) { delete m_rangeJob; m_rangeJob = nullptr; return; }
    double t = m_rangeJob->times[m_rangeJob->step];
    if (isSoap())         querySoap(m_rangeJob->dataset, m_rangeJob->field, t, m_rangeJob->points);
    else if (isGraphQL()) queryGraphQL(m_rangeJob->dataset, m_rangeJob->field, t, m_rangeJob->points);
    else                  queryCustom(m_rangeJob->dataset, m_rangeJob->field, t, m_rangeJob->points);
}

// reply handlers

void DatasetClient::onListReply()
{
    auto* reply  = qobject_cast<QNetworkReply*>(sender());
    bool  isGQL  = reply->property("isGraphQL").toBool();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        QString msg = reply->errorString();
        if (reply->error() == QNetworkReply::HostNotFoundError)
            msg += " - WSL fix: sudo sh -c 'echo 128.220.122.31 turbulence.idies.jhu.edu >> /etc/hosts'";
        emit error("Dataset list: " + msg); return;
    }
    QByteArray raw = reply->readAll();
    auto doc = QJsonDocument::fromJson(raw);
    QList<DatasetInfo> list;

    if (isGQL) {
        auto root = doc.object();
        if (root.contains("errors")) {
            auto errs = root["errors"].toArray();
            emit error("GraphQL: " + (errs.isEmpty()
                ? QString("unspecified error")
                : errs.first().toObject()["message"].toString()));
            return;
        }
        auto datasets = root["data"].toObject()
                            ["getQueryOptions"].toObject()
                            ["datasets"].toArray();
        for (const auto& v : datasets) {
            auto obj = v.toObject();
            DatasetInfo di;
            di.id          = obj["code"].toString();
            di.description = obj["name"].toString();
            if (!obj["description"].toString().isEmpty())
                di.description += ": " + obj["description"].toString();
            for (const auto& v : obj["physicalVariables"].toArray())
                di.fields << v.toObject()["code"].toString();
            list << di;
        }
    } else {
        for (const auto& v : doc.array()) {
            auto obj = v.toObject();
            DatasetInfo di;
            di.id          = obj["id"].toString();
            di.description = obj["description"].toString();
            for (const auto& f : obj["fields"].toArray()) di.fields << f.toString();
            di.tMin = obj["tMin"].toDouble();
            di.tMax = obj["tMax"].toDouble();
            di.dt   = obj["dt"].toDouble();
            list << di;
        }
    }
    emit datasetListReady(list);
}

void DatasetClient::onQueryReply()
{
    auto* reply  = qobject_cast<QNetworkReply*>(sender());
    QString dataset = reply->property("dataset").toString();
    QString field   = reply->property("field").toString();
    double  time    = reply->property("time").toDouble();
    bool    soap    = reply->property("isSoap").toBool();
    reply->deleteLater();
    m_inflight.removeAll(QPointer<QNetworkReply>(reply));

    // Issued before a Cancel, so drop it to protect newer results
    if (reply->property("generation").toULongLong() != m_generation) return;

    // Every failure path ends the range job so later queries are not refused
    auto fail = [this](const QString& msg) {
        emit error(msg);
        clearRangeJob();
    };

    if (reply->error() != QNetworkReply::NoError) {
        QString msg = reply->errorString();
        if (reply->error() == QNetworkReply::HostNotFoundError)
            msg += " - WSL fix: sudo sh -c 'echo 128.220.122.31 turbulence.idies.jhu.edu >> /etc/hosts'";
        fail("Query: " + msg);
        return;
    }

    QByteArray raw = reply->readAll();
    QVector<double> result;

    if (reply->property("isGraphQL").toBool()) {
        auto root = QJsonDocument::fromJson(raw).object();
        if (root.contains("errors")) {
            auto errs = root["errors"].toArray();
            fail("GraphQL: " + (errs.isEmpty()
                ? QString("unspecified error")
                : errs.first().toObject()["message"].toString()));
            return;
        }
        // Collect doubles from a JSONObject value, trying known component orderings first.
        std::function<void(const QJsonValue&, QVector<double>&)> collect;
        collect = [&](const QJsonValue& v, QVector<double>& out) {
            if (v.isDouble()) { out << v.toDouble(); return; }
            if (v.isArray()) {
                for (const auto& item : v.toArray()) collect(item, out);
                return;
            }
            if (v.isObject()) {
                auto obj = v.toObject();
                // Try canonical component keys in order
                static const QStringList ord = {"x","y","z","u","v","w","p","T"};
                QVector<double> ordered;
                for (const QString& k : ord)
                    if (obj.contains(k) && obj[k].isDouble())
                        ordered << obj[k].toDouble();
                if (!ordered.isEmpty()) { out << ordered; return; }
                // Recurse into each value (catches nested objects like {"Velocity":{...}})
                for (const auto& k : obj.keys()) collect(obj[k], out);
            }
        };
        auto data = root["data"].toObject();
        int nPts = reply->property("nPoints").toInt();
        for (int i = 0; i < nPts; ++i)
            collect(QJsonValue(data[QString("p%1").arg(i)]), result);

        if (result.isEmpty()) {
            fail("GraphQL response has no numeric data: " + QString::fromUtf8(raw).left(400));
            return;
        }
    } else if (soap) {
        // Collect only the wanted element tags from the SOAP XML response
        auto meta = k_fieldMeta.value(field, {"GetVelocity", {"x","y","z"}, 3});
        QSet<QString> wanted(meta.resultTags.begin(), meta.resultTags.end());

        QXmlStreamReader xml(raw);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                QString tag = xml.name().toString();
                if (wanted.contains(tag)) {
                    bool ok;
                    double v = xml.readElementText().toDouble(&ok);
                    if (ok) result << v;
                }
            }
        }
        if (result.isEmpty()) {
            fail("SOAP response: " + QString::fromUtf8(raw).left(400));
            return;
        }
    } else {
        // Custom API responses parse via the saved format settings
        QSettings settings("CakeCFD", "DatasetPanel");
        QString jsonPath = settings.value("customApi/jsonPath", "").toString();
        QString keysStr  = settings.value("customApi/componentKeys", "").toString();

        QString err;
        result = parseCustomJson(raw, jsonPath, keysStr.split(','), &err);
        if (!err.isEmpty()) { fail("JSON parse: " + err); return; }
    }

    // Range-job sequencing
    if (m_rangeJob && !m_rangeJob->cancelled) {
        m_rangeJob->results << result;
        ++m_rangeJob->step;
        emit queryRangeProgress(m_rangeJob->step, m_rangeJob->times.size());
        if (m_rangeJob->step < m_rangeJob->times.size()) {
            fireNextRangeStep();
        } else {
            // Detach before emitting so a slot may start a new range query
            RangeJob* job = m_rangeJob;
            m_rangeJob = nullptr;
            emit queryRangeFinished(job->dataset, job->field, job->times, job->results);
            delete job;
        }
    } else {
        clearRangeJob();
        emit queryFinished(dataset, field, time, result);
    }
}

// Walk one dotted segment as an object key, or an array index when numeric
static QJsonValue jsonStep(const QJsonValue& cur, const QString& seg)
{
    if (cur.isObject()) return cur.toObject().value(seg);
    if (cur.isArray()) {
        bool ok = false;
        int idx = seg.toInt(&ok);
        const QJsonArray arr = cur.toArray();
        return (ok && idx >= 0 && idx < arr.size()) ? arr.at(idx) : QJsonValue();
    }
    return QJsonValue();
}

QVector<double> DatasetClient::parseCustomJson(const QByteArray& raw, const QString& path,
                                               const QStringList& keysIn, QString* errOut)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError) {
        if (errOut) *errOut = err.errorString();
        return {};
    }

    QStringList keys;
    for (const QString& k : keysIn)
        if (!k.trimmed().isEmpty()) keys << k.trimmed();

    QJsonValue cur = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    for (const QString& seg : path.trimmed().split('.', Qt::SkipEmptyParts)) {
        cur = jsonStep(cur, seg);
        if (cur.isUndefined() || cur.isNull()) break;
    }

    QVector<double> result;
    if (cur.isArray()) {
        for (const QJsonValue& item : cur.toArray()) {
            if (item.isObject() && !keys.isEmpty()) {
                const QJsonObject obj = item.toObject();
                for (const QString& k : keys) {
                    // Literal key first (it may itself contain dots), then nested path
                    QJsonValue v = obj.value(k);
                    if (v.isUndefined() && k.contains('.')) {
                        v = item;
                        for (const QString& seg : k.split('.', Qt::SkipEmptyParts))
                            v = jsonStep(v, seg);
                    }
                    result << v.toDouble();
                }
            } else if (item.isDouble()) {
                result << item.toDouble();
            }
        }
    }
    if (result.isEmpty() && doc.isArray())
        for (const auto& v : doc.array()) result << v.toDouble();
    return result;
}

// turb stats

TurbStats DatasetClient::computeTurbStats(const QVector<double>& vel,
                                           double Lref, double Cmu)
{
    TurbStats s;
    int n = vel.size() / 3;
    if (n == 0) return s;

    double sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < n; ++i) { sx += vel[i*3]; sy += vel[i*3+1]; sz += vel[i*3+2]; }
    double Ux = sx/n, Uy = sy/n, Uz = sz/n;
    s.Umag = std::sqrt(Ux*Ux + Uy*Uy + Uz*Uz);
    s.Umean = {Ux, Uy, Uz};

    double vx = 0, vy = 0, vz = 0;
    for (int i = 0; i < n; ++i) {
        double dx = vel[i*3]-Ux, dy = vel[i*3+1]-Uy, dz = vel[i*3+2]-Uz;
        vx += dx*dx; vy += dy*dy; vz += dz*dz;
    }
    if (n > 1) { vx /= (n-1); vy /= (n-1); vz /= (n-1); }

    s.k = 0.5 * (vx + vy + vz);
    s.I = (s.Umag > 0) ? std::sqrt(2.0*s.k/3.0) / s.Umag : 0.0;
    if (Lref > 0 && s.k > 0)
        s.epsilon = std::pow(Cmu, 0.75) * std::pow(s.k, 1.5) / Lref;
    if (s.epsilon > 0 && s.k > 0)
        s.omega = s.epsilon / (Cmu * s.k);
    return s;
}

} // namespace CFD

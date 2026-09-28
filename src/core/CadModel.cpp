#include "CadModel.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QUrl>
#include <QtMath>

#include <cstring>

namespace {

QVector3D vec(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()));
}

QString localPath(const QString &s)
{
    if (s.startsWith(QLatin1String("qrc:")))
        return s.mid(3);
    if (s.startsWith(QLatin1String("file:")))
        return QUrl(s).toLocalFile();
    return s;
}

QMatrix4x4 aboutPoint(const QVector3D &p, const QVector3D &axis, double rad)
{
    QMatrix4x4 m;
    m.translate(p);
    m.rotate(float(qRadiansToDegrees(rad)), axis);
    m.translate(-p);
    return m;
}

struct V2 {
    double x, y;
};
double cross2(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
double dot2(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
V2 sub(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
double len2(V2 a) { return std::sqrt(dot2(a, a)); }

} // namespace

CadModel::CadModel(QObject *parent)
    : QObject(parent)
{
}

void CadModel::unload()
{
    m_meshes.clear();
    m_joints.clear();
    m_links.clear();
    m_name.clear();
    m_folder.clear();
    m_triangles = 0;
    emit loadedChanged();
}

bool CadModel::load(const QString &folderIn)
{
    const QString folder = localPath(folderIn);
    auto fail = [this](const QString &e) {
        m_error = e;
        emit loadedChanged();
        return false;
    };
    QFile jf(folder + QStringLiteral("/rig.json"));
    if (!jf.open(QIODevice::ReadOnly))
        return fail(tr("Không mở được %1").arg(jf.fileName()));
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(jf.readAll(), &perr);
    if (doc.isNull())
        return fail(tr("rig.json lỗi: %1").arg(perr.errorString()));
    const QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("format")).toString().startsWith(QLatin1String("cyberhand-rig-")))
        return fail(tr("rig.json không phải định dạng cyberhand-rig"));

    QStringList links;
    for (const QJsonValue &v : root.value(QStringLiteral("links")).toArray())
        links << v.toString();
    const QJsonArray meshDefs = root.value(QStringLiteral("meshes")).toArray();

    QFile bf(folder + QLatin1Char('/') + root.value(QStringLiteral("mesh")).toString(QStringLiteral("hand.bin")));
    if (!bf.open(QIODevice::ReadOnly))
        return fail(tr("Không mở được %1").arg(bf.fileName()));
    const QByteArray bin = bf.readAll();
    qsizetype pos = 0;
    auto readU32 = [&](quint32 *v) {
        if (pos + 4 > bin.size())
            return false;
        std::memcpy(v, bin.constData() + pos, 4);
        pos += 4;
        return true;
    };
    quint32 version = 0, count = 0;
    if (bin.left(4) != "CHMB")
        return fail(tr("hand.bin sai định dạng"));
    pos = 4;
    if (!readU32(&version) || !readU32(&count) || int(count) != meshDefs.size())
        return fail(tr("hand.bin không khớp rig.json"));

    QVector<Mesh> meshes;
    int tris = 0;
    for (quint32 i = 0; i < count; ++i) {
        quint32 nv = 0, ni = 0;
        if (!readU32(&nv) || !readU32(&ni))
            return fail(tr("hand.bin bị cắt"));
        const qsizetype vbytes = qsizetype(nv) * 24, ibytes = qsizetype(ni) * 4;
        if (pos + vbytes + ibytes > bin.size())
            return fail(tr("hand.bin bị cắt"));
        Mesh m;
        const QJsonObject d = meshDefs.at(int(i)).toObject();
        m.link = int(links.indexOf(d.value(QStringLiteral("link")).toString()));
        if (m.link < 0)
            return fail(tr("Mesh %1: khâu \"%2\" không có trong links").arg(i).arg(d.value(QStringLiteral("link")).toString()));
        m.color = QColor(d.value(QStringLiteral("color")).toString(QStringLiteral("#D6D6D6")));
        m.vertices = bin.mid(pos, vbytes);
        pos += vbytes;
        m.indices = bin.mid(pos, ibytes);
        pos += ibytes;
        m.vertexCount = int(nv);
        m.indexCount = int(ni);
        const float *f = reinterpret_cast<const float *>(m.vertices.constData());
        QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
        for (quint32 k = 0; k < nv; ++k) {
            const QVector3D p(f[k * 6], f[k * 6 + 1], f[k * 6 + 2]);
            lo = QVector3D(qMin(lo.x(), p.x()), qMin(lo.y(), p.y()), qMin(lo.z(), p.z()));
            hi = QVector3D(qMax(hi.x(), p.x()), qMax(hi.y(), p.y()), qMax(hi.z(), p.z()));
        }
        m.lo = lo;
        m.hi = hi;
        tris += int(ni / 3);
        meshes.append(m);
    }

    QVector<Joint> joints;
    QHash<QString, int> jointIndex;
    const QJsonArray jdefs = root.value(QStringLiteral("joints")).toArray();
    for (const QJsonValue &v : jdefs) {
        const QJsonObject o = v.toObject();
        Joint j;
        j.key = o.value(QStringLiteral("key")).toString();
        j.fourbar = o.value(QStringLiteral("type")).toString() == QLatin1String("fourbar");
        j.axis = vec(o.value(QStringLiteral("axis"))).normalized();
        if (j.fourbar) {
            j.A = vec(o.value(QStringLiteral("A")));
            j.B = vec(o.value(QStringLiteral("B")));
            j.C = vec(o.value(QStringLiteral("C")));
            j.D = vec(o.value(QStringLiteral("D")));
            j.input = int(links.indexOf(o.value(QStringLiteral("input")).toString()));
            j.coupler = int(links.indexOf(o.value(QStringLiteral("coupler")).toString()));
            j.follower = int(links.indexOf(o.value(QStringLiteral("follower")).toString()));
            if (j.input < 0 || j.coupler < 0)
                return fail(tr("Khớp %1: thiếu khâu").arg(j.key));
        } else {
            j.A = vec(o.value(QStringLiteral("point")));
            const int l = int(links.indexOf(o.value(QStringLiteral("link")).toString()));
            if (l < 0)
                return fail(tr("Khớp %1: thiếu khâu").arg(j.key));
            j.links << l;
            for (const QJsonValue &c : o.value(QStringLiteral("children")).toArray()) {
                const int ci = int(links.indexOf(c.toString()));
                if (ci >= 0)
                    j.links << ci;
            }
        }
        const QString parent = o.value(QStringLiteral("parent")).toString();
        j.parent = parent.isEmpty() ? -1 : jointIndex.value(parent, -1);
        jointIndex.insert(j.key, joints.size());
        joints.append(j);
    }

    const QJsonObject view = root.value(QStringLiteral("view")).toObject();
    m_viewEuler = vec(view.value(QStringLiteral("eulerDeg")));
    m_viewPos = vec(view.value(QStringLiteral("position")));
    m_links = links;
    m_meshes = meshes;
    m_joints = joints;
    m_triangles = tris;
    m_name = root.value(QStringLiteral("name")).toString(QFileInfo(folder).fileName());
    m_folder = folderIn;
    m_error.clear();
    emit loadedChanged();
    return true;
}

int CadModel::meshLink(int mesh) const
{
    return (mesh >= 0 && mesh < m_meshes.size()) ? m_meshes[mesh].link : 0;
}

QColor CadModel::meshColor(int mesh) const
{
    return (mesh >= 0 && mesh < m_meshes.size()) ? m_meshes[mesh].color : QColor(0xD6, 0xD6, 0xD6);
}

bool CadModel::fourbar(const Joint &j, double theta, QMatrix4x4 *in, QMatrix4x4 *cp, QMatrix4x4 *fo)
{
    const QVector3D ax = j.axis;
    const QVector3D t = qAbs(ax.x()) < 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
    const QVector3D e1 = QVector3D::crossProduct(ax, t).normalized();
    const QVector3D e2 = QVector3D::crossProduct(ax, e1);
    auto P = [&](const QVector3D &v) { return V2{double(QVector3D::dotProduct(v, e1)), double(QVector3D::dotProduct(v, e2))}; };

    const QMatrix4x4 mIn = aboutPoint(j.A, ax, theta);
    const QVector3D Bp = mIn.map(j.B);
    const V2 b = P(Bp), d = P(j.D), b0 = P(j.B), c0 = P(j.C);
    const double r1 = len2(sub(c0, b0)), r2 = len2(sub(c0, d));
    const double s0 = cross2(sub(d, b0), sub(c0, b0));
    // circle(b, r1) & circle(d, r2)
    const V2 dv = sub(d, b);
    const double dist = len2(dv);
    if (dist < 1e-9)
        return false;
    const double a = (r1 * r1 - r2 * r2 + dist * dist) / (2 * dist);
    const double h2 = r1 * r1 - a * a;
    if (h2 < 0)
        return false;
    const double h = std::sqrt(h2);
    const V2 m{b.x + a * dv.x / dist, b.y + a * dv.y / dist};
    const V2 perp{-dv.y / dist, dv.x / dist};
    V2 c{m.x + h * perp.x, m.y + h * perp.y};
    if ((cross2(sub(d, b), sub(c, b)) >= 0) != (s0 >= 0))
        c = V2{m.x - h * perp.x, m.y - h * perp.y};
    const V2 u1 = sub(c0, b0), v1 = sub(c, b);
    const double psi = std::atan2(cross2(u1, v1), dot2(u1, v1));
    const V2 u2 = sub(c0, d), v2 = sub(c, d);
    const double phi = std::atan2(cross2(u2, v2), dot2(u2, v2));

    *in = mIn;
    QMatrix4x4 mc;
    mc.translate(Bp);
    mc.rotate(float(qRadiansToDegrees(psi)), ax);
    mc.translate(-j.B);
    *cp = mc;
    *fo = aboutPoint(j.D, ax, phi);
    return true;
}

QVector<QMatrix4x4> CadModel::solve(const QHash<QString, double> &deg) const
{
    QVector<QMatrix4x4> out(m_links.size());
    QVector<QMatrix4x4> jointM(m_joints.size());
    for (int k = 0; k < m_joints.size(); ++k) {
        const Joint &j = m_joints[k];
        const QMatrix4x4 parent = j.parent >= 0 ? jointM[j.parent] : QMatrix4x4();
        double th = qDegreesToRadians(deg.value(j.key, 0.0));
        if (!j.fourbar) {
            jointM[k] = parent * aboutPoint(j.A, j.axis, th);
            for (int l : j.links)
                out[l] = jointM[k];
            continue;
        }
        QMatrix4x4 in, cp, fo;
        // outside the reachable range: back off toward 0 until the chain closes
        const double step = qDegreesToRadians(0.5) * (th > 0 ? -1 : 1);
        bool ok = false;
        for (int n = 0; n < 720 && !ok; ++n) {
            ok = fourbar(j, th, &in, &cp, &fo);
            if (!ok)
                th += step;
        }
        if (!ok)
            continue;
        jointM[k] = parent * in;
        out[j.input] = parent * in;
        out[j.coupler] = parent * cp;
        if (j.follower >= 0)
            out[j.follower] = parent * fo;
    }
    return out;
}

// ---------------------------------------------------------------------------

CadPose::CadPose(QObject *parent)
    : QObject(parent)
{
}

void CadPose::setModel(CadModel *m)
{
    if (m == m_model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (m_model)
        connect(m_model, &CadModel::loadedChanged, this, &CadPose::update);
    emit modelChanged();
    update();
}

void CadPose::setAngles(const QVariantList &a)
{
    m_angles = a;
    emit anglesChanged();
    update();
}

void CadPose::setKeys(const QStringList &k)
{
    m_keys = k;
    emit anglesChanged();
    update();
}

void CadPose::update()
{
    m_rot.clear();
    m_pos.clear();
    if (m_model && m_model->loaded()) {
        QHash<QString, double> deg;
        for (int i = 0; i < m_keys.size() && i < m_angles.size(); ++i) {
            const double v = m_angles[i].toDouble();
            deg.insert(m_keys[i], qIsNaN(v) ? 0.0 : v);
        }
        const QVector<QMatrix4x4> ms = m_model->solve(deg);
        for (const QMatrix4x4 &m : ms) {
            QMatrix3x3 r;
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    r(row, col) = m(row, col);
            m_rot << QVariant::fromValue(QQuaternion::fromRotationMatrix(r));
            m_pos << QVariant::fromValue(m.column(3).toVector3D());
        }
    }
    emit poseChanged();
}

// ---------------------------------------------------------------------------

CadGeometry::CadGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
}

void CadGeometry::setModel(CadModel *m)
{
    if (m == m_model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (m_model)
        connect(m_model, &CadModel::loadedChanged, this, &CadGeometry::rebuild);
    emit modelChanged();
    rebuild();
}

void CadGeometry::setMesh(int i)
{
    if (i == m_mesh)
        return;
    m_mesh = i;
    emit meshChanged();
    rebuild();
}

void CadGeometry::rebuild()
{
    clear();
    const CadModel::Mesh *m = m_model ? m_model->mesh(m_mesh) : nullptr;
    if (m) {
        setStride(24);
        setPrimitiveType(PrimitiveType::Triangles);
        addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
        addAttribute(Attribute::NormalSemantic, 12, Attribute::F32Type);
        addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
        setVertexData(m->vertices);
        setIndexData(m->indices);
        setBounds(m->lo, m->hi);
    }
    update();
}

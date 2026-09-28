#pragma once

#include <QByteArray>
#include <QColor>
#include <QMatrix4x4>
#include <QObject>
#include <QVariantList>
#include <QVector3D>
#include <QVector>

#include <QtQuick3D/QQuick3DGeometry>

// Hand model converted from CAD (tools/cad/step_to_rig.py): rig.json + hand.bin.
// Meshes are in CAD coordinates (mm); each belongs to one rigid link. Joints move
// links: "revolute" (one link + children about a pin) or "fourbar" (input link about
// pin A, coupler about B, follower about D, closed through C), as in the fingers of
// the OpenGrasp-Lite / CyberHand mechanics.
class CadModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString name READ name NOTIFY loadedChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY loadedChanged)
    Q_PROPERTY(int meshCount READ meshCount NOTIFY loadedChanged)
    Q_PROPERTY(int linkCount READ linkCount NOTIFY loadedChanged)
    Q_PROPERTY(int triangles READ triangles NOTIFY loadedChanged)
    Q_PROPERTY(QVector3D viewEuler READ viewEuler NOTIFY loadedChanged)
    Q_PROPERTY(QVector3D viewPosition READ viewPosition NOTIFY loadedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY loadedChanged)

public:
    struct Mesh {
        int link = 0;
        QColor color;
        QByteArray vertices; // float32 x y z nx ny nz
        QByteArray indices;  // uint32
        int vertexCount = 0;
        int indexCount = 0;
        QVector3D lo, hi;
    };
    struct Joint {
        QString key;
        bool fourbar = false;
        int parent = -1;       // index of the joint whose transform applies first (thumb T1 <- T2)
        QVector3D axis;        // positive = flexion / opposition
        QVector3D A, B, C, D;  // four-bar pins (revolute: A = point on the axis)
        int input = -1, coupler = -1, follower = -1; // links
        QVector<int> links;    // revolute: moved links
    };

    explicit CadModel(QObject *parent = nullptr);

    bool loaded() const { return !m_meshes.isEmpty(); }
    QString name() const { return m_name; }
    QString folder() const { return m_folder; }
    int meshCount() const { return m_meshes.size(); }
    int linkCount() const { return m_links.size(); }
    int triangles() const { return m_triangles; }
    QVector3D viewEuler() const { return m_viewEuler; }
    QVector3D viewPosition() const { return m_viewPos; }
    QString lastError() const { return m_error; }

    // folder with rig.json (local path, file:// URL or qrc path)
    Q_INVOKABLE bool load(const QString &folder);
    Q_INVOKABLE void unload();
    Q_INVOKABLE int meshLink(int mesh) const;
    Q_INVOKABLE QColor meshColor(int mesh) const;
    const Mesh *mesh(int i) const { return (i >= 0 && i < m_meshes.size()) ? &m_meshes[i] : nullptr; }
    const QVector<Joint> &joints() const { return m_joints; }
    QStringList links() const { return m_links; }

    // one transform per link (x' = M x, CAD coordinates) for joint angles in degrees,
    // keyed like the hand joints (T2, T1, I, M, R, P); missing keys = 0
    QVector<QMatrix4x4> solve(const QHash<QString, double> &deg) const;
    // four-bar: transforms of input / coupler / follower, false if the chain cannot close
    static bool fourbar(const Joint &j, double thetaRad, QMatrix4x4 *in, QMatrix4x4 *cp, QMatrix4x4 *fo);

signals:
    void loadedChanged();

private:
    QString m_name, m_folder, m_error;
    QStringList m_links;
    QVector<Mesh> m_meshes;
    QVector<Joint> m_joints;
    QVector3D m_viewEuler, m_viewPos;
    int m_triangles = 0;
};

// One posed instance of the model (the solid hand, the target ghost).
class CadPose : public QObject
{
    Q_OBJECT
    Q_PROPERTY(CadModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QVariantList angles READ angles WRITE setAngles NOTIFY anglesChanged)
    Q_PROPERTY(QStringList keys READ keys WRITE setKeys NOTIFY anglesChanged)
    Q_PROPERTY(QVariantList rotations READ rotations NOTIFY poseChanged)
    Q_PROPERTY(QVariantList positions READ positions NOTIFY poseChanged)

public:
    explicit CadPose(QObject *parent = nullptr);
    CadModel *model() const { return m_model; }
    void setModel(CadModel *m);
    QVariantList angles() const { return m_angles; }
    void setAngles(const QVariantList &a);
    QStringList keys() const { return m_keys; }
    void setKeys(const QStringList &k);
    QVariantList rotations() const { return m_rot; }
    QVariantList positions() const { return m_pos; }

signals:
    void modelChanged();
    void anglesChanged();
    void poseChanged();

private:
    void update();
    CadModel *m_model = nullptr;
    QVariantList m_angles;
    QStringList m_keys = {QStringLiteral("T2"), QStringLiteral("T1"), QStringLiteral("I"),
                          QStringLiteral("M"), QStringLiteral("R"), QStringLiteral("P")};
    QVariantList m_rot, m_pos;
};

// GPU geometry of one mesh of a CadModel.
class CadGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    Q_PROPERTY(CadModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int mesh READ mesh WRITE setMesh NOTIFY meshChanged)

public:
    explicit CadGeometry(QQuick3DObject *parent = nullptr);
    CadModel *model() const { return m_model; }
    void setModel(CadModel *m);
    int mesh() const { return m_mesh; }
    void setMesh(int i);

signals:
    void modelChanged();
    void meshChanged();

private:
    void rebuild();
    CadModel *m_model = nullptr;
    int m_mesh = -1;
};

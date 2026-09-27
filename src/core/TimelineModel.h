#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QVector>

class HandModel;
class Trajectory;

// Timeline: one track per joint. A clip is either a "move" (from the joint's value at
// the clip start to a target, with an easing curve) or a parametric curve that sets
// the joint directly: hold, linear ramp, sine / square / triangle / sawtooth wave,
// power curve. After a clip the joint holds its last value. Compiles to a Trajectory.
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int trackCount READ trackCount CONSTANT)
    Q_PROPERTY(double length READ length WRITE setLength NOTIFY lengthChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY nameChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(QStringList easingNames READ easingNames CONSTANT)
    Q_PROPERTY(QStringList kindNames READ kindNames CONSTANT)
    Q_PROPERTY(QStringList shapeNames READ shapeNames CONSTANT)
    Q_PROPERTY(QVariantList library READ library CONSTANT)
    Q_PROPERTY(QVariantList customClips READ customClips NOTIFY customChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Easing { Linear = 0, Smooth, EaseIn, EaseOut, Step };
    Q_ENUM(Easing)
    enum Kind { Move = 0, Hold, Ramp, Sine, Square, Triangle, Sawtooth, Power, KindCount };
    Q_ENUM(Kind)
    enum Shape { ShapeIn = 0, ShapeOut, ShapeInOut, ShapeOutIn };
    enum Roles {
        TrackRole = Qt::UserRole + 1, StartRole, DurationRole, TargetRole, EasingRole, FromRole,
        KindRole, ARole, BRole, PeriodRole, ShiftRole, ExponentRole, ShapeRole, EndValueRole, CurveRole
    };

    struct Clip {
        int track = 0;
        double start = 0;
        double duration = 1;
        int kind = Move;
        double target = 0;   // Move
        int easing = Smooth; // Move
        double a = 0;        // Hold value / Ramp,Power start / wave trough
        double b = 90;       // Ramp,Power end / wave crest
        double period = 1;   // waves, s
        double shift = 0;    // waves, s
        double exponent = 2; // Power
        int shape = ShapeIn; // Power
    };

    TimelineModel(HandModel *hand, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int trackCount() const;
    double length() const { return m_length; }
    void setLength(double l);
    QString name() const { return m_name; }
    void setName(const QString &n);
    QString filePath() const { return m_path; }
    int count() const { return m_clips.size(); }
    int selected() const { return m_selected; }
    void setSelected(int s);
    QStringList easingNames() const;
    QStringList kindNames() const;
    QStringList shapeNames() const;
    QVariantList library() const;
    QVariantList customClips() const { return m_custom; }
    QString lastError() const { return m_error; }

    Q_INVOKABLE int addClip(int track, double start, double duration, double target, int easing = Smooth);
    // add from the clip library ("hold", "rampUp", "sine", ...) or a custom clip ("custom:<i>")
    Q_INVOKABLE int addPreset(int track, double start, const QString &key);
    Q_INVOKABLE int duplicateClip(int row);
    Q_INVOKABLE void removeClip(int row);
    Q_INVOKABLE void removeSelected() { removeClip(m_selected); }
    // move/resize keep clips inside [0, length] and away from neighbours on the same track
    Q_INVOKABLE void moveClip(int row, double start);
    Q_INVOKABLE void resizeClip(int row, double duration);
    Q_INVOKABLE void setClipEnd(int row, double end);
    Q_INVOKABLE bool setClipTrack(int row, int track);
    Q_INVOKABLE void setClipTarget(int row, double target);
    Q_INVOKABLE void setClipEasing(int row, int easing);
    // kind, a, b, period, shift, exponent, shape, target, easing
    Q_INVOKABLE void setClipParam(int row, const QString &param, double value);
    Q_INVOKABLE QVariantMap clip(int row) const;
    Q_INVOKABLE QVariantList clipCurve(int row, int points = 60) const;
    Q_INVOKABLE QString formula(int row) const;
    Q_INVOKABLE double valueAt(int track, double t) const;
    Q_INVOKABLE double freeSpaceAfter(int track, double t) const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void makeDemo();
    Q_INVOKABLE bool saveJson(const QString &pathOrUrl);
    Q_INVOKABLE bool loadJson(const QString &pathOrUrl);
    Q_INVOKABLE void compileInto(Trajectory *out, double rate = 50.0) const;

    Q_INVOKABLE void saveCustom(int row, const QString &name);
    Q_INVOKABLE void removeCustom(int index);
    QJsonArray customToJson() const;
    void customFromJson(const QJsonArray &a);

    static double ease(int easing, double a);
    // value of a clip at local time tl (0..duration); `from` = joint value at clip start
    static double evalClip(const Clip &c, double tl, double from);
    static QJsonObject clipToJson(const Clip &c);
    static Clip clipFromJson(const QJsonObject &o, bool *ok = nullptr);

signals:
    void lengthChanged();
    void nameChanged();
    void countChanged();
    void selectedChanged();
    void changed();
    void lastErrorChanged();
    void customChanged();

private:
    void neighbours(int row, double *lo, double *hi) const;
    void emitRow(int row);
    int insertClip(Clip c);
    double fromValue(int row) const;
    Clip presetClip(const QString &key, int track, bool *ok) const;

    HandModel *m_hand;
    QVector<Clip> m_clips;
    double m_length = 6.0;
    QString m_name = QStringLiteral("untitled");
    QString m_path;
    int m_selected = -1;
    QString m_error;
    QVariantList m_custom; // {name, clip:{...json}}
};

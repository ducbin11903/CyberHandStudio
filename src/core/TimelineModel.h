#pragma once

#include <QAbstractListModel>
#include <QVector>

class HandModel;
class Trajectory;

// Keyframe-style timeline: one track per joint, clips move the joint to a target
// angle over the clip duration with an easing curve. Compiles to a Trajectory.
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
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Easing { Linear = 0, Smooth, EaseIn, EaseOut, Step };
    Q_ENUM(Easing)
    enum Roles { TrackRole = Qt::UserRole + 1, StartRole, DurationRole, TargetRole, EasingRole, FromRole };

    struct Clip {
        int track = 0;
        double start = 0;
        double duration = 1;
        double target = 0;
        int easing = Smooth;
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
    QString lastError() const { return m_error; }

    Q_INVOKABLE int addClip(int track, double start, double duration, double target, int easing = Smooth);
    Q_INVOKABLE void removeClip(int row);
    Q_INVOKABLE void removeSelected() { removeClip(m_selected); }
    // move/resize keep clips inside [0, length] and away from neighbours on the same track
    Q_INVOKABLE void moveClip(int row, double start);
    Q_INVOKABLE void resizeClip(int row, double duration);
    Q_INVOKABLE void setClipTarget(int row, double target);
    Q_INVOKABLE void setClipEasing(int row, int easing);
    Q_INVOKABLE QVariantMap clip(int row) const;
    Q_INVOKABLE double valueAt(int track, double t) const;
    Q_INVOKABLE double freeSpaceAfter(int track, double t) const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void makeDemo();
    Q_INVOKABLE bool saveJson(const QString &pathOrUrl);
    Q_INVOKABLE bool loadJson(const QString &pathOrUrl);
    Q_INVOKABLE void compileInto(Trajectory *out, double rate = 50.0) const;

    static double ease(int easing, double a);

signals:
    void lengthChanged();
    void nameChanged();
    void countChanged();
    void selectedChanged();
    void changed();
    void lastErrorChanged();

private:
    void neighbours(int row, double *lo, double *hi) const;
    void emitRow(int row);

    HandModel *m_hand;
    QVector<Clip> m_clips;
    double m_length = 6.0;
    QString m_name = QStringLiteral("untitled");
    QString m_path;
    int m_selected = -1;
    QString m_error;
};

#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QTimer>
#include <QVector>

class HandModel;

// Game controller input (SDL2) mapped to joints: Motion (velocity), Position (absolute),
// Pose (recall a saved pose) or Command (send a CLI line).
class GamepadInput : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY connectedChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool output READ output WRITE setOutput NOTIFY settingsChanged)
    Q_PROPERTY(double sendRate READ sendRate WRITE setSendRate NOTIFY settingsChanged)
    Q_PROPERTY(double deadzone READ deadzone WRITE setDeadzone NOTIFY settingsChanged)
    Q_PROPERTY(QStringList actionNames READ actionNames CONSTANT)
    Q_PROPERTY(QStringList groups READ groups CONSTANT)
    Q_PROPERTY(QVariantMap live READ live NOTIFY liveChanged)

public:
    enum Action { None = 0, Motion, Position, Pose, Command };
    Q_ENUM(Action)
    enum Roles { InputRole = Qt::UserRole + 1, LabelRole, GroupRole, KindRole, ActionRole, JointRole,
                 PoseRole, CommandRole, GainRole, InvertRole, ValueRole };

    explicit GamepadInput(HandModel *hand, QObject *parent = nullptr);
    ~GamepadInput() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const;
    bool connected() const { return m_connected; }
    QString deviceName() const { return m_deviceName; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool e);
    bool output() const { return m_output; }
    void setOutput(bool o) { m_output = o; emit settingsChanged(); }
    double sendRate() const { return m_rate; }
    void setSendRate(double r);
    double deadzone() const { return m_deadzone; }
    void setDeadzone(double d) { m_deadzone = qBound(0.0, d, 0.9); emit settingsChanged(); }
    QStringList actionNames() const;
    QStringList groups() const;
    QVariantMap live() const;

    Q_INVOKABLE void setAction(int row, int action);
    Q_INVOKABLE void setJoint(int row, int joint);
    Q_INVOKABLE void setPose(int row, const QString &pose);
    Q_INVOKABLE void setCommand(int row, const QString &cmd);
    Q_INVOKABLE void setGain(int row, double gain);
    Q_INVOKABLE void setInvert(int row, bool inv);
    Q_INVOKABLE void resetMapping();
    // Continue Motion inputs from the hand's current command (after a pose jump).
    Q_INVOKABLE void syncFromHand();
    // Simulated input (UI testing without a controller): value -1..1 or 0/1.
    Q_INVOKABLE void simulate(const QString &input, double value);

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &a);

signals:
    void connectedChanged();
    void enabledChanged();
    void settingsChanged();
    void liveChanged();
    void forward(const QVector<double> &deg);
    void poseRequested(const QString &name);
    void commandRequested(const QString &line);

private:
    struct Entry {
        QString input;   // id, e.g. "LX"
        QString label;   // "L - Left/Right"
        QString group;   // Buttons, D-Pad, Joysticks, Triggers
        int kind;        // 0 axis (-1..1), 1 trigger (0..1), 2 button
        int action = None;
        int joint = 0;
        QString pose;
        QString command;
        double gain = 90; // deg/s at full deflection for Motion
        bool invert = false;
        double value = 0;
        bool pressed = false;
    };
    void poll();
    void step(double dt);
    void openFirst();
    void closeDevice();
    void setValue(int row, double v);

    HandModel *m_hand;
    QVector<Entry> m_entries;
    QTimer m_timer;
    bool m_connected = false;
    QString m_deviceName;
    bool m_enabled = false;
    bool m_output = true;
    double m_rate = 50;
    double m_deadzone = 0.12;
    double m_sinceSend = 0;
    bool m_dirty = false;
    QVector<double> m_cmd;
    void *m_dev = nullptr;
    int m_rescan = 0;
    bool m_sdlOk = false;
};

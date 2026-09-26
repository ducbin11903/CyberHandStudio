#include "GamepadInput.h"

#include "HandModel.h"

#include <QJsonObject>

#ifdef CH_HAVE_SDL2
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

namespace {
struct Def {
    const char *id;
    const char *label;
    const char *group;
    int kind;
};
const Def kDefs[] = {
    {"A", "A", "Buttons", 2},
    {"B", "B", "Buttons", 2},
    {"X", "X", "Buttons", 2},
    {"Y", "Y", "Buttons", 2},
    {"LB", "LB", "Buttons", 2},
    {"RB", "RB", "Buttons", 2},
    {"Back", "Back", "Buttons", 2},
    {"Start", "Start", "Buttons", 2},
    {"DUp", "Up", "D-Pad", 2},
    {"DDown", "Down", "D-Pad", 2},
    {"DLeft", "Left", "D-Pad", 2},
    {"DRight", "Right", "D-Pad", 2},
    {"LX", "L - Left/Right", "Joysticks", 0},
    {"LY", "L - Up/Down", "Joysticks", 0},
    {"LS", "L - Press", "Joysticks", 2},
    {"RX", "R - Left/Right", "Joysticks", 0},
    {"RY", "R - Up/Down", "Joysticks", 0},
    {"RS", "R - Press", "Joysticks", 2},
    {"LT", "LT", "Triggers", 1},
    {"RT", "RT", "Triggers", 1},
};
} // namespace

GamepadInput::GamepadInput(HandModel *hand, QObject *parent)
    : QAbstractListModel(parent), m_hand(hand)
{
    for (const Def &d : kDefs) {
        Entry e;
        e.input = QString::fromLatin1(d.id);
        e.label = QString::fromLatin1(d.label);
        e.group = QString::fromLatin1(d.group);
        e.kind = d.kind;
        m_entries.append(e);
    }
    resetMapping();
    m_cmd = QVector<double>(hand->count(), 0.0);

#ifdef CH_HAVE_SDL2
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    m_sdlOk = SDL_Init(SDL_INIT_GAMECONTROLLER) == 0;
#endif
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, &GamepadInput::poll);
    m_timer.start();
}

GamepadInput::~GamepadInput()
{
    closeDevice();
#ifdef CH_HAVE_SDL2
    if (m_sdlOk)
        SDL_Quit();
#endif
}

bool GamepadInput::available() const
{
#ifdef CH_HAVE_SDL2
    return m_sdlOk;
#else
    return false;
#endif
}

int GamepadInput::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant GamepadInput::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries[index.row()];
    switch (role) {
    case InputRole: return e.input;
    case LabelRole: return e.label;
    case GroupRole: return e.group;
    case KindRole: return e.kind;
    case ActionRole: return e.action;
    case JointRole: return e.joint;
    case PoseRole: return e.pose;
    case CommandRole: return e.command;
    case GainRole: return e.gain;
    case InvertRole: return e.invert;
    case ValueRole: return e.value;
    }
    return {};
}

QHash<int, QByteArray> GamepadInput::roleNames() const
{
    return {{InputRole, "input"}, {LabelRole, "label"}, {GroupRole, "group"}, {KindRole, "kind"},
            {ActionRole, "action"}, {JointRole, "joint"}, {PoseRole, "pose"}, {CommandRole, "command"},
            {GainRole, "gain"}, {InvertRole, "invert"}, {ValueRole, "value"}};
}

QStringList GamepadInput::actionNames() const
{
    return {QStringLiteral("None"), QStringLiteral("Motion"), QStringLiteral("Position"),
            QStringLiteral("Pose"), QStringLiteral("Command")};
}

QStringList GamepadInput::groups() const
{
    return {QStringLiteral("Buttons"), QStringLiteral("D-Pad"), QStringLiteral("Joysticks"), QStringLiteral("Triggers")};
}

QVariantMap GamepadInput::live() const
{
    QVariantMap m;
    for (const Entry &e : m_entries)
        m.insert(e.input, e.value);
    return m;
}

void GamepadInput::setEnabled(bool e)
{
    if (e == m_enabled)
        return;
    m_enabled = e;
    if (e) {
        // start from where the hand is now
        m_cmd = m_hand->previewVector();
    }
    emit enabledChanged();
}

void GamepadInput::setSendRate(double r)
{
    m_rate = qBound(1.0, r, 200.0);
    emit settingsChanged();
}

void GamepadInput::setAction(int row, int action)
{
    if (row < 0 || row >= m_entries.size())
        return;
    m_entries[row].action = qBound(0, action, 4);
    emit dataChanged(index(row), index(row));
}

void GamepadInput::setJoint(int row, int joint)
{
    if (row < 0 || row >= m_entries.size())
        return;
    m_entries[row].joint = qBound(0, joint, m_hand->count() - 1);
    emit dataChanged(index(row), index(row));
}

void GamepadInput::setPose(int row, const QString &pose)
{
    if (row < 0 || row >= m_entries.size())
        return;
    m_entries[row].pose = pose;
    emit dataChanged(index(row), index(row));
}

void GamepadInput::setCommand(int row, const QString &cmd)
{
    if (row < 0 || row >= m_entries.size())
        return;
    m_entries[row].command = cmd;
    emit dataChanged(index(row), index(row));
}

void GamepadInput::setGain(int row, double gain)
{
    if (row < 0 || row >= m_entries.size())
        return;
    m_entries[row].gain = gain;
    emit dataChanged(index(row), index(row));
}

void GamepadInput::setInvert(int row, bool inv)
{
    if (row < 0 || row >= m_entries.size())
        return;
    m_entries[row].invert = inv;
    emit dataChanged(index(row), index(row));
}

void GamepadInput::resetMapping()
{
    auto set = [&](const char *id, int action, int joint, const QString &pose = QString(), const QString &cmd = QString()) {
        for (Entry &e : m_entries) {
            if (e.input == QLatin1String(id)) {
                e.action = action;
                e.joint = joint;
                e.pose = pose;
                e.command = cmd;
            }
        }
    };
    for (Entry &e : m_entries) {
        e.action = None;
        e.joint = 0;
        e.gain = 90;
        e.invert = false;
        e.pose.clear();
        e.command.clear();
    }
    set("LX", Motion, 0);                              // thumb rotation
    set("LY", Motion, 1);                              // thumb flexion
    set("LS", Pose, 0, QStringLiteral("Open"));
    set("RX", Motion, 2);                              // index
    set("RY", Motion, 3);                              // middle
    set("RS", Pose, 0, QStringLiteral("Fist"));
    set("LT", Position, 4);                            // ring
    set("RT", Position, 5);                            // pinky
    set("A", Pose, 0, QStringLiteral("Pinch"));
    set("B", Pose, 0, QStringLiteral("Open"));
    set("X", Pose, 0, QStringLiteral("Point"));
    set("Y", Pose, 0, QStringLiteral("Peace"));
    set("Start", Command, 0, QString(), QStringLiteral("e"));
    set("Back", Command, 0, QString(), QStringLiteral("d"));
    set("DUp", Command, 0, QString(), QStringLiteral("H"));
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(m_entries.size() - 1));
}

void GamepadInput::setValue(int row, double v)
{
    Entry &e = m_entries[row];
    if (e.kind == 0 && qAbs(v) < m_deadzone)
        v = 0;
    if (e.kind == 1 && v < m_deadzone * 0.5)
        v = 0;
    if (e.invert && e.kind != 2)
        v = (e.kind == 0) ? -v : 1.0 - v;
    const bool wasPressed = e.pressed;
    e.pressed = (e.kind == 2) ? v > 0.5 : qAbs(v) > 0.5;
    if (qFuzzyCompare(v + 2, e.value + 2) && wasPressed == e.pressed)
        return;
    e.value = v;
    emit dataChanged(index(row), index(row), {ValueRole});
    m_dirty = true;
    if (m_enabled && e.pressed && !wasPressed) {
        if (e.action == Pose && !e.pose.isEmpty())
            emit poseRequested(e.pose);
        else if (e.action == Command && !e.command.isEmpty())
            emit commandRequested(e.command);
    }
}

void GamepadInput::simulate(const QString &input, double value)
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].input == input)
            setValue(i, value);
    emit liveChanged();
}

void GamepadInput::openFirst()
{
#ifdef CH_HAVE_SDL2
    if (!m_sdlOk)
        return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameController *gc = SDL_GameControllerOpen(i);
            if (gc) {
                m_dev = gc;
                m_connected = true;
                m_deviceName = QString::fromUtf8(SDL_GameControllerName(gc));
                emit connectedChanged();
                return;
            }
        }
    }
#endif
}

void GamepadInput::closeDevice()
{
#ifdef CH_HAVE_SDL2
    if (m_dev) {
        SDL_GameControllerClose(static_cast<SDL_GameController *>(m_dev));
        m_dev = nullptr;
    }
#endif
    if (m_connected) {
        m_connected = false;
        m_deviceName.clear();
        emit connectedChanged();
    }
}

void GamepadInput::poll()
{
#ifdef CH_HAVE_SDL2
    if (m_sdlOk) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED)
                closeDevice();
        }
        if (!m_dev && ++m_rescan >= 100) { // look for a controller every second
            m_rescan = 0;
            openFirst();
        }
        if (m_dev) {
            auto *gc = static_cast<SDL_GameController *>(m_dev);
            if (!SDL_GameControllerGetAttached(gc)) {
                closeDevice();
            } else {
                auto axis = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(gc, a) / 32767.0; };
                auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(gc, b) ? 1.0 : 0.0; };
                const QVector<double> vals = {
                    btn(SDL_CONTROLLER_BUTTON_A), btn(SDL_CONTROLLER_BUTTON_B), btn(SDL_CONTROLLER_BUTTON_X),
                    btn(SDL_CONTROLLER_BUTTON_Y), btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER),
                    btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), btn(SDL_CONTROLLER_BUTTON_BACK),
                    btn(SDL_CONTROLLER_BUTTON_START), btn(SDL_CONTROLLER_BUTTON_DPAD_UP),
                    btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN), btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT),
                    btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), axis(SDL_CONTROLLER_AXIS_LEFTX),
                    -axis(SDL_CONTROLLER_AXIS_LEFTY), btn(SDL_CONTROLLER_BUTTON_LEFTSTICK),
                    axis(SDL_CONTROLLER_AXIS_RIGHTX), -axis(SDL_CONTROLLER_AXIS_RIGHTY),
                    btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK), axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT),
                    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT),
                };
                for (int i = 0; i < vals.size() && i < m_entries.size(); ++i)
                    setValue(i, vals[i]);
                if (m_dirty)
                    emit liveChanged();
            }
        }
    }
#endif
    step(m_timer.interval() / 1000.0);
}

void GamepadInput::step(double dt)
{
    if (!m_enabled)
        return;
    bool moved = false;
    for (const Entry &e : m_entries) {
        const Joint *j = m_hand->joint(e.joint);
        if (!j)
            continue;
        if (e.action == Motion && e.value != 0) {
            m_cmd[e.joint] = j->clamp(m_cmd[e.joint] + e.gain * e.value * dt);
            moved = true;
        } else if (e.action == Position) {
            const double a = (e.kind == 0) ? (e.value + 1.0) / 2.0 : e.value;
            const double v = j->minDeg() + (j->maxDeg() - j->minDeg()) * a;
            if (qAbs(v - m_cmd[e.joint]) > 0.05) {
                m_cmd[e.joint] = v;
                moved = true;
            }
        }
    }
    m_sinceSend += dt;
    if ((moved || m_dirty) && m_sinceSend >= 1.0 / m_rate) {
        m_sinceSend = 0;
        m_dirty = false;
        emit forward(m_cmd);
    }
}

QJsonArray GamepadInput::toJson() const
{
    QJsonArray a;
    for (const Entry &e : m_entries) {
        a.append(QJsonObject{{QStringLiteral("input"), e.input}, {QStringLiteral("action"), e.action},
                             {QStringLiteral("joint"), e.joint}, {QStringLiteral("pose"), e.pose},
                             {QStringLiteral("command"), e.command}, {QStringLiteral("gain"), e.gain},
                             {QStringLiteral("invert"), e.invert}});
    }
    return a;
}

void GamepadInput::fromJson(const QJsonArray &a)
{
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        for (Entry &e : m_entries) {
            if (e.input != o.value(QStringLiteral("input")).toString())
                continue;
            e.action = o.value(QStringLiteral("action")).toInt(e.action);
            e.joint = o.value(QStringLiteral("joint")).toInt(e.joint);
            e.pose = o.value(QStringLiteral("pose")).toString(e.pose);
            e.command = o.value(QStringLiteral("command")).toString(e.command);
            e.gain = o.value(QStringLiteral("gain")).toDouble(e.gain);
            e.invert = o.value(QStringLiteral("invert")).toBool(e.invert);
        }
    }
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(m_entries.size() - 1));
}

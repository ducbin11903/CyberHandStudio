#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "core/AppCore.h"
#include "core/BoardMonitor.h"
#include "core/CanBackend.h"
#include "core/CanBus.h"
#include "core/CanProtocol.h"
#include "core/EventLog.h"
#include "core/Recorder.h"
#include "core/CadModel.h"
#include "core/Calibration.h"
#include "core/ConsoleModel.h"
#include "core/SerialLink.h"
#include "core/HandController.h"
#include "core/HandModel.h"
#include "core/LineParser.h"
#include "core/LlmClient.h"
#include "core/StreamServer.h"
#include "core/Telemetry.h"
#include "core/TimelineModel.h"
#include "core/Trajectory.h"
#include "core/Updater.h"
#include "core/TcpLink.h"
#include <QTcpServer>
#include <QTcpSocket>

class TstCore : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // AppCore writes examples / settings: keep them out of the real user folders
        QStandardPaths::setTestModeEnabled(true);
    }

    void parseBenchLog()
    {
        const ParsedLine p = LineParser::parse(QStringLiteral("en:1,duty:0,out:42.0,i_mA:150,flt:0,diag:1,pos:700,tgt:729,vel:350,mode:1"));
        QVERIFY(p.telemetry);
        QCOMPARE(p.names.size(), 10);
        QCOMPARE(p.names.at(6), QStringLiteral("pos"));
        QCOMPARE(p.values.at(6), 700.0);
        QCOMPARE(p.values.at(2), 42.0);
    }

    void parseTextLines()
    {
        for (const char *s : {"HOME: done", "OK g 45", "ERR bad arg", "USBX: OK", "ID: CyberHand-H562 fw 0.4 joints=1 cnt90=7000", "help"}) {
            QVERIFY2(!LineParser::parse(QString::fromLatin1(s)).telemetry, s);
        }
        const ParsedLine p = LineParser::parse(QStringLiteral("1.5,2,-3"));
        QVERIFY(p.telemetry);
        QCOMPARE(p.names.at(2), QStringLiteral("ch3"));
    }

    void numFormat()
    {
        QCOMPARE(LineParser::num(45.0), QStringLiteral("45"));
        QCOMPARE(LineParser::num(45.26), QStringLiteral("45.3"));
        QCOMPARE(LineParser::num(-0.01), QStringLiteral("0"));
        QCOMPARE(LineParser::num(0.00002, 7), QStringLiteral("0.00002"));
    }

    void telemetryBench()
    {
        HandModel hand;
        Telemetry tel(&hand);
        const ParsedLine p = LineParser::parse(QStringLiteral("en:1,duty:0,out:42.0,i_mA:150,flt:2,diag:1,pos:7000,tgt:3500,vel:350,mode:1"));
        tel.processTelemetry(0.1, p.names, p.values);
        QCOMPARE(tel.flt(), 2);
        QVERIFY(qAbs(tel.posDeg() - 90.0) < 1e-6);
        QVERIFY(qAbs(tel.tgtDeg() - 45.0) < 1e-6);
        Joint *j = hand.joint(hand.benchJoint());
        QVERIFY(j->hasFeedback());
        QVERIFY(qAbs(j->measuredDeg() - 90.0) < 1e-6);
        QCOMPARE(Telemetry::faultText(3), QStringLiteral("DIAG · Quá dòng"));
        tel.processText(QStringLiteral("HOME: done"));
        QCOMPARE(tel.homed(), 1);
    }

    void telemetryMulti()
    {
        HandModel hand;
        Telemetry tel(&hand);
        const ParsedLine p = LineParser::parse(QStringLiteral("q1:0,q2:7000,q3:3500,q4:0,q5:0,q6:7000,i3:120,en:1,flt:0,mode:1"));
        tel.processTelemetry(0.0, p.names, p.values);
        QVERIFY(tel.multiJoint());
        QVERIFY(qAbs(hand.joint(1)->measuredDeg() - 90.0) < 1e-6);
        QVERIFY(qAbs(hand.joint(2)->measuredDeg() - 45.0) < 1e-6);
        QCOMPARE(hand.joint(2)->currentMa(), 120.0);
    }

    void trajectoryCsv()
    {
        HandModel hand;
        Trajectory tr(&hand);
        QVERIFY(tr.fromCsvText(QStringLiteral("# test\nt,I,M\n0,0,10\n1,90,10\n2,0,20\n")));
        QCOMPARE(tr.sampleCount(), 3);
        QCOMPARE(tr.duration(), 2.0);
        const QVector<double> s = tr.sample(0.5);
        QVERIFY(qIsNaN(s[0]));
        QVERIFY(qAbs(s[2] - 45.0) < 1e-6);
        QVERIFY(qAbs(s[3] - 10.0) < 1e-6);
        // round trip
        Trajectory tr2(&hand);
        QVERIFY(tr2.fromCsvText(tr.toCsvText()));
        QCOMPARE(tr2.sampleCount(), 3);
        QVERIFY(qAbs(tr2.sample(1.5)[3] - 15.0) < 1e-6);
        // percent + semicolons + time offset
        QVERIFY(tr.fromCsvText(QStringLiteral("# unit=percent\ntime;q3\n10;0\n11;50\n")));
        QCOMPARE(tr.duration(), 1.0);
        QVERIFY(qAbs(tr.sample(1.0)[2] - 45.0) < 1e-6);
        QVERIFY(!tr.fromCsvText(QStringLiteral("t,I\n1,0\n0,5\n")));
    }

    void timelineCompile()
    {
        HandModel hand;
        TimelineModel tl(&hand);
        tl.setLength(4);
        QVERIFY(tl.addClip(2, 0.0, 1.0, 90, TimelineModel::Linear) >= 0);
        QVERIFY(tl.addClip(2, 2.0, 1.0, 30, TimelineModel::Linear) >= 0);
        QVERIFY(tl.addClip(2, 0.5, 1.0, 10) < 0); // overlaps
        QVERIFY(qAbs(tl.valueAt(2, 0.5) - 45.0) < 1e-6);
        QVERIFY(qAbs(tl.valueAt(2, 1.5) - 90.0) < 1e-6);
        QVERIFY(qAbs(tl.valueAt(2, 2.5) - 60.0) < 1e-6);
        QVERIFY(qAbs(tl.valueAt(2, 3.5) - 30.0) < 1e-6);
        tl.moveClip(1, 0.2); // blocked by the first clip -> stays at 1.0 or later
        QVERIFY(tl.clip(1).value(QStringLiteral("start")).toDouble() >= 1.0 - 1e-9);
        Trajectory out(&hand);
        tl.compileInto(&out, 10);
        QCOMPARE(out.sampleCount(), 41);
        QVERIFY(qAbs(out.sample(0.5)[2] - 45.0) < 1e-6);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tl.json"));
        QVERIFY(tl.saveJson(path));
        TimelineModel tl2(&hand);
        QVERIFY(tl2.loadJson(path));
        QCOMPARE(tl2.count(), 2);
        QVERIFY(qAbs(tl2.valueAt(2, 3.5) - 30.0) < 1e-6);
    }

    void streamParsing()
    {
        HandModel hand;
        StreamServer s(&hand);
        s.setUnit(StreamServer::Degrees);
        QVERIFY(s.injectText(QStringLiteral("{\"q\":[10,20,30,40,50,60]}")));
        QCOMPARE(s.output().at(2).toDouble(), 30.0);
        QVERIFY(s.injectText(QStringLiteral("{\"I\":45}")));
        QCOMPARE(s.output().at(2).toDouble(), 45.0);
        QVERIFY(s.injectText(QStringLiteral("T2:5,P:80")));
        QCOMPARE(s.output().at(5).toDouble(), 80.0);
        s.setUnit(StreamServer::Percent);
        QVERIFY(s.injectText(QStringLiteral("0,0,50,0,0,100")));
        QCOMPARE(s.output().at(2).toDouble(), 45.0);
        QCOMPARE(s.output().at(5).toDouble(), 90.0);
        s.startCalibration();
        s.injectText(QStringLiteral("100,100,200,100,100,100"));
        s.injectText(QStringLiteral("300,300,600,300,300,300"));
        s.finishCalibration();
        s.injectText(QStringLiteral("200,200,400,200,200,200"));
        QVERIFY(qAbs(s.output().at(2).toDouble() - 45.0) < 1e-6);
        QVERIFY(!s.injectText(QStringLiteral("hello")));
    }

    void timelineParametric()
    {
        HandModel hand;
        TimelineModel tl(&hand);
        tl.setLength(10);
        // sine 0..90, period 2 s, starts at the trough
        const int sine = tl.addPreset(2, 1.0, QStringLiteral("sine"));
        QVERIFY(sine >= 0);
        tl.setClipParam(sine, QStringLiteral("period"), 2.0);
        tl.resizeClip(sine, 4.0);
        QVERIFY(qAbs(tl.valueAt(2, 1.0) - 0.0) < 1e-6);
        QVERIFY(qAbs(tl.valueAt(2, 2.0) - 90.0) < 1e-6);
        QVERIFY(qAbs(tl.valueAt(2, 1.5) - 45.0) < 1e-6);
        // hold continues from the value before it
        const int hold = tl.addPreset(2, 6.0, QStringLiteral("hold"));
        QVERIFY(hold >= 0);
        QVERIFY(qAbs(tl.clip(hold).value(QStringLiteral("a")).toDouble() - tl.valueAt(2, 5.99)) < 1.0);
        // ramp + slope
        const int ramp = tl.addPreset(3, 0.0, QStringLiteral("rampUp"));
        tl.resizeClip(ramp, 2.0);
        QVERIFY(qAbs(tl.valueAt(3, 1.0) - 45.0) < 1e-6);
        QVERIFY(qAbs(tl.clip(ramp).value(QStringLiteral("slope")).toDouble() - 45.0) < 1e-6);
        tl.setClipParam(ramp, QStringLiteral("slope"), 30.0);
        QVERIFY(qAbs(tl.valueAt(3, 3.0) - 60.0) < 1e-6);
        // power in, exponent 2: half way = a + (b - a) / 4
        const int pw = tl.addPreset(4, 0.0, QStringLiteral("powIn"));
        QVERIFY(qAbs(tl.valueAt(4, 0.5) - 22.5) < 1e-6);
        // square / triangle / sawtooth
        const int sq = tl.addPreset(5, 0.0, QStringLiteral("square"));
        QVERIFY(qAbs(tl.valueAt(5, 0.25) - 0.0) < 1e-6);
        QVERIFY(qAbs(tl.valueAt(5, 0.75) - 90.0) < 1e-6);
        tl.setClipParam(sq, QStringLiteral("kind"), TimelineModel::Triangle);
        QVERIFY(qAbs(tl.valueAt(5, 0.25) - 45.0) < 1e-6);
        tl.setClipParam(sq, QStringLiteral("kind"), TimelineModel::Sawtooth);
        QVERIFY(qAbs(tl.valueAt(5, 0.5) - 45.0) < 1e-6);
        QVERIFY(!tl.formula(sq).isEmpty());
        QCOMPARE(tl.clipCurve(pw, 10).size(), 11);
        // moving a clip onto an occupied track is refused
        QVERIFY(!tl.setClipTrack(pw, 3));
        QVERIFY(tl.setClipTrack(pw, 1));
        // json round trip keeps kinds and parameters
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("p.json"));
        QVERIFY(tl.saveJson(path));
        TimelineModel tl2(&hand);
        QVERIFY(tl2.loadJson(path));
        QCOMPARE(tl2.count(), tl.count());
        for (double t = 0; t <= 10; t += 0.37)
            for (int j = 0; j < 6; ++j)
                QVERIFY2(qAbs(tl2.valueAt(j, t) - tl.valueAt(j, t)) < 1e-6, qPrintable(QStringLiteral("t=%1 j=%2").arg(t).arg(j)));
        // custom clips
        tl.saveCustom(sine, QStringLiteral("wave2s"));
        QCOMPARE(tl.customClips().size(), 1);
        TimelineModel tl3(&hand);
        tl3.customFromJson(tl.customToJson());
        const int c = tl3.addPreset(0, 0.0, QStringLiteral("custom:0"));
        QVERIFY(c >= 0);
        QCOMPARE(tl3.clip(c).value(QStringLiteral("kind")).toInt(), int(TimelineModel::Sine));
        QVERIFY(qAbs(tl3.clip(c).value(QStringLiteral("period")).toDouble() - 2.0) < 1e-9);
    }

    void calibrationFit()
    {
        double a = 0, b = 0, r2 = 0;
        QVERIFY(Calibration::fitLine({0, 45, 90}, {10, 3760, 7510}, &a, &b, &r2));
        QVERIFY(qAbs(a - 83.3333) < 1e-3);
        QVERIFY(qAbs(b - 10) < 1e-6);
        QVERIFY(qAbs(r2 - 1.0) < 1e-9);
        QVERIFY(Calibration::fitLine({0, 30, 60, 90}, {0, 2400, 4600, 7000}, &a, &b, &r2));
        QVERIFY(r2 > 0.99 && r2 < 1.0);
        QVERIFY(!Calibration::fitLine({45, 45}, {1, 2}, &a, &b, &r2));
    }

    void streamProcessing()
    {
        HandModel hand;
        StreamServer s(&hand);
        s.setUnit(StreamServer::Degrees);
        // averaging of the last 4 frames
        s.setAverageN(4);
        for (double v : {0.0, 0.0, 0.0, 80.0})
            s.injectText(QStringLiteral("I:%1").arg(v));
        s.processStep(0.02);
        QVERIFY(qAbs(s.processed().at(2).toDouble() - 20.0) < 1e-6);
        // low-pass: first-order step response
        s.setAverageN(1);
        s.resetProcessing();
        s.setFilterHz(1.0);
        s.injectText(QStringLiteral("I:0"));
        s.processStep(0.02);
        s.injectText(QStringLiteral("I:90"));
        for (int i = 0; i < 8; ++i) // 0.16 s = one time constant
            s.processStep(0.02);
        const double y = s.processed().at(2).toDouble();
        QVERIFY2(y > 50 && y < 65, qPrintable(QString::number(y)));
        // velocity limit
        s.setFilterHz(0);
        s.resetProcessing();
        s.setMaxVel(100);
        s.injectText(QStringLiteral("I:0"));
        s.processStep(0.02);
        s.injectText(QStringLiteral("I:90"));
        s.processStep(0.1);
        QVERIFY(qAbs(s.processed().at(2).toDouble() - 10.0) < 1e-6);
        // acceleration limit: reaches the target without overshoot
        s.setMaxVel(0);
        s.setMaxAccel(500);
        s.resetProcessing();
        s.injectText(QStringLiteral("I:0"));
        s.processStep(0.01);
        s.injectText(QStringLiteral("I:60"));
        double prev = 0, peak = 0;
        for (int i = 0; i < 300; ++i) {
            s.processStep(0.01);
            const double v = s.processed().at(2).toDouble();
            QVERIFY(v >= prev - 1e-9);
            prev = v;
            peak = qMax(peak, v);
        }
        QVERIFY(qAbs(prev - 60.0) < 1e-6);
        QVERIFY(peak <= 60.0 + 1e-9);
        // handedness filter
        s.setHanded(StreamServer::LeftHand);
        QVERIFY(s.injectText(QStringLiteral("R I:10")));
        QCOMPARE(s.skippedFrames(), qint64(1));
        QVERIFY(s.injectText(QStringLiteral("{\"hand\":\"left\",\"I\":33}")));
        QCOMPARE(s.output().at(2).toDouble(), 33.0);
    }

    void overCurrentPerJoint()
    {
        HandModel hand;
        Telemetry tel(&hand);
        hand.joint(2)->setCurrentLimitMa(100);
        hand.joint(3)->setCurrentLimitMa(500);
        QSignalSpy spy(&tel, &Telemetry::overCurrent);
        const ParsedLine p = LineParser::parse(QStringLiteral("q1:0,q2:0,q3:0,q4:0,q5:0,q6:0,i3:150,i4:150,en:1,flt:0,mode:1"));
        tel.processTelemetry(0.0, p.names, p.values);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 2);
        QVERIFY(tel.overLimit());
        QCOMPARE(tel.totalMa(), 300.0);
        tel.processTelemetry(0.05, p.names, p.values); // still over: no new event
        QCOMPARE(spy.count(), 1);
        tel.processText(QStringLiteral("OVERI: retract i=412 mA to 5200 cnt"));
        QCOMPARE(tel.recentEvent(), QStringLiteral("RETR"));
        // the status-panel limit is the main joint's limit
        hand.setBenchJoint(3);
        QCOMPARE(tel.currentLimitMa(), 500.0);
    }

    void cadModel()
    {
        CadModel cad;
        QVERIFY2(cad.load(QStringLiteral(CH_SOURCE_DIR) + QStringLiteral("/assets/models/opengrasp_lite")), qPrintable(cad.lastError()));
        QCOMPARE(cad.joints().size(), 6);
        QVERIFY(cad.meshCount() > 10);
        QVERIFY(cad.triangles() > 10000);
        // rest pose = identity (within float rounding)
        auto nearId = [](const QMatrix4x4 &m) {
            const QMatrix4x4 id;
            for (int k = 0; k < 16; ++k)
                if (qAbs(m.constData()[k] - id.constData()[k]) > 1e-3f)
                    return false;
            return true;
        };
        for (const QMatrix4x4 &m : cad.solve({}))
            QVERIFY(nearId(m));
        const int links = cad.linkCount();
        const QStringList ln = cad.links();
        // index finger at 30 deg: proximal phalanx turns 30 deg, fingertip more (four-bar coupling)
        const QVector<QMatrix4x4> ms = cad.solve({{QStringLiteral("I"), 30.0}});
        QCOMPARE(ms.size(), links);
        auto angleOf = [](const QMatrix4x4 &m) {
            const double tr = m(0, 0) + m(1, 1) + m(2, 2);
            return qRadiansToDegrees(std::acos(qBound(-1.0, (tr - 1) / 2, 1.0)));
        };
        QVERIFY(qAbs(angleOf(ms[ln.indexOf(QStringLiteral("I_pp"))]) - 30.0) < 0.01);
        QVERIFY(angleOf(ms[ln.indexOf(QStringLiteral("I_tip"))]) > 40.0);
        QVERIFY(nearId(ms[ln.indexOf(QStringLiteral("M_pp"))]));
        // the chain stays closed: C on the tip keeps its distance to D
        const CadModel::Joint &ji = cad.joints()[0];
        QCOMPARE(ji.key, QStringLiteral("I"));
        const QVector3D c1 = ms[ln.indexOf(QStringLiteral("I_tip"))].map(ji.C);
        QVERIFY(qAbs((c1 - ji.D).length() - (ji.C - ji.D).length()) < 0.05);
        // thumb: T1 follows T2 (child of the base)
        const QVector<QMatrix4x4> mt = cad.solve({{QStringLiteral("T2"), 40.0}});
        QVERIFY(!nearId(mt[ln.indexOf(QStringLiteral("T_pp"))]));
        // far outside the range: backs off to a reachable pose, no NaN
        const QVector<QMatrix4x4> mx = cad.solve({{QStringLiteral("T1"), 170.0}, {QStringLiteral("P"), 170.0}});
        for (const QMatrix4x4 &m : mx)
            for (int k = 0; k < 16; ++k)
                QVERIFY(!qIsNaN(m.constData()[k]));
        // pose object for QML
        CadPose pose;
        pose.setModel(&cad);
        pose.setAngles({0, 0, 45, 0, 0, 0});
        QCOMPARE(pose.rotations().size(), links);
    }

    void g474IdKeys()
    {
        HandModel hand;
        Telemetry tel(&hand);
        QSignalSpy spy(&tel, &Telemetry::configFromFirmware);
        tel.processText(QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 3);
        QCOMPARE(spy.first().at(1).toDouble(), 7000.0);
        QVERIFY(tel.firmwareG474());
        QCOMPARE(tel.firmwareMcu(), QStringLiteral("U6"));
        QCOMPARE(tel.firmwareKeys(), (QStringList{QStringLiteral("I"), QStringLiteral("T1"), QStringLiteral("T2")}));
        tel.processText(QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U7 motor=2(R) keys=m,r,p"));
        QCOMPARE(tel.firmwareKeys(), (QStringList{QStringLiteral("M"), QStringLiteral("R"), QStringLiteral("P")}));
        // H562 bench: no keys, not G474
        tel.processText(QStringLiteral("ID: CyberHand-H562 fw 0.5 joints=1 cnt90=7000"));
        QVERIFY(!tel.firmwareG474());
        QVERIFY(tel.firmwareKeys().isEmpty());
    }

    void g474PortSync()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *link = core.link();
        HandModel *hand = core.hand();
        HandController *ctrl = core.ctrl();
        hand->setBenchJoint(hand->indexOfKey(QStringLiteral("I")));
        link->openVirtual();
        const QString id = QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2");
        link->injectLine(id);
        // ID -> m <main joint> -> j for every motor of the MCU -> back to the main joint
        QCOMPARE(link->virtualSent(), (QStringList{
            QStringLiteral("m I"),
            QStringLiteral("m I"), QStringLiteral("j 7000"),
            QStringLiteral("m T1"), QStringLiteral("j 7000"),
            QStringLiteral("m T2"), QStringLiteral("j 7000"),
            QStringLiteral("m I")}));
        QCOMPARE(ctrl->protocol(), int(HandController::PerJoint));
        QCOMPARE(ctrl->portMcu(), QStringLiteral("U6"));
        QCOMPARE(ctrl->selectedKey(), QStringLiteral("I"));
        QVERIFY(!hand->joint(hand->indexOfKey(QStringLiteral("I")))->offPort());
        QVERIFY(hand->joint(hand->indexOfKey(QStringLiteral("M")))->offPort());

        // main joint on the other MCU: no m, single-motor commands refused
        link->clearVirtualSent();
        hand->setBenchJoint(hand->indexOfKey(QStringLiteral("M")));
        QVERIFY(link->virtualSent().isEmpty());
        QVERIFY(!ctrl->benchOnPort());
        QVERIFY(ctrl->benchHint().contains(QStringLiteral("U7")));
        QVERIFY(!ctrl->home());
        QVERIFY(!ctrl->setDuty(30));
        QVERIFY(!ctrl->applyPid());
        QVERIFY(ctrl->enable()); // e = whole MCU, still allowed
        QCOMPARE(link->virtualSent(), QStringList{QStringLiteral("e")});

        link->clearVirtualSent();
        hand->setBenchJoint(hand->indexOfKey(QStringLiteral("T1")));
        QCOMPARE(link->virtualSent(), QStringList{QStringLiteral("m T1")});
        QVERIFY(ctrl->benchOnPort());

        // streaming: only the joints of this port, numbered like the app (1 T2 .. 6 P)
        link->clearVirtualSent();
        ctrl->sendJoints({10, 20, 30, 40, 50, 60}, true, true);
        QCOMPARE(link->virtualSent(), (QStringList{QStringLiteral("g 1 10"), QStringLiteral("g 2 20"), QStringLiteral("g 3 30")}));
        link->clearVirtualSent();
        QVERIFY(!ctrl->moveJoint(hand->indexOfKey(QStringLiteral("R")), 45));
        QVERIFY(link->virtualSent().isEmpty());

        // one motor on / off
        QVERIFY(ctrl->enableJoint(hand->indexOfKey(QStringLiteral("T2"))));
        QVERIFY(!ctrl->disableJoint(hand->indexOfKey(QStringLiteral("P"))));
        QCOMPARE(link->virtualSent(), QStringList{QStringLiteral("e T2")});

        // an `i` typed later: m again, no second calibration push
        link->clearVirtualSent();
        link->injectLine(id);
        QCOMPARE(link->virtualSent(), QStringList{QStringLiteral("m T1")});

        // calibration of a joint that is not the main joint: m <key>, j, back to the main joint
        link->clearVirtualSent();
        QVERIFY(ctrl->sendJointCountsPer90(hand->indexOfKey(QStringLiteral("I")), 7500));
        QCOMPARE(link->virtualSent(), (QStringList{QStringLiteral("m I"), QStringLiteral("j 7500"), QStringLiteral("m T1")}));

        // disconnect clears the port layout
        link->close();
        QVERIFY(ctrl->portKeys().isEmpty());
        QVERIFY(!hand->joint(hand->indexOfKey(QStringLiteral("M")))->offPort());
    }

    void calibrationHomeTwice()
    {
        // a second sweep in the same session: Homed is already Yes, the next "HOME: done" must still count
        HandModel hand;
        Telemetry tel(&hand);
        SerialLink link;
        ConsoleModel console;
        HandController ctrl(&link, &console, &hand);
        Calibration cal(&hand, &ctrl, &tel, &link, &console);
        link.openVirtual();
        cal.setJoint(hand.benchJoint());
        for (const char *line : {"HOME: done, zero set here", "HOME I: done"}) {
            link.clearVirtualSent();
            QVERIFY(cal.startSweep());
            QVERIFY(link.virtualSent().contains(QStringLiteral("e")));
            tel.processText(QString::fromLatin1(line));
            QVERIFY2(cal.status().contains(QStringLiteral("gập")), qPrintable(cal.status()));
            QVERIFY(link.virtualSent().contains(QStringLiteral("o 30")));
            cal.stopSweep();
            QVERIFY(!cal.running());
        }
        QSignalSpy spy(&tel, &Telemetry::homeResult);
        tel.processText(QStringLiteral("HOME(T1): timeout, no stop found"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 0);
        QCOMPARE(tel.homed(), 0);
    }

    void dualPortRouting()
    {
        // 2 ports: U6 on port 1, U7 on port 2, commands routed by joint owner
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *a = core.link();
        SerialLink *b = core.link2();
        HandModel *hand = core.hand();
        HandController *ctrl = core.ctrl();
        auto J = [hand](const char *k) { return hand->indexOfKey(QLatin1String(k)); };
        auto L = [](std::initializer_list<const char *> l) {
            QStringList r;
            for (const char *x : l)
                r << QString::fromLatin1(x);
            return r;
        };
        hand->setBenchJoint(J("I"));
        ctrl->setDualPort(true);
        a->openVirtual();
        b->openVirtual();
        QCOMPARE(ctrl->openPorts(), 2);
        a->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2"));
        QVERIFY(hand->joint(J("M"))->offPort()); // U7 has not answered yet
        b->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U7 motor=1(M) keys=M,R,P"));
        for (int i = 0; i < hand->count(); ++i)
            QVERIFY(!hand->joint(i)->offPort());
        QCOMPARE(ctrl->portKeys(), L({"T2", "T1", "I", "M", "R", "P"}));
        QCOMPARE(ctrl->portMcu(), QStringLiteral("U6+U7"));
        QCOMPARE(ctrl->portOfJoint(J("P")), 1);
        // each MCU gets the calibration of its own motors
        QCOMPARE(a->virtualSent(), L({"m I", "m I", "j 7000", "m T1", "j 7000", "m T2", "j 7000", "m I"}));
        QCOMPARE(b->virtualSent(), L({"m M", "j 7000", "m R", "j 7000", "m P", "j 7000"}));

        // streaming: each joint to its owner
        a->clearVirtualSent();
        b->clearVirtualSent();
        ctrl->sendJoints({10, 20, 30, 40, 50, 60}, true, true);
        QCOMPARE(a->virtualSent(), L({"g 1 10", "g 2 20", "g 3 30"}));
        QCOMPARE(b->virtualSent(), L({"g 4 40", "g 5 50", "g 6 60"}));

        // single-motor commands follow the main joint's port
        a->clearVirtualSent();
        b->clearVirtualSent();
        QVERIFY(ctrl->setDuty(30));
        QCOMPARE(a->virtualSent(), L({"o 30"}));
        QVERIFY(b->virtualSent().isEmpty());
        a->clearVirtualSent();
        hand->setBenchJoint(J("R"));
        QCOMPARE(b->virtualSent(), L({"m R"}));
        QVERIFY(a->virtualSent().isEmpty());
        QCOMPARE(ctrl->selectedKey(), QStringLiteral("R"));
        QVERIFY(ctrl->home());
        QVERIFY(b->virtualSent().last().startsWith(QLatin1String("H")));

        // whole-MCU commands to both, raw lines by command
        a->clearVirtualSent();
        b->clearVirtualSent();
        QVERIFY(ctrl->enable());
        QVERIFY(ctrl->send(QStringLiteral("g 6 15")));
        QVERIFY(ctrl->send(QStringLiteral("e T1")));
        QVERIFY(ctrl->send(QStringLiteral("@U6 s")));
        QVERIFY(ctrl->send(QStringLiteral("i")));
        QVERIFY(ctrl->send(QStringLiteral("x 50")));
        QCOMPARE(a->virtualSent(), L({"e", "e T1", "s", "i"}));
        QCOMPARE(b->virtualSent(), L({"e", "g 6 15", "i", "x 50"}));

        // console target: port 1 only
        a->clearVirtualSent();
        b->clearVirtualSent();
        ctrl->setConsoleTarget(1);
        QVERIFY(ctrl->sendConsole(QStringLiteral("s")));
        QCOMPARE(a->virtualSent(), L({"s"}));
        QVERIFY(b->virtualSent().isEmpty());
        ctrl->setConsoleTarget(0);

        // Tx lines tagged with the MCU
        const QModelIndex last = core.console()->index(core.console()->rowCount() - 1);
        QCOMPARE(last.data(ConsoleModel::SrcRole).toString(), QStringLiteral("U6"));

        // port 2 closed: its joints greyed, main joint R unreachable
        b->close();
        QVERIFY(hand->joint(J("M"))->offPort());
        QVERIFY(!hand->joint(J("I"))->offPort());
        QVERIFY(!ctrl->benchOnPort());
        QVERIFY2(ctrl->benchHint().contains(QStringLiteral("U7")), qPrintable(ctrl->benchHint()));
        QVERIFY(!ctrl->setDuty(10));
    }

    void dualPortTelemetryMerge()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *a = core.link();
        SerialLink *b = core.link2();
        HandModel *hand = core.hand();
        Telemetry *tel = core.telemetry();
        hand->setBenchJoint(hand->indexOfKey(QStringLiteral("I")));
        core.ctrl()->setDualPort(true);
        a->openVirtual();
        b->openVirtual();
        a->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2"));
        b->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U7 motor=1(M) keys=M,R,P"));
        QSignalSpy samples(tel, &Telemetry::sample);
        const QString lineA = QStringLiteral("en:1,duty:0,out:12.0,i_mA:40,flt:0,diag:1,pos:3500,tgt:3500,vel:0,mode:1,"
                                             "q1:700,t1:700,i1:10,q2:1400,t2:1400,i2:20,q3:3500,t3:3500,i3:40");
        const QString lineB = QStringLiteral("en:0,duty:0,out:0.0,i_mA:0,flt:4,diag:1,pos:7000,tgt:0,vel:0,mode:0,"
                                             "q4:7000,t4:7000,i4:30,q5:0,t5:0,i5:0,q6:3500,t6:3500,i6:0");
        b->injectLine(lineB); // U6 silent so far: U7 drives the samples
        QCOMPARE(samples.count(), 1);
        a->injectLine(lineA); // main port: one merged sample
        QCOMPARE(samples.count(), 2);
        b->injectLine(lineB); // stored, published with the next U6 line
        QCOMPARE(samples.count(), 2);
        a->injectLine(lineA);
        QCOMPARE(samples.count(), 3);
        auto deg = [hand](const char *k) { return hand->joint(hand->indexOfKey(QLatin1String(k)))->measuredDeg(); };
        QCOMPARE(deg("I"), 45.0);
        QCOMPARE(deg("M"), 90.0);
        QCOMPARE(deg("P"), 45.0);
        QCOMPARE(tel->pos(), 3500.0);     // main joint I
        QCOMPARE(tel->en(), 1);           // from U6
        QCOMPARE(tel->flt(), 4);          // a fault of U7 is a hand fault
        QCOMPARE(tel->totalMa(), 100.0);  // 10 + 20 + 40 + 30
        QVERIFY(tel->fields().contains(QStringLiteral("U7.en")));
        QVERIFY(!tel->fields().contains(QStringLiteral("U7.q4")));
    }

    void linkBridge()
    {
        // 1 port, fw 0.8: U6 bridges U7 over UART4
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *link = core.link();
        HandModel *hand = core.hand();
        HandController *ctrl = core.ctrl();
        Telemetry *tel = core.telemetry();
        auto J = [hand](const char *k) { return hand->indexOfKey(QLatin1String(k)); };
        auto L = [](std::initializer_list<const char *> l) {
            QStringList r;
            for (const char *x : l)
                r << QString::fromLatin1(x);
            return r;
        };
        hand->setBenchJoint(J("I"));
        link->openVirtual();
        const QString id6 = QStringLiteral("ID: CyberHand-G474 fw 0.8-g474 joints=6 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2,M,R,P link=U7");
        link->injectLine(id6);
        QVERIFY(ctrl->portHasPeer(0));
        for (int i = 0; i < hand->count(); ++i)
            QVERIFY(!hand->joint(i)->offPort());
        QCOMPARE(link->virtualSent(), L({"m I", "m I", "j 7000", "m T1", "j 7000", "m T2", "j 7000",
                                         "m M", "j 7000", "m R", "j 7000", "m P", "j 7000", "m I"}));
        link->clearVirtualSent();
        ctrl->sendJoints({10, 20, 30, 40, 50, 60}, true, true);
        QCOMPARE(link->virtualSent().size(), 6); // the firmware routes them
        link->clearVirtualSent();
        hand->setBenchJoint(J("M"));
        QVERIFY(ctrl->setDuty(20));
        QCOMPARE(link->virtualSent(), L({"m M", "o 20"}));

        // text of U7 arrives with "[U7] ": prefix stripped, tagged, HOME counts, its ID ignored
        QSignalSpy home(tel, &Telemetry::homeResult);
        link->injectLine(QStringLiteral("[U7] HOME: done, zero set here"));
        QCOMPARE(home.count(), 1);
        link->injectLine(QStringLiteral("[U7] ID: CyberHand-G474 fw 0.8-g474 joints=3 cnt90=7000 mcu=U7 motor=1(M) keys=M,R,P"));
        QCOMPARE(ctrl->portKeys().size(), 6);
        ConsoleModel *con = core.console();
        const QModelIndex last = con->index(con->rowCount() - 1);
        QCOMPARE(last.data(ConsoleModel::SrcRole).toString(), QStringLiteral("U7"));
        QVERIFY(last.data(ConsoleModel::TextRole).toString().startsWith(QLatin1String("ID:")));

        // link lost: new ID without U7, LINK line
        link->clearVirtualSent();
        link->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.8-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2 link=-"));
        link->injectLine(QStringLiteral("LINK: lost U7"));
        QVERIFY(hand->joint(J("M"))->offPort());
        QVERIFY(!ctrl->benchOnPort());
        QVERIFY(!ctrl->setDuty(20));
        QVERIFY(link->virtualSent().isEmpty());
        QCOMPARE(tel->recentEvent(), QStringLiteral("LINK"));

        // link back: the joints of U7 get their calibration again
        link->injectLine(id6);
        link->injectLine(QStringLiteral("LINK: up U7"));
        QVERIFY(!hand->joint(J("M"))->offPort());
        QCOMPARE(link->virtualSent(), L({"m M", "m M", "j 7000", "m R", "j 7000", "m P", "j 7000", "m M"}));

        // lk:0 in the log also drops the joints of U7
        link->injectLine(QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:8,diag:1,pos:0,tgt:0,vel:0,mode:0,q1:0,q4:0,lk:0"));
        QVERIFY(hand->joint(J("R"))->offPort());
        QVERIFY(Telemetry::faultText(8).contains(QStringLiteral("LINK")));
    }

    void consoleSourceFilter()
    {
        ConsoleModel con;
        con.append(ConsoleModel::Rx, QStringLiteral("OK m 1 I"), QStringLiteral("U6"));
        con.append(ConsoleModel::Rx, QStringLiteral("OK m 1 M"), QStringLiteral("U7"));
        con.info(QStringLiteral("app line"));
        QCOMPARE(con.rowCount(), 3);
        QCOMPARE(con.sources(), (QStringList{QStringLiteral("U6"), QStringLiteral("U7")}));
        con.setSourceFilter(QStringLiteral("U7"));
        QCOMPARE(con.rowCount(), 2); // U7 + untagged app line
        QCOMPARE(con.index(0).data(ConsoleModel::TextRole).toString(), QStringLiteral("OK m 1 M"));
        con.append(ConsoleModel::Rx, QStringLiteral("hidden"), QStringLiteral("U6"));
        QCOMPARE(con.rowCount(), 2);
        con.setSourceFilter(QString());
        QCOMPARE(con.rowCount(), 4);
    }

    void boardSenseParse()
    {
        BoardMonitor::Sense s;
        QVERIFY(BoardMonitor::parseSense(QStringLiteral(
            "OK sense U7 T=25.0 C RH=45.0 % VBAT=7.99 V | off at T>=70.0 C (on <60.0) VBAT<6.40 V (on >=6.80) | hot=0 low=0 i2c_err=0"), &s));
        QCOMPARE(s.mcu, QStringLiteral("U7"));
        QCOMPARE(s.t, 25.0);
        QCOMPARE(s.rh, 45.0);
        QCOMPARE(s.vbat, 7.99);
        QCOMPARE(s.tOff, 70.0);
        QCOMPARE(s.tOn, 60.0);
        QCOMPARE(s.vOff, 6.40);
        QCOMPARE(s.vOn, 6.80);
        QCOMPARE(s.hot, 0);
        QCOMPARE(s.low, 0);
        QCOMPARE(s.i2cErr, 0);
        QVERIFY(BoardMonitor::parseSense(QStringLiteral(
            "OK sense U7 T=n/a C RH=n/a % VBAT=7.50 V | off at T>=65.0 C (on <55.0) VBAT<6.40 V (on >=6.80) | hot=0 low=1 i2c_err=7 (T invalid)"), &s));
        QVERIFY(qIsNaN(s.t));
        QVERIFY(qIsNaN(s.rh));
        QCOMPARE(s.vbat, 7.5);
        QCOMPARE(s.tOff, 65.0); // thresholds come from the firmware
        QCOMPARE(s.low, 1);
        QCOMPARE(s.i2cErr, 7);
        QVERIFY(BoardMonitor::parseSense(QStringLiteral(
            "OK sense U7 T=31.5 C RH=40.0 % VBAT=0.00 V | off at T>=70.0 C (on <60.0) VBAT<6.40 V (on >=6.80) | hot=0 low=0 i2c_err=0 (VBAT not measured)"), &s));
        QCOMPARE(s.t, 31.5);
        QVERIFY(qIsNaN(s.vbat));
        QVERIFY(!BoardMonitor::parseSense(QStringLiteral("OK pos 1555 cnt T2"), &s));
        // the event lines are text, not telemetry
        QVERIFY(!LineParser::parse(QStringLiteral("VBAT: battery 6.31 V < 6.40 V, all motors off (flt 32)")).telemetry);
        QVERIFY(!LineParser::parse(QStringLiteral("OTEMP: board 72.0 C >= 70.0 C (SHT40 on U7), all motors off (flt 16)")).telemetry);
        // fault bits of fw 0.9
        QCOMPARE(Telemetry::faultText(4), QStringLiteral("ESTOP"));
        QCOMPARE(Telemetry::faultText(8), QStringLiteral("LINK"));
        QVERIFY(Telemetry::faultText(16).contains(QStringLiteral("nhiệt")));
        QVERIFY(Telemetry::faultText(32).contains(QStringLiteral("Pin")));
        QCOMPARE(Telemetry::faultText(1 | 2).split(QStringLiteral(" · ")).size(), 2);
        // 2S LiPo: 6.4 V = 0 %, 8.4 V = 100 %
        QCOMPARE(BoardMonitor::lipoPercent(3.2), 0.0);
        QCOMPARE(BoardMonitor::lipoPercent(4.2), 100.0);
        QCOMPARE(BoardMonitor::lipoPercent(3.84), 50.0);
        QVERIFY(BoardMonitor::lipoPercent(3.9) > BoardMonitor::lipoPercent(3.8));
    }

    void boardLogAndEvents()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *link = core.link();
        BoardMonitor *board = core.board();
        HandController *ctrl = core.ctrl();
        Telemetry *tel = core.telemetry();
        core.hand()->setBenchJoint(core.hand()->indexOfKey(QStringLiteral("I")));
        link->openVirtual();
        link->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.9-g474 joints=6 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2,M,R,P link=U7"));
        QVERIFY(board->supported());
        QCOMPARE(link->virtualSent().last(), QStringLiteral("sense")); // after the calibration push
        link->injectLine(QStringLiteral(
            "[U7] OK sense U7 T=25.0 C RH=45.0 % VBAT=7.99 V | off at T>=70.0 C (on <60.0) VBAT<6.40 V (on >=6.80) | hot=0 low=0 i2c_err=0"));
        QVERIFY(board->thresholdsKnown());
        QCOMPARE(board->tOff(), 70.0);
        QCOMPARE(board->vOn(), 6.8);

        const QString head = QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:0,diag:1,pos:0,tgt:0,vel:0,mode:0,q1:0,t1:0,i1:0,q3:0,t3:0,i3:0");
        link->injectLine(head + QStringLiteral(",temp:61.5,rh:40.0,vbat:6.62,lk:1"));
        QVERIFY(board->hasTemp());
        QCOMPARE(board->temp(), 61.5);
        QCOMPARE(board->vbat(), 6.62);
        QCOMPARE(board->tempLevel(), 1); // >= 70 - 10
        QCOMPARE(board->vbatLevel(), 1); // < 6.80
        QCOMPARE(board->source(), QStringLiteral("U7 qua U6"));
        QCOMPARE(tel->fields().value(QStringLiteral("temp")).toDouble(), 61.5);
        QVERIFY(!board->stale());
        // SHT40 not read: no temp / rh -> no value, not 0
        link->injectLine(head + QStringLiteral(",vbat:8.40,lk:1"));
        QVERIFY(!board->hasTemp());
        QVERIFY(qIsNaN(board->temp()));
        QVERIFY(board->hasVbat());
        QCOMPARE(board->batteryPct(), 100.0);
        QCOMPARE(board->tempLevel(), 0);
        QCOMPARE(board->vbatLevel(), 0);
        link->injectLine(head + QStringLiteral(",temp:24.0,rh:50.0,vbat:1.20,lk:1"));
        QVERIFY(!board->batteryPresent()); // < 3 V: no battery
        QVERIFY(qIsNaN(board->batteryPct()));

        // over-temperature: U6 line and "[U7]" line within 1 s -> one event
        QSignalSpy ev(board, &BoardMonitor::event);
        const int log0 = core.events()->count();
        link->injectLine(QStringLiteral("OTEMP: board 72.0 C >= 70.0 C (SHT40 on U7), all motors off (flt 16)"));
        link->injectLine(QStringLiteral("[U7] OTEMP: board 72.0 C >= 70.0 C (SHT40 on U7), all motors off (flt 16)"));
        QCOMPARE(ev.count(), 1);
        QCOMPARE(core.events()->count(), log0 + 1);
        QCOMPARE(board->otempState(), 2);
        QVERIFY(board->enableBlocked());
        QCOMPARE(board->otempText(), QStringLiteral("Quá nhiệt 72.0 °C — đã tắt cả 6 motor"));
        QCOMPARE(board->tempLevel(), 2);
        QVERIFY(!board->blockReason().isEmpty());
        // flt 16 in the log while tripped: no second event
        link->injectLine(QStringLiteral("en:0,duty:0,out:0,i_mA:0,flt:16,diag:1,pos:0,tgt:0,vel:0,mode:0,temp:72.0,rh:30.0,vbat:7.9,lk:1"));
        QCOMPARE(ev.count(), 1);
        // e refused
        link->injectLine(QStringLiteral("ERR board over-temperature, motors stay off ('sense')"));
        QCOMPARE(board->otempState(), 2);
        // cleared (twice) -> one event, banner "safe again", Enable unlocked
        link->injectLine(QStringLiteral("OTEMP: cleared (< 60.0 C), send e to run again"));
        link->injectLine(QStringLiteral("[U7] OTEMP: cleared (< 60.0 C), send e to run again"));
        QCOMPARE(ev.count(), 2);
        QCOMPARE(ev.last().at(2).toBool(), false);
        QCOMPARE(board->otempState(), 1);
        QCOMPARE(board->otempText(), QStringLiteral("Đã an toàn — bấm Enable để chạy lại"));
        QVERIFY(!board->enableBlocked());
        // the log still has flt 16 until e: stays "safe again"
        link->injectLine(QStringLiteral("en:0,duty:0,out:0,i_mA:0,flt:16,diag:1,pos:0,tgt:0,vel:0,mode:0,temp:58.0,lk:1"));
        QCOMPARE(board->otempState(), 1);
        // Enable acknowledges, the app never sends e by itself
        QVERIFY(!link->virtualSent().contains(QStringLiteral("e")));
        QVERIFY(ctrl->enable());
        QCOMPARE(board->otempState(), 0);

        // battery low, from the flt bit only (connected after the event)
        link->injectLine(QStringLiteral("en:0,duty:0,out:0,i_mA:0,flt:32,diag:1,pos:0,tgt:0,vel:0,mode:0,vbat:6.30,lk:1"));
        QTest::qWait(1600); // past the settling time after e
        link->injectLine(QStringLiteral("en:0,duty:0,out:0,i_mA:0,flt:32,diag:1,pos:0,tgt:0,vel:0,mode:0,vbat:6.30,lk:1"));
        QCOMPARE(board->vbatState(), 2);
        QCOMPARE(board->vbatText(), QStringLiteral("Pin yếu 6.30 V — đã tắt cả 6 motor"));
        link->injectLine(QStringLiteral("VBAT: battery 6.31 V < 6.40 V, all motors off (flt 32)"));
        QCOMPARE(board->vbatState(), 2);
        link->injectLine(QStringLiteral("[U7] VBAT: ok (7.40 V), send e to run again"));
        QCOMPARE(board->vbatState(), 1);
        // flt bit gone (motors allowed again) -> banner off
        link->injectLine(QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:0,diag:1,pos:0,tgt:0,vel:0,mode:0,vbat:7.40,lk:1"));
        QCOMPARE(board->vbatState(), 0);

        // link to U7 down
        link->injectLine(QStringLiteral("ERR no sensor data (SHT40 + VBAT are on U7, link to U7 is down)"));
        QVERIFY(!board->senseError().isEmpty());
        // disconnect clears everything
        link->close();
        QVERIFY(!board->hasData());
        QVERIFY(!board->thresholdsKnown());
    }

    void boardTwoPorts()
    {
        // both ports report the U7 sensor: one source (U7), not plotted twice
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *a = core.link();
        SerialLink *b = core.link2();
        core.hand()->setBenchJoint(core.hand()->indexOfKey(QStringLiteral("I")));
        core.ctrl()->setDualPort(true);
        a->openVirtual();
        b->openVirtual();
        a->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.9-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2"));
        QCOMPARE(a->virtualSent().last(), QStringLiteral("sense")); // U7 not known yet
        b->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.9-g474 joints=3 cnt90=7000 mcu=U7 motor=1(M) keys=M,R,P"));
        QCOMPARE(b->virtualSent().last(), QStringLiteral("sense"));
        a->clearVirtualSent();
        b->clearVirtualSent();
        QVERIFY(core.board()->refresh());
        QCOMPARE(b->virtualSent(), QStringList{QStringLiteral("sense")});
        QVERIFY(a->virtualSent().isEmpty());

        b->injectLine(QStringLiteral("en:0,duty:0,out:0,i_mA:0,flt:0,diag:1,pos:0,tgt:0,vel:0,mode:0,q4:0,t4:0,i4:0,temp:25.0,rh:45.0,vbat:7.99"));
        a->injectLine(QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:0,diag:1,pos:0,tgt:0,vel:0,mode:0,q3:0,t3:0,i3:0,temp:25.4,rh:45.0,vbat:7.98"));
        QCOMPARE(core.board()->temp(), 25.0);
        QCOMPARE(core.board()->source(), QStringLiteral("U7"));
        const QVariantMap f = core.telemetry()->fields();
        QCOMPARE(f.value(QStringLiteral("temp")).toDouble(), 25.0);
        QVERIFY(!f.contains(QStringLiteral("U7.temp")));
        QVERIFY(!f.contains(QStringLiteral("U6.temp")));
        QVERIFY(!f.contains(QStringLiteral("U7.vbat")));
    }

    void recorderColumns()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        Recorder *rec = core.recorder();
        const QStringList h = rec->header();
        QCOMPARE(h.first(), QStringLiteral("t"));
        QVERIFY(h.contains(QStringLiteral("temp")));
        QVERIFY(h.contains(QStringLiteral("rh")));
        QVERIFY(h.contains(QStringLiteral("vbat")));
        QCOMPARE(h.mid(1, 6), core.hand()->keys()); // replayable as a trajectory
        const QString file = dir.filePath(QStringLiteral("rec.csv"));
        QVERIFY(rec->start(file));
        core.link()->openVirtual();
        core.link()->injectLine(QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:0,diag:1,pos:0,tgt:0,vel:0,mode:0,q3:3500,t3:3500,i3:12,vbat:7.80"));
        core.link()->injectLine(QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:0,diag:1,pos:0,tgt:0,vel:0,mode:0,q3:3500,t3:3500,i3:12,temp:26.5,rh:44.0,vbat:7.80"));
        QCOMPARE(rec->rows(), 2);
        rec->stop();
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4); // comment, header, 2 rows
        const QStringList r1 = lines[2].split(QLatin1Char(','));
        const QStringList r2 = lines[3].split(QLatin1Char(','));
        const int ti = h.indexOf(QStringLiteral("temp"));
        QVERIFY(r1[ti].isEmpty()); // missing = empty, not 0
        QCOMPARE(r2[ti], QStringLiteral("26.5"));
        QCOMPARE(r2[h.indexOf(QStringLiteral("vbat"))], QStringLiteral("7.80"));
        QCOMPARE(r2[h.indexOf(QStringLiteral("I"))], QStringLiteral("45.00"));
        // the recording loads back as a trajectory
        Trajectory tr(core.hand());
        QVERIFY(tr.loadCsv(file));
    }

    void thumbKeysAndProfileMigration()
    {
        // hardware: T1 = thumb rotation, T2 = thumb flexion; old profiles had them the other way round
        QTemporaryDir dir;
        {
            QFile f(dir.filePath(QStringLiteral("Default.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"joints":[{"key":"T2","name":"Thumb Rotation"},{"key":"T1","name":"Thumb Flexion"},
                                  {"key":"I","name":"Index"}],
                       "poses":[{"name":"Pinch","q":[70,45,50,0,0,0]}]})");
        }
        QSettings().remove(QStringLiteral("lastProfile"));
        AppCore core(dir.path());
        HandModel *hand = core.hand();
        QCOMPARE(hand->joint(hand->indexOfKey(QStringLiteral("T1")))->name(), QStringLiteral("Thumb Rotation"));
        QCOMPARE(hand->joint(hand->indexOfKey(QStringLiteral("T2")))->name(), QStringLiteral("Thumb Flexion"));
        const int pinch = core.poses()->find(QStringLiteral("Pinch"));
        QCOMPARE(core.poses()->values(pinch), (QVariantList{45.0, 70.0, 50.0, 0.0, 0.0, 0.0}));
        // fresh model defaults
        HandModel fresh;
        QCOMPARE(fresh.joint(0)->key(), QStringLiteral("T2"));
        QCOMPARE(fresh.joint(0)->name(), QStringLiteral("Thumb Flexion"));
        QCOMPARE(fresh.joint(1)->name(), QStringLiteral("Thumb Rotation"));
    }

    void disconnectStopsMotors()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        SerialLink *link = core.link();
        // bench board (no MCU layout): duty 0, driver off
        link->openVirtual();
        link->clearVirtualSent();
        QVERIFY(!core.toggleConnection());
        QCOMPARE(link->virtualSent(), (QStringList{QStringLiteral("0"), QStringLiteral("d")}));
        QVERIFY(!core.ctrl()->connected());
        // MCB G474: D = every motor off
        link->openVirtual();
        link->injectLine(QStringLiteral("ID: CyberHand-G474 fw 0.8-g474 joints=6 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2,M,R,P link=U7"));
        link->clearVirtualSent();
        core.toggleConnection();
        QCOMPARE(link->virtualSent(), QStringList{QStringLiteral("D")});
    }

    void simulationWhileConnected()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        QVERIFY(core.simulating()); // not connected
        core.link()->openVirtual();
        core.setActiveMode(QStringLiteral("timeline"));
        core.player()->setOutput(true);
        QVERIFY(!core.simulating());
        QSignalSpy spy(&core, &AppCore::simulatingChanged);
        core.player()->setOutput(false); // Output off: the 3D follows the commands
        QVERIFY(core.simulating());
        QVERIFY(spy.count() >= 1);
        core.player()->setOutput(true);
        core.setManualPreview(true);
        QVERIFY(core.simulating());
        core.setManualPreview(false);
        QVERIFY(!core.simulating());
    }

    void canStatusDecode()
    {
        // hand-built frame: checks the byte offsets of the spec, not just a round trip
        QByteArray d(64, '\0');
        auto *p = reinterpret_cast<uchar *>(d.data());
        p[0] = 1;            // version
        p[1] = 1;            // node U6
        qToLittleEndian<quint16>(513, p + 2);
        // block 0: joint 3 (I), enabled + homed + PID, flt 16, duty -10, pos 3500, tgt 3600, 120 mA
        p[4] = 3;
        p[5] = 0x01 | 0x02 | (1 << 2);
        p[6] = 16;
        p[7] = uchar(qint8(-10));
        qToLittleEndian<qint32>(3500, p + 8);
        qToLittleEndian<qint32>(3600, p + 12);
        qToLittleEndian<qint16>(120, p + 16);
        // block 1: joint 2 (T1), DIAG low, pos -40
        p[18] = 2;
        p[19] = 0x10;
        qToLittleEndian<qint32>(-40, p + 22);
        qToLittleEndian<qint32>(-40, p + 26);
        // block 2: joint 1 (T2)
        p[32] = 1;
        // sensors missing
        qToLittleEndian<qint16>(0x7FFF, p + 46);
        qToLittleEndian<quint16>(0xFFFF, p + 48);
        qToLittleEndian<quint16>(0, p + 50);
        p[52] = 0x04; // link U6-U7 alive
        qToLittleEndian<quint32>(123456, p + 56);

        CanV1::Status s;
        QVERIFY(CanV1::decodeStatus(d, &s));
        QCOMPARE(s.node, 1);
        QCOMPARE(int(s.seq), 513);
        QCOMPARE(s.joints[0].joint, 3);
        QVERIFY(s.joints[0].enabled);
        QVERIFY(s.joints[0].homed);
        QCOMPARE(s.joints[0].mode, 1);
        QCOMPARE(s.joints[0].flt, 16);
        QCOMPARE(s.joints[0].duty, -10);
        QCOMPARE(int(s.joints[0].pos), 3500);
        QCOMPARE(int(s.joints[0].target), 3600);
        QCOMPARE(s.joints[0].currentMa, 120);
        QVERIFY(s.joints[1].diagLow);
        QCOMPARE(int(s.joints[1].pos), -40);
        QVERIFY(qIsNaN(s.temp));
        QVERIFY(qIsNaN(s.rh));
        QVERIFY(qIsNaN(s.vbat));
        QVERIFY(s.linkOk);
        QCOMPARE(s.uptimeMs, quint32(123456));
        // as a log line: head = main joint I, no sensor fields when missing
        const QString line = CanV1::statusToLogLine(s, 3);
        const ParsedLine pl = LineParser::parse(line);
        QVERIFY(pl.telemetry);
        auto field = [&](const char *n) { return pl.values.value(pl.names.indexOf(QLatin1String(n)), qQNaN()); };
        QCOMPARE(field("pos"), 3500.0);
        QCOMPARE(field("tgt"), 3600.0);
        QCOMPARE(field("mode"), 1.0);
        QCOMPARE(field("flt"), 16.0);
        QCOMPARE(field("q3"), 3500.0);
        QCOMPARE(field("i3"), 120.0);
        QCOMPARE(field("q2"), -40.0);
        QVERIFY(!pl.names.contains(QStringLiteral("temp")));
        QVERIFY(!pl.names.contains(QStringLiteral("vbat")));
        // with sensors: round trip
        s.temp = 31.5;
        s.rh = 44.2;
        s.vbat = 7.96;
        CanV1::Status r;
        QVERIFY(CanV1::decodeStatus(CanV1::encodeStatus(s), &r));
        QCOMPARE(r.temp, 31.5);
        QCOMPARE(r.rh, 44.2);
        QCOMPARE(r.vbat, 7.96);
        QVERIFY(CanV1::statusToLogLine(r, 3).contains(QStringLiteral("temp:31.5,rh:44.2,vbat:7.96")));
        // wrong version / short frame
        d[0] = 2;
        QVERIFY(!CanV1::decodeStatus(d, &s));
        QVERIFY(!CanV1::decodeStatus(QByteArray(20, '\0'), &s));
    }

    void canPosAllAndText()
    {
        const QByteArray d = CanV1::encodePosAll({qQNaN(), qQNaN(), 45.0, qQNaN(), -1.5, qQNaN()});
        QCOMPARE(d.size(), 12);
        QCOMPARE(qFromLittleEndian<qint16>(d.constData() + 0), qint16(0x7FFF));
        QCOMPARE(qFromLittleEndian<qint16>(d.constData() + 4), qint16(4500));
        QCOMPARE(qFromLittleEndian<qint16>(d.constData() + 8), qint16(-150));
        QCOMPARE(qFromLittleEndian<qint16>(d.constData() + 10), qint16(0x7FFF));
        const QVector<double> q = CanV1::decodePosAll(d);
        QVERIFY(qIsNaN(q[0]));
        QCOMPARE(q[2], 45.0);
        QCOMPARE(q[4], -1.5);
        // FD lengths
        QCOMPARE(CanV1::fdLength(9), 12);
        QCOMPARE(CanV1::fdLength(33), 48);
        QCOMPARE(CanV1::fdLength(65), -1);
        const QVector<CanV1::Frame> t = CanV1::encodeText(2, QStringLiteral("H 30 250"));
        QCOMPARE(t.size(), 1);
        QCOMPARE(t[0].id, quint32(0x202));
        QCOMPARE(t[0].data.size(), 8);
        QCOMPARE(CanV1::encodeText(1, QStringLiteral("x 55 extra")).first().data.size(), 12); // padded
        // TEXT reassembly: a line across frames, padding, CR
        CanV1::TextAssembler a;
        QVERIFY(a.feed(QByteArray("OK e a")).isEmpty());
        QByteArray chunk("ll\r\nHOME: done\nOVE");
        chunk.append(QByteArray(5, '\0'));
        QCOMPARE(a.feed(chunk), (QStringList{QStringLiteral("OK e all"), QStringLiteral("HOME: done")}));
        QCOMPARE(a.feed(QByteArray("RI: retract\n")), QStringList{QStringLiteral("OVERI: retract")});
        // STATUS frames lost by seq, with u16 wrap
        CanV1::SeqCounter c;
        for (quint16 v : {quint16(1), quint16(2), quint16(5), quint16(6)})
            c.feed(v);
        QCOMPARE(c.lost(), quint64(2));
        c.reset();
        for (quint16 v : {quint16(65534), quint16(65535), quint16(1)})
            c.feed(v);
        QCOMPARE(c.lost(), quint64(1));
        QCOMPARE(c.received(), quint64(3));
        // monitor filter
        CanV1::IdFilter f(QStringLiteral("280-2FF, 0x301 !282"));
        QVERIFY(f.accepts(0x281));
        QVERIFY(!f.accepts(0x282));
        QVERIFY(f.accepts(0x301));
        QVERIFY(!f.accepts(0x080));
        // UDP virtual bus datagram
        CanV1::Frame fr;
        fr.id = 0x301;
        fr.data = QByteArray(64, '\x5A');
        CanV1::Frame back;
        QVERIFY(CanUdp::decode(CanUdp::encode(fr), &back));
        QCOMPARE(back.id, fr.id);
        QCOMPARE(back.data, fr.data);
        QVERIFY(back.fd && back.brs);
    }

    void canHandOverBus()
    {
        QTemporaryDir dir;
        AppCore core(dir.path());
        HandModel *hand = core.hand();
        HandController *ctrl = core.ctrl();
        CanBus *bus = core.can();
        auto J = [hand](const char *k) { return hand->indexOfKey(QLatin1String(k)); };
        auto text = [](const CanV1::Frame &f) { QString s = QString::fromUtf8(f.data); s.remove(QChar(0)); return s; };
        auto sentAs = [&](quint32 id) {
            QStringList l;
            for (const CanV1::Frame &f : bus->virtualSent())
                if (f.id == id)
                    l << text(f);
            return l;
        };
        auto rx = [bus](quint32 id, const QByteArray &d) {
            CanV1::Frame f;
            f.id = id;
            f.data = d;
            bus->injectFrame(f);
        };
        hand->setBenchJoint(J("I"));
        ctrl->setConnectionMode(2);
        QVERIFY(ctrl->useCan());
        bus->openVirtual();
        QVERIFY(ctrl->connected());
        QCOMPARE(ctrl->openPorts(), 2);
        // at connect: i + canlog 50 to each node
        QCOMPARE(sentAs(0x201), (QStringList{QStringLiteral("i"), QStringLiteral("canlog 50")}));
        QCOMPARE(sentAs(0x202), (QStringList{QStringLiteral("i"), QStringLiteral("canlog 50")}));
        bus->clearVirtualSent();
        // ID lines arrive as TEXT, split over frames
        rx(0x281, "ID: CyberHand-G474 fw 0.11-g474 joints=3 cnt90=7000 ");
        rx(0x281, "mcu=U6 motor=1(I) keys=I,T1,T2\n");
        rx(0x282, "ID: CyberHand-G474 fw 0.11-g474 joints=3 cnt90=7000 mcu=U7 motor=1(M) keys=M,R,P\n");
        QCOMPARE(ctrl->portMcu(), QStringLiteral("U6+U7"));
        for (int i = 0; i < hand->count(); ++i)
            QVERIFY(!hand->joint(i)->offPort());
        // calibration push per node, sense to U7 (fw >= 0.9)
        QVERIFY(sentAs(0x201).contains(QStringLiteral("m T1")));
        QVERIFY(sentAs(0x202).contains(QStringLiteral("j 7000")));
        QVERIFY(sentAs(0x202).contains(QStringLiteral("sense")));
        bus->clearVirtualSent();

        // streaming: one POS_ALL with every joint
        ctrl->sendJoints({10, 20, 30, 40, 50, 60}, true, true);
        QCOMPARE(bus->virtualSent().size(), 1);
        QCOMPARE(bus->virtualSent()[0].id, quint32(0x100));
        QVector<double> q = CanV1::decodePosAll(bus->virtualSent()[0].data);
        QCOMPARE(q, (QVector<double>{10, 20, 30, 40, 50, 60}));
        // one joint: the rest 0x7FFF
        bus->clearVirtualSent();
        QVERIFY(ctrl->moveJoint(J("M"), 45));
        q = CanV1::decodePosAll(bus->virtualSent()[0].data);
        QCOMPARE(q[3], 45.0);
        QVERIFY(qIsNaN(q[0]) && qIsNaN(q[2]) && qIsNaN(q[5]));
        // whole hand on/off: 0x200
        bus->clearVirtualSent();
        QVERIFY(ctrl->enable());
        QCOMPARE(sentAs(0x200), QStringList{QStringLiteral("e")});
        // single-motor commands go to the MCU of the main joint, after m <key>
        bus->clearVirtualSent();
        QVERIFY(ctrl->home());
        QVERIFY(sentAs(0x201).last().startsWith(QLatin1String("H")));
        hand->setBenchJoint(J("R"));
        QVERIFY(ctrl->setDuty(20));
        QCOMPARE(sentAs(0x202), (QStringList{QStringLiteral("m R"), QStringLiteral("o 20")}));
        QVERIFY(sentAs(0x201).last().startsWith(QLatin1String("H")));
        // console: typed line to both = one 0x200 frame
        bus->clearVirtualSent();
        ctrl->setConsoleTarget(3);
        QVERIFY(ctrl->sendConsole(QStringLiteral("c")));
        QCOMPARE(sentAs(0x200), QStringList{QStringLiteral("c")});
        ctrl->setConsoleTarget(0);
        // Position Sample Rate -> canlog
        bus->clearVirtualSent();
        ctrl->setLogRate(25);
        QVERIFY(ctrl->applyLogRate());
        QCOMPARE(sentAs(0x200), QStringList{QStringLiteral("canlog 25")});

        // STATUS of both nodes -> joint feedback, board sensors from U7
        CanV1::Status s6;
        s6.version = 1;
        s6.node = 1;
        s6.seq = 1;
        s6.joints[0] = {3, true, true, 1, false, 0, 5, 3500, 3500, 80};
        s6.joints[1] = {2, true, true, 1, false, 0, 0, 0, 0, 0};
        s6.joints[2] = {1, true, true, 1, false, 0, 0, 0, 0, 0};
        CanV1::Status s7 = s6;
        s7.node = 2;
        s7.joints[0] = {4, true, true, 1, false, 0, 5, 7000, 7000, 90};
        s7.joints[1] = {5, true, true, 1, false, 0, 5, 1750, 1750, 70};
        s7.joints[2] = {6, true, true, 1, false, 0, 0, 0, 0, 0};
        s7.temp = 30.2;
        s7.vbat = 8.01;
        s6.temp = 30.2;
        s6.vbat = 8.01;
        rx(0x302, CanV1::encodeStatus(s7));
        rx(0x301, CanV1::encodeStatus(s6));
        s7.seq = 4; // 2 frames lost on U7
        rx(0x302, CanV1::encodeStatus(s7));
        auto deg = [hand, J](const char *k) { return hand->joint(J(k))->measuredDeg(); };
        QCOMPARE(deg("I"), 45.0);
        QCOMPARE(deg("M"), 90.0);
        QCOMPARE(deg("R"), 22.5);
        QCOMPARE(core.board()->temp(), 30.2);
        QCOMPARE(core.board()->source(), QStringLiteral("U7"));
        QCOMPARE(bus->statusLost(), qint64(2));
        // TEXT events reach the parsers, tagged by node
        QSignalSpy home(core.telemetry(), &Telemetry::homeResult);
        rx(0x281, "HOME I: done\n");
        QCOMPARE(home.count(), 1);
        const QModelIndex last = core.console()->index(core.console()->rowCount() - 1);
        QCOMPARE(last.data(ConsoleModel::SrcRole).toString(), QStringLiteral("U6"));

        // E-STOP: frame 0x000, no data
        bus->clearVirtualSent();
        core.estop();
        QCOMPARE(bus->virtualSent().size(), 1);
        QCOMPARE(bus->virtualSent()[0].id, quint32(0x000));
        QVERIFY(bus->virtualSent()[0].data.isEmpty());
        // Disconnect: D to both, then closed
        bus->clearVirtualSent();
        QVERIFY(!core.toggleConnection());
        QCOMPARE(sentAs(0x200), QStringList{QStringLiteral("D")});
        QVERIFY(!ctrl->connected());
        QVERIFY(!bus->connected());
    }

    void wifiTcpLink()
    {
        QTcpServer srv;
        QVERIFY(srv.listen(QHostAddress::LocalHost));
        TcpLink link;
        link.setHost(QStringLiteral("127.0.0.1"));
        link.setPort(srv.serverPort());
        link.setRetryIntervalMs(100);
        link.setPingIntervalMs(200);
        link.setTimeoutEnabled(false);
        QStringList got, sent;
        connect(&link, &Transport::lineReceived, this, [&got](const QString &l, double) { got << l; });
        connect(&link, &Transport::lineSent, this, [&sent](const QString &l, bool) { sent << l; });
        QVERIFY(link.open());
        QVERIFY(link.busy());
        QTRY_VERIFY(srv.hasPendingConnections());
        QTcpSocket *c = srv.nextPendingConnection();
        QTRY_VERIFY(link.connected());
        QVERIFY(link.noDelay());
        QCOMPARE(link.kind(), QStringLiteral("wifi"));
        QByteArray rx; // everything the "module" received
        auto drain = [&]() { rx += c->readAll(); return rx; };

        // TCP is a byte stream: a line split over several recv(), several lines in one
        c->write("ID: Cyber");
        c->flush();
        QTest::qWait(40);
        c->write("Hand-G474 fw 0.11\nOK e");
        c->flush();
        QTest::qWait(40);
        c->write(" all\r\nen:1,duty:0\n");
        c->flush();
        QTRY_COMPARE(got.size(), 3);
        QCOMPARE(got, (QStringList{QStringLiteral("ID: CyberHand-G474 fw 0.11"), QStringLiteral("OK e all"),
                                   QStringLiteral("en:1,duty:0")}));
        QVERIFY(link.sendLine(QStringLiteral("e")));
        QTRY_VERIFY(drain().contains("e\n"));

        // keep-alive: `ping` once a second when nothing else went out, not shown in the console
        rx.clear();
        QTRY_VERIFY_WITH_TIMEOUT(drain().contains("ping\n"), 2000);
        QVERIFY(!sent.contains(QStringLiteral("ping")));
        c->write("ping\n"); // ESP echo
        c->flush();
        QTRY_VERIFY(link.rttMs() >= 0);
        QVERIFY(!got.contains(QStringLiteral("ping")));
        // a command resets the ping clock: no ping right after it
        rx.clear();
        QVERIFY(link.sendLine(QStringLiteral("s")));
        QTest::qWait(120);
        QCOMPARE(drain(), QByteArray("s\n"));
        // debug option: ping / pong in the console
        link.setShowKeepalive(true);
        c->write("pong\n");
        c->flush();
        QTRY_COMPARE(got.last(), QStringLiteral("pong"));
        QTRY_VERIFY_WITH_TIMEOUT(sent.contains(QStringLiteral("ping")), 2000);
        link.setShowKeepalive(false);

        // the module drops the connection: reconnect by itself
        c->disconnectFromHost();
        QTRY_VERIFY(!link.connected());
        QVERIFY(link.busy());
        QTRY_VERIFY_WITH_TIMEOUT(srv.hasPendingConnections(), 3000);
        c = srv.nextPendingConnection();
        rx.clear();
        QTRY_VERIFY(link.connected());
        QVERIFY(link.reconnects() >= 1);

        // timeout on: a peer that answers the pings keeps the link up ...
        link.setSilenceTimeoutMs(500);
        link.setTimeoutEnabled(true);
        for (int k = 0; k < 15; ++k) {
            QTest::qWait(100);
            const QByteArray d = c->readAll();
            for (int n = d.count("ping\n"); n > 0; --n)
                c->write("ping\n");
            c->flush();
            QVERIFY(link.connected());
        }
        // ... a silent one is dropped after the timeout and reconnected
        QTRY_VERIFY_WITH_TIMEOUT(!link.connected(), 2000);
        QVERIFY(link.errorString().contains(QStringLiteral("mất liên lạc")));
        QTRY_VERIFY_WITH_TIMEOUT(srv.hasPendingConnections(), 3000);
        c = srv.nextPendingConnection();
        QTRY_VERIFY(link.connected());
        // timeout off (test with a silent peer): stays connected
        link.setTimeoutEnabled(false);
        QTest::qWait(1200);
        QVERIFY(link.connected());

        // Disconnect: no more retries
        link.close();
        QVERIFY(!link.connected());
        QVERIFY(!link.busy());
        while (srv.hasPendingConnections())
            delete srv.nextPendingConnection();
        QTest::qWait(300);
        QVERIFY(!srv.hasPendingConnections());

        // the app: mode WiFi puts the TCP link on port 0
        QTemporaryDir dir;
        AppCore core(dir.path());
        core.ctrl()->setConnectionMode(3);
        QCOMPARE(core.ctrl()->linkAt(0), static_cast<Transport *>(core.wifi()));
        core.ctrl()->setConnectionMode(0);
        QCOMPARE(core.ctrl()->linkAt(0), static_cast<Transport *>(core.link()));
    }

    void estopLatch()
    {
        BoardMonitor b(nullptr); // the controller is only used by refresh()
        QVERIFY(!b.estopLatched());
        b.updateFault(4);
        QVERIFY(b.estopLatched());
        QVERIFY(b.enableBlocked());
        QVERIFY(b.blockReason().contains(QStringLiteral("E-STOP")));
        b.updateFault(0); // after `c`
        QVERIFY(!b.estopLatched());
        QVERIFY(b.blockReason().isEmpty());
        // refusal text: latched, and a log line still without flt 4 right after does not clear it
        QVERIFY(!b.processText(QStringLiteral("ERR E-STOP latched (flt 4), send c first")));
        QVERIFY(b.estopLatched());
        b.updateFault(0);
        QVERIFY(b.estopLatched());
        b.reset();
        QVERIFY(!b.estopLatched());
    }

    void updaterInfoAndNotes()
    {
        const QByteArray json = R"({"version":"0.7.0","build":61,"commit":"def5678aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "date":"2026-10-15T03:22:00Z","zip":"CyberHandStudio-win64.zip","sha256":"ABCD","size":123,
            "log":[{"commit":"def5678aaaa","subject":"Updater"},{"commit":"bbb2222bbbb","subject":"CAN fix"},
                   {"commit":"abc1234cccc","subject":"0.6.0 CAN"},{"commit":"9990000dddd","subject":"older"}]})";
        UpdateInfo i;
        QVERIFY(Updater::parseInfo(json, &i));
        QCOMPARE(i.build, 61);
        QCOMPARE(i.sha256, QStringLiteral("abcd"));
        QVERIFY(i.zipUrl.endsWith(QStringLiteral("/releases/download/latest/CyberHandStudio-win64.zip")));
        QCOMPARE(i.log.size(), 4);
        // newer only for a CI build with a lower number and another commit
        QVERIFY(Updater::isNewer(i, 58, QStringLiteral("abc1234cccc")));
        QVERIFY(!Updater::isNewer(i, 61, QStringLiteral("def5678aaaa")));
        QVERIFY(!Updater::isNewer(i, 58, QStringLiteral("def5678")));      // same commit
        QVERIFY(!Updater::isNewer(i, 0, QString()));                        // dev build
        QVERIFY(!Updater::isNewer(i, 70, QStringLiteral("eee")));           // already newer
        // notes: what came after the running commit
        QCOMPARE(Updater::notesSince(i, QStringLiteral("abc1234")),
                 (QStringList{QStringLiteral("def5678  Updater"), QStringLiteral("bbb2222  CAN fix")}));
        QCOMPARE(Updater::notesSince(i, QStringLiteral("0000000"), 2),
                 (QStringList{QStringLiteral("def5678  Updater"), QStringLiteral("bbb2222  CAN fix"), QStringLiteral("…")}));
        UpdateInfo bad;
        QVERIFY(!Updater::parseInfo("{\"version\":\"1\"}", &bad));
        QVERIFY(!Updater::parseInfo("not json", &bad));
        // the copy script keeps its parameters and the robocopy success rule
        const QString ps = Updater::scriptText();
        QVERIFY(ps.contains(QStringLiteral("param([int]$ProcId, [string]$Stage, [string]$Dest, [string]$Log)")));
        QVERIFY(ps.contains(QStringLiteral("$code -lt 8")));
        QVERIFY(ps.contains(QStringLiteral("'--updated'")));
        for (QChar c : ps)
            QVERIFY(c.unicode() < 128);
    }

    void updaterDownloadExtract()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty())
            QSKIP("zip / unzip not installed");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // a release folder: zip with CyberHandStudio.exe + version.json (sha256, size)
        QDir(dir.path()).mkpath(QStringLiteral("pkg/qml"));
        auto put = [&](const QString &rel, const QByteArray &d) {
            QFile f(dir.path() + QStringLiteral("/pkg/") + rel);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(d);
        };
        put(QStringLiteral("CyberHandStudio.exe"), QByteArray(5000, 'x'));
        put(QStringLiteral("qml/a.qml"), "Item {}");
        QProcess zip;
        zip.setWorkingDirectory(dir.path() + QStringLiteral("/pkg"));
        zip.start(QStringLiteral("zip"), {QStringLiteral("-qr"), dir.path() + QStringLiteral("/CyberHandStudio-win64.zip"), QStringLiteral(".")});
        QVERIFY(zip.waitForFinished(20000));
        QFile z(dir.path() + QStringLiteral("/CyberHandStudio-win64.zip"));
        QVERIFY(z.open(QIODevice::ReadOnly));
        const QByteArray zipData = z.readAll();
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(zipData, QCryptographicHash::Sha256).toHex());
        auto writeInfo = [&](const QString &shaField) {
            QFile v(dir.path() + QStringLiteral("/version.json"));
            QVERIFY(v.open(QIODevice::WriteOnly | QIODevice::Truncate));
            v.write(QStringLiteral(R"({"version":"9.9.9","build":99,"commit":"feedbeef00","date":"2026-10-15T00:00:00Z",)"
                                   R"("zip":"CyberHandStudio-win64.zip","sha256":"%1","size":%2,"log":[{"commit":"feedbeef00","subject":"new"}]})")
                        .arg(shaField).arg(zipData.size()).toUtf8());
        };
        writeInfo(sha);
        qputenv("CH_UPDATE_URL", QUrl::fromLocalFile(dir.path() + QLatin1Char('/')).toString().toUtf8());
        Updater::setTestBuild(50, QStringLiteral("abcabc0"));

        Updater up;
        up.check();
        QTRY_COMPARE_WITH_TIMEOUT(up.state(), int(Updater::Available), 5000);
        QVERIFY(up.available());
        QCOMPARE(up.notes(), QStringList{QStringLiteral("feedbee  new")});
        up.download();
        QTRY_COMPARE_WITH_TIMEOUT(up.state(), int(Updater::Ready), 20000);
        const QString stage = Updater::workDir() + QStringLiteral("/stage");
        QVERIFY(QFileInfo::exists(stage + QStringLiteral("/CyberHandStudio.exe")));
        QVERIFY(QFileInfo::exists(stage + QStringLiteral("/qml/a.qml")));

        // wrong checksum: refused, nothing extracted
        writeInfo(QStringLiteral("00"));
        Updater up2;
        up2.check();
        QTRY_COMPARE_WITH_TIMEOUT(up2.state(), int(Updater::Available), 5000);
        up2.download();
        QTRY_COMPARE_WITH_TIMEOUT(up2.state(), int(Updater::Error), 20000);
        QVERIFY(up2.errorString().contains(QStringLiteral("sha256")));

        // already up to date
        Updater::setTestBuild(99, QStringLiteral("feedbeef00"));
        Updater up3;
        up3.check();
        QTRY_COMPARE_WITH_TIMEOUT(up3.state(), int(Updater::UpToDate), 5000);
        QDir(Updater::workDir()).removeRecursively();
        qunsetenv("CH_UPDATE_URL");
        Updater::setTestBuild(-1, QString());
    }

    void llmExtract()
    {
        const QString reply = QStringLiteral("Here you go:\n```csv\nt,T2,T1,I,M,R,P\n0,0,0,0,0,0,0\n1,10,20,30,40,50,60\n```\nEnjoy");
        const QString csv = LlmClient::extractCsv(reply);
        QVERIFY(csv.startsWith(QStringLiteral("t,T2")));
        HandModel hand;
        Trajectory tr(&hand);
        QVERIFY(tr.fromCsvText(csv));
        QCOMPARE(tr.sampleCount(), 2);
        QVERIFY(LlmClient::extractCsv(QStringLiteral("no data here")).isEmpty());
    }
};

QTEST_MAIN(TstCore)
#include "tst_core.moc"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "core/AppCore.h"
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
        link->injectLine(QStringLiteral("en:1,duty:0,out:0,i_mA:0,flt:256,diag:1,pos:0,tgt:0,vel:0,mode:0,q1:0,q4:0,lk:0"));
        QVERIFY(hand->joint(J("R"))->offPort());
        QVERIFY(Telemetry::faultText(256).contains(QStringLiteral("LINK")));
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

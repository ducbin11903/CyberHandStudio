#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core/Calibration.h"
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

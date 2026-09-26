#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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
        for (const char *s : {"HOME: done", "OK g 45", "ERR bad arg", "USBX: OK", "ID: CyberHand-H562 fw 0.4 joints=1 cnt90=937", "help"}) {
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
        const ParsedLine p = LineParser::parse(QStringLiteral("en:1,duty:0,out:42.0,i_mA:150,flt:2,diag:1,pos:937,tgt:468.5,vel:350,mode:1"));
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
        const ParsedLine p = LineParser::parse(QStringLiteral("q1:0,q2:937,q3:468.5,q4:0,q5:0,q6:937,i3:120,en:1,flt:0,mode:1"));
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

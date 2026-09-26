#include "LineParser.h"

#include <QRegularExpression>

static bool isLabel(QStringView s)
{
    if (s.isEmpty())
        return false;
    const QChar c0 = s.at(0);
    if (!(c0.isLetter() || c0 == QLatin1Char('_')))
        return false;
    for (QChar c : s) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_')))
            return false;
    }
    return true;
}

ParsedLine LineParser::parse(const QString &raw)
{
    ParsedLine out;
    const QString line = raw.trimmed();
    if (line.isEmpty())
        return out;

    const QStringList tokens = line.split(QLatin1Char(','), Qt::KeepEmptyParts);
    QStringList names;
    QVector<double> values;
    names.reserve(tokens.size());
    values.reserve(tokens.size());

    // Case 1: all tokens "label:number"
    bool labelled = true;
    for (const QString &tok : tokens) {
        const int colon = tok.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            labelled = false;
            break;
        }
        const QString label = tok.left(colon).trimmed();
        const QString num = tok.mid(colon + 1).trimmed();
        bool ok = false;
        const double v = num.toDouble(&ok);
        if (!ok || !isLabel(label)) {
            labelled = false;
            break;
        }
        names.append(label);
        values.append(v);
    }
    if (labelled && !names.isEmpty()) {
        out.telemetry = true;
        out.names = names;
        out.values = values;
        return out;
    }

    // Case 2: plain numeric CSV (first char is a digit/sign) -> ch1..chN
    const QChar c0 = line.at(0);
    if (c0.isDigit() || c0 == QLatin1Char('-') || c0 == QLatin1Char('+') || c0 == QLatin1Char('.')) {
        names.clear();
        values.clear();
        for (int i = 0; i < tokens.size(); ++i) {
            bool ok = false;
            const double v = tokens[i].trimmed().toDouble(&ok);
            if (!ok)
                return out; // not telemetry, not text either -> treat as text below
            names.append(QStringLiteral("ch%1").arg(i + 1));
            values.append(v);
        }
        out.telemetry = true;
        out.names = names;
        out.values = values;
    }
    return out;
}

QString LineParser::num(double v, int decimals)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

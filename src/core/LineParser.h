#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Splits firmware output into telemetry lines ("label:number,label:number,...")
// and text lines (help, OK, ERR, HOME:, ID:, ...).
struct ParsedLine
{
    bool telemetry = false;
    QStringList names;
    QVector<double> values;
};

class LineParser
{
public:
    static ParsedLine parse(const QString &line);
    // Formats a number for a CLI command: at most `decimals`, no trailing zeros.
    static QString num(double v, int decimals = 1);
};

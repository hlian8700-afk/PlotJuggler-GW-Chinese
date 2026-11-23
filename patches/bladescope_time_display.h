#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QtMath>
#include <cmath>

namespace BladeTimeDisplay
{
enum Precision { Automatic, Seconds, Milliseconds };

inline Precision precision()
{
  auto* app = QCoreApplication::instance();
  const auto cached = app->property("bladeTimePrecision");
  if (cached.isValid()) return static_cast<Precision>(cached.toInt());
  const int value = qBound(int(Automatic), QSettings("BladeScope", "BladeScope")
                                .value("appearance/time_precision", Automatic).toInt(),
                           int(Milliseconds));
  app->setProperty("bladeTimePrecision", value);
  return static_cast<Precision>(value);
}

inline void setPrecision(int value)
{
  QCoreApplication::instance()->setProperty("bladeTimePrecision", value);
  QSettings("BladeScope", "BladeScope").setValue("appearance/time_precision", value);
}

inline bool isCalendarTime(double seconds)
{
  return seconds >= 946684800.0 && seconds < 4102444800.0;
}

inline int decimals(Precision mode, double scaleStep)
{
  if (mode == Seconds) return 0;
  if (mode == Milliseconds) return 3;
  return std::abs(scaleStep - std::round(scaleStep)) > 1e-6 ? 3 : 0;
}

inline QString format(double seconds, bool clock, bool calendar, bool showDate, int digits,
                      Qt::TimeSpec calendarZone = Qt::LocalTime)
{
  if (!std::isfinite(seconds)) return {};
  if (!clock || (!calendar && (seconds < 0.0 || seconds >= 86400.0)))
    return QString::number(seconds, 'f', digits);
  const auto dt = QDateTime::fromMSecsSinceEpoch(qRound64(seconds * 1000.0),
                                              calendar ? calendarZone : Qt::UTC);
  const bool date = calendar && showDate;
  return dt.toString(digits ? (date ? "yyyy-MM-dd HH:mm:ss.zzz" : "HH:mm:ss.zzz")
                            : (date ? "yyyy-MM-dd HH:mm:ss" : "HH:mm:ss"));
}
}

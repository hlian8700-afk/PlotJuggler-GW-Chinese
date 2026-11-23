#pragma once

#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace BladeCsvRecording
{
// Recognize the PLC export by its two headers and first timestamp.
inline bool read(const QString& path, PJ::PlotDataMapRef& data, QVariantMap& aliases)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

  const auto split = [](QByteArray row) {
    if (row.startsWith("\xEF\xBB\xBF")) row.remove(0, 3);
    return QString::fromUtf8(row).trimmed().split(QLatin1Char(','));
  };
  const auto parseTime = [](const QString& text) {
    return QDateTime::fromString(text.trimmed(), QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
  };
  auto codes = split(file.readLine());
  auto labels = split(file.readLine());
  auto row = split(file.readLine());
  if (codes.size() < 2 || codes.front() != QStringLiteral("PlcTime") ||
      labels.front() != QStringLiteral("Time") || labels.size() != codes.size() ||
      row.size() != codes.size() || !parseTime(row.front()).isValid())
    return false;
  bool hasChinese = false;
  for (const auto& label : labels)
    for (const auto ch : label)
      if (ch.unicode() >= 0x4e00 && ch.unicode() <= 0x9fff) hasChinese = true;
  if (!hasChinese || labels.join(QLatin1Char(',')).contains(QLatin1Char('"'))) return false;

  QSet<QString> reserved, used;
  for (const auto& code : codes) reserved.insert(code.trimmed());
  std::vector<PJ::PlotData*> channels(codes.size(), nullptr);
  for (int i = 1; i < codes.size(); ++i)
  {
    auto code = codes.at(i).trimmed();
    if (code.isEmpty()) continue;
    if (used.contains(code))
    {
      code += QStringLiteral(" [%1]").arg(i + 1);
      while (used.contains(code) || reserved.contains(code)) code += QLatin1Char('_');
    }
    used.insert(code);
    channels[i] = &data.addNumeric(code.toStdString())->second;
    const auto label = labels.at(i).trimmed();
    if (!label.isEmpty()) aliases.insert(code, label);
  }

  int line = 3;
  while (true)
  {
    const auto time = parseTime(row.front());
    if (!time.isValid() || row.size() != codes.size())
      throw std::runtime_error(QStringLiteral("录波 CSV 第 %1 行的时间或列数无效。").arg(line).toUtf8().constData());
    const double seconds = time.toMSecsSinceEpoch() / 1000.0;
    for (int i = 1; i < row.size(); ++i)
    {
      if (!channels[i]) continue;
      bool valid = false;
      const double value = row.at(i).toDouble(&valid);
      if (valid && std::isfinite(value)) channels[i]->pushBack({seconds, value});
    }
    if (file.atEnd()) break;
    do
    {
      row = split(file.readLine());
      ++line;
    } while (row.size() == 1 && row.front().isEmpty() && !file.atEnd());
    if (row.size() == 1 && row.front().isEmpty()) break;
  }
  return !data.numeric.empty();
}
}

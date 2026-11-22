#pragma once
#include "bladescope_sqlite_recording.h"
#include <QSet>

namespace BladeTxtRecording
{
struct ReadResult : BladeSqliteRecording::ReadResult
{
  qsizetype duplicateTime = 0;
};

inline ReadResult read(const QString& path, std::unordered_set<std::string> names,
                       const std::atomic_bool& canceled)
{
  ReadResult result;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    result.error = file.errorString();
    return result;
  }

  constexpr qint64 maxLine = 4 * 1024 * 1024;
  QStringList header;
  int timeColumn = -1;
  // Controller metadata precedes the comma-separated measurement header.
  for (int lineNumber = 0; lineNumber < 256 && !file.atEnd() && !canceled; ++lineNumber) {
    auto bytes = file.readLine(maxLine);
    if (bytes.size() == maxLine - 1 && !bytes.endsWith('\n')) break;
    if (lineNumber == 0 && bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    auto fields = BladeRecordingIndex::cells(QString::fromUtf8(bytes), ',');
    const auto found = std::find_if(fields.cbegin(), fields.cend(), [](const QString& field) {
      return field.compare("TimeStamp", Qt::CaseInsensitive) == 0;
    });
    if (fields.size() > 1 && found != fields.cend()) {
      timeColumn = int(std::distance(fields.cbegin(), found));
      header = std::move(fields);
      break;
    }
  }
  if (canceled) return {};
  if (timeColumn < 0) {
    result.error = QStringLiteral("未找到逗号分隔的 TimeStamp 录波表头。");
    return result;
  }

  struct Signal {
    int column;
    QString name;
    std::vector<std::pair<qint64,double>> samples;
  };
  std::vector<Signal> signalsToRead;
  for (int i = 0; i < header.size(); ++i)
    if (i != timeColumn && !header[i].isEmpty())
      signalsToRead.push_back({i, header[i], {}});
  QSet<qint64> seenTimes;
  while (!file.atEnd() && !canceled) {
    const auto bytes = file.readLine(maxLine);
    if (bytes.size() == maxLine - 1 && !bytes.endsWith('\n')) {
      result.error = QStringLiteral("录波数据行过长，无法完整读取。");
      return result;
    }
    if (bytes.trimmed().isEmpty()) continue;
    const auto row = BladeRecordingIndex::cells(QString::fromUtf8(bytes), ',');
    auto date = QDateTime::fromString(row.value(timeColumn), "yyyy-MM-dd-HH:mm:ss.zzz");
    if (!date.isValid()) date = QDateTime::fromString(row.value(timeColumn), Qt::ISODateWithMs);
    if (!date.isValid()) { ++result.invalidTime; continue; }
    const qint64 millis = date.toMSecsSinceEpoch();
    if (seenTimes.contains(millis)) ++result.duplicateTime;
    else seenTimes.insert(millis);
    ++result.rows;
    for (auto& signal : signalsToRead) {
      const auto text = row.value(signal.column);
      bool ok = false;
      double value = text.toDouble(&ok);
      if (!ok && text.compare("true", Qt::CaseInsensitive) == 0) { value = 1; ok = true; }
      if (!ok && text.compare("false", Qt::CaseInsensitive) == 0) { value = 0; ok = true; }
      if (ok && std::isfinite(value)) signal.samples.emplace_back(millis,value);
      else ++result.invalidValues;
    }
  }
  if (file.error() != QFileDevice::NoError) {
    result.error = file.errorString();
    return result;
  }
  const auto byTime = [](const auto& a,const auto& b){return a.first < b.first;};
  for (auto& signal : signalsToRead) {
    if (canceled) return {};
    if (signal.samples.empty()) continue;
    if (!std::is_sorted(signal.samples.cbegin(),signal.samples.cend(),byTime))
      std::stable_sort(signal.samples.begin(),signal.samples.end(),byTime);
    const auto base = signal.name.toStdString();
    auto name = base;
    for (int suffix=2;!names.insert(name).second;++suffix) name=base+" ["+std::to_string(suffix)+"]";
    auto& series = result.data.addNumeric(name)->second;
    for (const auto& point : signal.samples) {
      if (canceled) return {};
      series.pushBack({point.first/1000.0,point.second});
    }
    std::vector<std::pair<qint64,double>>().swap(signal.samples);
  }
  return result;
}

inline ReadResult open(QWidget* parent, const QString& path, const std::unordered_set<std::string>& names)
{
  std::atomic_bool canceled{false};
  ReadResult result;
  QProgressDialog progress(QStringLiteral("正在读取 TXT 录波…"),QStringLiteral("取消"),0,0,parent);
  progress.setWindowModality(Qt::WindowModal);
  auto worker = std::unique_ptr<QThread>(QThread::create([&] {
    try { result=read(path,names,canceled); }
    catch(const std::exception& ex) { result.error=QString::fromUtf8(ex.what()); }
  }));
  QEventLoop loop;
  QObject::connect(worker.get(),&QThread::finished,&loop,&QEventLoop::quit);
  QObject::connect(&progress,&QProgressDialog::canceled,&loop,[&]{canceled=true;});
  progress.show();
  worker->start();
  loop.exec();
  worker->wait();
  progress.hide();
  if (canceled) return {};
  if (!result.error.isEmpty() || result.data.numeric.empty()) {
    QMessageBox::warning(parent,QStringLiteral("TXT 录波导入"),
                         result.error.isEmpty()?QStringLiteral("未找到有效的数值测点。"):result.error);
    return {};
  }
  return result;
}

inline bool isRecordingExtension(const QString& path)
{
  return QFileInfo(path).suffix().compare("txt",Qt::CaseInsensitive)==0;
}
}

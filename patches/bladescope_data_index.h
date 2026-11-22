#pragma once
#include "bladescope_sqlite_recording.h"
#include <QRegularExpression>
#include <QMap>
#include <map>

namespace BladeDataIndex
{
struct Identity
{
  QString site, category, turbine, kind;
  QDate date;
  QString key() const { return site + ":" + category + ":" + turbine + ":" + kind; }
};

inline Identity identify(const QString& path)
{
  static const QRegularExpression pattern(
      QStringLiteral(R"(^(?:(.*)[_-])?(\d{4})(\d{3})(\d{2})[_-](\d{8})\.data$)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = pattern.match(QFileInfo(path).fileName());
  if (!match.hasMatch()) return {};
  const auto date = QDate::fromString(match.captured(5), "yyyyMMdd");
  if (!date.isValid()) return {};
  return {match.captured(2), match.captured(3), match.captured(4), match.captured(1).toLower(), date};
}

struct File
{
  QString path;
  Identity id;
  QMap<QString, QStringList> schema;
  QString details;
  QString signature;
};

inline QString tableDetails(const QSqlDatabase& db, const QString& table, const QStringList& fields)
{
  using namespace BladeSqliteRecording;
  auto time = std::find_if(fields.cbegin(), fields.cend(), [](const QString& field) {
    return field.compare("WTUR.Tm.Rw.Dt", Qt::CaseInsensitive) == 0 ||
           field.compare("time", Qt::CaseInsensitive) == 0 ||
           field.compare("timestamp", Qt::CaseInsensitive) == 0 ||
           field.compare("datetime", Qt::CaseInsensitive) == 0;
  });
  QString cycle = QStringLiteral("\u672a\u77e5");
  if (time != fields.cend()) {
    QSqlQuery query(db);
    if (query.exec("SELECT " + identifier(*time) + " FROM " + identifier(table) + " LIMIT 129")) {
      std::vector<double> values;
      while (query.next()) {
        const double raw = timeValue(query.value(0), false);
        if (std::isfinite(raw)) values.push_back(raw);
      }
      std::sort(values.begin(), values.end());
      std::vector<double> deltas;
      for (size_t i = 1; i < values.size(); ++i)
        if (values[i] > values[i-1]) deltas.push_back(values[i] - values[i-1]);
      if (!deltas.empty()) {
        auto median = deltas.begin() + deltas.size()/2;
        std::nth_element(deltas.begin(), median, deltas.end());
        // Millisecond epoch values are distinguishable from second epochs by magnitude.
        const double seconds = std::abs(values.front()) >= 1e12 ? *median / 1000 : *median;
        cycle = QStringLiteral("%1 s").arg(seconds, 0, 'g', 6);
      }
    }
  }
  return QStringLiteral("%1: %2 \u70b9 / %3").arg(table).arg(fields.size() - (time != fields.cend())).arg(cycle);
}

inline File inspect(const QString& path, bool sample)
{
  using namespace BladeSqliteRecording;
  File file;
  file.path = path;
  file.id = identify(path);
  if (!validHeader(path)) return file;
  Connection connection(path);
  if (!connection.db.isOpen()) return file;
  QStringList details;
  QJsonObject schemaKey;
  for (const auto& table : tables(connection.db)) {
    auto tags = columns(connection.db, table);
    if (tags.isEmpty()) continue;
    file.schema.insert(table, tags);
    if (sample) details << tableDetails(connection.db, table, tags);
    tags.sort(Qt::CaseSensitive);
    QJsonArray names;
    for (const auto& tag : tags) names.append(tag);
    schemaKey.insert(table, names);
  }
  file.details = details.join(" | ");
  file.signature = file.id.kind + ":" + file.id.category + ":" +
                   QString::fromUtf8(QJsonDocument(schemaKey).toJson(QJsonDocument::Compact));
  return file;
}

using Group = std::vector<File>;

inline std::vector<Group> groups(std::vector<File> files)
{
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) {
    if (a.id.key() != b.id.key()) return a.id.key() < b.id.key();
    if (a.id.date != b.id.date) return a.id.date < b.id.date;
    return a.path < b.path;
  });
  std::vector<Group> result;
  for (auto& file : files) {
    if (result.empty() || !file.id.date.isValid() ||
        result.back().back().id.key() != file.id.key() ||
        result.back().back().id.date.daysTo(file.id.date) > 1)
      result.emplace_back();
    result.back().push_back(std::move(file));
  }
  return result;
}

inline QStringList importFiles(QWidget* parent, const QStringList& paths,
                          std::unordered_set<std::string> names,
                          const std::function<void(PJ::PlotDataMapRef&)>& receive)
{
  using namespace BladeSqliteRecording;
  if (paths.isEmpty()) return {};
  std::vector<File> files;
  QStringList messages;
  for (const auto& path : paths) {
    auto file = inspect(path, false);
    if (!file.schema.isEmpty()) files.push_back(std::move(file));
    else messages << QFileInfo(path).fileName() + QStringLiteral(": \u65e0\u53ef\u8bfb\u6570\u636e\u8868");
  }
  QHash<QString, RecordingOptions> selections;
  QSet<QString> skipped;
  for (const auto& group : groups(std::move(files))) {
    // Different schemas are imported separately, even within a continuous date span.
    QMap<QString, QStringList> compatible;
    for (const auto& file : group) compatible[file.signature] << file.path;
    for (auto it = compatible.cbegin(); it != compatible.cend(); ++it) {
      const auto& signature = it.key();
      const auto& members = it.value();
      if (skipped.contains(signature)) continue;
      if (!selections.contains(signature)) {
        const auto first = std::find_if(group.cbegin(), group.cend(),
            [&](const File& f) { return f.path == members.front(); });
        const auto options = selectRecording(parent, QStringLiteral("SQLite \u5f55\u6ce2\u6279\u91cf\u5bfc\u5165"),
            first->path, first->schema.keys(),
            [&](const QString& table) { return first->schema.value(table); });
        if (!options) {
          skipped.insert(signature);
          continue;
        }
        selections.insert(signature, *options);
      }
      const auto options = selections.value(signature);
      std::atomic_bool canceled{false};
      PJ::PlotDataMapRef data;
      QStringList errors;
      int loaded = 0;
      size_t totalRows = 0;
      QProgressDialog progress(QStringLiteral("\u6b63\u5728\u8bfb\u53d6 DATA \u5f55\u6ce2"), QStringLiteral("\u53d6\u6d88"), 0, 0, parent);
      progress.setWindowModality(Qt::WindowModal);
      auto worker = std::unique_ptr<QThread>(QThread::create([&] {
        try {
          using Sample = std::pair<double, double>;
          std::map<std::string, std::vector<Sample>> points;
          for (const auto& member : members) {
            if (canceled) return;
            auto result = read(member, options.selection, {}, canceled);
            if (!result.error.isEmpty() || result.data.numeric.empty()) {
              errors << QFileInfo(member).fileName() + ": " +
                  (result.error.isEmpty() ? QStringLiteral("\u9009\u5b9a\u8303\u56f4\u65e0\u6709\u6548\u6d4b\u70b9") : result.error);
              continue;
            }
            ++loaded;
            totalRows += size_t(result.rows);
            for (const auto& entry : result.data.numeric) {
              auto& samples = points[entry.first];
              const auto& curve = entry.second;
              for (size_t i = 0; i < curve.size() && !canceled; ++i)
                samples.emplace_back(curve.at(i).x, curve.at(i).y);
            }
          }
          const auto byTime = [](const Sample& a, const Sample& b) { return a.first < b.first; };
          for (auto& entry : points) {
            if (canceled) return;
            auto name = entry.first;
            for (int suffix = 2; !names.insert(name).second; ++suffix)
              name = entry.first + " [" + std::to_string(suffix) + "]";
            auto& pointsForTag = entry.second;
            if (!std::is_sorted(pointsForTag.cbegin(), pointsForTag.cend(), byTime))
              std::stable_sort(pointsForTag.begin(), pointsForTag.end(), byTime);
            auto& target = data.addNumeric(name)->second;
            for (const auto& p : pointsForTag) {
              if (canceled) return;
              target.pushBack({p.first, p.second});
            }
            std::vector<Sample>().swap(pointsForTag);
          }
        } catch (const std::exception& ex) {
          data.numeric.clear();
          errors << QString::fromUtf8(ex.what());
        }
      }));
      QEventLoop loop;
      QObject::connect(worker.get(), &QThread::finished, &loop, &QEventLoop::quit);
      QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] { canceled = true; });
      progress.show();
      worker->start();
      loop.exec();
      worker->wait();
      progress.hide();
      if (canceled) return messages;
      messages << errors;
      if (!data.numeric.empty()) {
        messages << QStringLiteral("%1: %2 \u4e2a\u6587\u4ef6, %3 \u884c, %4 \u4e2a\u6d4b\u70b9")
            .arg(QFileInfo(members.front()).fileName())
            .arg(loaded).arg(qulonglong(totalRows)).arg(qulonglong(data.numeric.size()));
        BladeTimeDisplay::setPrecision(options.precision);
        receive(data);
      }
    }
  }
  return messages;
}

inline void open(QWidget* parent, const std::function<void(const QStringList&)>& load)
{
  const auto directory = QFileDialog::getExistingDirectory(parent, QStringLiteral("DATA \u5f55\u6ce2\u76ee\u5f55"));
  if (directory.isEmpty()) return;
  std::vector<File> files;
  std::atomic_bool canceled{false};
  QProgressDialog progress(QStringLiteral("\u6b63\u5728\u626b\u63cf DATA \u5f55\u6ce2"), QStringLiteral("\u53d6\u6d88"), 0, 0, parent);
  progress.setWindowModality(Qt::WindowModal);
  auto worker = std::unique_ptr<QThread>(QThread::create([&] {
    QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
    while (!canceled && it.hasNext()) {
      const auto path = it.next();
      if (QFileInfo(path).suffix().compare("data", Qt::CaseInsensitive) == 0)
        files.push_back(inspect(path, true));
    }
  }));
  QEventLoop loop;
  QObject::connect(worker.get(), &QThread::finished, &loop, &QEventLoop::quit);
  QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] { canceled = true; });
  progress.show();
  worker->start();
  loop.exec();
  worker->wait();
  progress.hide();
  if (canceled) return;
  const auto rows = groups(std::move(files));
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("DATA \u5f55\u6ce2\u76ee\u5f55\u7d22\u5f15"));
  dialog.resize(1000, 520);
  QVBoxLayout layout(&dialog);
  auto* table = new QTableWidget(int(rows.size()), 6, &dialog);
  table->setHorizontalHeaderLabels({QStringLiteral("\u98ce\u573a"), QStringLiteral("\u5206\u7c7b"),
      QStringLiteral("\u673a\u7ec4"), QStringLiteral("\u5f55\u6ce2\u65e5\u671f"),
      QStringLiteral("\u6587\u4ef6\u6570"), QStringLiteral("\u6570\u636e\u8868 / \u70b9\u6570 / \u4f30\u7b97\u91c7\u6837\u5468\u671f")});
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setStretchLastSection(true);
  for (int i = 0; i < int(rows.size()); ++i) {
    const auto& group = rows[size_t(i)];
    const auto& first = group.front();
    QString dates = first.id.date.toString(Qt::ISODate);
    if (first.id.date != group.back().id.date) dates += " ~ " + group.back().id.date.toString(Qt::ISODate);
    if (dates.isEmpty()) dates = QFileInfo(first.path).fileName();
    QStringList variants, tooltips;
    for (const auto& file : group) {
      const auto details = file.details.isEmpty() ? QStringLiteral("\u65e0\u53ef\u8bfb\u6570\u636e\u8868") : file.details;
      if (!variants.contains(details)) variants << details;
      tooltips << QFileInfo(file.path).fileName() + ": " + details;
    }
    const QStringList values{first.id.site, first.id.category, first.id.turbine, dates,
                             QString::number(qulonglong(group.size())), variants.join(" / ")};
    for (int j = 0; j < values.size(); ++j) {
      auto* item = new QTableWidgetItem(values[j]);
      item->setToolTip(tooltips.join('\n'));
      table->setItem(i, j, item);
    }
  }
  table->resizeColumnsToContents();
  layout.addWidget(table);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("\u52a0\u8f7d\u6240\u9009\u5f55\u6ce2"));
  buttons.button(QDialogButtonBox::Ok)->setEnabled(false);
  QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, [&] {
    buttons.button(QDialogButtonBox::Ok)->setEnabled(table->selectionModel()->hasSelection());
  });
  QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout.addWidget(&buttons);
  if (dialog.exec() != QDialog::Accepted) return;
  QStringList paths;
  for (const auto& row : table->selectionModel()->selectedRows())
    for (const auto& file : rows[size_t(row.row())]) paths << file.path;
  if (!paths.isEmpty()) load(paths);
}
}

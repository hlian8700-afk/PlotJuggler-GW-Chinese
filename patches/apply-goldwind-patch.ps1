# Applies the Goldwind display-name patch to the exact PlotJuggler 3.17.0 checkout.
$ErrorActionPreference = "Stop"
$file = "plotjuggler_app/curvelist_panel.cpp"
if (!(Test-Path $file)) { throw "Expected PlotJuggler 3.17.0 source file not found: $file" }
$src = Get-Content -Raw -Encoding UTF8 $file
if ($src.Contains("goldwindChineseName")) {
  Write-Host "Goldwind patch already present."
  exit 0
}
$includeNeedle = '#include <QTreeWidget>'
if (!$src.Contains($includeNeedle)) { throw "Could not find include insertion point." }
$src = $src.Replace($includeNeedle, @'
#include <QTreeWidget>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QTextStream>
'@)
$namespaceNeedle = @'
namespace
{
bool isCurveTreeItem('@
$namespaceReplacement = @'
namespace
{
const QHash<QString, QString>& goldwindChineseNames()
{
  static const QHash<QString, QString> names = [] {
    QHash<QString, QString> result;
    QFile file(QCoreApplication::applicationDirPath() + "/gw_tag_mapping.csv");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      qWarning() << "Goldwind tag dictionary not found:" << file.fileName();
      return result;
    }
    QTextStream stream(&file);
    bool first_line = true;
    while (!stream.atEnd())
    {
      QString line = stream.readLine();
      if (first_line)
      {
        first_line = false;
        if (!line.isEmpty() && line.front() == QChar(0xFEFF))
        {
          line.remove(0, 1);
        }
        if (line == "raw_tag,cn_name")
        {
          continue;
        }
      }
      const int comma = line.indexOf(',');
      if (comma <= 0)
      {
        continue;
      }
      const QString raw = line.left(comma).trimmed();
      const QString chinese = line.mid(comma + 1).trimmed();
      if (!raw.isEmpty() && !chinese.isEmpty())
      {
        result.insert(raw, chinese);
      }
    }
    qInfo() << "Loaded Goldwind Chinese labels:" << result.size();
    return result;
  }();
  return names;
}

QString goldwindChineseName(const QString& raw_name)
{
  return goldwindChineseNames().value(raw_name);
}

bool isCurveTreeItem'@
if (!$src.Contains($namespaceNeedle)) { throw "Could not find anonymous namespace insertion point." }
$src = $src.Replace($namespaceNeedle, $namespaceReplacement)
$addNeedle = '_tree_view->addItem(group_name, getTreeName(plot_id), plot_id);'
$addReplacement = @'
  const QString chinese_name = goldwindChineseName(plot_id);
  if (chinese_name.isEmpty())
  {
    _tree_view->addItem(group_name, getTreeName(plot_id), plot_id);
  }
  else
  {
    // Display only the Chinese alias; keep plot_id as the immutable raw series key.
    _tree_view->addItem(QString(), chinese_name, plot_id);
  }
'@
if (!$src.Contains($addNeedle)) { throw "Could not find addCurve insertion point." }
$src = $src.Replace($addNeedle, $addReplacement.TrimStart())
Set-Content -Path $file -Value $src -Encoding UTF8
Write-Host "Goldwind label patch applied."

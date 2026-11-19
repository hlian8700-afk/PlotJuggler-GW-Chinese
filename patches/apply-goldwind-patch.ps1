# Applies the Goldwind display-name patch to the exact PlotJuggler 3.17.0 checkout.
$ErrorActionPreference = "Stop"
$file = "plotjuggler_app/curvelist_panel.cpp"
if (!(Test-Path $file)) { throw "Expected PlotJuggler 3.17.0 source file not found: $file" }
$src = Get-Content -Raw -Encoding UTF8 $file
# Normalize CRLF from the Windows checkout so exact multiline anchors match.
$src = $src.Replace("`r`n", "`n")
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
#include <QComboBox>
'@)
$namespaceNeedle = "namespace`n{`nbool isCurveTreeItem("
$namespaceReplacement = @'
namespace
{
int& goldwindLabelMode()
{
  static int mode = 0; // 0 Chinese, 1 raw, 2 bilingual
  return mode;
}

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
    stream.setCodec("UTF-8");
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

bool isCurveTreeItem(
'@
if (!$src.Contains($namespaceNeedle)) { throw "Could not find anonymous namespace insertion point." }
$src = $src.Replace($namespaceNeedle, $namespaceReplacement)
$addPattern = '(?m)^[ \t]*_tree_view->addItem\(group_name, getTreeName\(plot_id\), plot_id\);'
$addReplacement = @'
  const QString chinese_name = goldwindChineseName(plot_id);
  const int label_mode = goldwindLabelMode();
  if (chinese_name.isEmpty() || label_mode == 1)
  {
    _tree_view->addItem(group_name, getTreeName(plot_id), plot_id);
  }
  else
  {
    // Display only the Chinese alias; keep plot_id as the immutable raw series key.
    const QString label = (label_mode == 2)
                              ? chinese_name + " (" + plot_id + ")"
                              : chinese_name;
    _tree_view->addItem(QString(), label, plot_id);
  }
'@
# Insert a compact selector above the native series tree. Raw IDs remain unchanged.
$layoutNeedle = '  layout1->addWidget(_tree_view, 1);'
$layoutReplacement = @'
  auto* goldwindMode = new QComboBox(this);
  goldwindMode->addItem(QString::fromUtf8("中文标签"));
  goldwindMode->addItem(QString::fromUtf8("原始标签"));
  goldwindMode->addItem(QString::fromUtf8("中英文对照"));
  goldwindMode->setToolTip(QString::fromUtf8("金风标签显示模式"));
  layout1->addWidget(goldwindMode);
  layout1->addWidget(_tree_view, 1);
  connect(goldwindMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int mode) {
    goldwindLabelMode() = mode;
    const auto existing = _tree_view_items;
    _tree_view->clear();
    _tree_view_items.clear();
    for (const auto& name : existing)
    {
      addCurve(name);
    }
    refreshColumns();
  });
'@
if (!$src.Contains($layoutNeedle)) { throw "Could not find series tree layout insertion point." }
$src = $src.Replace('auto layout1 = new QHBoxLayout();', 'auto layout1 = new QVBoxLayout();')
$src = $src.Replace($layoutNeedle, $layoutReplacement)
if (![regex]::IsMatch($src, $addPattern)) { throw "Could not find addCurve insertion point." }
$src = [regex]::Replace($src, $addPattern, $addReplacement.Trim(), [System.Text.RegularExpressions.RegexOptions]::Multiline)
Set-Content -Path $file -Value $src -Encoding UTF8
Write-Host "Goldwind label patch applied."

# Disable the upstream startup splash image by default in this custom build.
# Do not remove unrelated icons, logos, or image assets.
$mainFile = "plotjuggler_app/main.cpp"
$mainSrc = (Get-Content -Raw -Encoding UTF8 $mainFile).Replace("`r`n", "`n")
$before = 'settings.value("Preferences::no_splash", false).toBool()'
$after = 'settings.value("Preferences::no_splash", true).toBool()'
if (!$mainSrc.Contains($before)) { throw "Splash preference anchor not found in main.cpp" }
$mainSrc = $mainSrc.Replace($before, $after)
# Also bypass the splash unconditionally: existing user settings may explicitly
# contain no_splash=false, which would otherwise override the new default.
$splashGuard = 'if (!parser.isSet(nosplash_option) &&'
if (!$mainSrc.Contains($splashGuard)) { throw "Splash guard not found in main.cpp" }
$mainSrc = $mainSrc.Replace($splashGuard, 'if (false && !parser.isSet(nosplash_option) &&')
Set-Content -Path $mainFile -Value $mainSrc -Encoding UTF8
Write-Host "Goldwind build defaults to no startup splash screen."

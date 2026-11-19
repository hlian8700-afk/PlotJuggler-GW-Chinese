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
#include <QPushButton>
#include <QHBoxLayout>
#include <QTreeWidgetItemIterator>
'@)
$namespaceNeedle = "namespace`n{`nbool isCurveTreeItem("
$namespaceReplacement = @'
namespace
{
bool& goldwindGroupBySystem()
{
  static bool enabled = false;
  return enabled;
}

QString goldwindSystemGroup(const QString& raw)
{
  const QString prefix = raw.section('.', 0, 0).toUpper();
  if (prefix == "WTPS") return QString::fromUtf8("变桨系统");
  if (prefix == "WCNV") return QString::fromUtf8("变流器");
  if (prefix == "WYAW") return QString::fromUtf8("偏航系统");
  if (prefix == "WTUR") return QString::fromUtf8("风机主控");
  return QString::fromUtf8("其他测点");
}

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
  const QString category = goldwindGroupBySystem() ? goldwindSystemGroup(plot_id) : QString();
  if (chinese_name.isEmpty() || label_mode == 1)
  {
    _tree_view->addItem(category.isEmpty() ? group_name : category, getTreeName(plot_id), plot_id);
  }
  else
  {
    // Display only the Chinese alias; keep plot_id as the immutable raw series key.
    const QString label = (label_mode == 2)
                              ? chinese_name + " (" + plot_id + ")"
                              : chinese_name;
    _tree_view->addItem(category, label, plot_id);
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
  auto* goldwindToolbar = new QHBoxLayout();
  goldwindToolbar->addWidget(goldwindMode);
  auto* goldwindGroup = new QPushButton(QString::fromUtf8("按系统分类"), this);
  goldwindGroup->setCheckable(true);
  goldwindToolbar->addWidget(goldwindGroup);
  auto* goldwindSelect = new QPushButton(QString::fromUtf8("选择筛选结果"), this);
  goldwindSelect->setToolTip(QString::fromUtf8("多选当前可见的测点，然后拖动所选测点到绘图区"));
  goldwindToolbar->addWidget(goldwindSelect);
  layout1->addLayout(goldwindToolbar);
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
  connect(goldwindGroup, &QPushButton::toggled, this, [this](bool checked) {
    goldwindGroupBySystem() = checked;
    const auto existing = _tree_view_items;
    _tree_view->clear();
    _tree_view_items.clear();
    for (const auto& name : existing)
    {
      addCurve(name);
    }
    refreshColumns();
  });
  connect(goldwindSelect, &QPushButton::clicked, this, [this]() {
    _tree_view->clearSelection();
    QTreeWidgetItemIterator it(_tree_view);
    while (*it)
    {
      auto* item = *it;
      if (!item->isHidden() && isCurveTreeItem(item))
      {
        bool parentsVisible = true;
        for (auto* parent = item->parent(); parent; parent = parent->parent())
        {
          if (parent->isHidden()) { parentsVisible = false; break; }
        }
        if (parentsVisible) item->setSelected(true);
      }
      ++it;
    }
  });
'@
if (!$src.Contains($layoutNeedle)) { throw "Could not find series tree layout insertion point." }
$src = $src.Replace('auto layout1 = new QHBoxLayout();', 'auto layout1 = new QVBoxLayout();')
$src = $src.Replace($layoutNeedle, $layoutReplacement)
# Localize the most frequently used series-list controls without changing raw data keys.
$localizationNeedle = '  ui->setupUi(this);'
$localizationReplacement = @'
  ui->setupUi(this);
  ui->lineEditFilter->setPlaceholderText(QString::fromUtf8("搜索中文标签或测点编码..."));
  ui->lineEditFilter->setToolTip(QString::fromUtf8("支持中文描述、英文编码及空格分隔的多个关键词"));
  ui->lineEditCustomFilter->setPlaceholderText(QString::fromUtf8("搜索自定义曲线..."));
'@
if (!$src.Contains($localizationNeedle)) { throw "Could not find panel localization anchor" }
$src = $src.Replace($localizationNeedle, $localizationReplacement)

if (![regex]::IsMatch($src, $addPattern)) { throw "Could not find addCurve insertion point." }
$src = [regex]::Replace($src, $addPattern, $addReplacement.Trim(), [System.Text.RegularExpressions.RegexOptions]::Multiline)
$searchTagNeedle = '  _tree_view_items.insert(plot_name);'
$searchTagReplacement = @'
  if (!chinese_name.isEmpty())
  {
    // Searchable Chinese alias in every display mode, including raw-only.
    QTreeWidgetItemIterator search_it(_tree_view);
    while (*search_it)
    {
      auto* item = *search_it;
      if (item->data(0, CustomRoles::Name).toString() == plot_id)
      {
        item->setData(0, Qt::ToolTipRole, chinese_name);
        break;
      }
      ++search_it;
    }
  }
  _tree_view_items.insert(plot_name);
'@
if (!$src.Contains($searchTagNeedle)) { throw "Missing tag insertion anchor" }
$src = $src.Replace($searchTagNeedle, $searchTagReplacement)

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

# Search fix: upstream CurveTreeView filters Qt::UserRole (the raw series ID),
# not the displayed Chinese alias. Match both fields, independent of label mode.
$treeFile = "plotjuggler_app/curvetree_view.cpp"
$treeSrc = (Get-Content -Raw -Encoding UTF8 $treeFile).Replace("`r`n", "`n")
$searchBefore = 'if (name.contains(spaced_item, Qt::CaseInsensitive) == false)'
$searchAfter = 'if (!name.contains(spaced_item, Qt::CaseInsensitive) && !item->text(0).contains(spaced_item, Qt::CaseInsensitive) && !item->data(0, Qt::ToolTipRole).toString().contains(spaced_item, Qt::CaseInsensitive))'
if (!$treeSrc.Contains($searchBefore)) { throw "Could not find CurveTreeView filter anchor" }
$treeSrc = $treeSrc.Replace($searchBefore, $searchAfter)
Set-Content -Path $treeFile -Value $treeSrc -Encoding UTF8
Write-Host "Goldwind search now matches visible labels and raw series IDs."


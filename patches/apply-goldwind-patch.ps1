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
#include <QGridLayout>
#include <QTreeWidgetItemIterator>
#include <QTimer>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSaveFile>
#include <QTextCodec>
#include <QDir>
'@)
$namespaceNeedle = "namespace`n{`nbool isCurveTreeItem("
$namespaceReplacement = @'
void goldwindReloadSearchAliases();
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

QHash<QString, QString>& goldwindChineseNames()
{
  static QHash<QString, QString> names = [] {
    QHash<QString, QString> result;
    const QString userMap = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gw_tag_mapping.csv";
    QFile file(QFile::exists(userMap) ? userMap : QCoreApplication::applicationDirPath() + "/gw_tag_mapping.csv");
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

void goldwindReloadChineseNames()
{
  auto& names = goldwindChineseNames();
  names.clear();
  const QString userMap = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gw_tag_mapping.csv";
  QFile file(QFile::exists(userMap) ? userMap : QCoreApplication::applicationDirPath() + "/gw_tag_mapping.csv");
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
  QTextStream stream(&file);
  stream.setCodec("UTF-8");
  while (!stream.atEnd())
  {
    const QString line = stream.readLine();
    const int comma = line.indexOf(',');
    if (comma <= 0) continue;
    const QString key = line.left(comma).trimmed();
    const QString value = line.mid(comma + 1).trimmed();
    if (!key.isEmpty() && !value.isEmpty()) names.insert(key, value);
  }
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
  auto* goldwindToolbar = new QGridLayout();
  goldwindToolbar->setContentsMargins(0, 0, 0, 0);
  goldwindToolbar->setHorizontalSpacing(5);
  goldwindToolbar->setVerticalSpacing(5);
  goldwindToolbar->addWidget(goldwindMode, 0, 0);
  auto* goldwindGroup = new QPushButton(QString::fromUtf8("按系统分类"), this);
  goldwindGroup->setCheckable(true);
  goldwindToolbar->addWidget(goldwindGroup, 0, 1);
  auto* goldwindSelect = new QPushButton(QString::fromUtf8("选择筛选结果"), this);
  goldwindSelect->setToolTip(QString::fromUtf8("多选当前可见的测点，然后拖动所选测点到绘图区"));
  goldwindToolbar->addWidget(goldwindSelect, 1, 0);
  layout1->addLayout(goldwindToolbar);
  auto* goldwindImport = new QPushButton(QString::fromUtf8("导入标签映射"), this);
  goldwindImport->setToolTip(QString::fromUtf8("导入 CSV 文件并全部替换当前标签映射"));
  goldwindToolbar->addWidget(goldwindImport, 1, 1);
  connect(goldwindImport, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("选择新的标签映射 CSV"), QString(),
        QString::fromUtf8("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      QMessageBox::warning(this, QString::fromUtf8("导入失败"), input.errorString());
      return;
    }
    // Detect UTF-8 (with or without BOM) first; fall back to GB18030/GBK.
    QByteArray rawBytes = input.readAll();
    if (rawBytes.startsWith("\xEF\xBB\xBF")) rawBytes.remove(0, 3);
    QTextCodec::ConverterState utf8State;
    QTextCodec* utf8Codec = QTextCodec::codecForName("UTF-8");
    QString decoded = utf8Codec->toUnicode(rawBytes.constData(), rawBytes.size(), &utf8State);
    QString detectedEncoding = "UTF-8";
    if (utf8State.invalidChars > 0)
    {
      QTextCodec* chineseCodec = QTextCodec::codecForName("GB18030");
      if (!chineseCodec)
      {
        QMessageBox::warning(this, QString::fromUtf8("导入失败"),
                             QString::fromUtf8("当前程序不支持 GB18030 编码，文件未被修改。"));
        return;
      }
      QTextCodec::ConverterState gbState;
      decoded = chineseCodec->toUnicode(rawBytes.constData(), rawBytes.size(), &gbState);
      if (gbState.invalidChars > 0 || decoded.contains(QChar::ReplacementCharacter))
      {
        QMessageBox::warning(this, QString::fromUtf8("编码识别失败"),
                             QString::fromUtf8("文件既不是有效 UTF-8，也无法按 GB18030 解码。原映射保持不变。"));
        return;
      }
      detectedEncoding = "GB18030/GBK";
    }
    QHash<QString, QString> checked;
    const QStringList lines = decoded.split('\n');
    bool first = true;
    for (QString line : lines)
    {
      if (line.endsWith('\r')) line.chop(1);
      if (first)
      {
        first = false;
        if (line.trimmed().compare("raw_tag,cn_name", Qt::CaseInsensitive) == 0) continue;
      }
      if (line.trimmed().isEmpty()) continue;
      const int comma = line.indexOf(',');
      if (comma <= 0) continue;
      const QString key = line.left(comma).trimmed();
      const QString val = line.mid(comma + 1).trimmed();
      if (!key.isEmpty() && !val.isEmpty()) checked.insert(key, val);
    }
    if (checked.isEmpty())
    {
      QMessageBox::warning(this, QString::fromUtf8("导入失败"),
                           QString::fromUtf8("未找到有效映射。需要 raw_tag,cn_name 两列"));
      return;
    }
    if (QMessageBox::question(this, QString::fromUtf8("确认全部替换"),
          QString::fromUtf8("将使用新文件的 %1 条标签完全替换当前映射。未包含的旧标签不再显示中文。是否继续？").arg(checked.size()),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dir))
    {
      QMessageBox::warning(this, QString::fromUtf8("导入失败"), QString::fromUtf8("无法创建配置目录"));
      return;
    }
    QSaveFile output(dir + "/gw_tag_mapping.csv");
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QMessageBox::warning(this, QString::fromUtf8("导入失败"), output.errorString());
      return;
    }
    QTextStream out(&output);
    out.setCodec("UTF-8");
    out << "raw_tag,cn_name\n";
    for (auto it = checked.cbegin(); it != checked.cend(); ++it)
      out << it.key() << "," << it.value() << "\n";
    out.flush();
    if (!output.commit())
    {
      QMessageBox::warning(this, QString::fromUtf8("导入失败"), output.errorString());
      return;
    }
    goldwindReloadChineseNames();
    goldwindReloadSearchAliases();
    const auto existing = _tree_view_items;
    _tree_view->setUpdatesEnabled(false);
    _tree_view->clear();
    _tree_view_items.clear();
    for (const auto& name : existing) addCurve(name);
    _tree_view->setUpdatesEnabled(true);
    refreshColumns();
    QMessageBox::information(this, QString::fromUtf8("导入成功"),
                             QString::fromUtf8("已全部替换为 %1 条标签映射（识别编码：%2）").arg(checked.size()).arg(detectedEncoding));
  });
  layout1->addWidget(_tree_view, 1);
  connect(goldwindMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int mode) {
    goldwindLabelMode() = mode;
    const auto existing = _tree_view_items;
    _tree_view->setUpdatesEnabled(false);
    _tree_view->clear();
    _tree_view_items.clear();
    for (const auto& name : existing)
    {
      addCurve(name);
    }
    _tree_view->setUpdatesEnabled(true);
    refreshColumns();
  });
  connect(goldwindGroup, &QPushButton::toggled, this, [this](bool checked) {
    goldwindGroupBySystem() = checked;
    const auto existing = _tree_view_items;
    _tree_view->setUpdatesEnabled(false);
    _tree_view->clear();
    _tree_view_items.clear();
    for (const auto& name : existing)
    {
      addCurve(name);
    }
    _tree_view->setUpdatesEnabled(true);
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
  ui->lineEditFilter->setClearButtonEnabled(true);
  ui->lineEditCustomFilter->setClearButtonEnabled(true);
  auto* goldwindSearchTimer = new QTimer(this);
  goldwindSearchTimer->setObjectName("goldwindSearchTimer");
  goldwindSearchTimer->setSingleShot(true);
  goldwindSearchTimer->setInterval(180);
  connect(goldwindSearchTimer, &QTimer::timeout, this, [this]() {
    const QString search_string = ui->lineEditFilter->text();
    bool updated = _tree_view->applyVisibilityFilter(search_string);
    const auto counts = _tree_view->hiddenItemsCount();
    const int visible_count = counts.second - counts.first;
    ui->labelNumberDisplayed->setText(QString::number(visible_count) +
                                     QString::fromUtf8(" / ") + QString::number(counts.second));
    if (updated) emit hiddenItemsChanged();
  });
'@
if (!$src.Contains($localizationNeedle)) { throw "Could not find panel localization anchor" }
$src = $src.Replace($localizationNeedle, $localizationReplacement)
# Debounce text edits for 100-500 MB datasets with thousands of columns.
# Keep the upstream filtering method intact for all non-typing refresh paths.
$filterMethodBefore = @'
void CurveListPanel::on_lineEditFilter_textChanged(const QString& search_string)
{
  bool updated = _tree_view->applyVisibilityFilter(search_string);
'@
$filterMethodAfter = @'
void CurveListPanel::on_lineEditFilter_textChanged(const QString& search_string)
{
  auto* searchTimer = findChild<QTimer*>("goldwindSearchTimer");
  if (searchTimer && ui->lineEditFilter->hasFocus())
  {
    searchTimer->start();
    return;
  }
  bool updated = _tree_view->applyVisibilityFilter(search_string);
'@
$filterPattern = 'void CurveListPanel::on_lineEditFilter_textChanged\(const QString& search_string\)\s*\{\s*bool updated = _tree_view->applyVisibilityFilter\(search_string\);'
if (![regex]::IsMatch($src, $filterPattern)) { throw "Missing search debounce anchor" }
$src = [regex]::Replace($src, $filterPattern, $filterMethodAfter.Trim())


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

# Search fix: upstream CurveTreeView filters Qt::UserRole (the raw series ID),
# not the displayed Chinese alias. Match both fields, independent of label mode.
$treeFile = "plotjuggler_app/curvetree_view.cpp"
$treeSrc = (Get-Content -Raw -Encoding UTF8 $treeFile).Replace("`r`n", "`n")
$searchBefore = 'if (name.contains(spaced_item, Qt::CaseInsensitive) == false)'
$searchAfter = 'if (!name.contains(spaced_item, Qt::CaseInsensitive) && !item->text(0).contains(spaced_item, Qt::CaseInsensitive) && !item->data(0, Qt::ToolTipRole).toString().contains(spaced_item, Qt::CaseInsensitive))'
if (!$treeSrc.Contains($searchBefore)) { throw "Could not find CurveTreeView filter anchor" }
$treeSrc = $treeSrc.Replace($searchBefore, $searchAfter)
# Cache the Chinese lookup once in CurveTreeView and set tooltips when leaves are
# inserted. Avoid scanning the entire growing tree once for each of 1,981 tags.
$treeInclude = '#include <QClipboard>'
if (!$treeSrc.Contains($treeInclude)) { throw "Missing tree include anchor" }
$treeSrc = $treeSrc.Replace($treeInclude, $treeInclude + "`n#include <QCoreApplication>`n#include <QFile>`n#include <QHash>`n#include <QTextStream>`n#include <QStandardPaths>")
$treeNamespace = "namespace`n{`nbool isCurveItem("
$treeLookup = @'
namespace
{
QHash<QString, QString>& goldwindSearchAliases()
{
  static QHash<QString, QString> aliases = [] {
    QHash<QString, QString> result;
    const QString userMap = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gw_tag_mapping.csv";
    QFile file(QFile::exists(userMap) ? userMap : QCoreApplication::applicationDirPath() + "/gw_tag_mapping.csv");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return result;
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    while (!stream.atEnd())
    {
      const QString line = stream.readLine();
      const int comma = line.indexOf(',');
      if (comma <= 0) continue;
      const QString key = line.left(comma).trimmed();
      const QString value = line.mid(comma + 1).trimmed();
      if (!key.isEmpty() && !value.isEmpty()) result.insert(key, value);
    }
    return result;
  }();
  return aliases;
}
bool isCurveItem(
'@
if (!$treeSrc.Contains($treeNamespace)) { throw "Missing tree lookup insertion anchor" }
$treeSrc = $treeSrc.Replace($treeNamespace, $treeLookup)
$treeLeafNeedle = 'setData(0, Name, plot_ID);'
$treeLeafReplacement = 'setData(0, Name, plot_ID);'
# Both existing-leaf and new-leaf paths assign Name; attach alias at that point.
$treeSrc = $treeSrc.Replace('matching_child->' + $treeLeafNeedle, 'matching_child->' + $treeLeafReplacement + "`n        matching_child->setToolTip(0, goldwindSearchAliases().value(plot_ID));")
$treeSrc = $treeSrc.Replace('child_item->' + $treeLeafNeedle, 'child_item->' + $treeLeafReplacement + "`n        child_item->setToolTip(0, goldwindSearchAliases().value(plot_ID));")

$reloadAnchor = '}  // namespace'
$reloadBlock = @'
}  // namespace
void goldwindReloadSearchAliases()
{
  auto& aliases = goldwindSearchAliases();
  aliases.clear();
  const QString userMap = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gw_tag_mapping.csv";
  QFile file(QFile::exists(userMap) ? userMap : QCoreApplication::applicationDirPath() + "/gw_tag_mapping.csv");
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
  QTextStream stream(&file);
  stream.setCodec("UTF-8");
  while (!stream.atEnd())
  {
    const QString line = stream.readLine();
    const int comma = line.indexOf(',');
    if (comma <= 0) continue;
    const QString key = line.left(comma).trimmed();
    const QString value = line.mid(comma + 1).trimmed();
    if (!key.isEmpty() && !value.isEmpty()) aliases.insert(key, value);
  }
}
'@
if (!$treeSrc.Contains($reloadAnchor)) { throw "Missing tree namespace end" }
$treeSrc = $treeSrc.Replace($reloadAnchor, $reloadBlock)
Set-Content -Path $treeFile -Value $treeSrc -Encoding UTF8
Write-Host "Goldwind search now matches visible labels and raw series IDs."


# Stream CSV from disk instead of keeping both QByteArray and std::string copies.
# This targets peak memory usage for 100-500 MB recordings; parsing remains unchanged.
$csvFile = "plotjuggler_plugins/DataLoadCSV/dataload_csv.cpp"
$csvSrc = (Get-Content -Raw -Encoding UTF8 $csvFile).Replace("`r`n", "`n")
$csvInclude = '#include <array>'
if (!$csvSrc.Contains($csvInclude)) { throw "Missing CSV include anchor" }
$csvSrc = $csvSrc.Replace($csvInclude, "#include <fstream>`n#include <filesystem>`n" + $csvInclude)
$csvBefore = @'
  file.open(QFile::ReadOnly);
  QByteArray file_data = file.readAll();
  file.close();

  std::string file_str(file_data.constData(), file_data.size());

  auto result = PJ::CSV::ParseCsvData(file_str, config, [&](int current, int) -> bool {
'@
$csvAfter = @'
  // Avoid allocating two additional copies of the complete CSV in memory.
  // std::filesystem::path preserves Unicode Windows filenames.
  std::ifstream csv_stream(std::filesystem::path(file.fileName().toStdWString()),
                           std::ios::binary);
  if (!csv_stream.is_open())
  {
    QMessageBox::warning(nullptr, QString::fromUtf8("CSV 文件读取失败"),
                         QString::fromUtf8("无法打开文件：") + file.fileName());
    return false;
  }
  auto result = PJ::CSV::ParseCsvData(csv_stream, config, [&](int current, int) -> bool {
'@
$csvPattern = '(?s)  file\.open\(QFile::ReadOnly\);\s*QByteArray file_data = file\.readAll\(\);\s*file\.close\(\);\s*std::string file_str\(file_data\.constData\(\), file_data\.size\(\)\);\s*auto result = PJ::CSV::ParseCsvData\(file_str, config, \[&\]\(int current, int\) -> bool \{'
if (![regex]::IsMatch($csvSrc, $csvPattern)) { throw "Missing CSV streaming replacement anchor" }
$csvSrc = [regex]::Replace($csvSrc, $csvPattern, $csvAfter.TrimEnd())
$csvSrc = $csvSrc.Replace('progress_dialog.setWindowTitle("Loading the CSV file");', 'progress_dialog.setWindowTitle(QString::fromUtf8("正在导入 CSV 文件"));')
$csvSrc = $csvSrc.Replace('progress_dialog.setLabelText("Loading... please wait");', 'progress_dialog.setLabelText(QString::fromUtf8("正在读取录波数据，请稍候..."));')
Set-Content -Path $csvFile -Value $csvSrc -Encoding UTF8
Write-Host "Goldwind CSV streaming import and Chinese progress labels applied."

# Phase 4: translate commonly used MainWindow UI strings at Qt UI compilation time.
# This leaves QAction names, signals, keyboard shortcuts and internal data keys intact.
$uiFile = "plotjuggler_app/mainwindow.ui"
$uiSrc = (Get-Content -Raw -Encoding UTF8 $uiFile).Replace("`r`n", "`n")
$translations = [ordered]@{
  'File' = '文件'
  'Data:' = '数据：'
  'Add prefix' = '添加前缀'
  'Merge data' = '合并数据'
  'Layout:' = '布局：'
  'Streaming' = '实时数据'
  'Start' = '开始'
  'Buffer:' = '缓冲区：'
  'Publishers' = '发布器'
  'Timeseries List' = '时间序列列表'
  'Show point in plot' = '在曲线图中显示数据点'
  'Keep the aspect ratio of XY plots' = '保持 XY 图纵横比'
  'Speed:' = '速度：'
  'Step size:' = '步长：'
  'File Menu (ALT+F)' = '文件菜单 (ALT+F)'
  'App' = '应用'
  'Help (ALT+H)' = '帮助 (ALT+H)'
  'Help' = '帮助'
  'Cloud' = '云服务'
  'Tools' = '工具'
  'Exit' = '退出'
  'About' = '关于'
  'Report a problem' = '报告问题'
  'Report issue on GitHub' = '在 GitHub 上报告问题'
  'Clear data points' = '清空数据点'
  'Cheatsheet' = '快捷操作指南'
  'Support PlotJuggler' = '支持 PlotJuggler'
  'Clear' = '清空'
  'Delete everything' = '删除全部数据'
  'Preferences...' = '首选项...'
  'Share the love' = '分享 PlotJuggler'
  'Load StyleSheet' = '加载界面样式'
  'ColorMap Editor' = '颜色映射编辑器'
  'Hide/Show' = '隐藏/显示'
  'Recent data files' = '最近打开的文件'
  'Show/Hide the grid' = '显示/隐藏网格'
  'Zoom Out All' = '全部缩小'
  'Loop' = '循环播放'
  'Zoom In' = '放大'
  'Zoom Out' = '缩小'
  'Pause' = '暂停'
  'Play' = '播放'
  'Load data' = '加载数据'
  'Save layout' = '保存布局'
  'Load layout' = '加载布局'
  'Reset layout' = '重置布局'
  'Remove' = '移除'
  'Rename' = '重命名'
  'Duplicate' = '复制'
  'Undo' = '撤销'
  'Redo' = '重做'
  'Select all' = '全选'
  'Deselect all' = '取消全选'
  'Open' = '打开'
  'Save' = '保存'
  'Cancel' = '取消'
  'OK' = '确定'
}
foreach ($entry in $translations.GetEnumerator()) {
  $needle = '<string>' + $entry.Key + '</string>'
  $replacement = '<string>' + $entry.Value + '</string>'
  $uiSrc = $uiSrc.Replace($needle, $replacement)
  # Qt Designer stores tooltip text as escaped HTML inside <string> elements.
  $escapedNeedle = [System.Security.SecurityElement]::Escape($entry.Key)
  $escapedReplacement = [System.Security.SecurityElement]::Escape($entry.Value)
  $uiSrc = $uiSrc.Replace($escapedNeedle, $escapedReplacement)
}
# BladeScope branding: use the Qt main window title without changing application
# settings identity, preserving users' existing imported mapping CSV paths.
$uiSrc = $uiSrc.Replace('<string>PlotJuggler</string>', '<string>BladeScope 风机录波数据分析工具</string>')
Set-Content -Path $uiFile -Value $uiSrc -Encoding UTF8
Write-Host "Goldwind main-window Chinese localization applied."

# Localize remaining common Timeseries List labels and short search tooltips.
$panelUiFile = "plotjuggler_app/curvelist_panel.ui"
$panelUi = (Get-Content -Raw -Encoding UTF8 $panelUiFile).Replace("`r`n", "`n")
$panelUi = $panelUi.Replace('<string>Values</string>', '<string>当前值</string>')
$panelUi = $panelUi.Replace('<string>Custom Series:</string>', '<string>自定义曲线：</string>')
$panelUi = $panelUi.Replace('<string>Filter...</string>', '<string>筛选...</string>')
$panelUi = $panelUi.Replace('<string>  Filter...</string>', '<string>  筛选...</string>')
Set-Content -Path $panelUiFile -Value $panelUi -Encoding UTF8
Write-Host "Goldwind Timeseries List UI labels localized."

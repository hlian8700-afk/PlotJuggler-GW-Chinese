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
#include <QIcon>
#include <QFile>
#include <QCoreApplication>
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
  // Start with raw identifiers, matching the default state requested in the review.
  goldwindMode->setCurrentIndex(1);
  goldwindLabelMode() = 1;
  QCoreApplication::instance()->setProperty("bladeScopeLabelMode", 1);
  goldwindMode->setMinimumHeight(30);
  goldwindMode->setStyleSheet(
      "QComboBox{background:#fffaf5;border:1px solid #ead9ca;"
      "border-radius:5px;padding:4px 8px;color:#59483c;}"
      "QComboBox:hover{border-color:#c9946b;}");

  goldwindMode->setToolTip(QString::fromUtf8("金风标签显示模式"));
  auto* goldwindToolbar = new QGridLayout();
  goldwindToolbar->setContentsMargins(0, 0, 0, 0);
  goldwindToolbar->setHorizontalSpacing(5);
  goldwindToolbar->setVerticalSpacing(5);
  goldwindToolbar->addWidget(goldwindMode, 0, 0);
  auto* goldwindGroup = new QPushButton(QString::fromUtf8("系统分类"), this);
  goldwindGroup->setCheckable(true);
  goldwindGroup->setMinimumHeight(30);
  goldwindGroup->setStyleSheet(
      "QPushButton{background:#fffaf5;border:1px solid #ead9ca;"
      "border-radius:5px;padding:4px 8px;color:#59483c;}"
      "QPushButton:checked{background:#f2e2d4;border-color:#c9946b;color:#704e38;}"
      "QPushButton:hover{border-color:#c9946b;}");

  goldwindToolbar->addWidget(goldwindGroup, 0, 1);
  auto* goldwindSelect = new QPushButton(QString::fromUtf8("选择筛选结果"), this);
  goldwindSelect->setToolTip(QString::fromUtf8("多选当前可见的测点，然后拖动所选测点到绘图区"));
  goldwindSelect->hide(); // Search box replaces the separate selection button.
  layout1->addLayout(goldwindToolbar);
  auto* goldwindImport = new QPushButton(QString::fromUtf8("导入标签映射"), this);
  goldwindImport->setToolTip(QString::fromUtf8("导入 CSV 文件并全部替换当前标签映射"));
  goldwindImport->hide(); // Available from File menu.
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
    {
      QVariantMap aliases;
      for (auto it = goldwindChineseNames().cbegin(); it != goldwindChineseNames().cend(); ++it)
        aliases.insert(it.key(), it.value());
      QCoreApplication::instance()->setProperty("bladeScopeTagAliases", aliases);
    }
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
    QCoreApplication::instance()->setProperty("bladeScopeLabelMode", mode);
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
  {
    QVariantMap aliases;
    for (auto it = goldwindChineseNames().cbegin(); it != goldwindChineseNames().cend(); ++it)
      aliases.insert(it.key(), it.value());
    QCoreApplication::instance()->setProperty("bladeScopeTagAliases", aliases);
  }
  // Real leading search icon/action, independent of placeholder text.
  auto* goldwindSearchIcon = new QAction(QString::fromUtf8("⌕"), ui->lineEditFilter);
  QPixmap goldwindSearchPixmap(18, 18);
  goldwindSearchPixmap.fill(Qt::transparent);
  {
    QPainter painter(&goldwindSearchPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#95745b"), 1.7));
    painter.drawEllipse(QRectF(2.0, 2.0, 10.0, 10.0));
    painter.drawLine(QPointF(11.0, 11.0), QPointF(16.0, 16.0));
  }
  goldwindSearchIcon->setIcon(QIcon(goldwindSearchPixmap));
  ui->lineEditFilter->addAction(goldwindSearchIcon, QLineEdit::LeadingPosition);
  goldwindSearchIcon->setToolTip(QString::fromUtf8("搜索测点（支持中文和原始编码）"));
  connect(goldwindSearchIcon, &QAction::triggered, ui->lineEditFilter,
          static_cast<void (QWidget::*)()>(&QWidget::setFocus));
  ui->lineEditFilter->setToolTip(QString::fromUtf8("支持中文描述、英文编码及空格分隔的多个关键词"));
  ui->lineEditCustomFilter->setPlaceholderText(QString::fromUtf8("搜索自定义曲线..."));
  ui->lineEditFilter->setClearButtonEnabled(true);
  ui->checkBoxShowValues->setChecked(false);
  ui->checkBoxShowValues->hide();
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

# Apply BladeScope window/taskbar icon at runtime. Copying an ICO beside the EXE
# alone does not change the Qt window icon; this explicitly loads it in MainWindow.
$mainFile = "plotjuggler_app/mainwindow.cpp"
$mainSrc = (Get-Content -Raw -Encoding UTF8 $mainFile).Replace("`r`n", "`n")
$mainAnchor = "  QLocale::setDefault(QLocale::c());  // set as default"
if (!$mainSrc.Contains($mainAnchor)) { throw "MainWindow icon insertion anchor missing" }
$mainUiAnchor = "  ui->setupUi(this);"
if (!$mainSrc.Contains($mainUiAnchor)) { throw "MainWindow setupUi anchor missing" }
$mainSrc = $mainSrc.Replace($mainUiAnchor, @'
  ui->setupUi(this);
  // BladeScope compact viewer layout: preserve all upstream widgets and signals.
  // The left pane stays resizable; the chart gets the majority of the workspace.
  ui->leftLayout->setContentsMargins(10, 8, 10, 8);
  ui->leftLayout->setSpacing(5);
  ui->verticalLayout_2->setContentsMargins(0, 0, 0, 0);
  ui->plottingLayout->setSpacing(5);
  auto* signalHeader = new QLabel(QString::fromUtf8("测点列表"), ui->leftMainWindowFrame);
  signalHeader->setObjectName("BladeScopeSignalHeader");
  signalHeader->setStyleSheet("font-size:16px;font-weight:600;padding:9px 4px;");
  ui->leftLayout->insertWidget(0, signalHeader);
  auto* chartHeader = new QLabel(QString::fromUtf8("风机运行数据  /  时序曲线"), ui->plotPage);
  chartHeader->setObjectName("BladeScopeChartHeader");
  chartHeader->setAlignment(Qt::AlignCenter);
  chartHeader->setStyleSheet("font-size:14px;font-weight:600;padding:7px 0;");
  ui->plottingLayout->insertWidget(0, chartHeader);
  // Compact and label the native timeline controls without changing playback behavior.
  ui->buttonTimeTracker->setToolTip(QString::fromUtf8("时间跟踪"));
  ui->buttonLegend->setToolTip(QString::fromUtf8("显示或隐藏曲线图例"));
  ui->buttonActivateGrid->setToolTip(QString::fromUtf8("显示或隐藏绘图区网格"));
  ui->buttonRemoveTimeOffset->setToolTip(QString::fromUtf8("清除时间偏移"));
  ui->buttonUseDateTime->setToolTip(QString::fromUtf8("切换日期时间显示"));
  ui->buttonZoomOut->setToolTip(QString::fromUtf8("缩放以显示完整时间范围"));
  ui->buttonPlay->setToolTip(QString::fromUtf8("播放或暂停时间轴"));
  ui->playbackLoop->setToolTip(QString::fromUtf8("循环播放"));

  ui->mainSplitter->setHandleWidth(1);
  ui->mainSplitter->setStyleSheet("QSplitter#mainSplitter{background:#f7f1eb;border:0px;} QSplitter#mainSplitter::handle:horizontal{background:#e8dcd0;border:0px;margin:0px;width:1px;} QSplitter#mainSplitter::handle:vertical{background:#e8dcd0;border:0px;margin:0px;height:1px;}");
  ui->mainSplitter->setChildrenCollapsible(false);
  ui->mainSplitter->setSizes(QList<int>{290, 1150});
  // BladeScope: one menu row only, no second-row quick toolbar.
  // Group infrequent actions in clear menus; reuse existing PlotJuggler buttons.
  auto* bladeFileMenu = new QMenu(QString::fromUtf8("文件"), this);
  ui->menuBar->insertMenu(ui->menuBar->actions().first(), bladeFileMenu);
  auto* bladeLoad = bladeFileMenu->addAction(QString::fromUtf8("打开录波..."));
  connect(bladeLoad, &QAction::triggered, ui->buttonLoadDatafile, &QPushButton::click);
  auto* bladeReload = bladeFileMenu->addAction(QString::fromUtf8("重新加载数据"));
  connect(bladeReload, &QAction::triggered, ui->buttonReloadData, &QPushButton::click);
  bladeFileMenu->addSeparator();
  auto* bladeLayout = bladeFileMenu->addAction(QString::fromUtf8("加载布局..."));
  connect(bladeLayout, &QAction::triggered, ui->buttonLoadLayout, &QPushButton::click);
  auto* bladeSaveLayout = bladeFileMenu->addAction(QString::fromUtf8("保存布局..."));
  connect(bladeSaveLayout, &QAction::triggered, ui->buttonSaveLayout, &QPushButton::click);
  auto* bladePlotMenu = new QMenu(QString::fromUtf8("曲线"), this);
  ui->menuBar->insertMenu(ui->menuBar->actions().first(), bladePlotMenu);
  auto* bladeZoomAll = bladePlotMenu->addAction(QString::fromUtf8("适应时间范围"));
  connect(bladeZoomAll, &QAction::triggered, ui->buttonZoomOut, &QPushButton::click);
  auto* bladeGrid = bladePlotMenu->addAction(QString::fromUtf8("显示/隐藏网格"));
  connect(bladeGrid, &QAction::triggered, ui->buttonActivateGrid, &QPushButton::click);
  auto* bladeLegend = bladePlotMenu->addAction(QString::fromUtf8("显示/隐藏图例"));
  connect(bladeLegend, &QAction::triggered, ui->buttonLegend, &QPushButton::click);
  auto* bladeSettingsMenu = new QMenu(QString::fromUtf8("设置"), this);
  ui->menuBar->insertMenu(ui->menuBar->actions().first(), bladeSettingsMenu);
  bladeSettingsMenu->addAction(ui->actionPreferences);
  bladeSettingsMenu->addAction(ui->actionColorMap_Editor);
  auto* themeMenu = bladeSettingsMenu->addMenu(QString::fromUtf8("界面主题"));
  const QStringList themeNames = {QString::fromUtf8("暖橙奶油（默认）"), QString::fromUtf8("清爽海蓝"), QString::fromUtf8("极简灰白"), QString::fromUtf8("雅致香槟"), QString::fromUtf8("柔和深色")};
  const QStringList palettes[] = {
    QStringList{"#f7f1eb","#fffdfa","#fffaf5","#49382d","#c98250","#f8e8da","#fbf1e9","#fffdfa"},
    QStringList{"#edf3f8","#fbfdff","#f7fbff","#29445d","#648fb2","#e8f2fa","#edf4fa","#ffffff"},
    QStringList{"#f0f1ef","#ffffff","#fafbfa","#3d4540","#8c998f","#edf0ed","#f0f1ef","#ffffff"},
    QStringList{"#f3efe9","#fffdf9","#fcf8f2","#514538","#aa8b65","#f2e9dc","#f4eee5","#fffdf9"},
    QStringList{"#202b38","#273443","#263342","#e8edf2","#d18b58","#435466","#2d3b4b","#202b38"}};
  const QString styleTemplate = QString::fromUtf8(
    "QMainWindow,QWidget{background:%1;color:%4;}"
    "QFrame#leftMainWindowFrame{background:%2;border-right:1px solid %6;}"
    "QWidget#widgetLabelLoad,QWidget#BladeScopeSignalActions{background:%3;color:%4;}"
    "QToolBar,QMenuBar{background:%3;color:%4;border:0px;spacing:7px;padding:4px;}"
    "QMenuBar::item{background:transparent;color:%4;padding:5px 8px;border-radius:4px;}"
    "QMenuBar::item:selected,QMenu::item:selected{background:%5;color:%4;}"
    "QMenu{background:%2;color:%4;border:1px solid %6;}"
    "QMenu::item{background:transparent;color:%4;padding:5px 22px;}"
    "QToolButton,QPushButton{color:%4;background:%2;border:1px solid %6;border-radius:5px;padding:5px 9px;}"
    "QToolButton:hover,QPushButton:hover{background:%5;border-color:%5;color:%4;}"
    "QToolButton:pressed,QPushButton:pressed,QToolButton:checked,QPushButton:checked{background:%5;border-color:%5;color:%4;}"
    "QToolButton:disabled,QPushButton:disabled{background:%3;color:%6;border-color:%6;}"
    "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:%2;color:%4;border:1px solid %6;border-radius:5px;padding:5px;selection-background-color:%5;selection-color:%4;}"
    "QComboBox QAbstractItemView{background:%2;color:%4;selection-background-color:%5;selection-color:%4;border:1px solid %6;}"
    "QTreeView,QTreeWidget,QListView,QListWidget,QTableView,QTableWidget{background:%2;alternate-background-color:%3;color:%4;gridline-color:%6;border:1px solid %6;}"
    "QTreeView::item:selected,QListView::item:selected,QTableView::item:selected{background:%5;color:%4;}"
    "QHeaderView::section{background:%3;color:%4;border:0px;border-bottom:1px solid %6;padding:4px;}"
    "QCheckBox,QRadioButton,QLabel{color:%4;}"
    "QScrollBar:vertical,QScrollBar:horizontal{background:%3;border:0px;margin:2px;}"
    "QScrollBar::handle:vertical,QScrollBar::handle:horizontal{background:%6;border-radius:4px;min-height:22px;min-width:22px;}"
    "QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover{background:%5;}"
    "QSlider::groove:horizontal{height:4px;background:%6;border-radius:2px;}"
    "QSlider::handle:horizontal{width:12px;margin:-5px 0;border-radius:6px;background:%5;}"
    "QWidget#plotPage,QWidget#plottingArea,QWidget#plottingAreaH,QFrame#tabsFrame{background:%3;color:%4;border:0px;}"
    "QLabel#BladeScopeChartHeader{background:%3;color:%4;border-bottom:1px solid %6;padding:8px 4px;font-weight:600;}"
    "QWidget#widgetTimescale{background:%3;color:%4;border-top:1px solid %6;padding:3px 5px;}"
    "QWidget#widgetTimescale QPushButton{min-height:25px;max-height:29px;padding:3px 8px;border-radius:5px;}"
    "QWidget#widgetTimescale QDoubleSpinBox{min-height:25px;max-height:29px;padding:2px 5px;border:1px solid %6;border-radius:4px;background:%2;color:%4;}"
    "QWidget#widgetTimescale QLineEdit{min-height:25px;max-height:29px;padding:2px 5px;}"
    "QWidget#BladeScopeSignalActions QPushButton{background:%2;color:%4;border:1px solid %6;border-radius:5px;padding:6px 2px;min-height:24px;}"
    "QWidget#BladeScopeSignalActions QPushButton:hover{background:%5;border-color:%5;}"
    "QFrame#tabsFrame{background:%3;border:0px;border-radius:0px;}"
    "QSplitter#mainSplitter{background:%3;border:0px;}"
    "QSplitter#mainSplitter::handle:horizontal{background:%6;width:1px;border:0px;}"
    "QSplitter#mainSplitter::handle:vertical{background:%6;height:1px;border:0px;}"
    "QwtPlotCanvas{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %7,stop:1 %8);}");
  QStringList themeStyles;
  for (const auto& p : palettes) {
    themeStyles << styleTemplate.arg(p[0]).arg(p[1]).arg(p[2]).arg(p[3]).arg(p[4]).arg(p[5]).arg(p[6]).arg(p[7]);
  }
  auto* themeSettings = new QSettings("BladeScope", "BladeScope", this);
  auto applyBladeTheme = [this, themeSettings, themeStyles, palettes](int index) {
    if (index < 0 || index >= themeStyles.size()) index = 0;
    const auto p = palettes[index];
    this->setStyleSheet(themeStyles[index]);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, QColor(p[0]));
    palette.setColor(QPalette::Base, QColor(p[1]));
    palette.setColor(QPalette::AlternateBase, QColor(p[2]));
    palette.setColor(QPalette::Button, QColor(p[1]));
    palette.setColor(QPalette::Text, QColor(p[3]));
    palette.setColor(QPalette::WindowText, QColor(p[3]));
    palette.setColor(QPalette::ButtonText, QColor(p[3]));
    palette.setColor(QPalette::HighlightedText, QColor(p[3]));
    palette.setColor(QPalette::Highlight, QColor(p[4]));
    palette.setColor(QPalette::Mid, QColor(p[5]));
    this->setPalette(palette);
    this->setAutoFillBackground(true);
    for (auto* child : this->findChildren<QWidget*>()) child->setPalette(palette);
    themeSettings->setValue("appearance/theme_index", index);
    themeSettings->sync();
  };
  for (int i = 0; i < themeNames.size(); ++i) {
    QAction* themeAction = themeMenu->addAction(themeNames[i]);
    connect(themeAction, &QAction::triggered, this, [applyBladeTheme, i]() { applyBladeTheme(i); });
  }
  applyBladeTheme(themeSettings->value("appearance/theme_index", 0).toInt());
'@.TrimEnd())
$mainSrc = $mainSrc.Replace($mainAnchor, @'
  const QString bladeScopeIconPath = QCoreApplication::applicationDirPath() + "/BladeScope.ico";
  if (QFile::exists(bladeScopeIconPath))
  {
    setWindowIcon(QIcon(bladeScopeIconPath));
  }
  QLocale::setDefault(QLocale::c());  // set as default
'@.TrimEnd())
$mainInclude = '#include <QMainWindow>'
if ($mainSrc.Contains($mainInclude)) {
  $extraIncludes = @('#include <QPalette>', '#include <QColor>', '#include <QToolButton>', '#include <QMenu>', '#include <QSettings>', '#include <QStringList>', '#include <QAction>', '#include <QLabel>', '#include <QList>', '#include <QSplitter>', '#include <QHBoxLayout>', '#include <QLineEdit>', '#include <QPainter>', '#include <QPixmap>', '#include <QPen>')
  foreach ($header in $extraIncludes) {
    if (!$mainSrc.Contains($header)) { $mainSrc = $mainSrc.Replace('#include <QPushButton>', '#include <QPushButton>' + [Environment]::NewLine + $header) }
  }
}
# Configure the real curve-list panel and the initial splitter proportions.
# Keep this as an exact one-line anchor; do not include injected code in the anchor.
$curvePanelAnchor = '  ui->leftLayout->addWidget(_curvelist_widget, 1);'
if (!$mainSrc.Contains($curvePanelAnchor)) { throw "Curve list layout anchor missing in upstream MainWindow source" }
$curvePanelLayout = @'
  ui->leftLayout->addWidget(_curvelist_widget, 1);
  // Compact left signal pane and give the time-series canvas the remaining width.
  ui->mainSplitter->setStretchFactor(0, 0);
  ui->mainSplitter->setStretchFactor(1, 1);
  ui->mainSplitter->setSizes(QList<int>{265, 1175});
  ui->mainSplitter->setHandleWidth(1);
  ui->mainSplitter->setStyleSheet("QSplitter#mainSplitter{background:#f7f1eb;border:0px;} QSplitter#mainSplitter::handle:horizontal{background:#e8dcd0;border:0px;margin:0px;width:1px;} QSplitter#mainSplitter::handle:vertical{background:#e8dcd0;border:0px;margin:0px;height:1px;}");
  ui->mainSplitter->setChildrenCollapsible(false);
  ui->leftMainWindowFrame->setMinimumWidth(235);
  ui->leftMainWindowFrame->setMaximumWidth(350);
  // Keep advanced panels available but collapsed for the normal viewer workflow.
  // Move file/layout and advanced functions to the main menu row.
  // Keep their widgets alive (menu actions forward to the original buttons).
  ui->widgetLabelLoad->hide();
  ui->frameFile->hide();
  ui->widgetLabelStreaming->hide();
  ui->widgetLabelPublishers->hide();
  ui->frameStreaming->hide();
  ui->buttonHideStreamingFrame->setText(QString::fromUtf8("实时数据 +"));
  ui->framePublishers->hide();
  ui->buttonHidePublishersFrame->setText(QString::fromUtf8("数据发布 +"));
  ui->buttonHideStreamingFrame->setToolTip(QString::fromUtf8("展开实时数据高级功能"));
  ui->buttonHidePublishersFrame->setToolTip(QString::fromUtf8("展开数据发布高级功能"));
  ui->leftLayout->removeWidget(ui->widgetLabelTimeseries);
  ui->leftLayout->insertWidget(1, ui->widgetLabelTimeseries);
  ui->leftLayout->removeWidget(_curvelist_widget);
  ui->leftLayout->insertWidget(2, _curvelist_widget, 1);
  // Move rarely used dictionary import from the signal panel to File menu.
  // Forward to the original import button so file encoding and persistence stay intact.
  for (auto* panelButton : _curvelist_widget->findChildren<QPushButton*>()) {
    if (panelButton->text() == QString::fromUtf8("导入标签映射")) {
      auto* importAction = bladeFileMenu->addAction(QString::fromUtf8("导入中文标签映射..."));
      connect(importAction, &QAction::triggered, panelButton, &QPushButton::click);
      panelButton->hide();
      break;
    }
  }
  // One compact action row: load recording, focus filter, clear search.
  // Buttons call the original controls and never modify the underlying samples.
  auto* bladeSignalActions = new QWidget(ui->leftMainWindowFrame);
  bladeSignalActions->setObjectName("BladeScopeSignalActions");
  auto* bladeActionRow = new QHBoxLayout(bladeSignalActions);
  bladeActionRow->setContentsMargins(0, 2, 0, 4);
  bladeActionRow->setSpacing(5);
  bladeSignalActions->setStyleSheet(
      "QPushButton{background:#fff9f3;border:1px solid #e8d8c9;"
      "border-radius:5px;color:#704e38;padding:6px 2px;min-height:24px;}"
      "QPushButton:hover{background:#f5e5d6;border-color:#c9946b;}"
      "QPushButton:pressed{background:#ebd2bd;}");
  auto* bladeLoadData = new QPushButton(QString::fromUtf8("加载数据"), bladeSignalActions);
  bladeLoadData->setToolTip(QString::fromUtf8("打开风机录波文件"));
  auto* bladeFilterData = new QPushButton(QString::fromUtf8("筛选"), bladeSignalActions);
  bladeFilterData->setToolTip(QString::fromUtf8("定位到测点搜索框"));
  auto* bladeClearSearch = new QPushButton(QString::fromUtf8("清空"), bladeSignalActions);
  bladeClearSearch->setToolTip(QString::fromUtf8("清空搜索条件，不删除已加载数据或曲线"));
  bladeActionRow->addWidget(bladeLoadData, 1);
  bladeActionRow->addWidget(bladeFilterData, 1);
  bladeActionRow->addWidget(bladeClearSearch, 1);
  ui->leftLayout->insertWidget(1, bladeSignalActions);
  connect(bladeLoadData, &QPushButton::clicked, ui->buttonLoadDatafile, &QPushButton::click);
  connect(bladeFilterData, &QPushButton::clicked, this, [this]() {
    auto* search = _curvelist_widget->findChild<QLineEdit*>("lineEditFilter");
    if (search) { search->setFocus(); search->selectAll(); }
  });
  connect(bladeClearSearch, &QPushButton::clicked, this, [this]() {
    auto* search = _curvelist_widget->findChild<QLineEdit*>("lineEditFilter");
    if (search) { search->clear(); search->setFocus(); }
  });
  // Remove visually redundant headings and utility row marked in the review screenshot.
  signalHeader->hide();
  chartHeader->hide();
  ui->widgetLabelTimeseries->hide();
  if (auto* trash = _curvelist_widget->findChild<QPushButton*>("pushButtonTrash")) trash->hide();
  if (auto* count = _curvelist_widget->findChild<QLabel*>("labelNumberDisplayed")) count->hide();
  if (auto* values = _curvelist_widget->findChild<QCheckBox*>("checkBoxShowValues")) values->hide();
  // Reduce the tab strip height and surrounding whitespace; retain tab controls.
  // tabsFrame contains the *entire* plotting widget, not just its tab strip.
  // Never constrain its height: doing so hides the plot canvas.
  ui->tabsFrame->setContentsMargins(0, 0, 0, 0);
  ui->plottingLayout->setContentsMargins(0, 0, 0, 3);
  ui->plottingLayout->setSpacing(2);
  ui->horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
  ui->horizontalLayout_5->setSpacing(3);
  ui->verticalLayout_5->setContentsMargins(0, 0, 0, 0);
  ui->verticalLayout_5->setSpacing(0);
  ui->plottingArea->setStyleSheet("QWidget#plottingArea{background:#f7f1eb;border:0px;}");
  ui->plotPage->setStyleSheet("QWidget#plotPage{background:#f7f1eb;border:0px;}");
  ui->plottingAreaH->setStyleSheet("QWidget#plottingAreaH{background:#f7f1eb;border:0px;}");
  ui->tabsFrame->setMinimumHeight(0);
  ui->tabsFrame->setMaximumHeight(QWIDGETSIZE_MAX);
  ui->tabsFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ui->tabsFrame->setStyleSheet("QFrame#tabsFrame{background:#f7f1eb;border:0px;}");
  ui->plottingAreaH->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ui->label_7->setText(QString::fromUtf8("搜索并选择测点"));
  ui->label_5->setText(QString::fromUtf8("文件与布局"));
  ui->label_6->setText(QString::fromUtf8("实时数据（高级）"));
  ui->label_8->setText(QString::fromUtf8("数据发布（高级）"));
'@
$mainSrc = $mainSrc.Replace($curvePanelAnchor, $curvePanelLayout.TrimEnd())
Set-Content -Path $mainFile -Value $mainSrc -Encoding UTF8
Write-Host "BladeScope runtime window icon applied."

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
  # Only translate visible text between <p> tags; never replace identifiers,
  # objectName properties, actions or Qt-generated C++ member names.
  $uiSrc = $uiSrc.Replace('&lt;p&gt;' + $entry.Key + '&lt;/p&gt;',
                          '&lt;p&gt;' + $entry.Value + '&lt;/p&gt;')
}
# BladeScope phase 1 visual refresh: scoped Qt stylesheet and a wider work area.
# Restrict selectors to chrome and left panels, not plotting widgets, so existing
# plot rendering and mouse interaction remain unchanged.
$uiSrc = $uiSrc.Replace('<width>1058</width>', '<width>1440</width>')
$uiSrc = $uiSrc.Replace('<height>830</height>', '<height>900</height>')
# Give the chart more room while keeping the familiar resizable left-side signal panel.
$uiSrc = $uiSrc.Replace('<width>300</width>', '<width>260</width>')
$uiSrc = $uiSrc.Replace('<number>1</number>' + "`n" + '      </property>' + "`n" + '      <property name="childrenCollapsible">', '<number>5</number>' + "`n" + '      </property>' + "`n" + '      <property name="childrenCollapsible">')

$uiSrc = $uiSrc.Replace('<widget class="QSplitter" name="mainSplitter">', '<widget class="QSplitter" name="mainSplitter">')
$uiSrc = $uiSrc.Replace('<number>1</number>' + "`n" + '      </property>' + "`n" + '      <property name="childrenCollapsible">', '<number>5</number>' + "`n" + '      </property>' + "`n" + '      <property name="childrenCollapsible">')
$bladeScopeStyle = @'
QMainWindow#MainWindow { background: #f3f7fb; color: #22354b; }
QFrame#leftMainWindowFrame { background: #ffffff; border-right: 1px solid #dce6f0; }
QWidget#widgetLabelLoad { background: #eaf2fa; }
QPushButton { border: 1px solid #d7e3ef; border-radius: 5px; padding: 3px 8px; background: #ffffff; color: #27415e; }
QPushButton:hover { background: #e8f4fc; border-color: #8dc4e8; }
QPushButton:pressed { background: #d6eafa; }
QLineEdit { background: #ffffff; border: 1px solid #cdddeb; border-radius: 5px; padding: 4px; color: #203b55; }
QLineEdit:focus { border: 1px solid #2389ce; }
QMenuBar { background: #eaf2fa; color: #254361; padding: 3px; }
QMenuBar::item:selected { background: #d7e9f7; }
QMenu { background: #ffffff; color: #203b55; border: 1px solid #dce6f0; }
QMenu::item:selected { background: #e0f1fc; }
QToolTip { background: #fffef1; color: #273a50; border: 1px solid #cedbe6; }
QLabel#BladeScopeSignalHeader { font-size: 16px; font-weight: 600; padding: 8px 4px; }
QLabel#BladeScopeChartHeader { font-size: 14px; font-weight: 600; padding: 7px 0; }
QFrame#tabsFrame { background: #f7f1eb; border: 0px; border-radius: 0px; }
QWidget#widgetTimescale { background: #fffdfa; border-top: 1px solid #e7e0da; }
QSplitter::handle { background: #ece5df; }
QFrame#frameFile { background: #fffdfa; border: 1px solid #eadfd4; border-radius: 7px; }
QFrame#frameStreaming, QFrame#framePublishers { background: #faf6f1; border: 1px solid #eadfd4; border-radius: 6px; }
QPushButton#buttonHideStreamingFrame, QPushButton#buttonHidePublishersFrame { text-align: left; color: #8a684e; background: #f7eee5; border: 0; padding: 6px 8px; }


'@
$styleAnchor = '<string notr="true"/>'
if (!$uiSrc.Contains($styleAnchor)) { throw "BladeScope styleSheet anchor missing" }
$stylePos = $uiSrc.IndexOf($styleAnchor)
$uiSrc = $uiSrc.Substring(0, $stylePos) + '<string notr="true">' + $bladeScopeStyle + '</string>' + $uiSrc.Substring($stylePos + $styleAnchor.Length)
Write-Host "BladeScope light UI chrome, menu and signal-list styling applied."

# BladeScope branding: use the Qt main window title without changing application
# settings identity, preserving users' existing imported mapping CSV paths.
$uiSrc = $uiSrc.Replace('<string>PlotJuggler</string>', '<string>BladeScope 风机录波数据分析工具</string>')
Set-Content -Path $uiFile -Value $uiSrc -Encoding UTF8
Write-Host "Goldwind main-window Chinese localization applied."

# Soften the native Qwt grid without changing plot samples or axes.
$plotWidgetFile = "plotjuggler_app/plotwidget.cpp"
$plotWidgetSrc = (Get-Content -Raw -Encoding UTF8 $plotWidgetFile).Replace("`r`n", "`n")
$gridAnchor = '  _grid->setPen(QPen(Qt::gray, 0.0, Qt::DotLine));'
if (!$plotWidgetSrc.Contains($gridAnchor)) { throw "Plot grid pen anchor missing" }
$plotWidgetSrc = $plotWidgetSrc.Replace($gridAnchor,
    '  _grid->setPen(QPen(QColor(188, 174, 162, 125), 0.0, Qt::DotLine));')
Set-Content -Path $plotWidgetFile -Value $plotWidgetSrc -Encoding UTF8
Write-Host "BladeScope subtle time-series grid styling applied."

# Compact the native Qwt in-canvas legend; preserve its collapse and item-click logic.
$plotLegendFile = "plotjuggler_base/src/plotlegend.cpp"
$plotLegendSrc = (Get-Content -Raw -Encoding UTF8 $plotLegendFile).Replace("`r`n", "`n")
$legendAnchor = '  setBorderRadius(0);'
if (!$plotLegendSrc.Contains($legendAnchor)) { throw "Plot legend border anchor missing" }
$plotLegendSrc = $plotLegendSrc.Replace($legendAnchor, '  setBorderRadius(5);')
$plotLegendSrc = $plotLegendSrc.Replace('  setMargin(2);', '  setMargin(4);')
$plotLegendSrc = $plotLegendSrc.Replace('  font.setPointSize(9);', '  font.setPointSize(8);')
# Reserve actual text width for long raw tags and Chinese names.
$legendSizeAnchor = '  setRenderHint(QwtPlotItem::RenderAntialiased);'
if (!$plotLegendSrc.Contains($legendSizeAnchor)) { throw "Legend constructor anchor missing" }
$legendSizeHook = @'
  setRenderHint(QwtPlotItem::RenderAntialiased);
  setItemSpacing(7);
'@
$plotLegendSrc = $plotLegendSrc.Replace($legendSizeAnchor, $legendSizeHook.TrimEnd())

Set-Content -Path $plotLegendFile -Value $plotLegendSrc -Encoding UTF8
Write-Host "BladeScope compact rounded plot legend styling applied."

# Compact the real QTabWidget tab bar (the green-marked area), not only its outer frame.
$tabFile = "plotjuggler_app/tabbedplotwidget.cpp"
$tabSrc = (Get-Content -Raw -Encoding UTF8 $tabFile).Replace("`r`n", "`n")
$tabAnchor = "  _tabWidget->setMovable(true);"
if (!$tabSrc.Contains($tabAnchor)) { throw "Plot tab bar anchor missing" }
$tabInsert = @'
  _tabWidget->setMovable(true);
  _tabWidget->setDocumentMode(true);
  _tabWidget->tabBar()->setExpanding(false);
  _tabWidget->tabBar()->setUsesScrollButtons(true);
  _tabWidget->setStyleSheet(
      "QTabWidget::pane{background:#fffdfa;border:1px solid #e5d7ca;border-top:0px;}"
      "QTabBar{background:#f7f1eb;border:0px;}"
      "QTabWidget::tab-bar{subcontrol-origin:margin; left:0px;}"
      "QTabBar::tab{min-height:22px;max-height:25px;min-width:48px;"
      "padding:3px 11px;margin-right:3px;border:1px solid #e6d7ca;"
      "border-bottom:0;border-top-left-radius:5px;border-top-right-radius:5px;"
      "background:#f7f1eb;color:#735a46;}"
      "QTabBar::tab:selected{background:#ffffff;color:#4e3929;font-weight:600;}"
      "QTabBar::tab:hover:!selected{background:#efe4d9;}"
      "QTabBar::close-button{margin-left:4px;}");
'@
$tabSrc = $tabSrc.Replace($tabAnchor, $tabInsert.TrimEnd())
# The upstream paintEvent moves the add-tab button relative to the tab bar width.
# Pin it to the far right and keep the full strip aligned with the plot container.
$tabPaintAnchor = "  int button_pos = std::min(width_tabbar + margin, max_width_tabbar);"
if (!$tabSrc.Contains($tabPaintAnchor)) { throw "Tab paintEvent button anchor missing" }
$tabSrc = $tabSrc.Replace($tabPaintAnchor,
    "  int button_pos = std::max(0, width() - _buttonAddTab->width() - margin);")
$tabSrc = $tabSrc.Replace("  tabWidget()->tabBar()->setMaximumWidth(max_width_tabbar);",
    "  tabWidget()->tabBar()->setMaximumWidth(max_width_tabbar);")

Set-Content -Path $tabFile -Value $tabSrc -Encoding UTF8
Write-Host "BladeScope native tab bar spacing and style applied."

# Keep plot legends synchronized with the left-side label mode and CSV aliases.
$plotSyncFile = "plotjuggler_app/plotwidget.cpp"
$plotSyncSrc = (Get-Content -Raw -Encoding UTF8 $plotSyncFile).Replace("`r`n", "`n")
$plotSyncAnchor = '  _grid = new QwtPlotGrid();'
if (!$plotSyncSrc.Contains($plotSyncAnchor)) { throw "Plot legend sync anchor missing" }
$plotSyncInsert = @'
  // Poll lightweight UI metadata, not the recording samples.
  auto* bladeLegendSync = new QTimer(this);
  connect(bladeLegendSync, &QTimer::timeout, this, [this]() {
    const int mode = QCoreApplication::instance()->property("bladeScopeLabelMode").toInt();
    const QVariantMap aliases = QCoreApplication::instance()->property("bladeScopeTagAliases").toMap();
    bool changed = false;
    for (auto& item : curveList()) {
      const QString raw = QString::fromStdString(item.src_name);
      const QString chinese = aliases.value(raw).toString();
      const QString title = (mode == 1 || chinese.isEmpty()) ? raw :
                            (mode == 2 ? chinese + " (" + raw + ")" : chinese);
      if (item.curve->title().text() != title) {
        item.curve->setTitle(title);
        changed = true;
      }
    }
    if (changed) qwtPlot()->replot();
  });
  bladeLegendSync->start(450);

  _grid = new QwtPlotGrid();
'@
$plotSyncSrc = $plotSyncSrc.Replace($plotSyncAnchor, $plotSyncInsert.TrimEnd())
$plotSyncSrc = "#include <QTimer>`n#include <QCoreApplication>`n#include <QVariantMap>`n" + $plotSyncSrc
Set-Content -Path $plotSyncFile -Value $plotSyncSrc -Encoding UTF8
Write-Host "BladeScope legend follows label mode and Chinese aliases."

# Calculate plot legend entry width from the full label, not the old narrow layout.
$legendHeaderFile = "plotjuggler_base/src/plotlegend.h"
$legendHeaderSrc = (Get-Content -Raw -Encoding UTF8 $legendHeaderFile).Replace("`r`n", "`n")
$legendHeaderAnchor = "  QRectF hideButtonRect() const;"
if (!$legendHeaderSrc.Contains($legendHeaderAnchor)) { throw "Legend header size anchor missing" }
$legendHeaderSrc = $legendHeaderSrc.Replace($legendHeaderAnchor,
    "  QSize minimumSize(const QwtLegendData& data) const override;`n`n" + $legendHeaderAnchor)
Set-Content -Path $legendHeaderFile -Value $legendHeaderSrc -Encoding UTF8
$legendCppFile = "plotjuggler_base/src/plotlegend.cpp"
$legendCppSrc = (Get-Content -Raw -Encoding UTF8 $legendCppFile).Replace("`r`n", "`n")
$legendCppAnchor = "QRectF PlotLegend::hideButtonRect() const"
if (!$legendCppSrc.Contains($legendCppAnchor)) { throw "Legend source size anchor missing" }
$legendCppMethod = @'
QSize PlotLegend::minimumSize(const QwtLegendData& data) const
{
  QSize size = QwtPlotLegendItem::minimumSize(data);
  const QString label = data.title().text();
  if (!label.isEmpty())
  {
    const int fullWidth = QFontMetrics(font()).horizontalAdvance(label) + 36;
    size.setWidth(qMax(size.width(), qMin(460, fullWidth)));
  }
  return size;
}

'@
$legendCppSrc = $legendCppSrc.Replace($legendCppAnchor, $legendCppMethod + $legendCppAnchor)
$legendCppSrc = "#include <QFontMetrics>`n" + $legendCppSrc
Set-Content -Path $legendCppFile -Value $legendCppSrc -Encoding UTF8
Write-Host "BladeScope legend allocates width for full raw/Chinese labels."

# Fix legend text clipping: Qwt text size can exceed the compact legend row height.
$legendFixFile = "plotjuggler_base/src/plotlegend.cpp"
$legendFixSrc = (Get-Content -Raw -Encoding UTF8 $legendFixFile).Replace("`r`n", "`n")
$legendFixAnchor = "    size.setWidth(qMax(size.width(), qMin(460, fullWidth)));"
if (!$legendFixSrc.Contains($legendFixAnchor)) { throw "Legend full width anchor missing" }
$legendFixSrc = $legendFixSrc.Replace($legendFixAnchor,
  "    size.setWidth(qMax(size.width(), qMin(460, fullWidth)));`n    size.setHeight(qMax(size.height(), QFontMetrics(font()).height() + 16));")
$legendClipAnchor = "  painter->setClipRect(r, Qt::IntersectClip);"
if (!$legendFixSrc.Contains($legendClipAnchor)) { throw "Legend clip anchor missing" }
$legendFixSrc = $legendFixSrc.Replace($legendClipAnchor,
  "  // Do not clip the glyph ascenders/descenders inside a narrow Qwt legend row.`n  // The row height is reserved by minimumSize above.")
Set-Content -Path $legendFixFile -Value $legendFixSrc -Encoding UTF8
Write-Host "BladeScope legend text height and clipping fixed."

# Localize remaining common Timeseries List labels and short search tooltips.
$panelUiFile = "plotjuggler_app/curvelist_panel.ui"
$panelUi = (Get-Content -Raw -Encoding UTF8 $panelUiFile).Replace("`r`n", "`n")
$panelUi = $panelUi.Replace('<string>Values</string>', '<string>当前值</string>')
$panelUi = $panelUi.Replace('<string>Custom Series:</string>', '<string>自定义曲线：</string>')
$panelUi = $panelUi.Replace('<string>Filter...</string>', '<string>筛选...</string>')
$panelUi = $panelUi.Replace('<string>  Filter...</string>', '<string>  筛选...</string>')
Set-Content -Path $panelUiFile -Value $panelUi -Encoding UTF8
Write-Host "Goldwind Timeseries List UI labels localized."

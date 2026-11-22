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
#include "bladescope_label_xlsx.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QInputDialog>
#include <QLineEdit>
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

QHash<QString,QString>& goldwindImportedSystems()
{
  static QHash<QString,QString> systems;
  return systems;
}

QString goldwindSystemGroup(const QString& raw)
{
  const auto explicitSystem=goldwindImportedSystems().value(raw);
  if (!explicitSystem.isEmpty()) return explicitSystem;
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

QString goldwindBatchPath()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/gw_tag_mapping_batches.json";
}

QJsonObject goldwindBatch(const QString& name, const QJsonObject& entries)
{
  return {{"name", name}, {"entries", entries}};
}

QJsonArray goldwindReadBatches(QString* error = nullptr)
{
  const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QFile batches(goldwindBatchPath());
  if (batches.exists())
  {
    if (!batches.open(QIODevice::ReadOnly))
    {
      if (error) *error = batches.errorString();
      return {};
    }
    const auto document = QJsonDocument::fromJson(batches.readAll());
    if (!document.isArray())
    {
      if (error) *error = QStringLiteral("\u6620\u5c04\u6279\u6b21\u6587\u4ef6\u683c\u5f0f\u65e0\u6548\u3002");
      return {};
    }
    return document.array();
  }
  QFile legacy(dir + "/gw_tag_mapping.json");
  if (legacy.exists())
  {
    if (!legacy.open(QIODevice::ReadOnly))
    {
      if (error) *error = legacy.errorString();
      return {};
    }
    const auto document = QJsonDocument::fromJson(legacy.readAll());
    if (!document.isObject())
    {
      if (error) *error = QStringLiteral("\u65e7\u7248\u6620\u5c04\u6587\u4ef6\u683c\u5f0f\u65e0\u6548\u3002");
      return {};
    }
    const auto entries = document.object();
    return entries.isEmpty() ? QJsonArray{} :
           QJsonArray{goldwindBatch(QStringLiteral("\u65e7\u7248\u6620\u5c04"), entries)};
  }
  const QString userPath = dir + "/gw_tag_mapping.csv";
  const bool user = QFile::exists(userPath);
  QFile file(user ? userPath : QCoreApplication::applicationDirPath()+"/gw_tag_mapping.csv");
  if (!file.exists()) return {};
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    if (error) *error = file.errorString();
    return {};
  }
  QJsonObject entries;
  QTextStream stream(&file);
  stream.setCodec("UTF-8");
  while (!stream.atEnd())
  {
    const auto line = stream.readLine();
    const int comma = line.indexOf(',');
    if (comma <= 0 || line.trimmed().compare("raw_tag,cn_name", Qt::CaseInsensitive) == 0) continue;
    const auto tag = line.left(comma).trimmed(), name = line.mid(comma+1).trimmed();
    if (!tag.isEmpty() && !name.isEmpty()) entries.insert(tag, QJsonArray{name, QString()});
  }
  return entries.isEmpty() ? QJsonArray{} :
         QJsonArray{goldwindBatch(user ? QStringLiteral("\u65e7\u7248\u6620\u5c04") :
                                   QStringLiteral("\u5185\u7f6e\u6620\u5c04"), entries)};
}

bool goldwindWriteBatches(QWidget* parent, const QJsonArray& batches)
{
  const auto path = goldwindBatchPath();
  QSaveFile file(path);
  const auto bytes = QJsonDocument(batches).toJson(QJsonDocument::Compact);
  if (QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
      file.write(bytes) == bytes.size() && file.commit())
    return true;
  QMessageBox::warning(parent, QStringLiteral("\u4fdd\u5b58\u6620\u5c04\u5931\u8d25"), file.errorString());
  return false;
}

QJsonObject goldwindEffectiveMapping(const QJsonArray& batches)
{
  QJsonObject result;
  for (const auto& batch : batches)
  {
    const auto entries = batch.toObject().value("entries").toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it)
      result.insert(it.key(), it.value());
  }
  return result;
}

QHash<QString,QString> goldwindReadNames()
{
  QHash<QString,QString> names;
  auto& systems = goldwindImportedSystems();
  systems.clear();
  const auto mapping = goldwindEffectiveMapping(goldwindReadBatches());
  for (auto it = mapping.begin(); it != mapping.end(); ++it)
  {
    const auto row = it.value().toArray();
    if (row.size() != 2 || row[0].toString().isEmpty()) continue;
    names.insert(it.key(), row[0].toString());
    if (!row[1].toString().isEmpty()) systems.insert(it.key(), row[1].toString());
  }
  return names;
}

QHash<QString, QString>& goldwindChineseNames()
{
  static auto names=goldwindReadNames();
  return names;
}

void goldwindReloadChineseNames()
{
  goldwindChineseNames()=goldwindReadNames();
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
  // Selector colors inherit from the active global theme.

  goldwindMode->setToolTip(QString::fromUtf8("金风标签显示模式"));
  auto* goldwindToolbar = new QGridLayout();
  goldwindToolbar->setContentsMargins(0, 0, 0, 0);
  goldwindToolbar->setHorizontalSpacing(5);
  goldwindToolbar->setVerticalSpacing(5);
  goldwindToolbar->addWidget(goldwindMode, 0, 0);
  auto* goldwindGroup = new QPushButton(QString::fromUtf8("系统分类"), this);
  goldwindGroup->setCheckable(true);
  goldwindGroup->setMinimumHeight(30);
  // Category toggle colors inherit from the active global theme.

  goldwindToolbar->addWidget(goldwindGroup, 1, 0);
  auto* goldwindSelect = new QPushButton(QString::fromUtf8("选择筛选结果"), this);
  goldwindSelect->setToolTip(QString::fromUtf8("多选当前可见的测点，然后拖动所选测点到绘图区"));
  goldwindSelect->hide(); // Search box replaces the separate selection button.
  layout1->addLayout(goldwindToolbar);
  auto* goldwindImport = new QPushButton(QString::fromUtf8("导入标签映射"), this);
  goldwindImport->setToolTip(QString::fromUtf8("导入 CSV 或 XLSX 标签映射批次"));
  goldwindImport->hide(); // Available from File menu.
  const auto refreshMapping = [this] {
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
  };
  auto* goldwindClearMapping = new QPushButton(this);
  goldwindClearMapping->setObjectName("bladeScopeClearChineseMapping");
  goldwindClearMapping->hide();
  connect(goldwindClearMapping, &QPushButton::clicked, this, [this, refreshMapping] {
    const auto title = QStringLiteral("\u6e05\u9664\u4e2d\u6587\u6620\u5c04");
    QString error;
    auto batches = goldwindReadBatches(&error);
    if (!error.isEmpty())
    {
      QMessageBox::warning(this, title, error);
      return;
    }
    if (batches.isEmpty())
    {
      QMessageBox::information(this, title, QStringLiteral("\u5f53\u524d\u6ca1\u6709\u6620\u5c04\u6279\u6b21\u3002"));
      return;
    }
    QStringList names;
    for (const auto& batch : batches)
      names << batch.toObject().value("name").toString();
    bool confirmed = false;
    const auto selected = QInputDialog::getItem(this, title,
        QStringLiteral("\u9009\u62e9\u8981\u6e05\u9664\u7684\u6620\u5c04\u6279\u6b21"), names, 0, false, &confirmed);
    if (!confirmed) return;
    const int index = names.indexOf(selected);
    if (index < 0) return;
    batches.removeAt(index);
    if (!goldwindWriteBatches(this, batches)) return;
    refreshMapping();
  });
  connect(goldwindImport, &QPushButton::clicked, this, [this, refreshMapping]() {
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("选择标签点表"), QString(),
        QString::fromUtf8("标签点表 (*.xlsx *.csv)"));
    if (path.isEmpty()) return;
    QJsonObject checked;
    QString detectedEncoding=QStringLiteral("XLSX");
    if(QFileInfo(path).suffix().compare("xlsx",Qt::CaseInsensitive)==0)
    {
      try { checked=BladeLabels::readWorkbook(path); }
      catch(const std::exception& ex)
      {
        QMessageBox::warning(this,QStringLiteral("导入失败"),QString::fromUtf8(ex.what()));
        return;
      }
    }
    else
    {
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
      detectedEncoding = "UTF-8";
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
        if (!key.isEmpty() && !val.isEmpty()) checked.insert(key, QJsonArray{val,QString()});
      }
    }
    if (checked.isEmpty())
    {
      QMessageBox::warning(this, QString::fromUtf8("导入失败"),
                           QString::fromUtf8("未找到有效映射。XLSX 需要原始字段和中文含义，CSV 需要 raw_tag 和 cn_name。"));
      return;
    }
    const auto title = QStringLiteral("\u5bfc\u5165\u4e2d\u6587\u6620\u5c04");
    QString error;
    auto batches = goldwindReadBatches(&error);
    if (!error.isEmpty())
    {
      QMessageBox::warning(this, title, error);
      return;
    }
    QStringList names;
    for (const auto& batch : batches)
      names << batch.toObject().value("name").toString();
    const QString base = QFileInfo(path).completeBaseName();
    QString name = base;
    for (int suffix = 2; names.contains(name, Qt::CaseInsensitive); ++suffix)
      name = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
    while (true)
    {
      bool confirmed = false;
      name = QInputDialog::getText(this, title, QStringLiteral("\u6620\u5c04\u6279\u6b21\u540d\u79f0"),
                                  QLineEdit::Normal, name, &confirmed).trimmed();
      if (!confirmed) return;
      if (!name.isEmpty() && !names.contains(name, Qt::CaseInsensitive)) break;
      QMessageBox::warning(this, title, QStringLiteral("\u8bf7\u8f93\u5165\u975e\u7a7a\u4e14\u4e0d\u91cd\u590d\u7684\u6279\u6b21\u540d\u79f0\u3002"));
    }
    const auto existing = goldwindEffectiveMapping(batches);
    int duplicates = 0;
    for (auto it = checked.begin(); it != checked.end(); ++it)
      if (existing.contains(it.key())) ++duplicates;

    bool replace = true;
    if (duplicates > 0)
    {
      QMessageBox conflict(QMessageBox::Question, title,
          QStringLiteral("\u53d1\u73b0 %1 \u6761\u91cd\u590d\u6d4b\u70b9\uff0c\u8bf7\u9009\u62e9\u4f7f\u7528\u54ea\u4e00\u6279\u7684\u6620\u5c04\u3002").arg(duplicates),
          QMessageBox::NoButton, this);
      auto* useNew = conflict.addButton(QStringLiteral("\u66ff\u6362\u91cd\u590d\u6620\u5c04"), QMessageBox::AcceptRole);
      auto* keep = conflict.addButton(QStringLiteral("\u4fdd\u7559\u5df2\u6709\u6620\u5c04"), QMessageBox::RejectRole);
      auto* cancel = conflict.addButton(QStringLiteral("\u53d6\u6d88\u5bfc\u5165"), QMessageBox::RejectRole);
      conflict.setDefaultButton(keep);
      conflict.setEscapeButton(cancel);
      conflict.exec();
      if (conflict.clickedButton() != keep && conflict.clickedButton() != useNew) return;
      replace = conflict.clickedButton() == useNew;
    }
    const auto batch = goldwindBatch(name, checked);
    if (replace) batches.append(batch);
    else batches.prepend(batch);
    if (!goldwindWriteBatches(this, batches)) return;
    refreshMapping();
    QMessageBox::information(this, title,
        QStringLiteral("\u5df2\u5bfc\u5165\u6279\u6b21\u300c%1\u300d\uff0c%2 \u6761\u6620\u5c04\uff08%3\uff09\u3002")
            .arg(name).arg(checked.size()).arg(detectedEncoding));
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
  goldwindReloadSearchAliases();
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
  static QHash<QString, QString> aliases;
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
  const auto values = QCoreApplication::instance()->property("bladeScopeTagAliases").toMap();
  for (auto it = values.begin(); it != values.end(); ++it)
    aliases.insert(it.key(), it.value().toString());
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
  ui->leftLayout->setContentsMargins(6, 4, 6, 4);
  ui->leftLayout->setSpacing(3);
  ui->verticalLayout_2->setContentsMargins(0, 0, 0, 0);
  ui->plottingLayout->setSpacing(2);
  auto* signalHeader = new QLabel(QString::fromUtf8("测点列表"), ui->leftMainWindowFrame);
  signalHeader->setObjectName("BladeScopeSignalHeader");
  signalHeader->setStyleSheet("font-size:12px;font-weight:600;padding:3px 2px;");
  ui->leftLayout->insertWidget(0, signalHeader);
  auto* chartHeader = new QLabel(QString::fromUtf8("风机运行数据  /  时序曲线"), ui->plotPage);
  chartHeader->setObjectName("BladeScopeChartHeader");
  chartHeader->setAlignment(Qt::AlignCenter);
  chartHeader->setStyleSheet("font-size:11px;font-weight:600;padding:2px 0;");
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
  // Splitter colors inherit from the active global theme.
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
  ui->menuBar->removeAction(ui->menuCloudData->menuAction());
  auto* bladeSettingsMenu = new QMenu(QString::fromUtf8("设置"), this);
  ui->menuBar->insertMenu(ui->menuBar->actions().first(), bladeSettingsMenu);
  bladeSettingsMenu->addAction(ui->actionPreferences);
  bladeSettingsMenu->addAction(ui->actionColorMap_Editor);
  auto* themeMenu = bladeSettingsMenu->addMenu(QString::fromUtf8("界面主题"));
  QList<QStringList> palettes;
  for (const auto& entry : BladePlotTheme::themePalettes())
    palettes << entry.colors;
  const QString styleTemplate = QString::fromUtf8(
    "QMainWindow,QWidget{background:%1;color:%4;font-size:9pt;}"
    "QFrame#leftMainWindowFrame{background:%2;border-right:1px solid %6;}"
    "QWidget#widgetLabelLoad,QWidget#BladeScopeSignalActions{background:%3;color:%4;}"
    "QToolBar,QMenuBar{background:%3;color:%4;border:0px;spacing:7px;padding:4px;}"
    "QMenuBar::item{background:transparent;color:%4;padding:5px 8px;border-radius:4px;}"
    "QMenuBar::item:selected,QMenu::item:selected{background:%5;color:%4;}"
    "QMenu{background:%2;color:%4;border:1px solid %6;}"
    "QMenu::item{background:transparent;color:%4;padding:5px 22px;}"
    "QToolButton,QPushButton{color:%4;background:%2;border:1px solid %6;border-radius:4px;padding:3px 6px;font-size:9pt;min-height:20px;}"
    "QToolButton:hover,QPushButton:hover{background:%5;border-color:%5;color:%4;}"
    "QToolButton:checked,QToolButton:pressed,QPushButton:checked,QPushButton:pressed{background:%5;color:%4;border:1px solid %5;}"
    "QGroupBox{color:%4;border:1px solid %6;border-radius:5px;margin-top:8px;padding-top:7px;}"
    "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:%4;}"
    "QTabWidget::pane{background:%3;color:%4;border:0px;}"
    "QTabBar{background:%3;color:%4;max-height:22px;}"
    "QTabBar::tab{background:%2;color:%4;border:1px solid %6;padding:0px 5px;margin-right:2px;min-height:18px;max-height:18px;font-size:8pt;border-radius:4px;}"
    "QTabBar::tab:selected{background:%1;color:%4;border:1px solid %5;font-weight:600;}"
    "QTabBar::tab:hover:!selected{background:%5;color:%4;}"
    "QToolTip{background:%2;color:%4;border:1px solid %6;padding:4px;}"
    "QStatusBar{background:%3;color:%4;border-top:1px solid %6;}"
    "QStatusBar::item{border:0px;}"
    "QProgressBar{background:%2;color:%4;border:1px solid %6;border-radius:4px;text-align:center;}"
    "QProgressBar::chunk{background:%5;border-radius:3px;}"
    "QToolButton:pressed,QPushButton:pressed,QToolButton:checked,QPushButton:checked{background:%5;border-color:%5;color:%4;}"
    "QToolButton:disabled,QPushButton:disabled{background:%3;color:%9;border-color:%6;}"
    "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:%2;color:%4;border:1px solid %6;border-radius:4px;padding:2px 4px;font-size:9pt;selection-background-color:%5;selection-color:%4;}"
    "QComboBox QAbstractItemView{background:%2;color:%4;selection-background-color:%5;selection-color:%4;border:1px solid %6;}"
    "QTreeView,QTreeWidget,QListView,QListWidget,QTableView,QTableWidget{background:%2;alternate-background-color:%3;color:%4;gridline-color:%6;border:1px solid %6;font-size:9pt;}"
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
    "QWidget#widgetTimescale{background:%3;color:%4;border-top:1px solid %6;padding:1px 3px;}"
    "QWidget#widgetTimescale QPushButton{min-height:20px;max-height:23px;padding:1px 5px;border-radius:4px;font-size:9pt;}"
    "QWidget#widgetTimescale QDoubleSpinBox{min-height:20px;max-height:23px;padding:1px 3px;border:1px solid %6;border-radius:4px;background:%2;color:%4;font-size:9pt;}"
    "QWidget#widgetTimescale QLineEdit{min-height:25px;max-height:29px;padding:2px 5px;}"
    "QWidget#BladeScopeSignalActions QPushButton{background:%2;color:%4;border:1px solid %6;border-radius:5px;padding:6px 2px;min-height:24px;}"
    "QWidget#BladeScopeSignalActions QPushButton:hover{background:%5;border-color:%5;}"
    "QFrame#tabsFrame{background:%3;border:0px;border-radius:0px;}"
    "QSplitter#mainSplitter{background:%3;border:0px;}"
    "QSplitter#mainSplitter::handle:horizontal{background:%6;width:1px;border:0px;}"
    "QSplitter#mainSplitter::handle:vertical{background:%6;height:1px;border:0px;}"
    "QWidget#qwtCanvas,QwtPlotCanvas#qwtCanvas,QwtPlotOpenGLCanvas#qwtCanvas{background:%7;border:0px;}");
  QStringList themeStyles;
  for (const auto& p : palettes) {
    const bool dark = QColor(p[0]).lightness() < 128;
    themeStyles << styleTemplate.arg(p[0]).arg(p[1]).arg(p[2]).arg(p[3]).arg(p[4]).arg(p[5]).arg(p[6]).arg(p[7])
        .arg(dark ? "#a2b3c4" : "#566575") +
        (dark ? QStringLiteral(
            "QPushButton:checked:enabled,QPushButton:hover:enabled,"
            "QToolButton:checked:enabled,QToolButton:hover:enabled{color:#241b14;}") : QString());
  }
  auto* themeSettings = new QSettings("BladeScope", "BladeScope", this);
  auto applyBladeTheme = [this, themeSettings, themeStyles, palettes](int index) {
    if (index < 0 || index >= themeStyles.size()) index = 0;
    const auto p = palettes[index];
    const bool dark = QColor(p[0]).lightness() < 128;
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
    palette.setColor(QPalette::Light, QColor(p[2]));
    palette.setColor(QPalette::Dark, QColor(p[5]));
    palette.setColor(QPalette::Shadow, QColor(p[0]));
    palette.setColor(QPalette::Link, QColor(p[4]));
    palette.setColor(QPalette::LinkVisited, QColor(p[4]));
    const QColor disabledText(dark ? "#a2b3c4" : "#566575");
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    this->setPalette(palette);
    QCoreApplication::instance()->setProperty("bladeScopeCanvasTop", p[6]);
    QCoreApplication::instance()->setProperty("bladeScopeCanvasBottom", p[7]);
    QCoreApplication::instance()->setProperty("bladeScopeThemeText", p[8]);
    QCoreApplication::instance()->setProperty("bladeScopeThemeGrid", p[9]);
    QCoreApplication::instance()->setProperty("bladeScopeThemePanel", p[1]);
    QCoreApplication::instance()->setProperty("bladeScopeThemeAccent", p[4]);
    QCoreApplication::instance()->setProperty("bladeScopeThemeBorder", p[5]);
    this->setAutoFillBackground(true);
    for (auto* child : this->findChildren<QWidget*>()) child->setPalette(palette);
    // The native signal tree stores explicit item brushes, so refresh them as well
    // as the inherited palette; this prevents brown/dark labels on dark panels.
    for (auto* tree : this->findChildren<QTreeWidget*>()) {
      QTreeWidgetItemIterator itemIt(tree);
      while (*itemIt) {
        for (int column = 0; column < tree->columnCount(); ++column)
          (*itemIt)->setForeground(column, QBrush(QColor(p[3])));
        ++itemIt;
      }
    }
    int plotTheme = index;
    if (dark) {
      plotTheme = themeSettings->value("appearance/last_light_plot_theme", 0).toInt();
      if (plotTheme < 0 || plotTheme >= palettes.size() ||
          QColor(palettes[plotTheme][0]).lightness() < 128)
        plotTheme = 0;
    } else {
      themeSettings->setValue("appearance/last_light_plot_theme", index);
    }
    BladePlotTheme::interfaceChanged(dark, BladePlotTheme::firstCatalogPreset + plotTheme);
    BladePlotTheme::refreshButtonIcons(this, dark);
    themeSettings->setValue("appearance/theme_index", index);
    themeSettings->sync();
  };
  auto* solidThemes = themeMenu->addMenu(QStringLiteral("\u7eaf\u8272\u4e3b\u9898"));
  auto* gradientThemes = themeMenu->addMenu(QStringLiteral("\u6e10\u53d8\u4e3b\u9898"));
  const auto& catalog = BladePlotTheme::themePalettes();
  for (int i = 0; i < int(catalog.size()); ++i) {
    auto* group = i < BladePlotTheme::solidPaletteCount ? solidThemes : gradientThemes;
    QAction* themeAction = group->addAction(catalog[i].name);
    connect(themeAction, &QAction::triggered, this, [applyBladeTheme, i]() { applyBladeTheme(i); });
  }
  applyBladeTheme(themeSettings->value("appearance/theme_index", 0).toInt());
  auto* themeIconTimer = new QTimer(this);
  connect(themeIconTimer, &QTimer::timeout, this, [this] {
    if (QCoreApplication::instance()->property("bladeScopeDarkInterface").toBool())
      BladePlotTheme::refreshButtonIcons(this, true);
  });
  themeIconTimer->start(200);
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
  ui->mainSplitter->setSizes(QList<int>{235, 1450});
  ui->mainSplitter->setHandleWidth(1);
  // Splitter colors inherit from the active global theme.
  ui->mainSplitter->setChildrenCollapsible(false);
  ui->leftMainWindowFrame->setMinimumWidth(205);
  ui->leftMainWindowFrame->setMaximumWidth(295);
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
  if (auto* clear = _curvelist_widget->findChild<QPushButton*>("bladeScopeClearChineseMapping"))
  {
    auto* action = bladeFileMenu->addAction(QString::fromUtf8("清除中文映射..."));
    connect(action, &QAction::triggered, clear, &QPushButton::click);
  }
  // One compact action row: load recording, focus filter, clear search.
  // Buttons call the original controls and never modify the underlying samples.
  auto* bladeSignalActions = new QWidget(ui->leftMainWindowFrame);
  bladeSignalActions->setObjectName("BladeScopeSignalActions");
  auto* bladeActionRow = new QHBoxLayout(bladeSignalActions);
  bladeActionRow->setContentsMargins(0, 2, 0, 4);
  bladeActionRow->setSpacing(5);
  // Button colors inherit from the active global theme.
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



  ui->tabsFrame->setMinimumHeight(0);
  ui->tabsFrame->setMaximumHeight(QWIDGETSIZE_MAX);
  ui->tabsFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

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
  _tabWidget->setStyleSheet(
      // In document mode the native pane may start inside the header by a few pixels.
      // Allocate the clearance in the page layout so every split shares the same boundary.
      "QTabWidget::pane { border: 0; padding-top: 5px; }"
      "QTabBar::tab { background: palette(base); color: palette(text); "
      "border: 1px solid palette(midlight); border-radius: 4px; "
      "padding: 0 5px; margin-right: 2px; height: 18px; "
      "min-height: 18px; max-height: 18px; font-size: 8pt; }"
      "QTabBar::tab:selected { border-color: palette(highlight); font-weight: 600; }"
      "QTabBar::tab:hover:!selected { background: palette(alternate-base); }"
      "QTabBar::scroller { width: 28px; }");
  _tabWidget->tabBar()->setFixedHeight(22);
  _tabWidget->tabBar()->setExpanding(false);
  _tabWidget->tabBar()->setUsesScrollButtons(true);
  // Tab colors inherit from the active global theme stylesheet.
'@
$tabSrc = $tabSrc.Replace($tabAnchor, $tabInsert.TrimEnd())
# The upstream paintEvent moves the add-tab button relative to the tab bar width.
# Pin it to the far right and keep the full strip aligned with the plot container.
$tabPaintBlock = @'
  int width_tabbar = tabWidget()->tabBar()->width();
  int max_width_tabbar = std::max(0, width() - _buttonAddTab->width() - margin);
  int button_pos = std::min(width_tabbar + margin, max_width_tabbar);
  tabWidget()->tabBar()->setMaximumWidth(max_width_tabbar);
  _buttonAddTab->move(QPoint(button_pos, 0));
'@
$tabPaintBlock = $tabPaintBlock.Replace("`r`n", "`n")
if (!$tabSrc.Contains($tabPaintBlock)) { throw "Original tab paintEvent block missing; cannot position add-tab button" }
$tabPaintReplacement = @'
  Q_UNUSED(margin);
  auto* tabs = tabWidget();
  auto* bar = tabs->tabBar();
  if (auto* corner = tabs->cornerWidget(Qt::TopRightCorner))
  {
    const int y = bar->y() + (bar->height() - corner->height()) / 2;
    if (corner->y() != y)
      corner->move(corner->x(), y);
  }
  for (int i = 0; i < bar->count(); ++i)
  {
    if (auto* close = bar->tabButton(i, QTabBar::RightSide))
    {
      const QRect tab = bar->tabRect(i);
      const int y = tab.top() + (tab.height() - close->height()) / 2;
      if (close->y() != y)
        close->move(close->x(), y);
    }
  }
'@
$tabSrc = $tabSrc.Replace($tabPaintBlock, $tabPaintReplacement.TrimEnd())
$tabSrc = $tabSrc.Replace("  _buttonAddTab->setFixedSize(QSize(32, 32));",
                          "  _buttonAddTab->setFixedSize(QSize(22, 22));")
# Use a native corner widget so QTabWidget reserves space for the right-side controls.
$tabButtonConnect = '  connect(_buttonAddTab, &QPushButton::pressed, this, &TabbedPlotWidget::on_addTabButton_pressed);'
if (!$tabSrc.Contains($tabButtonConnect)) { throw "Add-tab button connection missing" }
$tabCorner = @'
  auto* corner = new QWidget(_tabWidget);
  corner->setFixedHeight(22);
  auto* cornerLayout = new QHBoxLayout(corner);
  cornerLayout->setContentsMargins(0, 0, 2, 0);
  cornerLayout->setSpacing(2);
  cornerLayout->addWidget(_buttonAddTab);
  _tabWidget->setCornerWidget(corner, Qt::TopRightCorner);
'@
$tabSrc = $tabSrc.Replace($tabButtonConnect, $tabCorner + "`n" + $tabButtonConnect)
$tabSrc = $tabSrc.Replace('  layout->addWidget(close_button);', @'
  button_widget->setFixedSize(16, 16);
  close_button->setIconSize(QSize(12, 12));
  layout->addWidget(close_button, 0, Qt::AlignCenter);
'@)
Set-Content -Path $tabFile -Value $tabSrc -Encoding UTF8
Write-Host "BladeScope native tab bar spacing and style applied."

# Keep plot legends synchronized with the left-side label mode and CSV aliases.
# Use the raster Qwt canvas so the theme brush is painted by Qwt itself.
# The upstream OpenGL canvas ignores the normal QWidget/Qwt background styling
# on some Windows drivers, leaving the plot surface in the dark panel color.
$plotBaseFile = "plotjuggler_base/src/plotwidget_base.cpp"
$plotBaseSrc = (Get-Content -Raw -Encoding UTF8 $plotBaseFile).Replace("`r`n", "`n")
$openGlAnchor = '  bool use_opengl = settings.value("Preferences::use_opengl", true).toBool();'
if (!$plotBaseSrc.Contains($openGlAnchor)) { throw "Qwt OpenGL canvas selection anchor missing" }
$plotBaseSrc = $plotBaseSrc.Replace($openGlAnchor,
@'
  const bool nativeRendering = false;
  const bool use_opengl = true;
'@)
$plotBaseSrc = $plotBaseSrc.Replace('  abs_canvas->setObjectName("qwtCanvas");', @'
  abs_canvas->setObjectName("qwtCanvas");
  abs_canvas->setProperty("bladeScopeNativeRendering", nativeRendering);
  abs_canvas->setProperty("bladeScopeOpenGLCanvas", use_opengl);
'@)
Set-Content -Path $plotBaseFile -Value $plotBaseSrc -Encoding UTF8
Write-Host "BladeScope rendering mode selected at startup."

$plotSyncFile = "plotjuggler_app/plotwidget.cpp"
$plotSyncSrc = (Get-Content -Raw -Encoding UTF8 $plotSyncFile).Replace("`r`n", "`n")
$plotSyncAnchor = '  _grid = new QwtPlotGrid();'
if (!$plotSyncSrc.Contains($plotSyncAnchor)) { throw "Plot legend sync anchor missing" }
$plotSyncInsert = @'
  // Poll lightweight UI metadata, not the recording samples.
  auto* bladeLegendSync = new QTimer(this);
  connect(bladeLegendSync, &QTimer::timeout, this, [this]() {
    auto* app = QCoreApplication::instance();
    const int mode = app->property("bladeScopeLabelMode").toInt();
    const QVariantMap aliases = app->property("bladeScopeTagAliases").toMap();
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
$plotSyncSrc = "#include <QTimer>`n#include <QCoreApplication>`n#include <QVariantMap>`n#include <qwt_text.h>`n" + $plotSyncSrc
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

# Keep the theme implementation alongside the customization sources.
$themeHeader = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "bladescope_plot_theme.h")
Set-Content -Path "plotjuggler_app/bladescope_plot_theme.h" -Value $themeHeader -Encoding UTF8BOM

function Update-BladeThemeSource($Path, $Replacements) {
  $source = (Get-Content -Raw -Encoding UTF8 $Path).Replace("`r`n", "`n")
  foreach ($replacement in $Replacements) {
    $old = $replacement.Old.Replace("`r`n", "`n").TrimEnd()
    $new = $replacement.New.Replace("`r`n", "`n").TrimEnd()
    if ([regex]::Matches($source, [regex]::Escape($old)).Count -ne 1) {
      throw "Expected one BladeScope theme anchor in ${Path}: $old"
    }
    $source = $source.Replace($old, $new)
  }
  Set-Content -Path $Path -Value $source -Encoding UTF8BOM
}

$gridFunction = @'
  _grid->enableX(activate);
  _grid->enableXMin(activate);
  _grid->enableY(activate);
  _grid->enableYMin(activate);
  _grid->attach(qwtPlot());
'@
Update-BladeThemeSource "plotjuggler_app/plotwidget.cpp" @(
  @{ Old = '#include "statistics_dialog.h"'; New = "#include `"statistics_dialog.h`"`n#include `"bladescope_plot_theme.h`"" }
  @{ Old = 'class TimeScaleDraw : public QwtScaleDraw'; New = 'class TimeScaleDraw : public BladePlotTheme::ScaleDraw' }
  @{ Old = '  _grid = new QwtPlotGrid();'; New = @'
  if (qwtPlot()->canvas()->property("bladeScopeNativeRendering").toBool())
    _grid = new QwtPlotGrid();
  else
    _grid = new BladePlotTheme::Grid();
'@ }
  @{ Old = '  _show_point_text->attach(qwtPlot());'; New = @'
  _show_point_text->attach(qwtPlot());
  if (auto* grid = dynamic_cast<BladePlotTheme::Grid*>(_grid))
    BladePlotTheme::install(qwtPlot(), grid);
'@ }
  @{ Old = $gridFunction; New = @'
  if (!qwtPlot()->property("bladeScopePlotTheme").toBool())
  {
    _grid->enableX(activate);
    _grid->enableXMin(activate);
    _grid->enableY(activate);
    _grid->enableYMin(activate);
    _grid->attach(qwtPlot());
  }
'@ }
  @{ Old = 'qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new QwtScaleDraw);'; New = 'qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new BladePlotTheme::ScaleDraw);' }
  @{ Old = '  auto documentRect = qwtPlot()->canvas()->rect();'; New = '  const QRect documentRect(QPoint(), qwtPlot()->size());' }
  @{ Old = '  QPixmap pixmap(documentRect.width(), documentRect.height());'; New = @'
  const bool themed = qwtPlot()->property("bladeScopePlotTheme").toBool();
  rend.setDiscardFlag(QwtPlotRenderer::DiscardBackground, themed);
  rend.setDiscardFlag(QwtPlotRenderer::DiscardCanvasBackground, themed);
  QPixmap pixmap(documentRect.size());
  pixmap.fill(Qt::transparent);
'@ }
  @{ Old = '  clipboard->setPixmap(pixmap);'; New = "  painter.end();`n  clipboard->setImage(pixmap.toImage());" }
)

# Keep the current tracker state in both capture paths so the legend can
# remain above a visible tracker in the exported result.
$themePlotPath = "plotjuggler_app/plotwidget.cpp"
$themePlotSource = (Get-Content -Raw -Encoding UTF8 $themePlotPath).Replace("`r`n", "`n")
$trackerSuspend = @'
  bool tracker_enabled = _tracker->isEnabled();
  if (tracker_enabled)
  {
    this->enableTracker(false);
    replot();
  }

'@
$trackerResume = @'

  if (tracker_enabled)
  {
    this->enableTracker(true);
    replot();
  }
'@
foreach ($fragment in @(($trackerSuspend.Replace("`r`n", "`n").TrimEnd() + "`n`n"), $trackerResume.Replace("`r`n", "`n"))) {
  if ([regex]::Matches($themePlotSource, [regex]::Escape($fragment)).Count -ne 2) {
    throw "Expected both BladeScope image capture tracker anchors"
  }
  $themePlotSource = $themePlotSource.Replace($fragment, "")
}
Set-Content -Path $themePlotPath -Value $themePlotSource -Encoding UTF8BOM

$gridStartup = @'
  bool activate_grid = settings.value("MainWindow.activateGrid", false).toBool();
  ui->buttonActivateGrid->setChecked(activate_grid);
'@
$gridToggle = @'
  forEachWidget([checked](PlotWidget* plot) {
    plot->activateGrid(checked);
    plot->replot();
  });
'@
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include `"bladescope_plot_theme.h`"" }
  @{ Old = '  bladeSettingsMenu->addAction(ui->actionColorMap_Editor);'; New = @'
  bladeSettingsMenu->addAction(ui->actionColorMap_Editor);
  auto* plotThemeAction = bladeSettingsMenu->addAction(QString::fromUtf8("绘图主题调色盘"));
  connect(plotThemeAction, &QAction::triggered, this, [this] {
    BladePlotTheme::openPanel(this);
    const QSignalBlocker blocked(ui->buttonActivateGrid);
    const auto& grid = BladePlotTheme::current().grid;
    ui->buttonActivateGrid->setChecked(grid[0] || grid[1]);
  });
'@ }
  @{ Old = $gridStartup; New = @'
  {
    const QSignalBlocker blocked(ui->buttonActivateGrid);
    const auto& grid = BladePlotTheme::current().grid;
    ui->buttonActivateGrid->setChecked(grid[0] || grid[1]);
  }
'@ }
  @{ Old = $gridToggle; New = @'
  auto theme = BladePlotTheme::current();
  theme.grid = {{ checked, checked }};
  BladePlotTheme::apply(theme);
  forEachWidget([checked](PlotWidget* plot) {
    plot->activateGrid(checked);
    if (plot->findChild<QWidget*>("qwtCanvas")->property("bladeScopeNativeRendering").toBool())
      plot->replot();
  });
'@ }
)
Update-BladeThemeSource "plotjuggler_base/src/save_plot.cpp" @(
  @{ Old = '    auto* pixmap = new QPixmap(dims.width(), dims.height());'; New = @'
    auto* pixmap = new QPixmap(dims.width(), dims.height());
    const QString suffix = QFileInfo(_save_filename).suffix().toLower();
    pixmap->fill(suffix == "jpg" || suffix == "jpeg" ? parent->palette().window().color()
                                                     : QColor(Qt::transparent));
'@ }
  @{ Old = '  _painter->fillRect(rect, parent->palette().window());'; New = '' }
  @{ Old = 'PlotSaveHelper::~PlotSaveHelper()' + "`n{"; New = @'
PlotSaveHelper::~PlotSaveHelper()
{
  if (_painter)
    _painter->end();
'@ }
  @{ Old = '  _renderer->render(plot, _painter.get(), paint_at);'; New = @'
  const bool themed = plot->property("bladeScopePlotTheme").toBool();
  _renderer->setDiscardFlag(QwtPlotRenderer::DiscardBackground, themed);
  _renderer->setDiscardFlag(QwtPlotRenderer::DiscardCanvasBackground, themed);
  _renderer->render(plot, _painter.get(), paint_at);
'@ }
)
Update-BladeThemeSource "plotjuggler_app/curve_tracker.cpp" @(
  @{ Old = '  _line_marker->attach(plot);'; New = "  _line_marker->setZ(100);`n  _line_marker->attach(plot);" }
  @{ Old = '  _text_marker->attach(plot);'; New = "  _text_marker->setZ(102);`n  _text_marker->attach(plot);" }
  @{ Old = '    _point_markers[i]->attach(_plot);'; New = "    _point_markers[i]->setZ(101);`n    _point_markers[i]->attach(_plot);" }
)
Write-Host "BladeScope plot theme controls and transparent rendering applied."

# Qwt's standard exporter paints all canvas items before the native scales.
# For themed plots split those items into two passes around the scale drawing.
$drawItemsAnchor = @'
    const QwtPlotItemList& itmList = itemList();
    for ( QwtPlotItemIterator it = itmList.begin();
        it != itmList.end(); ++it )
    {
        QwtPlotItem* item = *it;
        if ( item && item->isVisible() )
'@
Update-BladeThemeSource "3rdparty/qwt/src/qwt_plot.cpp" @(
  @{ Old = $drawItemsAnchor; New = @'
    const int exportPass = property( "bladeScopeExportPass" ).toInt();
    const int canvasPass = property( "bladeScopeCanvasPass" ).toInt();
    const QwtPlotItemList& itmList = itemList();
    for ( QwtPlotItemIterator it = itmList.begin();
        it != itmList.end(); ++it )
    {
        QwtPlotItem* item = *it;
        if ( item && item->isVisible() &&
            ( exportPass == 0 || ( item->z() < 0.0 ) == ( exportPass == 1 ) ) &&
            ( canvasPass == 0 ||
              ( canvasPass == 1 && item->z() < 100.0 ) ||
              ( canvasPass == 2 && item->z() >= 100.0 ) ||
              ( canvasPass == 3 && item->z() == -100.0 ) ||
              ( canvasPass == 4 && item->z() != -100.0 ) ) )
'@ }
)
Update-BladeThemeSource "3rdparty/qwt/src/qwt_plot_renderer.cpp" @(
  @{ Old = '    renderCanvas( plot, painter, layout->canvasRect(), maps );'; New = @'
    const bool themed = plot->property( "bladeScopePlotTheme" ).toBool();
    if ( themed )
        plot->setProperty( "bladeScopeExportPass", 1 );
    renderCanvas( plot, painter, layout->canvasRect(), maps );
'@ }
  @{ Old = @'
    painter->restore();

    // restore all setting to their original attributes.
'@; New = @'
    if ( themed )
    {
        plot->setProperty( "bladeScopeExportPass", 2 );
        renderCanvas( plot, painter, layout->canvasRect(), maps );
        plot->setProperty( "bladeScopeExportPass", QVariant() );
    }

    painter->restore();

    // restore all setting to their original attributes.
'@ }
)
$rendererPath = "3rdparty/qwt/src/qwt_plot_renderer.cpp"
$rendererSource = (Get-Content -Raw -Encoding UTF8 $rendererPath).Replace("`r`n", "`n")
$canvasBackground = 'if ( !( m_data->discardFlags & DiscardCanvasBackground ) )'
if ([regex]::Matches($rendererSource, [regex]::Escape($canvasBackground)).Count -ne 3) {
  throw "Expected three Qwt export canvas background checks"
}
$rendererSource = $rendererSource.Replace($canvasBackground,
  'if ( !( m_data->discardFlags & DiscardCanvasBackground ) &&
             plot->property( "bladeScopeExportPass" ).toInt() != 2 )')
Set-Content -Path $rendererPath -Value $rendererSource -Encoding UTF8

# Float each original DockToolbar inside its own canvas. Leave its button
# enter/leave, fullscreen and drag/drop handlers in charge of visibility.
Update-BladeThemeSource "plotjuggler_app/plot_docker.cpp" @(
  @{ Old = '      this->toolBar()->buttonClose()->setHidden(fullscreen);'; New = '' }
  @{ Old = '  this->layout()->setMargin(10);'; New = '  this->layout()->setContentsMargins(2, 0, 2, 2);' }
  @{ Old = '  qobject_cast<QBoxLayout*>(layout())->insertWidget(0, _toolbar);'; New = @'
  if (auto* canvas = _plot_widget->findChild<QWidget*>("qwtCanvas"))
  {
    _toolbar->setParent(canvas);
    _toolbar->setObjectName("bladeScopeCanvasToolbar");
    _toolbar->setStyleSheet(
        "#bladeScopeCanvasToolbar, #bladeScopeCanvasToolbar QWidget "
        "{ background: transparent; }"
        "#bladeScopeCanvasToolbar QPushButton:hover "
        "{ background: rgba(128, 128, 128, 45); }");
    _toolbar->setFixedHeight(24);
    canvas->installEventFilter(_toolbar);
    _toolbar->show();
  }
  else
  {
    qobject_cast<QBoxLayout*>(layout())->insertWidget(0, _toolbar);
  }
'@ }
)
Update-BladeThemeSource "plotjuggler_app/plot_docker_toolbar.cpp" @(
  @{ Old = 'void DockToolbar::enterEvent(QEvent* ev)' + "`n{"; New = @'
void DockToolbar::enterEvent(QEvent* ev)
{
  ui->buttonClose->setVisible(!_fullscreen_mode);
'@ }
  @{ Old = 'void DockToolbar::leaveEvent(QEvent* ev)' + "`n{"; New = @'
void DockToolbar::leaveEvent(QEvent* ev)
{
  ui->buttonClose->hide();
'@ }
  @{ Old = '  ui->buttonClose->setHidden(_fullscreen_mode);'; New = '  ui->buttonClose->setVisible(!_fullscreen_mode && underMouse());' }
  @{ Old = '  ui->setupUi(this);'; New = @'
  ui->setupUi(this);

  // Give every control its own cell, including Close. Designer's nested
  // button container otherwise caps the width of the four hover controls.
  ui->label->hide();
  ui->horizontalSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
  ui->mainHorizontalLayout->setContentsMargins(1, 1, 1, 1);
  ui->mainHorizontalLayout->setSpacing(1);
  ui->widgetButtons->hide();
  int controlIndex = ui->mainHorizontalLayout->indexOf(ui->buttonClose);
  for (auto* button : {ui->buttonBackground, ui->buttonSplitHorizontal,
                       ui->buttonSplitVertical, ui->buttonFullscreen})
  {
    ui->mainHorizontalLayout->insertWidget(controlIndex++, button);
    button->setFixedSize(22, 22);
    button->setIconSize(QSize(16, 16));
  }
  ui->buttonClose->setFixedSize(22, 22);
  ui->buttonClose->setIconSize(QSize(16, 16));
  ui->buttonClose->hide();
'@ }
  @{ Old = '  if (event->type() == QEvent::MouseButtonDblClick)'; New = @'
  if (object == parentWidget() && object != _parent)
  {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
    {
      const int inset = 2;
      const int available = qMax(0, parentWidget()->width() - 2 * inset);
      // Five 22 px controls, four 1 px gaps and two 1 px margins.
      const int toolbarWidth = qMin(available, 116);
      setGeometry(parentWidget()->width() - toolbarWidth - inset, inset, toolbarWidth, height());
      raise();
    }
    return QWidget::eventFilter(object, event);
  }

  if (event->type() == QEvent::MouseButtonDblClick)
'@ }
)

# Reuse the original buttons so shortcuts, checked states and slots stay wired.
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include <QTabWidget>" }
  @{ Old = '  ui->tabsFrame->layout()->addWidget(_main_tabbed_widget);'; New = @'
  ui->tabsFrame->layout()->addWidget(_main_tabbed_widget);
  auto* tabCorner = _main_tabbed_widget->tabWidget()->cornerWidget(Qt::TopRightCorner);
  auto* tabCornerLayout = static_cast<QHBoxLayout*>(tabCorner->layout());
  int headerButtonIndex = 0;
  for (auto* button : {ui->buttonLink, ui->buttonTimeTracker, ui->buttonShowpoint,
                       ui->buttonReferencePoint, ui->buttonLegend, ui->buttonActivateGrid,
                       ui->buttonRatio, ui->buttonRemoveTimeOffset, ui->buttonUseDateTime,
                       ui->buttonZoomOut, ui->buttonDots})
  {
    ui->verticalLayoutButtons->removeWidget(button);
    button->setFixedSize(22, 22);
    button->setIconSize(QSize(16, 16));
    tabCornerLayout->insertWidget(headerButtonIndex++, button);
  }
  delete ui->verticalLayoutButtons;
  ui->verticalLayoutButtons = nullptr;
'@ }
)
Update-BladeThemeSource "plotjuggler_base/src/plotlegend.h" @(
  @{ Old = '  QRectF hideButtonRect() const;'; New = @'
  QRectF hideButtonRect() const;
  QRect geometry(const QRectF& canvasRect) const override;
'@ }
)
Update-BladeThemeSource "plotjuggler_base/src/plotlegend.cpp" @(
  @{ Old = 'QRectF PlotLegend::hideButtonRect() const'; New = @'
QRect PlotLegend::geometry(const QRectF& canvasRect) const
{
  QRectF available = canvasRect;
  if (alignmentInCanvas().testFlag(Qt::AlignTop) && alignmentInCanvas().testFlag(Qt::AlignRight))
  {
    // The legend already has a canvas inset; add only the clearance needed.
    available.adjust(0, qMax(0, 33 - offsetInCanvas(Qt::Vertical)), 0, 0);
  }
  return QwtPlotLegendItem::geometry(available);
}

QRectF PlotLegend::hideButtonRect() const
'@ }
)

# Limit interactive time-series painting to the visible interval. Keep the
# neighboring samples so segments crossing either edge still draw correctly.
Update-BladeThemeSource "3rdparty/qwt/src/qwt_plot_curve.h" @(
  @{ Old = '        FilterPointsAggressive = 0x10,'; New = @'
        FilterPointsAggressive = 0x10,

        //! Data has ascending timestamps; enable viewport culling on screen.
        BladeScopeTimeSeries = 0x100,
'@ }
)
Update-BladeThemeSource "plotjuggler_base/src/plotwidget_base.cpp" @(
  @{ Old = '      plot_qwt = createTimeSeries(ts_data);'; New = @'
      plot_qwt = createTimeSeries(ts_data);
      curve->setPaintAttribute(QwtPlotCurve::BladeScopeTimeSeries);
'@ }
)
Update-BladeThemeSource "3rdparty/qwt/src/qwt_plot_curve.cpp" @(
  @{ Old = '#include <qpainter.h>'; New = "#include <qpainter.h>`n#include <qvariant.h>" }
  @{ Old = '        drawCurve( painter, m_data->style, xMap, yMap, canvasRect, from, to );'; New = @'
        if ( testPaintAttribute( BladeScopeTimeSeries ) &&
             ( m_data->style == Lines || m_data->style == LinesAndDots ) &&
             !m_data->symbol && !testCurveAttribute( Fitted ) &&
             m_data->brush.style() == Qt::NoBrush &&
             plot() && plot()->property( "bladeScopePlotTheme" ).toBool() &&
             !plot()->canvas()->property( "bladeScopeOpenGLCanvas" ).toBool() &&
             ( painter->device() == plot()->canvas() ||
                         plot()->property( "bladeScopeCanvasPass" ).toInt() == 1 ) )
        {
            const double left = qMin( xMap.s1(), xMap.s2() );
            const double right = qMax( xMap.s1(), xMap.s2() );
            const auto boundary = [&]( double x, bool upper ) {
                int low = from;
                int high = to + 1;
                while ( low < high )
                {
                    const int mid = low + ( high - low ) / 2;
                    const double t = sample( mid ).x();
                    if ( t < x || ( upper && t == x ) )
                        low = mid + 1;
                    else
                        high = mid;
                }
                return low;
            };

            const int first = qMax( from, boundary( left, false ) - 1 );
            const int last = qMin( to, boundary( right, true ) );
            from = first;
            to = last;
            if ( to - from > canvasRect.width() * painter->device()->devicePixelRatioF() * 4 )
                painter->setRenderHint( QPainter::Antialiasing, false );
        }
        drawCurve( painter, m_data->style, xMap, yMap, canvasRect, from, to );
'@ }
)
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '    if (do_replot)'; New = '    if (do_replot && plot->isVisible())' }
)

# Cache the gradient, grid and waveforms on the raster canvas. Cursor markers
# and the legend are composited afterwards, in the existing Z order.
Update-BladeThemeSource "3rdparty/qwt/src/qwt_plot.cpp" @(
  @{ Old = '#include <qpainter.h>'; New = "#include <qpainter.h>`n#include <qpixmap.h>`n#include <qvariant.h>" }
  @{ Old = '    drawItems( painter, m_data->canvas->contentsRect(), maps );'; New = @'
    auto* canvas = m_data->canvas.data();
    const QRect rect = canvas->contentsRect();
    const bool themed = property( "bladeScopePlotTheme" ).toBool();
    if ( themed && canvas->property( "bladeScopeOpenGLCanvas" ).toBool() && !rect.isEmpty() )
    {
        // Composite the cached theme backdrop, then draw the original Qwt items.
        setProperty( "bladeScopeCanvasPass", 3 );
        drawItems( painter, rect, maps );
        setProperty( "bladeScopeCanvasPass", 4 );
        drawItems( painter, rect, maps );
        setProperty( "bladeScopeCanvasPass", QVariant() );
        return;
    }
    if ( !themed || painter->device() != canvas || rect.isEmpty() )
    {
        drawItems( painter, rect, maps );
        return;
    }

    const qreal dpr = canvas->devicePixelRatioF();
    const QSize size = canvas->size() * dpr;
    QPixmap layer = qvariant_cast<QPixmap>( canvas->property( "bladeScopeWaveformLayer" ) );
    if ( layer.isNull() || layer.size() != size || layer.devicePixelRatioF() != dpr )
    {
        layer = QPixmap( size );
        if ( layer.isNull() )
        {
            drawItems( painter, rect, maps );
            return;
        }
        layer.setDevicePixelRatio( dpr );
        layer.fill( Qt::transparent );
        QPainter raster( &layer );
        raster.setClipRect( rect );
        setProperty( "bladeScopeCanvasPass", 1 );
        drawItems( &raster, rect, maps );
        raster.end();
        canvas->setProperty( "bladeScopeWaveformLayer", QVariant::fromValue( layer ) );
    }

    painter->drawPixmap( QPoint( 0, 0 ), layer );
    setProperty( "bladeScopeCanvasPass", 2 );
    drawItems( painter, rect, maps );
    setProperty( "bladeScopeCanvasPass", QVariant() );
'@ }
)
Update-BladeThemeSource "3rdparty/qwt/src/qwt_plot_canvas.cpp" @(
  @{ Old = '#include <qpainter.h>'; New = "#include <qpainter.h>`n#include <qvariant.h>" }
  @{ Old = 'void QwtPlotCanvas::invalidateBackingStore()' + "`n{"; New = @'
void QwtPlotCanvas::invalidateBackingStore()
{
    if ( property( "bladeScopeWaveformLayer" ).isValid() )
        setProperty( "bladeScopeWaveformLayer", QVariant() );
'@ }
)
Update-BladeThemeSource "plotjuggler_base/src/plotwidget_base.cpp" @(
  @{ Old = '  marker->attach(qwtPlot());'; New = "  marker->setZ(101);`n  marker->attach(qwtPlot());" }
)
Update-BladeThemeSource "plotjuggler_app/plotwidget.h" @(
  @{ Old = '  void setTrackerPosition(double abs_time);'; New = "  void setTrackerPosition(double abs_time);`n  void refreshTrackerOverlay();" }
)
Update-BladeThemeSource "plotjuggler_app/plotwidget.cpp" @(
  @{ Old = 'void PlotWidget::setTrackerPosition(double abs_time)'; New = @'
void PlotWidget::refreshTrackerOverlay()
{
  auto* canvas = qwtPlot()->canvas();
  if (qwtPlot()->property("bladeScopePlotTheme").toBool() &&
      !canvas->property("bladeScopeOpenGLCanvas").toBool())
    canvas->update();
  else
    qwtPlot()->replot();
}

void PlotWidget::setTrackerPosition(double abs_time)
'@ }
)
$trackerVisibleReplot = @'
    if (do_replot && plot->isVisible())
    {
      plot->replot();
    }
'@
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = $trackerVisibleReplot; New = @'
    if (do_replot && plot->isVisible())
    {
      plot->refreshTrackerOverlay();
    }
'@ }
)

Update-BladeThemeSource "plotjuggler_app/main.cpp" @(
  @{ Old = '  app.setApplicationVersion(VERSION_STRING);'; New = '  app.setApplicationVersion(QStringLiteral("3.17.0.10"));' + "`n" + '  app.setApplicationDisplayName(QStringLiteral("BladeScope"));' }
)
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '      setWindowTitle(title);'; New = '      setWindowTitle(_skin_path == QStringLiteral("://resources/skin") ? QStringLiteral("BladeScope 3.17.0.10") : title);' }
)

# Replace the FFT layout while retaining the upstream controls and signals.
$fftUi = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "toolbox_FFT.ui")
Set-Content -Path "plotjuggler_plugins/ToolboxFFT/toolbox_FFT.ui" -Value $fftUi -Encoding UTF8
Update-BladeThemeSource "plotjuggler_plugins/ToolboxFFT/toolbox_FFT.cpp" @(
  @{ Old = '  ui->setupUi(_widget);'; New = @'
  ui->setupUi(_widget);
  ui->plotSplitter->setStretchFactor(0, 1);
  ui->plotSplitter->setStretchFactor(1, 1);
  ui->plotSplitter->setSizes({ 1, 1 });
'@ }
  @{ Old = '  preview_layout_A->setMargin(6);'; New = '  preview_layout_A->setContentsMargins(0, 0, 0, 0);' }
  @{ Old = '  preview_layout_B->setMargin(6);'; New = '  preview_layout_B->setContentsMargins(0, 0, 0, 0);' }
)
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '    connect(action, &QAction::triggered, toolbox_ptr, &ToolboxPlugin::onShowWidget);'; New = @'
    if (toolbox_name == QStringLiteral("Fast Fourier Transform"))
      action->setText(QString::fromUtf8("FFT 频谱分析"));
    else if (toolbox_name == QStringLiteral("CSV/Parquet Exporter"))
      action->setText(QStringLiteral("CSV / Parquet \u5bfc\u51fa"));
    else if (toolbox_name == QStringLiteral("Reactive Script Editor"))
      action->setText(QStringLiteral("Lua \u811a\u672c\u7f16\u8f91\u5668"));
    else if (toolbox_name == QStringLiteral("Quaternion to RPY"))
      action->setText(QStringLiteral("\u56db\u5143\u6570\u8f6c\u6b27\u62c9\u89d2"));
    connect(action, &QAction::triggered, toolbox_ptr, &ToolboxPlugin::onShowWidget);
'@ }
)

# Localized layouts for the remaining built-in tools.
$toolUi = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "toolbox_csv.ui")
Set-Content -Path "plotjuggler_plugins/ToolboxCSV/toolbox_csv.ui" -Value $toolUi -Encoding UTF8
$toolUi = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "lua_editor.ui")
Set-Content -Path "plotjuggler_plugins/ToolboxLuaEditor/lua_editor.ui" -Value $toolUi -Encoding UTF8
$toolUi = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "quaternion_to_rpy.ui")
Set-Content -Path "plotjuggler_plugins/ToolboxQuaternion/quaternion_to_rpy.ui" -Value $toolUi -Encoding UTF8
Update-BladeThemeSource "plotjuggler_plugins/ToolboxCSV/toolbox_ui.cpp" @(
  @{ Old = @'
  ui->setupUi(_widget);
'@; New = @'
  ui->setupUi(_widget);
'@ }
  @{ Old = @'
"Export data"
'@; New = @'
QStringLiteral("\u5bfc\u51fa\u6570\u636e")
'@ }
  @{ Old = @'
"Select export directory"
'@; New = @'
QStringLiteral("\u9009\u62e9\u5bfc\u51fa\u6587\u4ef6\u5939")
'@ }
)
Update-BladeThemeSource "plotjuggler_plugins/ToolboxQuaternion/toolbox_quaternion.cpp" @(
  @{ Old = @'
  ui->setupUi(_widget);
'@; New = @'
  ui->setupUi(_widget);
'@ }
  @{ Old = @'
new PJ::PlotWidgetBase(ui->frame);
'@; New = @'
new PJ::PlotWidgetBase(ui->framePlotPreview);
'@ }
)
Update-BladeThemeSource "plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp" @(
  @{ Old = @'
  ui->setupUi(_widget);
'@; New = @'
  ui->setupUi(_widget);
  ui->scriptSplitter->setStretchFactor(0, 0);
  ui->scriptSplitter->setStretchFactor(1, 1);
  ui->scriptSplitter->setSizes({ 176, 700 });
  connect(ui->showScriptList, &QCheckBox::toggled, ui->scriptList, &QWidget::setVisible);
  ui->codeSplitter->setStretchFactor(0, 1);
  ui->codeSplitter->setStretchFactor(1, 2);
  ui->codeSplitter->setSizes({ 180, 360 });
'@ }
  @{ Old = @'
"Do you want to save the current active scripts?\n\n"
'@; New = @'
QStringLiteral("\u662f\u5426\u4fdd\u5b58\u5f53\u524d\u4f7f\u7528\u7684\u811a\u672c\uff1f\n\n")
'@ }
  @{ Old = @'
tr("Overwrite")
'@; New = @'
QStringLiteral("\u8986\u76d6")
'@ }
  @{ Old = @'
    msgBox.setText(tr("A dfunction with the same name exists already.\n"
                      " Do you want to overwrite it?\n"));
'@; New = @'
    msgBox.setText(QStringLiteral("\u5df2\u6709\u540c\u540d\u811a\u672c\uff0c\u662f\u5426\u8986\u76d6\uff1f"));
'@ }
  @{ Old = @'
"Everything is fine :)"
'@; New = @'
QStringLiteral("\u51fd\u6570\u5e93\u9a8c\u8bc1\u901a\u8fc7")
'@ }
)
$toolSource = (Get-Content -Raw -Encoding UTF8 "plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp").Replace("`r`n", "`n")
$toolSource = $toolSource.Replace('"Error in Lua code"', 'QStringLiteral("Lua \u4ee3\u7801\u9519\u8bef")')
$toolSource = $toolSource.Replace('QMessageBox::question(nullptr, this->name(), msg)', 'QMessageBox::question(nullptr, QStringLiteral("Lua \u811a\u672c\u7f16\u8f91\u5668"), msg)')
Set-Content -Path "plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp" -Value $toolSource -Encoding UTF8BOM
$toolSource = (Get-Content -Raw -Encoding UTF8 "plotjuggler_plugins/ToolboxCSV/toolbox_csv.cpp").Replace("`r`n", "`n")
$toolSource = $toolSource.Replace('"Export"', 'QStringLiteral("\u5bfc\u51fa")')
$toolSource = $toolSource.Replace('"Can''t load Arrow/Parquet"', 'QStringLiteral("\u65e0\u6cd5\u52a0\u8f7d Arrow/Parquet")')
$toolSource = $toolSource.Replace('"No topics selected."', 'QStringLiteral("\u672a\u9009\u62e9\u5f85\u5bfc\u51fa\u7684\u66f2\u7ebf\u5206\u7ec4\u3002")')
$toolSource = $toolSource.Replace('"No samples found in the selected time range."', 'QStringLiteral("\u6240\u9009\u65f6\u95f4\u8303\u56f4\u5185\u6ca1\u6709\u91c7\u6837\u70b9\u3002")')
$toolSource = $toolSource.Replace('"Failed to write the output file."', 'QStringLiteral("\u65e0\u6cd5\u5199\u5165\u8f93\u51fa\u6587\u4ef6\u3002")')
$toolSource = $toolSource.Replace('"Failed to write file: %1"', 'QStringLiteral("\u65e0\u6cd5\u5199\u5165\u6587\u4ef6\uff1a%1")')
$toolSource = $toolSource.Replace('QString("<b>File saved in folder:</b><br>%1<br><br><b>File name:</b><br>%2")', 'QStringLiteral("<b>\u4fdd\u5b58\u76ee\u5f55</b><br>%1<br><br><b>\u6587\u4ef6\u540d</b><br>%2")')
$toolSource = $toolSource.Replace('QString("<b>Files saved in folder:</b><br>%1<br><br><b>%2</b><br>%3")', 'QStringLiteral("<b>\u4fdd\u5b58\u76ee\u5f55</b><br>%1<br><br><b>%2</b><br>%3")')
Set-Content -Path "plotjuggler_plugins/ToolboxCSV/toolbox_csv.cpp" -Value $toolSource -Encoding UTF8BOM

# Isolate tool size hints so hidden tools cannot enlarge the main stacked layout.
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include <QScrollArea>" }
  @{ Old = '    ui->widgetStack->addWidget(widget);'; New = @'
    auto* toolViewport = new QScrollArea(ui->widgetStack);
    toolViewport->setFrameShape(QFrame::NoFrame);
    toolViewport->setWidgetResizable(true);
    toolViewport->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    toolViewport->setWidget(widget);
    ui->widgetStack->addWidget(toolViewport);
'@ }
)

Update-BladeThemeSource "plotjuggler_plugins/ToolboxFFT/toolbox_FFT.cpp" @(
  @{ Old = 'connect(ui->buttonBox, &QDialogButtonBox::rejected,'; New = 'connect(ui->closeToolButton, &QPushButton::clicked,' }
)

Update-BladeThemeSource "plotjuggler_plugins/ToolboxCSV/toolbox_ui.cpp" @(
  @{ Old = 'connect(ui->buttonBox, &QDialogButtonBox::rejected,'; New = 'connect(ui->closeToolButton, &QPushButton::clicked,' }
)

Update-BladeThemeSource "plotjuggler_plugins/ToolboxQuaternion/toolbox_quaternion.cpp" @(
  @{ Old = 'connect(ui->buttonBox, &QDialogButtonBox::rejected,'; New = 'connect(ui->closeToolButton, &QPushButton::clicked,' }
)

Update-BladeThemeSource "plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp" @(
  @{ Old = 'connect(ui->buttonBox, &QDialogButtonBox::rejected,'; New = 'connect(ui->closeToolButton, &QPushButton::clicked,' }
)

$analysisHeader = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "bladescope_analysis.h")
Set-Content -Path "plotjuggler_app/bladescope_analysis.h" -Value $analysisHeader -Encoding UTF8BOM
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include `"bladescope_analysis.h`"" }
  @{ Old = '  ui->menuBar->removeAction(ui->menuCloudData->menuAction());'; New = @'
  ui->menuBar->removeAction(ui->menuCloudData->menuAction());
  auto* bladeGroupManager = bladePlotMenu->addAction(QString::fromUtf8("测点组管理..."));
  connect(bladeGroupManager, &QAction::triggered, this, [this] {
    BladeAnalysis::openGroupManager(this, _mapped_plot_data);
  });
  auto* bladeAnalysis = bladePlotMenu->addAction(QString::fromUtf8("测点组与录波分析..."));
  connect(bladeAnalysis, &QAction::triggered, this, [this] {
    if (_mapped_plot_data.numeric.empty())
    {
      QMessageBox::information(this, QString::fromUtf8("录波分析"), QString::fromUtf8("请先加载录波。"));
      return;
    }
    auto* docker = _main_tabbed_widget->currentTab();
    auto* plot = docker && docker->plotCount() ? docker->plotAt(0) : nullptr;
    QStringList selected;
    double start = std::numeric_limits<double>::infinity();
    double end = -start;
    for (const auto& item : _mapped_plot_data.numeric)
      if (item.second.size())
      {
        start = std::min(start, item.second.at(0).x);
        end = std::max(end, item.second.at(item.second.size() - 1).x);
      }
    if (plot && !plot->isXYPlot())
    {
      for (const auto& curve : plot->curveList())
        selected << QString::fromStdString(curve.src_name);
      if (!selected.isEmpty())
      {
        const auto rect = plot->currentBoundingRect();
        start = rect.left() + plot->timeOffset();
        end = rect.right() + plot->timeOffset();
      }
    }
    BladeAnalysis::open(this, _mapped_plot_data, selected, start, end,
        [this, plot](const QStringList& names, bool newTab, double first, double last) {
          auto* target = plot;
          if (newTab || !target || target->isXYPlot())
            target = _main_tabbed_widget->addTab({})->plotAt(0);
          if (!target)
            return;
          for (const auto& name : names)
          {
            const auto raw = name.toStdString();
            const auto& curves = target->curveList();
            if (std::none_of(curves.begin(), curves.end(), [&](const auto& c) { return c.src_name == raw; }))
              target->addCurve(raw);
          }
          target->zoomOut(false);
          if (first < last)
          {
            auto rect = target->currentBoundingRect();
            rect.setLeft(first - target->timeOffset());
            rect.setRight(last - target->timeOffset());
            target->setZoomRectangle(rect, false);
            target->on_zoomOutVertical_triggered(false);
          }
          target->replot();
          onUndoableChange();
        });
  });
'@ }
)

$recordingIndexHeader = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "bladescope_recording_index.h")
Set-Content -Path "plotjuggler_app/bladescope_recording_index.h" -Value $recordingIndexHeader -Encoding UTF8BOM
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include `"bladescope_recording_index.h`"" }
  @{ Old = '  auto* bladeReload = bladeFileMenu->addAction'; New = @'
  auto* bladeIndex = bladeFileMenu->addAction(QString::fromUtf8("CSV 录波目录索引..."));
  connect(bladeIndex, &QAction::triggered, this, [this] {
    BladeRecordingIndex::open(this, [this](QStringList files) {
      loadDataFromFiles(files, files.size() > 1);
    }, [this](PlotDataMapRef& data) {
      importPlotDataMap(data, false);
      _curvelist_widget->updateFilter();
      updateDataAndReplot(true);
    }, [this] { return _mapped_plot_data.getAllNames(); });
  });
  auto* dataIndex = bladeFileMenu->addAction(QStringLiteral("DATA \u5f55\u6ce2\u76ee\u5f55\u7d22\u5f15..."));
  connect(dataIndex, &QAction::triggered, this, [this] {
    BladeDataIndex::open(this, [this](const QStringList& files) {
      const auto messages = BladeDataIndex::importFiles(this, files, _mapped_plot_data.getAllNames(),
          [this](PlotDataMapRef& data) { importPlotDataMap(data, false); });
      if (messages.isEmpty()) return;
      forEachWidget([this](PlotWidget* plot) {
        plot->on_changeDateTimeScale(ui->buttonUseDateTime->isChecked());
      });
      _curvelist_widget->updateFilter();
      updateDataAndReplot(true);
      linkedZoomOut();
      updatedDisplayTime();
      QMessageBox::information(this, QStringLiteral("DATA \u5bfc\u5165\u7ed3\u679c"), messages.join('\n'));
    });
  });
  auto* bladeReload = bladeFileMenu->addAction
'@ }
)

foreach ($fitHeader in @("bladescope_fit_math.h", "bladescope_fit_panel.h", "bladescope_trend.h", "bladescope_fit_acceptance.h", "bladescope_fit_xlsx.h", "bladescope_label_xlsx.h")) {
  $fitSource = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot $fitHeader)
  Set-Content -Path (Join-Path "plotjuggler_app" $fitHeader) -Value $fitSource -Encoding UTF8BOM
}
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include `"bladescope_fit_panel.h`"`n#include `"bladescope_fit_acceptance.h`"" }
  @{ Old = '  int headerButtonIndex = 0;'; New = @'
  auto* fitEntry = new QToolButton(tabCorner);
  fitEntry->setText(QStringLiteral("曲线分析"));
  fitEntry->setObjectName("bladeFitEntry");
  fitEntry->setMinimumWidth(78);
  fitEntry->setStyleSheet(QStringLiteral(
    "QToolButton#bladeFitEntry { background: palette(base); color: palette(text); "
    "border: 1px solid palette(mid); border-radius: 4px; padding: 0 7px; font-weight: 600; }"
    "QToolButton#bladeFitEntry:hover { background: palette(alternate-base); }"
    "QToolButton#bladeFitEntry:pressed { background: palette(highlight); color: palette(highlighted-text); }"));
  fitEntry->setFixedHeight(22);
  tabCornerLayout->insertWidget(0, fitEntry);
  auto showFitPanel = [this] {
    auto* panel = ui->widgetStack->findChild<QWidget*>("bladeFitPanel");
    if (!panel)
    {
      auto* view = new BladeFitUI::Panel(_mapped_plot_data, [this] {
        ui->widgetStack->setCurrentIndex(0);
      });
      view->setObjectName("bladeFitPanel");
      auto* scroll = new QScrollArea(ui->widgetStack);
      scroll->setFrameShape(QFrame::NoFrame);
      scroll->setWidgetResizable(true);
      scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
      scroll->setWidget(view);
      ui->widgetStack->addWidget(scroll);
      panel = view;
    }
    auto* scroll = qobject_cast<QScrollArea*>(panel->parentWidget()->parentWidget());
    if (scroll) ui->widgetStack->setCurrentWidget(scroll);
    static_cast<BladeFitUI::Panel*>(panel)->enter();
  };
  connect(fitEntry, &QToolButton::clicked, this, showFitPanel);
  connect(ui->menuTools->addAction(QStringLiteral("曲线分析")), &QAction::triggered, this, showFitPanel);
  const auto fitAcceptanceOutput = qEnvironmentVariable("BLADESCOPE_FIT_ACCEPTANCE_OUTPUT");
  if (!fitAcceptanceOutput.isEmpty())
  {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    BladeFitUI::verifyM01(this, _mapped_plot_data, fitEntry, ui->widgetStack, fitAcceptanceOutput);
  }
  int headerButtonIndex = 1;
'@ }
)
Update-BladeThemeSource "plotjuggler_app/plotwidget.cpp" @(
  @{ Old = '#include "statistics_dialog.h"'; New = "#include `"statistics_dialog.h`"`n#include `"bladescope_trend.h`"" }
  @{ Old = '  menu.addAction(_action_formula);'; New = @'
  menu.addAction(_action_formula);
  if (!isXYPlot() && !curveList().empty())
  {
    auto* trends = menu.addMenu(QStringLiteral("添加趋势线"));
    for (const auto& entry : curveList())
    {
      const auto raw = entry.src_name;
      if (!_mapped_data.numeric.count(raw)) continue;
      connect(trends->addAction(entry.curve->title().text()), &QAction::triggered, this,
              [this, raw] { BladeTrend::open(this, qwtPlot(), raw); });
    }
  }
'@ }
)
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '  if (curve_updated)'; New = @'
  if (auto* fit = ui->widgetStack->findChild<QWidget*>("bladeFitPanel"))
    static_cast<BladeFitUI::Panel*>(fit)->invalidate();
  if (curve_updated)
'@ }
)
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '  _mapped_plot_data.clear();'; New = @'
  _mapped_plot_data.clear();
  if (auto* fit = ui->widgetStack->findChild<QWidget*>("bladeFitPanel"))
    static_cast<BladeFitUI::Panel*>(fit)->invalidate();
'@ }
)


Add-Content -Path "plotjuggler_app/CMakeLists.txt" -Value 'target_link_libraries(plotjuggler PRIVATE Qt5::GuiPrivate)' -Encoding UTF8

Copy-Item (Join-Path $PSScriptRoot "bladescope_time_display.h") "plotjuggler_app/bladescope_time_display.h" -Force
Copy-Item (Join-Path $PSScriptRoot "bladescope_time_display.h") "plotjuggler_plugins/ToolboxFFT/bladescope_time_display.h" -Force
$txtHeader = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "bladescope_txt_recording.h")
Set-Content -Path "plotjuggler_app/bladescope_txt_recording.h" -Value $txtHeader -Encoding UTF8BOM
Copy-Item (Join-Path $PSScriptRoot "bladescope_data_index.h") "plotjuggler_app/bladescope_data_index.h" -Force
$sqliteHeader = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "bladescope_sqlite_recording.h")
Set-Content -Path "plotjuggler_app/bladescope_sqlite_recording.h" -Value $sqliteHeader -Encoding UTF8BOM
Add-Content -Path "plotjuggler_app/CMakeLists.txt" -Value 'find_package(Qt5 REQUIRED COMPONENTS Sql)' -Encoding UTF8
Add-Content -Path "plotjuggler_app/CMakeLists.txt" -Value 'target_link_libraries(plotjuggler PRIVATE Qt5::Sql)' -Encoding UTF8
Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = '#include "mainwindow.h"'; New = "#include `"mainwindow.h`"`n#include `"bladescope_sqlite_recording.h`"`n#include `"bladescope_txt_recording.h`"`n#include `"bladescope_data_index.h`"" }
  @{ Old = '#include <QFileDialog>'; New = @'
#include <QFileDialog>
#include <QComboBox>
#include <QGridLayout>
#include "bladescope_time_display.h"
'@ }
  @{ Old = '  loadDialog.setOption(QFileDialog::DontUseNativeDialog, true);'; New = @'
  loadDialog.setOption(QFileDialog::DontUseNativeDialog, true);
  auto* precisionBox = new QComboBox(&loadDialog);
  precisionBox->addItems({QStringLiteral("\u7cfb\u7edf\u9ed8\u8ba4\uff08\u81ea\u52a8\uff09"),
                          QStringLiteral("\u79d2"), QStringLiteral("\u6beb\u79d2")});
  precisionBox->setCurrentIndex(BladeTimeDisplay::precision());
  precisionBox->setToolTip(QStringLiteral("\u4ec5\u5f71\u54cd\u65f6\u95f4\u663e\u793a\uff0c\u4fdd\u7559\u539f\u59cb\u91c7\u6837\u65f6\u95f4\u3002"));
  auto* fileLayout = static_cast<QGridLayout*>(loadDialog.layout());
  const int precisionRow = fileLayout->rowCount();
  fileLayout->addWidget(new QLabel(QStringLiteral("\u65f6\u95f4\u7cbe\u5ea6"), &loadDialog), precisionRow, 0);
  fileLayout->addWidget(precisionBox, precisionRow, 1);
'@ }
  @{ Old = '  directory_path = QFileInfo(fileNames[0]).absolutePath();'; New = @'
  BladeTimeDisplay::setPrecision(precisionBox->currentIndex());
  forEachWidget([this](PlotWidget* plot) {
    plot->on_changeDateTimeScale(ui->buttonUseDateTime->isChecked());
    plot->replot();
  });
  updatedDisplayTime();
  directory_path = QFileInfo(fileNames[0]).absolutePath();
'@ }
  @{ Old = '  extensions.push_front(QString("All Supported Files (%1)").arg(single_line_extensions.trimmed()));'; New = @'
  const QString sqliteExtensions = QStringLiteral("*.data *.db *.sqlite *.sqlite3");
  single_line_extensions.append(sqliteExtensions + QStringLiteral(" *.txt"));
  extensions.push_back(QStringLiteral("TXT 高频录波文件 (*.txt)"));
  extensions.push_back(QStringLiteral("SQLite 录波文件 (%1)").arg(sqliteExtensions));
  extensions.push_front(QString("All Supported Files (%1)").arg(single_line_extensions.trimmed()));
'@ }
  @{ Old = @'
  if (loadDataFromFiles(fileNames))
  {
    updateRecentDataMenu(fileNames);
  }
'@; New = @'
  QStringList ordinaryFiles;
  for (const auto& path : fileNames)
    if (!BladeSqliteRecording::isRecordingExtension(path) && !BladeTxtRecording::isRecordingExtension(path))
      ordinaryFiles << path;
  if (!ordinaryFiles.isEmpty() && loadDataFromFiles(ordinaryFiles))
    updateRecentDataMenu(ordinaryFiles);
  if (ordinaryFiles.size() == fileNames.size()) return;

  QStringList summaries, dataFiles;
  for (const auto& path : fileNames)
  {
    if (BladeTxtRecording::isRecordingExtension(path))
    {
      auto recording = BladeTxtRecording::open(this, path, _mapped_plot_data.getAllNames());
      if (recording.data.numeric.empty()) continue;
      const auto series = recording.data.numeric.size();
      importPlotDataMap(recording.data, false);
      summaries << QStringLiteral("%1：%2 行，%3 个测点，%4 条重复时间记录（已保留），跳过 %5 条无效时间和 %6 个无效测点值。")
        .arg(QFileInfo(path).fileName()).arg(qlonglong(recording.rows)).arg(qulonglong(series))
        .arg(qlonglong(recording.duplicateTime)).arg(qlonglong(recording.invalidTime))
        .arg(qlonglong(recording.invalidValues));
      continue;
    }
    if (BladeSqliteRecording::isRecordingExtension(path)) dataFiles << path;
  }
  summaries << BladeDataIndex::importFiles(this, dataFiles, _mapped_plot_data.getAllNames(),
      [this](PlotDataMapRef& data) { importPlotDataMap(data, false); });
  if (!summaries.isEmpty())
  {
    forEachWidget([this](PlotWidget* plot) {
      plot->on_changeDateTimeScale(ui->buttonUseDateTime->isChecked());
    });
    _curvelist_widget->updateFilter();
    updateDataAndReplot(true);
    linkedZoomOut();
    updatedDisplayTime();
    QMessageBox::information(this, QStringLiteral("录波导入完成"), summaries.join('\n'));
  }
'@ }
)


Update-BladeThemeSource "plotjuggler_app/plotwidget.cpp" @(
  @{ Old = '#include "statistics_dialog.h"'; New = @'
#include "statistics_dialog.h"
#include "bladescope_time_display.h"
#include "qwt_scale_div.h"
'@ }
  @{ Old = @'
  virtual QwtText label(double v) const
  {
    QDateTime dt = QDateTime::fromMSecsSinceEpoch((qint64)(v * 1000), Qt::UTC);
    if (dt.date().year() == 1970 && dt.date().month() == 1 && dt.date().day() == 1)
    {
      return dt.toString("hh:mm:ss.z");
    }
    return dt.toString("hh:mm:ss.z\nyyyy MMM dd");
  }
'@; New = @'
public:
  explicit TimeScaleDraw(bool clock) : _clock(clock) {}
  QwtText label(double v) const override
  {
    if (!_clock && _precision == BladeTimeDisplay::Automatic)
      return BladePlotTheme::ScaleDraw::label(v);
    const auto ticks = scaleDiv().ticks(QwtScaleDiv::MajorTick);
    const double step = ticks.size() > 1 ? ticks.at(1) - ticks.at(0) : v;
    const bool calendar = BladeTimeDisplay::isCalendarTime(v);
    return BladeTimeDisplay::format(v, _clock, calendar, calendar,
                                   BladeTimeDisplay::decimals(_precision, step));
  }

private:
  const bool _clock;
  const BladeTimeDisplay::Precision _precision = BladeTimeDisplay::precision();
'@ }
  @{ Old = @'
  if (enable && !isXYPlot())
  {
    if (!is_timescale)
    {
      qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new TimeScaleDraw());
    }
  }
  else
  {
    if (is_timescale)
    {
      qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new BladePlotTheme::ScaleDraw);
    }
  }
'@; New = @'
  if (!isXYPlot())
    qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new TimeScaleDraw(enable));
  else if (is_timescale)
    qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new BladePlotTheme::ScaleDraw);
'@ }
)

Update-BladeThemeSource "plotjuggler_app/mainwindow.cpp" @(
  @{ Old = @'
    updateTimeOffset();
    updateTimeSlider();
  });
'@; New = @'
    updateTimeOffset();
    updateTimeSlider();
    updatedDisplayTime();
  });
'@ }
  @{ Old = @'
  if (ui->buttonUseDateTime->isChecked())
  {
    if (ui->buttonRemoveTimeOffset->isChecked())
    {
      QTime time = QTime::fromMSecsSinceStartOfDay(std::round(relative_time * 1000.0));
      timeLine->setText(time.toString("HH:mm::ss.zzz"));
    }
    else
    {
      QDateTime datetime = QDateTime::fromMSecsSinceEpoch(std::round(_tracker_time * 1000.0));
      timeLine->setText(datetime.toString("[yyyy MMM dd] HH:mm::ss.zzz"));
    }
  }
  else
  {
    timeLine->setText(QString::number(relative_time, 'f', 3));
  }
'@; New = @'
  const bool clock = ui->buttonUseDateTime->isChecked();
  const bool calendar = clock && !ui->buttonRemoveTimeOffset->isChecked() &&
                        BladeTimeDisplay::isCalendarTime(_tracker_time);
  const double time = calendar ? _tracker_time : relative_time;
  const auto mode = BladeTimeDisplay::precision();
  const int digits = mode == BladeTimeDisplay::Automatic ? 3 :
                     BladeTimeDisplay::decimals(mode, 1.0);
  timeLine->setText(BladeTimeDisplay::format(time, clock, calendar, calendar, digits));
'@ }
)

Update-BladeThemeSource "plotjuggler_plugins/ToolboxFFT/toolbox_FFT.cpp" @(
  @{ Old = '#include "KissFFT/kiss_fftr.h"'; New = @'
#include "KissFFT/kiss_fftr.h"
#include "qwt_plot.h"
#include "qwt_scale_draw.h"
#include "qwt_text.h"
#include "qwt_scale_div.h"
#include "bladescope_time_display.h"
#include <algorithm>

namespace
{
class RecordingTimeScale final : public QwtScaleDraw
{
public:
  RecordingTimeScale(double origin, bool showDate)
    : _calendar(BladeTimeDisplay::isCalendarTime(origin)), _showDate(showDate) {}
  QwtText label(double timestamp) const override
  {
    const auto ticks = scaleDiv().ticks(QwtScaleDiv::MajorTick);
    const double step = ticks.size() > 1 ? ticks.at(1) - ticks.at(0) : timestamp;
    return BladeTimeDisplay::format(timestamp, _calendar, _calendar, _showDate,
                                   BladeTimeDisplay::decimals(_precision, step));
  }
private:
  const bool _calendar;
  const bool _showDate;
  const BladeTimeDisplay::Precision _precision = BladeTimeDisplay::precision();
};

class FftTimePlot final : public PJ::PlotWidgetBase
{
public:
  using PJ::PlotWidgetBase::PlotWidgetBase;
  void setTimeOrigin(double origin)
  {
    _origin = origin;
    qwtPlot()->setAxisScaleDraw(QwtPlot::xBottom, new RecordingTimeScale(origin, _showDate));
  }
  void setShowDate(bool show)
  {
    _showDate = show;
    setTimeOrigin(_origin);
    qwtPlot()->replot();
  }

private:
  double _origin = 0;
  bool _showDate = false;
};
}
'@ }
  @{ Old = '  _plot_widget_A = new PJ::PlotWidgetBase(ui->framePlotPreviewA);'; New = @'
  _plot_widget_A = new FftTimePlot(ui->framePlotPreviewA);
  auto* timePlot = static_cast<FftTimePlot*>(_plot_widget_A);
  timePlot->setTimeOrigin(0);
  connect(ui->checkShowDate, &QCheckBox::toggled, _widget, [timePlot](bool visible) {
    timePlot->setShowDate(visible);
  });
'@ }
  @{ Old = @'
  _zoom_range.min = std::numeric_limits<double>::lowest();
  _zoom_range.max = std::numeric_limits<double>::max();
'@; New = @'
  _zoom_range.min = std::numeric_limits<double>::max();
  _zoom_range.max = std::numeric_limits<double>::lowest();
'@ }
  @{ Old = @'
    PlotData& curve_data = _plot_data->getOrCreateNumeric(curve_id);

    _plot_widget_A->addCurve(curve_id, curve_data);
    _curve_names.push_back(curve_id);
    _zoom_range.min = std::min(_zoom_range.min, curve_data.front().x);
    _zoom_range.max = std::max(_zoom_range.max, curve_data.back().x);
  }

  ui->pushButtonSave->setEnabled(true);
'@; New = @'
    const auto found = _plot_data->numeric.find(curve_id);
    if (found == _plot_data->numeric.end() || found->second.size() == 0 ||
        std::find(_curve_names.begin(), _curve_names.end(), curve_id) != _curve_names.end()) continue;
    _plot_widget_A->addCurve(curve_id, found->second);
    _curve_names.push_back(curve_id);
  }
  for (const auto& curve_id : _curve_names)
  {
    const auto found = _plot_data->numeric.find(curve_id);
    if (found == _plot_data->numeric.end() || found->second.size() == 0) continue;
    _zoom_range.min = std::min(_zoom_range.min, found->second.front().x);
    _zoom_range.max = std::max(_zoom_range.max, found->second.back().x);
  }
  if (_zoom_range.min > _zoom_range.max) { _dragging_curves.clear(); return; }
  static_cast<FftTimePlot*>(_plot_widget_A)->setTimeOrigin(_zoom_range.min);

  ui->pushButtonSave->setEnabled(true);
'@ }
  @{ Old = @'
  _plot_widget_A->removeAllCurves();
  _plot_widget_A->resetZoom();
'@; New = @'
  _plot_widget_A->removeAllCurves();
  static_cast<FftTimePlot*>(_plot_widget_A)->setTimeOrigin(0);
  _plot_widget_A->resetZoom();
'@ }
)

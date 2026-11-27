#pragma once
#include <QtWidgets>
#include <QtSql>
#include <atomic>
#include <functional>
#include <memory>
#include "bladescope_diagnosis_events.h"
#include "bladescope_diagnosis_trial_panel.h"
#include "bladescope_diagnosis_pack_panel.h"

namespace BladeDiagnosisUI {
class Panel final:public QWidget {
  QVector<EventRow> events;
  QTableWidget* table;
  QLabel *status,*quality;
  QPlainTextEdit* eventDetails=nullptr;
  QDateTimeEdit* center;
  QSpinBox *offset,*before,*after,*maxGap;
  QLineEdit *vendor,*model;
  QListWidget* measurementList;
  std::function<QStringList()> available;
  std::function<void(QStringList,double,double)> evidence;
  std::shared_ptr<std::atomic_bool> cancellation;
  int generation=0;
  QVector<EventRow> visible;
  void refresh(){
    visible.clear();table->setRowCount(0);if(eventDetails)eventDetails->clear();
    const qint64 t=center->dateTime().toMSecsSinceEpoch();
    for(auto item:events){
      item.aligned=BladeDiagnosis::alignedScTime(item.original,item.kind=="SC"?qint64(offset->value()):0);
      if(item.aligned<t-qint64(before->value())*1000||item.aligned>t+qint64(after->value())*1000)continue;
      visible<<item;
    }
    std::stable_sort(visible.begin(),visible.end(),[](const auto& a,const auto& b){return a.aligned<b.aligned;});
    for(const auto& item:visible){
      const int row=table->rowCount();table->insertRow(row);
      QStringList cells{iso(item.original),iso(item.aligned),item.kind,item.code,item.state,item.source,QString::number(item.row),item.description};
      for(int col=0;col<cells.size();++col){auto* cell=new QTableWidgetItem(cells[col]);cell->setToolTip(cells[col]);table->setItem(row,col,cell);}
    }
    quality->setText(QStringLiteral("当前窗口 %1 条 / 已加载 %2 条；SC 对齐 = 原始时间 + %3 ms，TR 不移动。状态值保留原文，不猜测触发/恢复含义。")
      .arg(visible.size()).arg(events.size()).arg(offset->value()));
  }
  void load(QString kind,QString path={}){
    if(path.isEmpty())path=QFileDialog::getOpenFileName(this,kind+" CSV",{},"CSV / TXT (*.csv *.txt);;All files (*)");
    if(path.isEmpty())return;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"读取失败",file.errorString());return;}
    if(file.size()>64*1024*1024){QMessageBox::warning(this,"文件过大","当前事件导入上限为 64 MiB；大文件流式读取待真实样本验证后接入。");return;}
    const QByteArray bytes=file.readAll();
    QDialog d(this);d.setObjectName("diagnosisImportDialog");d.setWindowTitle(kind+" 导入预览与列映射");
    auto* root=new QVBoxLayout(&d);auto* f=new QFormLayout;root->addLayout(f);
    const auto screen=QGuiApplication::primaryScreen()->availableGeometry();
    d.resize(qMin(920,screen.width()-40),qMin(740,screen.height()-60));
    d.setMaximumSize(screen.size()-QSize(20,40));
    QSettings settings;const QString prefix="BladeScope/diagnosisImport/"+kind+"/";
    QComboBox enc,sep;enc.addItems({"自动","UTF-8","GB18030","UTF-16LE","UTF-16BE"});
    sep.addItems({"自动","逗号","Tab","分号"});
    enc.setCurrentText(settings.value(prefix+"encoding","自动").toString());
    sep.setCurrentIndex(settings.value(prefix+"separator",0).toInt());
    QSpinBox header;header.setRange(0,10000);header.setSpecialValueText("自动定位表头");header.setValue(settings.value(prefix+"header",0).toInt()==1?0:settings.value(prefix+"header",0).toInt());
    QCheckBox validOnly("仅导入有效记录（保留全部拒绝原因）");
    f->addRow("编码",&enc);f->addRow("分隔符",&sep);f->addRow("表头：0 自动 / 非空行号",&header);f->addRow(&validOnly);
    QComboBox times,codes,states,descriptions;
    for(auto* combo:{&times,&codes,&states,&descriptions}){
      combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
      combo->setMinimumContentsLength(12);combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      combo->setMaxVisibleItems(10);combo->view()->setTextElideMode(Qt::ElideRight);combo->view()->setMaximumWidth(qMin(800,screen.width()-80));
      connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),&d,[combo]{combo->setToolTip(combo->currentText());});
    }
    f->addRow("时间列",&times);f->addRow("代码/动作列",&codes);f->addRow("状态列（可选）",&states);f->addRow("说明列（可选）",&descriptions);
    QComboBox unit,zone;unit.addItems({"数字时间：Unix 秒","数字时间：Unix 毫秒"});zone.addItems({"无时区文本按 UTC","无时区文本按 UTC+08:00"});
    unit.setCurrentIndex(settings.value(prefix+"unit",0).toInt());zone.setCurrentIndex(settings.value(prefix+"zone",0).toInt());
    QLineEdit format(settings.value(prefix+"format","yyyy-MM-dd HH:mm:ss.zzz").toString());
    f->addRow("数字时间单位",&unit);f->addRow("文本时间时区",&zone);f->addRow("自定义文本格式",&format);
    QLabel notice("带 Z/UTC 偏移的时间保留其时区。先检查预览中的中文、时间和列；无法识别时修改上方参数后点击重新预览。");
    notice.setWordWrap(true);root->addWidget(&notice);
    QTableWidget preview;preview.setObjectName("diagnosisImportPreview");preview.setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview.horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    preview.horizontalHeader()->setDefaultSectionSize(160);root->addWidget(&preview,1);
    QLabel summary;summary.setWordWrap(true);root->addWidget(&summary);
    QPushButton retry("重新预览");root->addWidget(&retry);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);root->addWidget(&buttons);
    buttons.button(QDialogButtonBox::Ok)->setEnabled(false);
    Csv csv;ImportOptions o;bool ready=false;
    auto invalidate=[&]{ready=false;buttons.button(QDialogButtonBox::Ok)->setEnabled(false);};
    auto validate=[&]{
      o.timeColumn=times.currentIndex();o.codeColumn=codes.currentIndex();o.stateColumn=states.currentIndex()-1;o.descriptionColumn=descriptions.currentIndex()-1;
      o.numericMilliseconds=unit.currentIndex()==1;o.utcOffsetSeconds=zone.currentIndex()?28800:0;o.dateFormat=format.text();
      int good=0;
      if(ready&&o.timeColumn>=0&&o.codeColumn>=0&&o.timeColumn!=o.codeColumn)
        for(int i=0;i<qMin(20,csv.rows.size());++i)try{parseTime(csv.rows[i][o.timeColumn],o);if(!csv.rows[i][o.codeColumn].trimmed().isEmpty())++good;}catch(...){}
      buttons.button(QDialogButtonBox::Ok)->setEnabled(good>0);
      summary.setText(ready?QString("识别到 %1 列、%2 条结构有效记录；结构拒绝 %3 条；跳过前置 %4 条；移除空尾列单元 %5 个。预览前 20 条中可解析事件 %6 条。%7")
        .arg(csv.headers.size()).arg(csv.rows.size()).arg(csv.errors.size()).arg(csv.skippedPreamble).arg(csv.trimmedTrailing).arg(good)
        .arg(good?"":"请修正时间列、代码列或时间格式。"):"请重新预览。");
    };
    auto rebuild=[&]{
      invalidate();times.clear();codes.clear();states.clear();descriptions.clear();preview.clear();preview.setRowCount(0);
      struct Parsed {CsvDetection value;QString error;};auto result=std::make_shared<Parsed>();
      auto cancelled=std::make_shared<std::atomic_bool>(false);
      const QString encodingName=enc.currentText();
      const QChar delimiter=sep.currentIndex()==0?QChar():sep.currentIndex()==1?QChar(','):sep.currentIndex()==2?QChar('\t'):QChar(';');
      const int headerIndex=header.value();const bool partial=validOnly.isChecked();
      QProgressDialog progress("正在识别事件文件…","取消",0,0,&d);progress.setWindowModality(Qt::WindowModal);
      QEventLoop loop;
      auto* worker=QThread::create([=]{try{result->value=detectCsv(bytes,encodingName,delimiter,headerIndex,partial,cancelled.get(),kind=="ActionList");}catch(const std::exception& e){result->error=QString::fromUtf8(e.what());}});
      connect(&progress,&QProgressDialog::canceled,&loop,[=]{cancelled->store(true);});
      connect(worker,&QThread::finished,&loop,&QEventLoop::quit);
      progress.show();worker->start();loop.exec();worker->wait();delete worker;QObject::disconnect(&progress,nullptr,&loop,nullptr);progress.reset();progress.close();
      if(cancelled->load()){summary.setText("已取消预览");return;}
      if(!result->error.isEmpty()){summary.setText(result->error);return;}
      csv=std::move(result->value.csv);
      for(auto* combo:{&times,&codes})combo->addItems(csv.headers);
      states.addItem("未提供");states.addItems(csv.headers);descriptions.addItem("未提供");descriptions.addItems(csv.headers);
      auto restore=[&](QComboBox& combo,QString key,int fallback){
        const int i=combo.findText(settings.value(prefix+key).toString());combo.setCurrentIndex(i>=0?i:fallback);
        for(int j=0;j<combo.count();++j)combo.setItemData(j,combo.itemText(j),Qt::ToolTipRole);
      };
      int timeDefault=0,codeDefault=1;
      for(int i=0;i<csv.headers.size();++i){
        const auto name=csv.headers[i].toLower();
        if(name=="sttriggertimestamp"||name=="strtriggertimestamp"||name=="sttriggertime"||name=="strtriggertime"||name=="triggertime"||name=="timestamp"||name=="时间")timeDefault=i;
        if(name=="stdescription"||name=="strdescription"||name=="errorcode"||name=="faultcode"||name=="actionname"||name=="actioncode")codeDefault=i;
      }
      if(csv.inferredHeader){timeDefault=1;codeDefault=3;}
      restore(times,"time",timeDefault);restore(codes,"code",codeDefault);restore(states,"state",0);restore(descriptions,"description",0);
      if(csv.inferredHeader){times.setCurrentIndex(1);codes.setCurrentIndex(3);states.setCurrentIndex(0);descriptions.setCurrentIndex(3);}
      preview.setColumnCount(csv.headers.size());preview.setHorizontalHeaderLabels(csv.headers);
      preview.setRowCount(qMin(20,csv.rows.size()));
      for(int i=0;i<preview.rowCount();++i)for(int j=0;j<csv.headers.size();++j){
        auto* item=new QTableWidgetItem(csv.rows[i][j]);item->setToolTip(item->text());preview.setItem(i,j,item);
      }
      notice.setText(QString("当前识别：%1 / %2。请确认预览可读；已跳过前置说明 %3 行和结束标记 %4 条。DT# 时间自动识别，时区仍须确认。%5")
        .arg(result->value.encoding).arg(result->value.separator=='\t'?"Tab":QString(result->value.separator)).arg(csv.skippedPreamble).arg(csv.skippedFooter).arg(csv.inferredHeader?" 无表头动作清单按厂商四列格式补全，请确认动作列表列。":""));
      ready=true;validate();
    };
    connect(&retry,&QPushButton::clicked,&d,rebuild);
    for(auto* combo:{&enc,&sep})connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),&d,invalidate);
    connect(&header,qOverload<int>(&QSpinBox::valueChanged),&d,invalidate);connect(&validOnly,&QCheckBox::toggled,&d,invalidate);
    for(auto* combo:{&times,&codes,&states,&descriptions,&unit,&zone})connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),&d,validate);
    connect(&format,&QLineEdit::textChanged,&d,validate);
    connect(&buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    QTimer::singleShot(0,&d,rebuild);
    if(d.exec()!=QDialog::Accepted)return;
    validate();if(!buttons.button(QDialogButtonBox::Ok)->isEnabled())return;
    settings.setValue(prefix+"encoding",enc.currentText());settings.setValue(prefix+"separator",sep.currentIndex());
    settings.setValue(prefix+"header",header.value());settings.setValue(prefix+"time",times.currentText());
    settings.setValue(prefix+"code",codes.currentText());settings.setValue(prefix+"state",states.currentText());settings.setValue(prefix+"description",descriptions.currentText());
    settings.setValue(prefix+"unit",unit.currentIndex());settings.setValue(prefix+"zone",zone.currentIndex());settings.setValue(prefix+"format",format.text());
    if(cancellation)cancellation->store(true);cancellation=std::make_shared<std::atomic_bool>(false);const auto cancel=cancellation;const int ticket=++generation;
    auto result=std::make_shared<Imported>();auto error=std::make_shared<QString>();
    auto* thread=QThread::create([result,error,csv,o,path,kind,cancel]{try{*result=importEvents(csv,o,path,kind,cancel.get());}catch(const std::exception& e){*error=QString::fromUtf8(e.what());}});
    status->setText("正在导入事件…");
    connect(thread,&QThread::finished,this,[this,ticket,result,error,kind]{
      if(ticket!=generation)return;
      if(!error->isEmpty()){status->setText(*error);return;}
      if(result->events.empty()){status->setText("没有有效事件；请检查列、时区与时间格式。原有数据未清除。");return;}
      const bool keepCenter=kind=="ActionList"&&!events.empty();
      events.erase(std::remove_if(events.begin(),events.end(),[&](const auto& e){return e.kind==kind&&e.source==result->events.front().source;}),events.end());
      events+=result->events;
      qint64 t=result->events.front().original+(kind=="SC"?qint64(offset->value()):0);
      if(!keepCenter&&recordingRange){const auto range=recordingRange();
        for(const auto& event:result->events){const auto aligned=event.original+(kind=="SC"?qint64(offset->value()):0);
          if(aligned/1000.>=range.first&&aligned/1000.<=range.second){t=aligned;break;}}
      }
      if(!keepCenter)center->setDateTime(QDateTime::fromMSecsSinceEpoch(t,Qt::UTC));refresh();
      status->setText(QString("导入 %1 条，拒绝 %2 条。当前仅事件核对，尚未启用自动故障结论。").arg(result->events.size()).arg(result->errors.size()));
      if(!result->errors.empty()){QMessageBox msg(QMessageBox::Warning,"部分记录被拒绝","展开详情查看错误；未静默忽略无效记录。",QMessageBox::Ok,this);
        msg.setDetailedText(result->errors.join('\n'));msg.exec();}
    });
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);thread->start();
  }
  void inspectPackage(){
    const auto path=QFileDialog::getOpenFileName(this,"检查本地草稿规则包",{},"SQLite (*.sqlite *.db)");
    if(path.isEmpty())return;
    const auto name=QUuid::createUuid().toString();QString message;
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setConnectOptions("QSQLITE_OPEN_READONLY");db.setDatabaseName(path);
      if(!db.open())message=db.lastError().text();
      else{QSqlQuery q(db);if(!q.exec("SELECT COUNT(*) FROM records"))message="不是受支持的规则包";
        else{q.next();message=QString("记录 %1 条；").arg(q.value(0).toLongLong());
          if(q.exec("SELECT COUNT(*) FROM issues")&&q.next())message+=QString("导入问题 %1 项。").arg(q.value(0).toLongLong());
          message+="\n当前阶段只检查草稿台账，不将原配置自动转为诊断结论。";}}
      db.close();}
    QSqlDatabase::removeDatabase(name);QMessageBox::information(this,"规则包检查",message);
  }
public:
  TrialReader trialReader;
  std::function<std::pair<double,double>()> recordingRange;
  std::function<QStringList(QStringList,double,double,int)> inspectDataQuality;
  void importEventFile(const QString& path,const QString& kind){if(kind=="SC"||kind=="ActionList")load(kind,path);}
  Panel(std::function<QStringList()> names,std::function<void(QStringList,double,double)> draw,
        std::function<void()> back,QWidget* parent=nullptr):QWidget(parent),available(std::move(names)),evidence(std::move(draw)){
    setObjectName("bladeDiagnosisPanel");auto* root=new QVBoxLayout(this);auto* top=new QHBoxLayout;root->addLayout(top);
    auto button=[&](QString text,auto callback){auto* b=new QPushButton(text);top->addWidget(b);connect(b,&QPushButton::clicked,this,callback);return b;};
    button("返回时序",back);button("导入 SC",[this]{load("SC");});button("关联 ActionList",[this]{load("ActionList");});
    button("草稿台账检查",[this]{inspectPackage();});
    button("条件规则包",[this]{
      if(vendor->text().trimmed().isEmpty()||model->text().trimmed().isEmpty()){status->setText("请先在事件参数中填写当前厂商与机型，再配置规则包");return;}
      const double t=center->dateTime().toMSecsSinceEpoch()/1000.;setProperty("diagnosisCenterUtc",t);
      showConditionPack(this,available(),vendor->text().trimmed(),model->text().trimmed(),t-before->value(),t+after->value(),trialReader,evidence,QString("中心 UTC=%1；SC 偏移=%2 ms；当前窗口事件数=%3；状态含义未配置").arg(iso(qRound64(t*1000))).arg(offset->value()).arg(visible.size()));});
    auto navigate=[this](bool forward){
      const qint64 now=center->dateTime().toMSecsSinceEpoch();qint64 target=now;bool found=false;
      for(const auto& item:events){const auto t=BladeDiagnosis::alignedScTime(item.original,item.kind=="SC"?qint64(offset->value()):0);
        if((forward?t>now:t<now)&&(!found||(forward?t<target:t>target))){target=t;found=true;}}
      if(found)center->setDateTime(QDateTime::fromMSecsSinceEpoch(target,Qt::UTC));
      else status->setText(forward?"已经是最后一个事件时间":"已经是第一个事件时间");
    };
    button("上一事件",[navigate]{navigate(false);});button("下一事件",[navigate]{navigate(true);});
    top->addStretch();
    auto* optionsWidget=new QWidget;auto* options=new QFormLayout(optionsWidget);root->addWidget(optionsWidget);optionsWidget->hide();button("事件参数",[optionsWidget]{optionsWidget->setVisible(!optionsWidget->isVisible());});vendor=new QLineEdit;model=new QLineEdit;
    vendor->setPlaceholderText("厂商，待确认");model->setPlaceholderText("机型，待确认");options->addRow("厂商 / 机型",vendor);options->addRow("机型",model);
    center=new QDateTimeEdit(QDateTime::currentDateTimeUtc());center->setTimeSpec(Qt::UTC);center->setDisplayFormat("yyyy-MM-dd HH:mm:ss.zzz");
    offset=new QSpinBox;offset->setRange(-86400000,86400000);offset->setSuffix(" ms");
    before=new QSpinBox;after=new QSpinBox;for(auto* spin:{before,after}){spin->setRange(0,3600);spin->setValue(60);spin->setSuffix(" s");}
    options->addRow("事件中心（UTC）",center);options->addRow("SC 手动对时偏移",offset);
    maxGap=new QSpinBox;maxGap->setRange(1,3600000);maxGap->setValue(100);maxGap->setSuffix(" ms");options->addRow("允许的最大采样间隔",maxGap);
    options->addRow("前窗口",before);options->addRow("后窗口",after);
    table=new QTableWidget(0,8);table->setObjectName("diagnosisEventTable");table->setHorizontalHeaderLabels({"原始时间","对齐时间（UTC）","来源类别","代码/动作","状态原文","源文件","非空记录号","说明原文"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->horizontalHeader()->setDefaultSectionSize(160);root->addWidget(table,2);
    eventDetails=new QPlainTextEdit;eventDetails->setObjectName("diagnosisSingleEventDetails");eventDetails->setReadOnly(true);eventDetails->setMaximumHeight(140);
    eventDetails->setPlaceholderText("点击事件行查看原始时间、对时定义与来源；双击将其设为事件中心。");root->addWidget(eventDetails);
    connect(table,&QTableWidget::cellClicked,this,[this](int row,int){
      if(row<0||row>=visible.size())return;const auto& item=visible[row];
      eventDetails->setPlainText(QString("类别：%1\n代码/动作：%2\n原始时间文本：%3\n原始时间（UTC）：%4\n对齐时间（UTC）：%5；偏移 %6 ms\n状态原文：%7；说明：%8\n来源：%9；非空记录号：%10\n当前只核对事件证据，不将首触代码当作根因；状态含义尚未配置。")
        .arg(item.kind).arg(item.code).arg(item.originalTimeText).arg(iso(item.original)).arg(iso(item.aligned))
        .arg(item.kind=="SC"?offset->value():0).arg(item.state).arg(item.description).arg(item.source).arg(item.row));
    });
    measurementList=new QListWidget;measurementList->setObjectName("diagnosisEvidenceSignals");measurementList->setMaximumHeight(150);measurementList->setSelectionMode(QAbstractItemView::ExtendedSelection);root->addWidget(measurementList);
    quality=new QLabel;quality->setObjectName("diagnosisEventQuality");quality->setWordWrap(true);root->addWidget(quality);
    status=new QLabel("请先用主界面加载录波；SC 和动作清单均可选。当前为本地事件核对与证据查看基础界面，未启用自动诊断。");status->setWordWrap(true);root->addWidget(status);
    auto* bottom=new QHBoxLayout;root->addLayout(bottom);
    auto* show=new QPushButton("在录波视图查看所选证据");bottom->addWidget(show);
    connect(show,&QPushButton::clicked,this,[this]{QStringList fields;for(auto* item:measurementList->selectedItems())fields<<item->text();
      if(fields.empty()||fields.size()>16){status->setText("请选择 1～16 个证据测点");return;}
      const double t=center->dateTime().toMSecsSinceEpoch()/1000.;setProperty("diagnosisCenterUtc",t);evidence(fields,t-before->value(),t+after->value());});
    auto* trial=new QPushButton("单条规则试跑");trial->setObjectName("diagnosisTrialEntry");bottom->addWidget(trial);
    connect(trial,&QPushButton::clicked,this,[this]{const double t=center->dateTime().toMSecsSinceEpoch()/1000.;
      showTrial(this,available(),vendor->text(),model->text(),t-before->value(),t+after->value(),maxGap->value(),trialReader,QString("中心 UTC=%1；SC 偏移=%2 ms；当前窗口事件数=%3；状态触发/恢复含义未配置").arg(iso(qRound64(t*1000))).arg(offset->value()).arg(visible.size()));});
    auto* checkQuality=new QPushButton("诊断前数据检查");bottom->addWidget(checkQuality);
    connect(checkQuality,&QPushButton::clicked,this,[this]{
      QStringList fields;for(auto* item:measurementList->selectedItems())fields<<item->text();
      if(fields.empty()||fields.size()>16){status->setText("请选择 1～16 个证据测点");return;}
      if(!inspectDataQuality){status->setText("当前数据源未提供完整原始样本检查接口");return;}
      const double t=center->dateTime().toMSecsSinceEpoch()/1000.;
      const auto report=inspectDataQuality(fields,t-before->value(),t+after->value(),maxGap->value());
      QDialog dialog(this);dialog.setWindowTitle("诊断前数据质量（原始样本）");dialog.resize(850,450);
      auto* layout=new QVBoxLayout(&dialog);auto* text=new QPlainTextEdit(report.join('\n'));text->setReadOnly(true);layout->addWidget(text);
      auto* close=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(close);connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
    });
    auto* exportButton=new QPushButton("导出当前事件窗口 CSV");bottom->addWidget(exportButton);
    connect(exportButton,&QPushButton::clicked,this,[this]{
      const auto path=QFileDialog::getSaveFileName(this,"导出事件窗口",{},"CSV (*.csv)");if(path.isEmpty())return;
      QByteArray bytes("\xEF\xBB\xBF");bytes+="original_utc,aligned_utc,kind,code,state,source,record,offset_ms,vendor,model,description,original_time_text\n";
      for(const auto& item:visible){QStringList row{iso(item.original),iso(item.aligned),item.kind,item.code,item.state,item.source,QString::number(item.row),QString::number(item.kind=="SC"?offset->value():0),vendor->text(),model->text(),item.description,item.originalTimeText};
        for(auto& value:row)value=csvCell(value);bytes+=row.join(',').toUtf8()+'\n';}
      QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())QMessageBox::warning(this,"导出失败",file.errorString());
    });
    auto update=[this]{refresh();};connect(center,&QDateTimeEdit::dateTimeChanged,this,update);
    for(auto* spin:{before,after,offset})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,update);
    connect(table,&QTableWidget::cellDoubleClicked,this,[this](int row,int){if(row>=0&&row<visible.size()){const auto t=visible[row].aligned;center->setDateTime(QDateTime::fromMSecsSinceEpoch(t,Qt::UTC));}});
    refresh();
  }
  void enter(){QStringList selected;for(auto* item:measurementList->selectedItems())selected<<item->text();
    measurementList->clear();measurementList->addItems(available());for(int i=0;i<measurementList->count();++i)
      measurementList->item(i)->setSelected(selected.contains(measurementList->item(i)->text()));refresh();}
  ~Panel()override{if(cancellation)cancellation->store(true);}
};
}

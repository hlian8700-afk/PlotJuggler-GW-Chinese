#pragma once
#include <QtWidgets>
#include <QtSql>
#include <atomic>
#include <functional>
#include <memory>
#include "bladescope_diagnosis_events.h"

namespace BladeDiagnosisUI {
class Panel final:public QWidget {
  QVector<EventRow> events;
  QTableWidget* table;
  QLabel *status,*quality;
  QDateTimeEdit* center;
  QSpinBox *offset,*before,*after;
  QComboBox *encoding,*separator;
  QLineEdit *vendor,*model;
  QListWidget* measurementList;
  std::function<QStringList()> available;
  std::function<void(QStringList,double,double)> evidence;
  std::shared_ptr<std::atomic_bool> cancellation;
  int generation=0;
  QVector<EventRow> visible;
  void refresh(){
    visible.clear();table->setRowCount(0);
    const qint64 t=center->dateTime().toMSecsSinceEpoch();
    for(auto item:events){
      item.aligned=BladeDiagnosis::alignedScTime(item.original,item.kind=="SC"?qint64(offset->value()):0);
      if(item.aligned<t-qint64(before->value())*1000||item.aligned>t+qint64(after->value())*1000)continue;
      visible<<item;
    }
    std::stable_sort(visible.begin(),visible.end(),[](const auto& a,const auto& b){return a.aligned<b.aligned;});
    for(const auto& item:visible){
      const int row=table->rowCount();table->insertRow(row);
      QStringList cells{iso(item.original),iso(item.aligned),item.kind,item.code,item.state,item.source,QString::number(item.row)};
      for(int col=0;col<cells.size();++col){auto* cell=new QTableWidgetItem(cells[col]);cell->setToolTip(cells[col]);table->setItem(row,col,cell);}
    }
    quality->setText(QStringLiteral("当前窗口 %1 条 / 已加载 %2 条；SC 对齐 = 原始时间 + %3 ms，TR 不移动。状态值保留原文，不猜测触发/恢复含义。")
      .arg(visible.size()).arg(events.size()).arg(offset->value()));
  }
  void load(QString kind){
    const auto path=QFileDialog::getOpenFileName(this,kind+" CSV",{},"CSV / TXT (*.csv *.txt);;All files (*)");
    if(path.isEmpty())return;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"读取失败",file.errorString());return;}
    if(file.size()>64*1024*1024){QMessageBox::warning(this,"文件过大","当前事件导入上限为 64 MiB；大文件流式读取待真实样本验证后接入。");return;}
    Csv csv;
    try{csv=parseCsv(decodeCsv(file.readAll(),encoding->currentText()),separator->currentIndex()?QChar('\t'):QChar(','));}
    catch(const std::exception& e){QMessageBox::warning(this,"解析失败",QString::fromUtf8(e.what()));return;}
    QDialog d(this);d.setWindowTitle("确认事件列和时间定义");auto* f=new QFormLayout(&d);
    QComboBox times,codes,states;times.addItems(csv.headers);codes.addItems(csv.headers);
    states.addItem("未提供");states.addItems(csv.headers);
    if(codes.count()>1)codes.setCurrentIndex(1);
    QComboBox unit,zone;unit.addItems({"数字时间：Unix 秒","数字时间：Unix 毫秒"});zone.addItems({"无时区文本按 UTC","无时区文本按 UTC+08:00"});
    QLineEdit format("yyyy-MM-dd HH:mm:ss.zzz");
    f->addRow("时间列",&times);f->addRow("代码/动作列",&codes);f->addRow("状态列（可选）",&states);
    f->addRow("数字时间单位",&unit);f->addRow("文本时间时区",&zone);f->addRow("自定义文本格式",&format);
    QLabel notice("带 Z/UTC 偏移的时间保留其时区；数字时间为 Unix 时间，不再叠加文本时区。");notice.setWordWrap(true);f->addRow(&notice);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    f->addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    if(d.exec()!=QDialog::Accepted)return;
    ImportOptions o;o.timeColumn=times.currentIndex();o.codeColumn=codes.currentIndex();o.stateColumn=states.currentIndex()-1;
    o.numericMilliseconds=unit.currentIndex()==1;o.utcOffsetSeconds=zone.currentIndex()?28800:0;o.dateFormat=format.text();
    if(cancellation)cancellation->store(true);cancellation=std::make_shared<std::atomic_bool>(false);const auto cancel=cancellation;const int ticket=++generation;
    auto result=std::make_shared<Imported>();auto error=std::make_shared<QString>();
    auto* thread=QThread::create([result,error,csv,o,path,kind,cancel]{try{*result=importEvents(csv,o,path,kind,cancel.get());}catch(const std::exception& e){*error=QString::fromUtf8(e.what());}});
    status->setText("正在导入事件…");
    connect(thread,&QThread::finished,this,[this,ticket,result,error,kind]{
      if(ticket!=generation)return;
      if(!error->isEmpty()){status->setText(*error);return;}
      if(result->events.empty()){status->setText("没有有效事件；请检查列、时区与时间格式。原有数据未清除。");return;}
      events.erase(std::remove_if(events.begin(),events.end(),[&](const auto& e){return e.kind==kind;}),events.end());
      events+=result->events;
      const qint64 t=result->events.front().original+(kind=="SC"?qint64(offset->value()):0);
      center->setDateTime(QDateTime::fromMSecsSinceEpoch(t,Qt::UTC));refresh();
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
  Panel(std::function<QStringList()> names,std::function<void(QStringList,double,double)> draw,
        std::function<void()> back,QWidget* parent=nullptr):QWidget(parent),available(std::move(names)),evidence(std::move(draw)){
    setObjectName("bladeDiagnosisPanel");auto* root=new QVBoxLayout(this);auto* top=new QHBoxLayout;root->addLayout(top);
    auto button=[&](QString text,auto callback){auto* b=new QPushButton(text);top->addWidget(b);connect(b,&QPushButton::clicked,this,callback);return b;};
    button("返回时序",back);button("导入 SC",[this]{load("SC");});button("关联 ActionList",[this]{load("ActionList");});
    button("检查规则包",[this]{inspectPackage();});
    encoding=new QComboBox;encoding->addItems({"UTF-8","GB18030","UTF-16"});top->addWidget(encoding);
    separator=new QComboBox;separator->addItems({"逗号分隔","Tab 分隔"});top->addWidget(separator);top->addStretch();
    auto* optionsWidget=new QWidget;auto* options=new QFormLayout(optionsWidget);root->addWidget(optionsWidget);optionsWidget->hide();button("事件参数",[optionsWidget]{optionsWidget->setVisible(!optionsWidget->isVisible());});vendor=new QLineEdit;model=new QLineEdit;
    vendor->setPlaceholderText("厂商，待确认");model->setPlaceholderText("机型，待确认");options->addRow("厂商 / 机型",vendor);options->addRow("机型",model);
    center=new QDateTimeEdit(QDateTime::currentDateTimeUtc());center->setTimeSpec(Qt::UTC);center->setDisplayFormat("yyyy-MM-dd HH:mm:ss.zzz");
    offset=new QSpinBox;offset->setRange(-86400000,86400000);offset->setSuffix(" ms");
    before=new QSpinBox;after=new QSpinBox;for(auto* spin:{before,after}){spin->setRange(0,3600);spin->setValue(60);spin->setSuffix(" s");}
    options->addRow("事件中心（UTC）",center);options->addRow("SC 手动对时偏移",offset);
    options->addRow("前窗口",before);options->addRow("后窗口",after);
    table=new QTableWidget(0,7);table->setObjectName("diagnosisEventTable");table->setHorizontalHeaderLabels({"原始时间","对齐时间（UTC）","来源类别","代码/动作","状态原文","源文件","数据记录号"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);root->addWidget(table,2);
    measurementList=new QListWidget;measurementList->setObjectName("diagnosisEvidenceSignals");measurementList->setMaximumHeight(150);measurementList->setSelectionMode(QAbstractItemView::ExtendedSelection);root->addWidget(measurementList);
    quality=new QLabel;quality->setWordWrap(true);root->addWidget(quality);
    status=new QLabel("请先用主界面加载录波；SC 和动作清单均可选。当前为本地事件核对与证据查看基础界面，未启用自动诊断。");status->setWordWrap(true);root->addWidget(status);
    auto* bottom=new QHBoxLayout;root->addLayout(bottom);
    auto* show=new QPushButton("在录波视图查看所选证据");bottom->addWidget(show);
    connect(show,&QPushButton::clicked,this,[this]{QStringList fields;for(auto* item:measurementList->selectedItems())fields<<item->text();
      if(fields.empty()||fields.size()>16){status->setText("请选择 1～16 个证据测点");return;}
      const double t=center->dateTime().toMSecsSinceEpoch()/1000.;evidence(fields,t-before->value(),t+after->value());});
    auto* exportButton=new QPushButton("导出当前事件窗口 CSV");bottom->addWidget(exportButton);
    connect(exportButton,&QPushButton::clicked,this,[this]{
      const auto path=QFileDialog::getSaveFileName(this,"导出事件窗口",{},"CSV (*.csv)");if(path.isEmpty())return;
      QByteArray bytes("\xEF\xBB\xBF");bytes+="original_utc,aligned_utc,kind,code,state,source,record,offset_ms,vendor,model\n";
      for(const auto& item:visible){QStringList row{iso(item.original),iso(item.aligned),item.kind,item.code,item.state,item.source,QString::number(item.row),QString::number(item.kind=="SC"?offset->value():0),vendor->text(),model->text()};
        for(auto& value:row)value=csvCell(value);bytes+=row.join(',').toUtf8()+'\n';}
      QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())QMessageBox::warning(this,"导出失败",file.errorString());
    });
    auto update=[this]{refresh();};connect(center,&QDateTimeEdit::dateTimeChanged,this,update);
    for(auto* spin:{before,after,offset})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,update);
    connect(table,&QTableWidget::cellDoubleClicked,this,[this](int row,int){if(row>=0&&row<visible.size()){const auto t=visible[row].aligned;center->setDateTime(QDateTime::fromMSecsSinceEpoch(t,Qt::UTC));}});
    refresh();
  }
  void enter(){measurementList->clear();measurementList->addItems(available());refresh();}
  ~Panel()override{if(cancellation)cancellation->store(true);}
};
}

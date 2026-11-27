#pragma once
#include <QtWidgets>
#include <functional>
#include <memory>
#include "bladescope_diagnosis_trial.h"
#include "bladescope_diagnosis_events.h"
namespace BladeDiagnosisUI {
using TrialReader=std::function<std::vector<BladeDiagnosis::RawSample>(QString,double,double)>;
inline QString trialStateName(BladeDiagnosis::TrialState state){
  using S=BladeDiagnosis::TrialState;
  switch(state){case S::Supported:return "试验条件成立";case S::NotSupported:return "试验条件不成立";
    case S::Unknown:return "数据不足 / 质量异常";case S::NotApplicable:return "机型不适用";case S::Invalid:return "参数未确认或无效";}
  return "未知";
}
inline QString trialReasonName(const std::string& reason){
  const QMap<QString,QString> messages{
    {"Vendor/model scope is missing or does not match","缺少机型范围或厂商/机型不匹配"},
    {"Confirm units, positive threshold/gains, window and timing parameters","请确认单位、正数阈值与倍率、窗口和时间参数"},
    {"Pitch actual and command must be mapped to the same confirmed blade axis","实际角度与指令必须映射到同一已确认的叶片轴"},
    {"Primary data missing, truncated, invalid, duplicated, reversed or gapped","主要测点存在缺测、窗口截断、无效值、重复/倒序时间或过大间隔"},
    {"Reference data quality is insufficient","指令测点数据质量不足"},
    {"Reference boundary timestamps must be strictly increasing","指令测点边界时间必须严格递增"},
    {"Unmatched reference timestamps or insufficient samples","指令时间未匹配或样本不足"},
    {"Timestamps cannot be represented as unique milliseconds","时间戳不能用唯一毫秒值表示"},
    {"Converted value overflow","单位换算溢出"},{"RMS calculation overflow","RMS 计算溢出"},
    {"Timestamp outside supported range","时间戳超出支持范围"},
    {"Experimental condition only; not a validated turbine root cause","仅说明试验条件，不代表已经验证的机组故障根因"}};
  const auto key=QString::fromStdString(reason);return messages.value(key,key);
}
inline void showTrial(QWidget* parent,QStringList names,QString vendor,QString model,double lo,double hi,int gap,
                      TrialReader reader,QString eventContext={}){
  if(!reader){QMessageBox::information(parent,"规则试跑","当前数据源没有提供完整原始样本快照接口");return;}
  QDialog d(parent);d.setObjectName("diagnosisTrialDialog");d.setWindowTitle("单条规则试跑（待验证）");const auto screen=QGuiApplication::primaryScreen()->availableGeometry();d.resize(qMin(900,screen.width()-40),qMin(760,screen.height()-60));d.setMaximumSize(screen.size()-QSize(20,40));
  auto* root=new QVBoxLayout(&d);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto* controls=new QWidget;auto* form=new QFormLayout(controls);scroll->setWidget(controls);root->addWidget(scroll,2);
  QLabel notice("使用原始快照，不应用绘图条件筛选；仅验证用户指定条件，不输出故障根因或置信概率。阈值必须按机型与测点定义填写；运行状态与 r/f 动作含义不自动推断。");notice.setWordWrap(true);root->addWidget(&notice);
  QComboBox family,primary,reference,unit;family.setObjectName("trialFamily");primary.setObjectName("trialPrimary");reference.setObjectName("trialReference");
  family.addItems({"超速：持续高于阈值","变桨：实际与指令的绝对偏差","振动：交流分量样本 RMS","转速波动：去线性趋势样本 RMS"});
  for(auto* box:{&primary,&reference}){box->addItem("请选择测点",QString());for(const auto& name:names)box->addItem(name,name);box->setMaxVisibleItems(10);box->setMinimumContentsLength(12);box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);box->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);}
  QLineEdit vendorEdit(vendor),modelEdit(model),threshold,gain("1"),refGain("1");threshold.setObjectName("trialThreshold");threshold.setPlaceholderText("必填：请输入经核对的正数阈值");
  QDoubleSpinBox duration;duration.setRange(.001,3600);duration.setDecimals(3);duration.setValue(1);duration.setSuffix(" s");
  QSpinBox tolerance,cycles,axis;axis.setRange(1,3);tolerance.setRange(0,60000);tolerance.setSuffix(" ms");cycles.setRange(1,1000);cycles.setValue(2);
  QCheckBox confirmed("已确认单位、倍率和机型阈值；变桨两测点来自同一轴"),merge("仅在分析副本合并相同时间且同值的相邻点");confirmed.setObjectName("trialUnitsConfirmed");
  form->addRow("厂商（必填）",&vendorEdit);form->addRow("机型（必填）",&modelEdit);form->addRow("试验条件",&family);
  form->addRow("主要测点 / 实际角度",&primary);form->addRow("指令角度（变桨用）",&reference);form->addRow("实际与指令的共同叶片轴号",&axis);form->addRow("目标单位",&unit);
  form->addRow("主要测点 → 目标单位倍率",&gain);form->addRow("指令测点 → 目标单位倍率",&refGain);
  form->addRow("阈值（目标单位）",&threshold);form->addRow("持续时间（超速 / 跟随偏差）",&duration);
  form->addRow("最大时间匹配差（变桨用）",&tolerance);form->addRow("最低完整周期数（转速波动）",&cycles);form->addRow(&confirmed);form->addRow(&merge);
  auto* summary=new QLabel(QString("沿用当前事件窗口 UTC：%1 ～ %2；最大采样间隔 %3 ms").arg(iso(qRound64(lo*1000))).arg(iso(qRound64(hi*1000))).arg(gap));summary->setWordWrap(true);root->addWidget(summary);
  QPlainTextEdit output;output.setObjectName("trialOutput");output.setReadOnly(true);root->addWidget(&output,1);
  QDialogButtonBox buttons(QDialogButtonBox::Close);auto* run=buttons.addButton("运行试验",QDialogButtonBox::ActionRole);run->setObjectName("trialRun");auto* exportButton=buttons.addButton("导出本次结果",QDialogButtonBox::ActionRole);exportButton->setObjectName("trialExport");exportButton->setEnabled(false);root->addWidget(&buttons);
  QSettings settings;const auto key=QStringLiteral("BladeScope/diagnosisTrial/")+vendor+"/"+model;
  auto saved=QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
  family.setCurrentIndex(qBound(0,saved.value("family").toInt(),3));
  auto update=[&]{const int f=family.currentIndex();unit.clear();if(f==1)unit.addItem("deg");else if(f==2)unit.addItems({"m/s²","mm/s"});else unit.addItem("rpm");reference.setEnabled(f==1);axis.setEnabled(f==1);refGain.setEnabled(f==1);tolerance.setEnabled(f==1);duration.setEnabled(f<2);cycles.setEnabled(f==3);confirmed.setChecked(false);threshold.clear();gain.setText("1");refGain.setText("1");};update();
  QObject::connect(&family,qOverload<int>(&QComboBox::currentIndexChanged),&d,update);QObject::connect(&unit,qOverload<int>(&QComboBox::currentIndexChanged),&d,[&]{confirmed.setChecked(false);});
  for(auto* box:{&primary,&reference})QObject::connect(box,qOverload<int>(&QComboBox::currentIndexChanged),&d,[&]{confirmed.setChecked(false);});
  primary.setCurrentIndex(qMax(0,primary.findData(saved.value("primary").toString())));reference.setCurrentIndex(qMax(0,reference.findData(saved.value("reference").toString())));
  threshold.setText(saved.value("threshold").toString());gain.setText(saved.value("gain").toString("1"));refGain.setText(saved.value("referenceGain").toString("1"));
  for(auto* edit:{&threshold,&gain,&refGain,&vendorEdit,&modelEdit})QObject::connect(edit,&QLineEdit::textChanged,&d,[&]{confirmed.setChecked(false);});
  duration.setValue(saved.value("duration").toDouble(1));tolerance.setValue(saved.value("tolerance").toInt());cycles.setValue(saved.value("cycles").toInt(2));merge.setChecked(saved.value("merge").toBool());unit.setCurrentIndex(qMax(0,unit.findText(saved.value("unit").toString())));axis.setValue(saved.value("axis").toInt(1));
  QString report;bool completed=false;
  auto invalidate=[&]{completed=false;exportButton->setEnabled(false);};
  for(auto* box:{&family,&primary,&reference,&unit})QObject::connect(box,qOverload<int>(&QComboBox::currentIndexChanged),&d,invalidate);
  for(auto* edit:{&threshold,&gain,&refGain,&vendorEdit,&modelEdit})QObject::connect(edit,&QLineEdit::textChanged,&d,invalidate);
  QObject::connect(&duration,qOverload<double>(&QDoubleSpinBox::valueChanged),&d,invalidate);for(auto* box:{&tolerance,&cycles,&axis})QObject::connect(box,qOverload<int>(&QSpinBox::valueChanged),&d,invalidate);
  QObject::connect(&confirmed,&QCheckBox::toggled,&d,invalidate);QObject::connect(&merge,&QCheckBox::toggled,&d,invalidate);
  QObject::connect(run,&QPushButton::clicked,&d,[&]{
    invalidate();using namespace BladeDiagnosis;TrialSpec spec;spec.family=static_cast<TrialFamily>(family.currentIndex());
    spec.scope={vendorEdit.text().trimmed().toStdString(),modelEdit.text().trimmed().toStdString(),{}};
    bool thresholdOk=false,gainOk=false,refOk=false;spec.threshold=threshold.text().toDouble(&thresholdOk);spec.gain=gain.text().toDouble(&gainOk);spec.referenceGain=refGain.text().toDouble(&refOk);
    spec.unitsConfirmed=confirmed.isChecked();spec.mergeIdentical=merge.isChecked();spec.durationMs=qRound64(duration.value()*1000);spec.maximumGapMs=gap;spec.toleranceMs=tolerance.value();spec.minimumCycles=cycles.value();spec.primaryAxis=axis.value();spec.referenceAxis=axis.value();
    if(!thresholdOk||!gainOk||!refOk||!std::isfinite(spec.threshold)||spec.threshold<=0||!std::isfinite(spec.gain)||spec.gain<=0||!std::isfinite(spec.referenceGain)||spec.referenceGain<=0||!spec.unitsConfirmed||spec.scope.vendor.empty()||spec.scope.model.empty()||primary.currentData().toString().isEmpty()||(spec.family==TrialFamily::PitchFollowing&&(reference.currentData().toString().isEmpty()||reference.currentData()==primary.currentData()))){output.setPlainText("请先填写厂商、机型、测点和正数阈值/倍率，再确认单位。变桨必须选择不同的实际角度和同轴指令测点。");return;}
    const auto primaryName=primary.currentData().toString(),referenceName=reference.currentData().toString();
    std::vector<RawSample> snapshot,refSnapshot;
    try{snapshot=reader(primaryName,lo,hi);if(spec.family==TrialFamily::PitchFollowing)refSnapshot=reader(referenceName,lo,hi);}catch(const std::exception& e){output.setPlainText(QString::fromUtf8(e.what()));return;}
    auto result=std::make_shared<TrialResult>();auto error=std::make_shared<QString>();auto cancelled=std::make_shared<std::atomic_bool>(false);
    QProgressDialog progress("正在计算原始快照…","取消",0,0,&d);progress.setWindowModality(Qt::WindowModal);QEventLoop loop;
    auto* worker=QThread::create([=]{try{*result=runTrial(spec,spec.scope,{qRound64(lo*1000),qRound64(hi*1000)},snapshot,refSnapshot,cancelled.get());}catch(const std::exception& e){*error=QString::fromUtf8(e.what());}});
    QObject::connect(&progress,&QProgressDialog::canceled,&loop,[=]{cancelled->store(true);});QObject::connect(worker,&QThread::finished,&loop,&QEventLoop::quit);progress.show();worker->start();loop.exec();worker->wait();delete worker;QObject::disconnect(&progress,nullptr,&loop,nullptr);progress.reset();progress.close();
    if(cancelled->load()){output.setPlainText("已取消，未输出试验结论");return;}if(!error->isEmpty()){output.setPlainText(*error);return;}
    const auto& r=*result;
    report=QString("BladeScope 3.17.0.22 / trial-v1\n规则生命周期：待验证；仅为试验条件，不是根因结论\n厂商：%1；机型：%2\n条件：%3\n原始角色映射：主要=%4；指令=%5\n目标单位=%6；主要倍率=%7；指令倍率=%8\n时间 UTC：%9 ～ %10\n最大采样间隔=%11 ms；持续要求=%12 ms；匹配差=%13 ms；最少周期=%14\n阈值=%15；合并同值重复=%16\n结果：%17\n特征值=%18；最长连续超限=%19 s；完整周期=%20\n有效样本=%21；未匹配=%22；主要合并=%23；指令合并=%24\n主要数据：无效=%25；重复=%26；倒序=%27；最大间隔=%28 ms；窗口覆盖=%29\n说明：%30\n定义：交流 RMS 去均值；波动 RMS 去线性趋势，周期按超过阈值 5% 的双向过零计数估计。未做频带或故障类型确认。")
      .arg(vendorEdit.text()).arg(modelEdit.text()).arg(family.currentText()).arg(primaryName).arg(referenceName).arg(unit.currentText()).arg(spec.gain,0,'g',12).arg(spec.referenceGain,0,'g',12)
      .arg(iso(qRound64(lo*1000))).arg(iso(qRound64(hi*1000))).arg(gap).arg(spec.durationMs).arg(spec.toleranceMs).arg(spec.minimumCycles).arg(spec.threshold,0,'g',12).arg(spec.mergeIdentical?"是":"否")
      .arg(trialStateName(r.state)).arg(r.metric,0,'g',12).arg(r.longestSeconds,0,'g',12).arg(r.cycles).arg(qulonglong(r.samples)).arg(qulonglong(r.unmatched)).arg(qulonglong(r.merged)).arg(qulonglong(r.referenceMerged))
      .arg(qulonglong(r.quality.invalid)).arg(qulonglong(r.quality.duplicates)).arg(qulonglong(r.quality.backwards)).arg(r.quality.maxGapSeconds*1000,0,'g',12).arg(r.quality.boundsCovered?"是":"否").arg(trialReasonName(r.reason));
    report+="\n事件背景："+eventContext;
    report+=QString("\n用户确认的叶片轴号=%1（仅变桨条件使用）").arg(axis.value());output.setPlainText(report);completed=true;exportButton->setEnabled(true);
    settings.setValue(QStringLiteral("BladeScope/diagnosisTrial/")+vendorEdit.text().trimmed()+"/"+modelEdit.text().trimmed(),QJsonDocument(QJsonObject{{"family",family.currentIndex()},{"primary",primaryName},{"reference",referenceName},{"threshold",threshold.text()},{"gain",gain.text()},{"referenceGain",refGain.text()},{"duration",duration.value()},{"tolerance",tolerance.value()},{"cycles",cycles.value()},{"merge",merge.isChecked()},{"unit",unit.currentText()},{"axis",axis.value()}}).toJson());
  });
  QObject::connect(exportButton,&QPushButton::clicked,&d,[&]{if(!completed)return;const auto path=QFileDialog::getSaveFileName(&d,"导出试跑证据",{},"CSV (*.csv)");if(path.isEmpty())return;
    QByteArray bytes("\xEF\xBB\xBF");bytes+="line,evidence\n";int index=0;for(const auto& line:report.split('\n'))bytes+=QString::number(++index).toUtf8()+','+csvCell(line).toUtf8()+'\n';QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())QMessageBox::warning(&d,"导出失败",file.errorString());});
  QObject::connect(&buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);d.exec();
}
}

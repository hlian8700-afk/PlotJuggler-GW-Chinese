#pragma once
#include "bladescope_yaw_analysis.h"
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QSplitter>

namespace BladeYawUI
{
using BladeYaw::Config;
inline Config config(const QJsonObject& o)
{
  Config c;
  auto n=[&](const char* key,double initial){return o.value(key).toDouble(initial);};
  c.windLow=n("windLow",6);c.windHigh=n("windHigh",9);c.windStep=n("windStep",1);
  c.angleLow=n("angleLow",-20);c.angleHigh=n("angleHigh",20);c.angleStep=n("angleStep",1);
  c.baselineLow=n("baselineLow",-2);c.baselineHigh=n("baselineHigh",2);c.minBaselinePower=n("minBaselinePower",100);
  c.positiveLow=n("positiveLow",4);c.positiveHigh=n("positiveHigh",8);c.negativeLow=n("negativeLow",-8);c.negativeHigh=n("negativeHigh",-4);c.pairStep=n("pairStep",1);
  c.physicalWindHigh=n("physicalWindHigh",60);c.physicalPowerLow=n("physicalPowerLow",0);c.physicalPowerHigh=n("physicalPowerHigh",20000);
  c.deadband=n("deadband",1);c.minCoverage=n("minCoverage",.5);c.mixedFraction=n("mixedFraction",.3);
  c.minSamples=int(n("minSamples",20));c.baselineMode=int(n("baselineMode",0));
  c.mirror=o.value("mirror").toBool(true);c.poolDates=o.value("poolDates").toBool();c.groupDates=o.value("groupDates").toBool(true);c.rejectYaw=o.value("rejectYaw").toBool();
  return c;
}
class Settings : public QWidget
{
  QMap<QString,QDoubleSpinBox*> numbers;
  QMap<QString,QCheckBox*> checks;
  QComboBox* baseline;
  QLabel* preview;
public:
  explicit Settings(const QJsonObject& values,QWidget* parent=nullptr):QWidget(parent)
  {
    auto* layout=new QVBoxLayout(this);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);layout->addWidget(scroll);
    auto* body=new QWidget;auto* form=new QFormLayout(body);scroll->setWidget(body);
    auto number=[&](QString key,QString label,double initial,double low,double high,int decimals=2){
      auto* control=new QDoubleSpinBox;control->setRange(low,high);control->setDecimals(decimals);control->setValue(values.value(key).toDouble(initial));
      control->setObjectName("m56_"+key);numbers[key]=control;form->addRow(label,control);
    };
    auto check=[&](QString key,QString label,bool initial){auto* control=new QCheckBox(label);
      control->setChecked(values.value(key).toBool(initial));control->setObjectName("m56_"+key);checks[key]=control;form->addRow(control);};
    number("windLow","风速下限 (m/s)",6,.01,60);number("windHigh","风速上限 (m/s)",9,.01,60);number("windStep","风速箱宽 (m/s)",1,.01,20);
    number("angleLow","角度下限 (°)",-20,-180,179.99);number("angleHigh","角度上限 (°)",20,-180,179.99);number("angleStep","角度箱宽 (°)",1,.01,180);
    number("minSamples","每箱最低样本数",20,1,1000000,0);
    check("groupDates","按录波显示日期配对（关闭则按整个时间窗口）",true);
    check("rejectYaw","排除偏航活动（须在测点关联中配置，0 为静止）",false);
    baseline=new QComboBox;baseline->addItems({"零附近实测基线","导入标准功率曲线"});baseline->setCurrentIndex(values.value("baselineMode").toInt());form->addRow("基线来源",baseline);
    number("baselineLow","基线角度下限 (°)",-2,-180,179.99);number("baselineHigh","基线角度上限 (°)",2,-180,179.99);
    number("minBaselinePower","百分比基线功率须高于 (kW)",100,0,100000);
    check("poolDates","允许跨日期合并实测基线",false);
    check("mirror","镜像配对（自动计算负侧区间）",true);
    number("positiveLow","正侧近零端 (°)",4,0,179.99);number("positiveHigh","正侧远零端 (°)",8,0,179.99);
    number("negativeLow","负侧远零端 (°)",-8,-180,0);number("negativeHigh","负侧近零端 (°)",-4,-180,0);
    number("pairStep","配对细分宽度 (°)",1,.01,180);
    preview=new QLabel;preview->setWordWrap(true);preview->setObjectName("m56PairPreview");form->addRow(preview);
    number("physicalWindHigh","物理风速上限 (m/s)",60,.01,200);
    number("physicalPowerLow","物理功率下限 (kW)",0,-100000,100000);
    number("physicalPowerHigh","物理功率上限 (kW)",20000,-100000,100000);
    number("deadband","功率差死区 (kW)",1,0,100000);
    number("minCoverage","最低配对覆盖率 (0–1)",.5,0,1);
    number("mixedFraction","正负方向分歧比例 (0–0.5)",.3,.01,.5);
    auto* note=new QLabel("共用外部时间范围、运行状态和故障筛选。配对由近零端向远零端排列；零角度只参与残差。\n日期按当前录波显示时区划分。连续样本点数不代表独立证据数量。");note->setWordWrap(true);form->addRow(note);
    for(auto* control:numbers)connect(control,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{updatePreview();});
    connect(checks["mirror"],&QCheckBox::toggled,this,[this]{updatePreview();});updatePreview();
  }
  QJsonObject values() const
  {
    QJsonObject o;for(auto i=numbers.cbegin();i!=numbers.cend();++i)o[i.key()]=i.value()->value();
    for(auto i=checks.cbegin();i!=checks.cend();++i)o[i.key()]=i.value()->isChecked();
    o["baselineMode"]=baseline->currentIndex();return o;
  }
  void updatePreview()
  {
    const auto c=config(values());const auto neg=BladeYaw::negativeRange(c);
    numbers["negativeLow"]->setEnabled(!c.mirror);numbers["negativeHigh"]->setEnabled(!c.mirror);
    if(c.mirror){QSignalBlocker a(numbers["negativeLow"]),b(numbers["negativeHigh"]);numbers["negativeLow"]->setValue(neg.first);numbers["negativeHigh"]->setValue(neg.second);}
    try{
      BladeYaw::binCount(c.positiveLow,c.positiveHigh,c.pairStep,true);
      BladeYaw::binCount(neg.first,neg.second,c.pairStep,true);
      if(c.positiveLow<0||neg.second>0||std::abs(c.positiveHigh-c.positiveLow-(neg.second-neg.first))>1e-6)
        throw std::runtime_error("正负区间须方向正确且等宽");
      preview->setText(QString("首组 +%1～+%2 ↔ %3～%4°；末组包含最远端点。%5")
        .arg(c.positiveLow).arg(c.positiveLow+c.pairStep).arg(neg.second-c.pairStep).arg(neg.second)
        .arg(c.mirror?"镜像配对":"非对称区间比较"));
    }catch(const std::exception& e){preview->setText(QString("请调整范围或步长：%1").arg(QString::fromUtf8(e.what())));}
  }
};
inline QString date(long long key){return key?QString::number(key):QStringLiteral("整个时间窗口");}
inline QString range(double lo,double hi){return QString("%1～%2").arg(lo).arg(hi);}
inline QString windRange(const Config& c,int i){return range(c.windLow+i*c.windStep,std::min(c.windHigh,c.windLow+(i+1)*c.windStep));}
inline QStringList pairRange(const Config& c,int i)
{
  auto negative=BladeYaw::negativeRange(c);
  return {range(c.positiveLow+i*c.pairStep,c.positiveLow+(i+1)*c.pairStep),
          range(negative.second-(i+1)*c.pairStep,negative.second-i*c.pairStep)};
}
}

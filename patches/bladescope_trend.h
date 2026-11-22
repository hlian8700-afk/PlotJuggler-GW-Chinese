#pragma once
#include "bladescope_fit_math.h"
#include "plotwidget.h"
#include <QtWidgets>
#include <memory>
#include <atomic>

namespace BladeTrend
{
template<class T, class Work, class Done>
void background(QObject* owner, Work work, Done done)
{
  struct Outcome { T value; QString error; };
  auto result=std::make_shared<Outcome>();
  auto* thread=QThread::create([work,result] {
    try {result->value=work();}
    catch(const std::exception& ex){result->error=QString::fromUtf8(ex.what());}
  });
  QObject::connect(thread,&QThread::finished,owner,[result,done]{done(std::move(result->value),result->error);});
  QObject::connect(thread,&QThread::finished,thread,&QObject::deleteLater);
  thread->start();
}

struct Trend { BladeFit::Points points; QString summary; };
inline Trend calculate(const BladeFit::Points& input,int mode,int degree,int width,double lo,double hi)
{
  using namespace BladeFit;
  Points rows;
  for(auto p:input) if(p.x>=lo && p.x<=hi && std::isfinite(p.y)) rows.push_back(p);
  if(rows.empty()) throw std::runtime_error("当前时段没有有效样本");
  if(mode==1)
  {
    auto smooth=average(rows,width);
    if(smooth.size()>200){
      Points display;display.reserve(200);
      for(size_t i=0;i<200;++i)display.push_back(smooth[i*(smooth.size()-1)/199]);
      smooth=std::move(display);
    }
    return {std::move(smooth),QString::fromUtf8("滑动平均 · %1 点窗口 · %2 个样本").arg(width).arg(qulonglong(rows.size()))};
  }
  if(mode==2)
  {
    const auto fit=fitExponential(rows);
    return {sample(rows.front().x,rows.back().x,fit),
      QStringLiteral("y = %1 + (%2) × exp(−%3 × (t−%4)) · 样本 %5")
        .arg(fit.offset,0,'g',12).arg(fit.amplitude,0,'g',12)
        .arg(fit.decayPerSecond,0,'g',12).arg(fit.origin,0,'g',16).arg(qulonglong(rows.size()))};
  }
  const auto poly=regress(rows,degree);
  QStringList coeff;
  long double error=0;
  for(auto p:rows){auto d=poly(p.x)-p.y;error+=d*d;}
  for(double v:poly.coefficients) coeff<<QString::number(v,'g',12);
  return {sample(rows.front().x,rows.back().x,poly),
    QString::fromUtf8("z=(t−%1)/%2，p₀…pₙ = [%3]，RMSE %4，样本 %5")
    .arg(poly.origin,0,'g',16).arg(poly.scale,0,'g',12).arg(coeff.join(", "))
    .arg(std::sqrt(double(error/rows.size())),0,'g',8).arg(qulonglong(rows.size()))};
}

inline void open(PlotWidget* plot,QwtPlot*,const std::string& source)
{
  auto& map=plot->datamap();
  const auto it=map.numeric.find(source);
  if(it==map.numeric.end() || !it->second.size()) return;
  auto rows=std::make_shared<BladeFit::Points>();
  rows->reserve(it->second.size());
  for(size_t i=0;i<it->second.size();++i){const auto& p=it->second.at(i);rows->push_back({p.x,p.y});}
  QDialog dialog(plot);
  dialog.setWindowTitle(QString::fromUtf8("添加趋势线 · ")+QString::fromStdString(source));
  auto* form=new QFormLayout(&dialog);
  auto* model=new QComboBox;model->addItems({QStringLiteral("多项式"),QStringLiteral("滑动平均"),QStringLiteral("指数衰减 / 温升饱和")});
  auto* degree=new QSpinBox;degree->setRange(1,3);degree->setValue(1);
  auto* window=new QSpinBox;window->setRange(1,10000000);window->setValue(11);
  auto* range=new QComboBox;range->addItems({QStringLiteral("全部数据"),QStringLiteral("当前可视时段")});
  auto* colorButton=new QPushButton(QStringLiteral("选择颜色与透明度"));
  QColor color(226,88,40);
  auto* penWidth=new QDoubleSpinBox;penWidth->setRange(.5,8);penWidth->setValue(2);
  auto* summary=new QLabel;summary->setWordWrap(true);summary->setMinimumWidth(320);
  form->addRow(QStringLiteral("模型"),model);form->addRow(QStringLiteral("阶数"),degree);
  form->addRow(QStringLiteral("窗口点数"),window);form->addRow(QStringLiteral("时段"),range);
  form->addRow(QStringLiteral("线宽"),penWidth);form->addRow(colorButton);form->addRow(summary);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
  form->addRow(buttons);
  auto* ok=buttons->button(QDialogButtonBox::Ok);
  ok->setText(QStringLiteral("添加到当前图"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  QObject::connect(colorButton,&QPushButton::clicked,&dialog,[&]{
    const auto chosen=QColorDialog::getColor(color,&dialog,QStringLiteral("趋势线"),QColorDialog::ShowAlphaChannel);
    if(chosen.isValid()) color=chosen;
  });
  Trend result;
  QTimer timer;timer.setSingleShot(true);timer.setInterval(140);
  quint64 revision=0;bool busy=false;
  auto changed=[&]{++revision;ok->setEnabled(false);degree->setEnabled(model->currentIndex()==0);
    window->setEnabled(model->currentIndex()==1);timer.start();};
  QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{
    if(busy) return;
    busy=true;
    auto rect=plot->currentBoundingRect();
    const double lo=range->currentIndex()?rect.left()+plot->timeOffset():rows->front().x;
    const double hi=range->currentIndex()?rect.right()+plot->timeOffset():rows->back().x;
    int m=model->currentIndex(), d=degree->value(),w=window->value();
    auto ticket=revision;
    background<Trend>(&dialog,[=]{return calculate(*rows,m,d,w,lo,hi);},[&,ticket](Trend data,QString err){
      busy=false;
      if(ticket!=revision){timer.start();return;}
      result=std::move(data);
      summary->setText(err.isEmpty()?result.summary:err);
      ok->setEnabled(err.isEmpty()&&!result.points.empty());
    });
  });
  for(auto* combo:{model,range}) QObject::connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,changed);
  for(auto* spin:{degree,window}) QObject::connect(spin,qOverload<int>(&QSpinBox::valueChanged),&dialog,changed);
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
  QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  changed();
  const bool accepted=dialog.exec()==QDialog::Accepted;
  if(accepted && !result.points.empty()){
    const std::string prefix=source+"/Trend";
    std::string name=prefix;
    for(int n=2;map.getAllNames().count(name);++n) name=prefix+std::to_string(n);
    auto& series=map.addNumeric(name)->second;
    for(auto p:result.points) series.pushBack({p.x,p.y});
    if(auto* c=plot->addCurve(name,color)){c->curve->setPen(QPen(color,penWidth->value()));emit plot->curveListChanged();}
    emit plot->undoableChange();
  }
  plot->replot();
}
}

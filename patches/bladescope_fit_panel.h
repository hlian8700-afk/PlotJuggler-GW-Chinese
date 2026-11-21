#pragma once
#include "bladescope_trend.h"
#include <QSaveFile>
#include <QTextStream>
#include <deque>

namespace BladeFitUI
{
using namespace BladeFit;
inline const QStringList& kinds()
{
  static const QStringList names={"M01 风速–功率","M02 风速分布","M03 Cp–λ","M05 风速–Cp","M06 风速–λ",
    "M11 风向玫瑰图","M12 风向–风速","M21 风速–转速","M22 风速–桨距角","M23 风速–对风角度",
    "M35 转速–Cp","M41 叶轮–发电机转速","M32 转速–功率","M34 转速–转矩百分比","M33 转速–换算转矩",
    "M51 室外温度–Cp","M52 对风角度–Cp","M53 桨距角–Cp","M55 月份–Cp"};
  return names;
}
inline const QStringList& roles()
{
  static const QStringList names={"风速 (m/s)","有功功率 (kW)","叶轮转速 (rpm)","发电机转速 (rpm)",
    "桨距角 (°)","绝对风向 (°)","对风角度 (°)","转矩 (%)","运行状态码","室外温度 (°C)"};
  return names;
}
inline QString num(double x){return std::isfinite(x)?QString::number(x,'g',12):QString();}
inline QStringList axes(int k)
{
  static const QStringList x={"风速 (m/s)","风速 (m/s)","叶尖速比 λ","风速 (m/s)","风速 (m/s)","东西","风向 (°)","风速 (m/s)","风速 (m/s)","风速 (m/s)","转速 (rpm)","叶轮转速 (rpm)","发电机转速 (rpm)","发电机转速 (rpm)","发电机转速 (rpm)","温度 (°C)","对风角度 (°)","桨距角 (°)","月份"};
  static const QStringList y={"功率 (kW)","频率 (%)","电功率口径 Cp","电功率口径 Cp","叶尖速比 λ","南北","风速 (m/s)","转速 (rpm)","桨距角 (°)","对风角度 (°)","电功率口径 Cp","发电机转速 (rpm)","功率 (kW)","转矩 (%)","估算转矩 (N·m)","电功率口径 Cp","电功率口径 Cp","电功率口径 Cp","电功率口径 Cp"};
  return {x.at(k),y.at(k)};
}
inline void saveCsv(QWidget* parent,const QString& title,const QVector<QStringList>& rows)
{
  const auto path=QFileDialog::getSaveFileName(parent,title,{},QStringLiteral("CSV (*.csv)"));
  if(path.isEmpty())return;
  QSaveFile file(path);
  if(file.open(QIODevice::WriteOnly)){
    file.write("\xef\xbb\xbf");
    QTextStream out(&file);out.setCodec("UTF-8");
    for(auto row:rows){
      for(auto& text:row){text.replace('"',QStringLiteral("\"\""));text='"'+text+'"';}
      out<<row.join(',')<<'\n';
    }
    out.flush();if(out.status()==QTextStream::Ok && file.commit())return;
  }
  QMessageBox::warning(parent,title,file.errorString());
}

struct Snapshot
{
  std::shared_ptr<const Channels> channels;
  Params params;
  Masks excluded,marked;
  QString name;
  std::map<int,std::shared_ptr<const Result>> cache;
};
inline QStringList defaults()
{
  return {"WTUR.WSpd.Ra.F32;grWindSpeed;风速实时值","grGridActivePower;有功功率;电网有功功率",
    "grRotorSpeed1;叶轮转速","WGEN.Spd.Ra.F32;grGeneratorSpeed;发电机转速","grPitchAngleA;桨距角",
    "绝对风向;环境风向","对风角度;偏航误差","发电机转矩百分比","机组运行模式","室外温度;环境温度"};
}

class Panel : public QWidget
{
  PJ::PlotDataMapRef& source;
  PJ::PlotDataMapRef shown;
  PlotWidget* plot=nullptr;
  QComboBox *kind,*select,*roseMetric;
  QCheckBox *density;
  QLabel* status;
  QWidget* canvas;
  QToolButton* poolMenu;
  QMenu* compareMenu;
  QJsonObject config;
  std::shared_ptr<Snapshot> current;
  std::vector<std::shared_ptr<Snapshot>> pool;
  std::shared_ptr<const Result> active;
  std::vector<std::pair<QString,std::shared_ptr<const Result>>> comparisons;
  std::vector<std::pair<Masks,Masks>> undoStack;
  QVector<QStringList> stats,report;
  std::string sourceKey;
  int generation=0,displayKind=-1;
  bool selecting=false;
  QPoint begin;
  QRubberBand* rubber;
  QList<bool> visibleLayers{true,true,true,true};
  std::vector<std::pair<QwtPlotCurve*,int>> layers;

  QString resolve(int role,const QStringList& names,const QVariantMap& aliases) const
  {
    const auto candidates=config.value("role"+QString::number(role)).toString(defaults()[role]).split(';',Qt::SkipEmptyParts);
    for(auto c:candidates){
      c=c.trimmed();
      const int index=names.indexOf(c);
      if(index>=0)return names[index];
      for(const auto& name:names)
        if(name.compare(c,Qt::CaseInsensitive)==0 || aliases.value(name).toString()==c)return name;
    }
    return {};
  }
  Params parameters() const
  {
    Params p;
    const auto n=[&](const char* key,double initial){return config.value(key).toDouble(initial);};
    p.rated=n("rated",0);p.diameter=n("diameter",0);p.rho=n("rho",1.225);
    p.ratio=n("ratio",1);p.rpm=n("rpm",0);p.cutIn=n("cutIn",3);p.cutOut=n("cutOut",25);
    p.ratedWind=n("ratedWind",0);p.width=n("width",.5);p.tolerance=n("tolerance",1);
    p.efficiency=n("efficiency",1);p.sectors=int(n("sectors",16));p.degree=int(n("degree",4));
    p.normal=int(n("normal",0));p.filterState=config.value("filterState").toBool();
    p.generator=config.value("generator").toBool();p.absoluteYaw=config.value("absoluteYaw").toBool(true);
    for(int i=0;i<NRoles;++i){
      const auto bounds=config.value("limit"+QString::number(i)).toArray();
      if(bounds.size()==2)p.limits[i]={bounds[0].toDouble(),bounds[1].toDouble()};
    }
    if(config.contains("start"))p.start=n("start",0);
    if(config.contains("end"))p.end=n("end",0);
    for(auto v:config.value("reference").toArray()){
      auto pair=v.toArray();p.reference.push_back({pair[0].toDouble(),pair[1].toDouble()});
    }
    return p;
  }
  QStringList available() const
  {
    QStringList result;for(const auto& it:source.numeric)result<<QString::fromStdString(it.first);
    result.sort(Qt::CaseInsensitive);return result;
  }
  void refreshKinds()
  {
    auto names=available();auto aliases=qApp->property("bladeScopeTagAliases").toMap();const auto p=parameters();
    auto* model=qobject_cast<QStandardItemModel*>(kind->model());
    for(int k=0;k<NKind;++k){
      QStringList missingRoles;
      for(auto role:required(k,p)) if(resolve(role,names,aliases).isEmpty())missingRoles<<roles()[role];
      auto* item=model->item(k);item->setEnabled(missingRoles.isEmpty());
      item->setToolTip(missingRoles.isEmpty()?QString():QStringLiteral("缺少 ")+missingRoles.join("、"));
    }
  }
  void persist()
  {
    QSettings().setValue("BladeScope/curveAnalysis",QJsonDocument(config).toJson(QJsonDocument::Compact));
  }
  void settings()
  {
    QDialog d(this);d.setWindowTitle(QStringLiteral("测点关联和机组参数"));d.resize(730,560);
    QVBoxLayout layout(&d);
    auto* templates=new QHBoxLayout;layout.addLayout(templates);
    auto* templateName=new QComboBox;templateName->setEditable(true);
    QSettings saved;
    auto library=QJsonDocument::fromJson(saved.value("BladeScope/fitTemplates").toByteArray()).object();
    templateName->addItems(library.keys());templates->addWidget(templateName,1);
    auto* use=new QPushButton(QStringLiteral("载入模板"));templates->addWidget(use);
    auto* keep=new QCheckBox(QStringLiteral("同时保存为模板"));templates->addWidget(keep);
    auto* pages=new QTabWidget;layout.addWidget(pages,1);
    auto* mapping=new QTableWidget(NRoles,4);pages->addTab(mapping,QStringLiteral("测点关联"));
    mapping->setHorizontalHeaderLabels({"物理量","测点或别名（分号分隔）","有效下限","有效上限"});
    mapping->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    mapping->verticalHeader()->hide();
    auto names=available();auto aliases=qApp->property("bladeScopeTagAliases").toMap();
    std::array<QComboBox*,NRoles> selectors;
    for(int i=0;i<NRoles;++i){
      auto* label=new QTableWidgetItem(roles()[i]);label->setFlags(Qt::ItemIsEnabled);
      if(resolve(i,names,aliases).isEmpty())label->setForeground(QColor("#d64545"));mapping->setItem(i,0,label);
      auto* combo=new QComboBox;combo->setEditable(true);combo->addItems(names);
      combo->completer()->setFilterMode(Qt::MatchContains);combo->setInsertPolicy(QComboBox::NoInsert);
      combo->setEditText(config.value("role"+QString::number(i)).toString(defaults()[i]));
      selectors[i]=combo;mapping->setCellWidget(i,1,combo);
      auto lim=config.value("limit"+QString::number(i)).toArray();
      for(int j=0;j<2;++j)mapping->setItem(i,j+2,new QTableWidgetItem(lim.size()==2?num(lim[j].toDouble()):QString()));
    }
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
    auto* formPage=new QWidget;auto* f=new QFormLayout(formPage);scroll->setWidget(formPage);
    pages->addTab(scroll,QStringLiteral("机组与计算"));
    QMap<QString,QLineEdit*> fields;
    auto parameter=[&](QString key,QString title,QString fallback){
      auto* e=new QLineEdit(config.contains(key)?num(config.value(key).toDouble()):fallback);
      fields.insert(key,e);f->addRow(title,e);
    };
    parameter("rated","额定功率 (kW)","");parameter("diameter","叶轮直径 (m)","");
    parameter("rho","空气密度 (kg/m³)","1.225");parameter("ratio","齿轮箱变比","1");
    parameter("rpm","当前转速轴的额定转速 (rpm)","");parameter("cutIn","切入风速 (m/s)","3");
    parameter("ratedWind","额定风速 (m/s)","");parameter("cutOut","切出风速 (m/s)","25");
    parameter("efficiency","发电机效率 (0–1)","1");parameter("width","横轴分箱宽度","0.5");
    parameter("tolerance","最大时间匹配差 (s)","1");parameter("degree","Cp–λ 多项式阶数 (4–6)","4");
    parameter("start","起始时间（原始秒，可留空）","");parameter("end","结束时间（原始秒，可留空）","");
    parameter("normal","正常运行状态码","0");
    auto* state=new QCheckBox(QStringLiteral("仅保留正常运行状态"));state->setChecked(config.value("filterState").toBool());f->addRow(state);
    auto* gen=new QCheckBox(QStringLiteral("M21 / M35 使用发电机转速"));gen->setChecked(config.value("generator").toBool());f->addRow(gen);
    auto* abs=new QCheckBox(QStringLiteral("M23 使用对风偏差绝对值"));abs->setChecked(config.value("absoluteYaw").toBool(true));f->addRow(abs);
    auto* sectors=new QComboBox;sectors->addItems({"16","24"});sectors->setCurrentText(QString::number(config.value("sectors").toInt(16)));f->addRow(QStringLiteral("风玫瑰扇区"),sectors);
    auto* theory=new QPlainTextEdit;QStringList lines;
    for(auto p:parameters().reference)lines<<num(p.x)+","+num(p.y);
    theory->setPlainText(lines.join('\n'));theory->setPlaceholderText(QStringLiteral("每行输入 风速,功率(kW)，按风速递增。留空使用机组参数估算。"));
    pages->addTab(theory,QStringLiteral("厂家功率曲线"));
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    connect(use,&QPushButton::clicked,&d,[&]{if(library.contains(templateName->currentText())){
      config=library.value(templateName->currentText()).toObject();d.done(2);}});
    connect(buttons,&QDialogButtonBox::accepted,&d,[&]{
      auto next=config;
      auto invalid=[&](QString t){QMessageBox::warning(&d,d.windowTitle(),t);};
      for(int i=0;i<NRoles;++i){
        next["role"+QString::number(i)]=selectors[i]->currentText().trimmed();
        const auto a=mapping->item(i,2)->text().trimmed(),b=mapping->item(i,3)->text().trimmed();const auto key="limit"+QString::number(i);
        if(a.isEmpty()&&b.isEmpty())next.remove(key);
        else{bool okA,okB;double lo=a.toDouble(&okA),hi=b.toDouble(&okB);
          if(!okA||!okB||!std::isfinite(lo)||!std::isfinite(hi)||lo>hi){invalid(roles()[i]+QStringLiteral("有效范围有误"));return;}
          next[key]=QJsonArray{lo,hi};}
      }
      for(auto it=fields.begin();it!=fields.end();++it){
        auto t=it.value()->text().trimmed();if(t.isEmpty()){next.remove(it.key());continue;}
        bool ok;double v=t.toDouble(&ok);
        if(!ok||!std::isfinite(v)){invalid(QStringLiteral("请输入有效数字"));return;} next[it.key()]=v;
      }
      if(next.value("width").toDouble(.5)<=0 || next.value("efficiency").toDouble(1)<=0 ||
         next.value("efficiency").toDouble(1)>1 || next.value("degree").toInt(4)<4 ||next.value("degree").toInt(4)>6){
        invalid(QStringLiteral("请检查分箱宽度、效率和拟合阶数"));return;}
      QJsonArray refs;double prev=-std::numeric_limits<double>::infinity();
      for(auto line:theory->toPlainText().split('\n',Qt::SkipEmptyParts)){
        auto pair=line.trimmed().split(QRegularExpression("[,，;\\s]+"),Qt::SkipEmptyParts);
        bool a,b;double x=pair.value(0).toDouble(&a),y=pair.value(1).toDouble(&b);
        if(pair.size()!=2||!a||!b||!std::isfinite(x)||!std::isfinite(y)||x<=prev){invalid(QStringLiteral("请检查厂家曲线的数据和顺序"));return;}
        refs.append(QJsonArray{x,y});prev=x;
      }
      if(refs.size()==1){invalid(QStringLiteral("厂家曲线至少需要两个点"));return;}
      next["reference"]=refs;next["generator"]=gen->isChecked();next["absoluteYaw"]=abs->isChecked();
      next["filterState"]=state->isChecked();next["sectors"]=sectors->currentText().toInt();
      if(keep->isChecked()){
        auto name=templateName->currentText().trimmed();if(name.isEmpty()){invalid(QStringLiteral("请输入模板名"));return;}
        library[name]=next;saved.setValue("BladeScope/fitTemplates",QJsonDocument(library).toJson(QJsonDocument::Compact));
      }
      config=next;d.accept();
    });
    const auto old=config;
    int ret=d.exec();
    if(ret==2){settings();return;}
    if(ret==QDialog::Accepted){persist();refreshKinds();reload();}
    else config=old;
  }
  void reload()
  {
    auto snap=std::make_shared<Snapshot>();snap->params=parameters();
    auto channels=std::make_shared<Channels>();const auto names=available();
    const auto aliases=qApp->property("bladeScopeTagAliases").toMap();
    for(int i=0;i<NRoles;++i){
      const auto name=resolve(i,names,aliases);if(name.isEmpty())continue;
      const auto it=source.numeric.find(name.toStdString());if(it==source.numeric.end())continue;
      const auto& curve=it->second;
      auto& data=(*channels)[i];data.reserve(curve.size());
      for(size_t j=0;j<curve.size();++j){const auto& p=curve.at(j);if(std::isfinite(p.x))data.push_back({p.x,p.y});}
    }
    snap->channels=channels;snap->name=QStringLiteral("当前数据");
    current=snap;undoStack.clear();refreshKinds();calculate();
  }
  void clearPlot()
  {
    layers.clear();plot->removeAllCurves();shown.clear();
    QMetaObject::invokeMethod(plot,"setModeXY",Qt::DirectConnection,Q_ARG(bool,true));
  }
  void calculate()
  {
    const int k=kind->currentIndex();const auto ticket=++generation;
    active.reset();comparisons.clear();stats.clear();report.clear();clearPlot();plot->replot();
    if(!current||k<0)return;
    status->setText(QStringLiteral("正在计算…"));
    struct Task {std::shared_ptr<Snapshot> snapshot;std::shared_ptr<const Result> cached;Masks mask;};
    std::vector<Task> tasks;
    auto add=[&](const std::shared_ptr<Snapshot>& s){
      const auto it=s->cache.find(k);tasks.push_back({s,it==s->cache.end()?nullptr:it->second,s->excluded});};
    add(current);
    for(auto* a:compareMenu->actions()) if(a->isCheckable()&&a->isChecked()){
      int i=a->data().toInt();if(i>=0&&size_t(i)<pool.size())add(pool[i]);}
    using Answer=std::vector<std::pair<std::shared_ptr<const Result>,QString>>;
    auto receive=[this,k,ticket,tasks](Answer results,QString error){
      if(ticket!=generation)return;
      if(!error.isEmpty()||results.empty()||!results[0].first){
        status->setText(error.isEmpty()&&!results.empty()?results[0].second:error);return;
      }
      QStringList omitted;
      for(size_t i=0;i<results.size();++i){
        if(!results[i].first){omitted<<tasks[i].snapshot->name+" ("+results[i].second+")";continue;}
        auto& cache=tasks[i].snapshot->cache;
        if(cache.size()>=3 && !cache.count(k))cache.erase(cache.begin());
        cache[k]=results[i].first;
        if(!i)active=results[i].first;
        else comparisons.emplace_back(tasks[i].snapshot->name,results[i].first);
      }
      displayKind=k;render();
      if(!omitted.isEmpty())status->setText(status->text()+QStringLiteral("；未参与比较：")+omitted.join(", "));
    };
    const bool ready=std::all_of(tasks.begin(),tasks.end(),[](const Task& t){return bool(t.cached);});
    if(ready){Answer a;for(auto& t:tasks)a.emplace_back(t.cached,QString());receive(std::move(a),{});return;}
    BladeTrend::background<Answer>(this,[tasks,k]{
      Answer out;
      for(const auto& t:tasks){
        try{
          auto value=t.cached;
          if(!value) value=std::make_shared<Result>(analyze(k,*t.snapshot->channels,t.snapshot->params,t.mask,
               [](double ts){return QDateTime::fromSecsSinceEpoch(qint64(std::floor(ts))).date().month();}));
          out.emplace_back(value,QString());
        }catch(const std::exception& ex){out.emplace_back(nullptr,QString::fromUtf8(ex.what()));}
      }
      return out;
    },receive);
  }
  void layer(QString name,const Points& values,int group,QColor color,
             QwtPlotCurve::CurveStyle style=QwtPlotCurve::Lines,bool fill=false)
  {
    if(values.empty())return;
    auto id=name.toStdString();auto& data=shown.addScatterXY(id)->second;
    for(auto p:values)if(std::isfinite(p.x)&&std::isfinite(p.y))data.pushBack({p.x,p.y});
    if(!data.size())return;
    auto* info=plot->addCurve(id,color);if(!info)return;
    auto* curve=info->curve;
    curve->setStyle(style);curve->setSymbol(nullptr);
    curve->setPen(QPen(color,style==QwtPlotCurve::Dots?(group==1?5:2):1.6));
    if(fill)curve->setBrush(color);
    curve->setVisible(visibleLayers[group]);layers.push_back({curve,group});
  }
  static Points histogram(const Result& r,double width)
  {
    Points points;
    for(auto& entry:r.bins){double x=entry.first*width,y=entry.second.n*100./r.raw.size();
      points.insert(points.end(),{{x,0},{x,y},{x+width,y},{x+width,0}});}
    return points;
  }
  double roseValue(const Result& r,int sector) const
  {
    auto it=r.bins.find(sector);
    if(it==r.bins.end())return 0;
    if(roseMetric->currentIndex()==1)return it->second.y;
    if(roseMetric->currentIndex()==2)return std::isfinite(r.sectorPower.at(sector))?r.sectorPower.at(sector):0;
    return 100.*it->second.n/r.raw.size();
  }
  Points roseOutline(const Result& r) const
  {
    Points p;
    const int n=int(r.sectors.size());
    for(int i=0;i<=n;++i){double a=2*pi*i/n,d=roseValue(r,i%n);p.push_back({d*std::sin(a),d*std::cos(a)});}
    return p;
  }
  void drawRose()
  {
    const QColor colors[]={QColor("#4196cf"),QColor("#29b399"),QColor("#e6a43a"),QColor("#d65e54")};
    const int n=int(active->sectors.size());
    const QStringList levels={"<4","4–8","8–12","≥12"};
    for(int s=0;s<n;++s){
      const bool bands=roseMetric->currentIndex()==0;
      for(int b=0;b<(bands?4:1);++b){
        double inner=b?active->sectors[s][b-1].y:0,outer=bands?active->sectors[s][b].y:roseValue(*active,s);
        if(!(outer>inner))continue;
        Points p;
        for(int i=0;i<=8;++i){double a=(s-.5+i/8.)*2*pi/n;p.push_back({outer*std::sin(a),outer*std::cos(a)});}
        for(int i=8;i>=0;--i){double a=(s-.5+i/8.)*2*pi/n;p.push_back({inner*std::sin(a),inner*std::cos(a)});}
        p.push_back(p.front());layer(QString("%1° %2").arg(s*360./n).arg(bands?levels[b]:QString()),p,1,colors[b],QwtPlotCurve::Lines,true);
        if(!layers.empty())layers.back().first->setItemAttribute(QwtPlotItem::Legend,false);
      }
    }
  }
  void render()
  {
    if(!active)return;
    clearPlot();
    const auto& r=*active;const auto& p=current->params;
    const int k=displayKind;const auto labels=axes(k);
    plot->qwtPlot()->setAxisTitle(QwtPlot::xBottom,labels[0]);plot->qwtPlot()->setAxisTitle(QwtPlot::yLeft,labels[1]);
    layer(k==M01&&p.reference.empty()?QStringLiteral("机组参数估算参考"):QStringLiteral("理论参考"),r.reference,0,QColor("#a57bd5"));
    Points means;
    for(auto& e:r.bins)means.push_back({e.second.x,e.second.y});
    if(k==M11)drawRose();
    else if(k==M02)layer(QStringLiteral("实测频率"),histogram(r,p.width),1,QColor("#599bc2"),QwtPlotCurve::Lines,true);
    else{
      if(k!=M12)layer(QStringLiteral("分箱均值"),means,1,QColor("#e69a22"),QwtPlotCurve::Dots);
      Points raw,marked;
      std::array<Points,4> colors;
      const size_t count=std::min<size_t>(50000,r.raw.size());
      std::map<std::pair<int,int>,size_t> occupancy;
      auto tile=[](const Record& a){return std::make_pair(int(std::floor(a.x/5)),int(std::floor(a.y)));};
      if(k==M12&&density->isChecked())for(size_t i=0;i<count;++i)++occupancy[tile(r.raw[i*r.raw.size()/count])];
      for(size_t i=0;i<count;++i){
        const auto& a=r.raw[i*r.raw.size()/count];
        if(current->marked.count(a.t))marked.push_back({a.x,a.y});
        if(k==M12&&density->isChecked()){
          auto v=occupancy[tile(a)];colors[v<5?0:v<20?1:v<100?2:3].push_back({a.x,a.y});
        }else raw.push_back({a.x,a.y});
      }
      layer(QStringLiteral("实测散点"),raw,2,QColor("#538cbc"),QwtPlotCurve::Dots);
      const QColor palette[]={QColor("#6686b8"),QColor("#3ba99b"),QColor("#dc9b3c"),QColor("#d6534a")};
      for(int i=0;i<4;++i)layer(QStringLiteral("密度 %1").arg(i+1),colors[i],2,palette[i],QwtPlotCurve::Dots);
      layer(QStringLiteral("已标注"),marked,2,QColor("#d91d86"),QwtPlotCurve::Dots);
    }
    layer(QStringLiteral("拟合结果"),r.fit,3,QColor("#e86137"));
    if(k==M03&&std::isfinite(r.peakX))layer(QStringLiteral("最大 Cp %1 · λ %2").arg(num(r.peakY),num(r.peakX)),
      {{r.peakX,r.peakY}},3,QColor("#df2a8c"),QwtPlotCurve::Dots);
    for(size_t i=0;i<comparisons.size();++i){
      const auto& pair=comparisons[i]; const auto& compared=*pair.second;
      Points path;
      if(k==M11)path=roseOutline(compared);
      else if(k==M12){
        const size_t count=std::min<size_t>(50000,compared.raw.size());
        for(size_t j=0;j<count;++j){auto a=compared.raw[j*compared.raw.size()/count];path.push_back({a.x,a.y});}
      }else path=compared.fit;
      layer(QStringLiteral("对比 ")+pair.first,path,3,QColor::fromHsv(int((125+67*i)%360),180,205),
            k==M12?QwtPlotCurve::Dots:QwtPlotCurve::Lines);
    }
    plot->zoomOut(false);plot->replot();
    QString summary=QStringLiteral("有效 %1　已筛除 %2　标注 %3").arg(qulonglong(r.raw.size())).arg(qulonglong(r.rejected)).arg(qulonglong(current->marked.size()));
    if(k==M02)summary+=std::isfinite(r.shape)?QStringLiteral("　Weibull k=%1，A=%2 m/s").arg(num(r.shape),num(r.scale)):QStringLiteral("　样本不足以估计 Weibull 参数");
    if(k==M41)summary+=QStringLiteral("　实测传动比 %1").arg(num(r.slope));
    if(k==M11)summary+=QStringLiteral("　正北向上，顺时针　风速分级 <4 / 4–8 / 8–12 / ≥12 m/s");
    status->setText(summary);makeTables();
  }
  void makeTables()
  {
    const auto& r=*active;const int k=displayKind;
    stats={{"箱序号","横轴均值","纵轴均值","样本数","样本标准差","最小值","最大值","拟合值","参考值","偏差(%)","频率(%)","扇区平均功率(kW)"}};
    for(auto& entry:r.bins){
      const auto& b=entry.second;double ref=interpolate(r.reference,b.x),fit=interpolate(r.fit,b.x);
      stats.append({QString::number(qlonglong(entry.first)),num(b.x),num(b.y),QString::number(qulonglong(b.n)),num(b.sd()),num(b.lo),num(b.hi),
                   num(fit),num(ref),num(ref!=0?100*(fit-ref)/ref:missing()),num(100.*b.n/r.raw.size()),
                   k==M11?num(r.sectorPower.at(size_t(entry.first))):QString()});
    }
    report={{"当前曲线集","比较曲线集","曲线类型","共同取样点","平均差值","平均相对偏差(%)","基线峰值横坐标","对比峰值横坐标","峰值差","共同风况覆盖率(%)","覆盖区间年化电量差(kWh)"}};
    for(auto& item:comparisons){
      const auto& other=*item.second;Bin deltas,relative;
      for(auto b:r.fit){double y=interpolate(other.fit,b.x);if(!std::isfinite(y))continue;
        deltas.add(b.x,y-b.y);if(std::abs(b.y)>1e-9)relative.add(b.x,100*(y-b.y)/std::abs(b.y));}
      double mwh=0;size_t covered=0,total=0;
      if(k==M01)for(auto& b:r.resource){
        double x=(b.first+.5)*current->params.width;total+=b.second;
        double a=interpolate(r.fit,x),c=interpolate(other.fit,x);
        if(std::isfinite(a)&&std::isfinite(c)){covered+=b.second;mwh+=(c-a)*b.second;}
      }
      auto peak=[](const Points& pts){return pts.empty()?Point{missing(),missing()}:
        *std::max_element(pts.begin(),pts.end(),[](Point a,Point b){return a.y<b.y;});};
      auto a=peak(r.fit),b=peak(other.fit);
      report.append({current->name,item.first,kinds()[k],QString::number(qulonglong(deltas.n)),num(deltas.n?deltas.y:missing()),
        num(relative.n?relative.y:missing()),num(a.x),num(b.x),num(b.y-a.y),total?num(100.*covered/total):QString(),
        covered?num(8760*mwh/total):QString()});
    }
  }
  void showTable(const QVector<QStringList>& rows,QString name)
  {
    if(rows.isEmpty())return;
    QDialog d(this);d.setWindowTitle(name);d.resize(950,450);QVBoxLayout l(&d);
    auto* t=new QTableWidget(rows.size()-1,rows.front().size());t->setHorizontalHeaderLabels(rows.front());
    for(int i=1;i<rows.size();++i)for(int j=0;j<rows[i].size();++j)t->setItem(i-1,j,new QTableWidgetItem(rows[i][j]));
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->resizeColumnsToContents();l.addWidget(t);
    auto* save=new QPushButton(QStringLiteral("导出 CSV"));l.addWidget(save);
    connect(save,&QPushButton::clicked,&d,[&]{saveCsv(&d,name,rows);});d.exec();
  }
  void selection(QPoint end)
  {
    if(!active||!current||displayKind==M11||displayKind==M02)return;
    auto xy=[this](QPoint pos){return QPointF(plot->qwtPlot()->invTransform(QwtPlot::xBottom,pos.x()),
                                              plot->qwtPlot()->invTransform(QwtPlot::yLeft,pos.y()));};
    const auto a=xy(begin),b=xy(end);
    const int mode=select->currentIndex();
    if(begin==end||(mode>=3 && a.x()==b.x()))return;
    const QRectF rect=QRectF(a,b).normalized();
    Masks chosen;
    for(auto pt:active->raw){
      if(pt.x<rect.left()||pt.x>rect.right())continue;
      bool hit=rect.contains(QPointF(pt.x,pt.y));
      if(mode>=3){double line=a.y()+(b.y()-a.y())*(pt.x-a.x())/(b.x()-a.x());hit=mode==3?pt.y>line:pt.y<line;}
      if(hit)chosen.insert(pt.t);
    }
    if(chosen.empty())return;
    if(undoStack.size()==16)undoStack.erase(undoStack.begin());
    undoStack.emplace_back(current->excluded,current->marked);
    if(mode==2){current->marked.insert(chosen.begin(),chosen.end());render();}
    else{current->excluded.insert(chosen.begin(),chosen.end());current->cache.clear();calculate();}
  }
protected:
  bool eventFilter(QObject* watched,QEvent* event) override
  {
    if(watched==canvas&&select->currentIndex()&&active &&displayKind!=M02&&displayKind!=M11){
      if(event->type()==QEvent::MouseButtonPress){
        auto* e=static_cast<QMouseEvent*>(event);if(e->button()!=Qt::LeftButton)return false;
        begin=e->pos();selecting=true;rubber->setGeometry(QRect(begin,QSize()));rubber->show();return true;
      }
      if(selecting&&event->type()==QEvent::MouseMove){auto* e=static_cast<QMouseEvent*>(event);
        rubber->setGeometry(QRect(begin,e->pos()).normalized());return true;}
      if(selecting&&event->type()==QEvent::MouseButtonRelease){selecting=false;rubber->hide();
        selection(static_cast<QMouseEvent*>(event)->pos());return true;}
    }
    return QWidget::eventFilter(watched,event);
  }
public:
  explicit Panel(PJ::PlotDataMapRef& src,std::function<void()> closePage,QWidget* parent=nullptr)
    :QWidget(parent),source(src)
  {
    config=QJsonDocument::fromJson(QSettings().value("BladeScope/curveAnalysis").toByteArray()).object();
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(3,2,3,3);
    auto* top=new QHBoxLayout;layout->addLayout(top);
    auto button=[this](QHBoxLayout* line,QString title,auto fn){
      auto* b=new QPushButton(title);line->addWidget(b);connect(b,&QPushButton::clicked,this,fn);return b;};
    button(top,"时序图",closePage);top->addWidget(new QLabel(QStringLiteral("曲线分析")));
    kind=new QComboBox;kind->addItems(kinds());top->addWidget(kind);
    button(top,"测点与机组参数",[this]{settings();});button(top,"获取数据",[this]{reload();});
    top->addStretch();
    poolMenu=new QToolButton;poolMenu->setText("曲线集比较");poolMenu->setPopupMode(QToolButton::InstantPopup);compareMenu=new QMenu(poolMenu);poolMenu->setMenu(compareMenu);top->addWidget(poolMenu);
    auto* saveSet=compareMenu->addAction("保存当前曲线集");
    connect(saveSet,&QAction::triggered,this,[this]{
      if(!current||!active)return;
      bool ok=false;auto name=QInputDialog::getText(this,"曲线集","名称",QLineEdit::Normal,{},&ok).trimmed();if(!ok||name.isEmpty())return;
      for(auto s:pool)if(s->name==name){QMessageBox::information(this,"曲线集","已有同名曲线集");return;}
      auto copy=std::make_shared<Snapshot>(*current);copy->name=name;pool.push_back(copy);
      auto* action=compareMenu->addAction(name);action->setCheckable(true);action->setData(int(pool.size()-1));
      connect(action,&QAction::toggled,this,[this]{calculate();});
    });
    auto* reset=compareMenu->addAction("清空比较池");
    connect(reset,&QAction::triggered,this,[this]{for(auto* a:compareMenu->actions())if(a->isCheckable())delete a;pool.clear();calculate();});
    compareMenu->addSeparator();
    plot=new PlotWidget(shown,this);layout->addWidget(plot,1);plot->activateGrid(true);plot->activateLegend(true);
    plot->setContextMenuEnabled(false);plot->qwtPlot()->setAcceptDrops(false);
    canvas=plot->qwtPlot()->canvas();canvas->installEventFilter(this);rubber=new QRubberBand(QRubberBand::Rectangle,canvas);
    auto* tools=new QHBoxLayout;layout->addLayout(tools);select=new QComboBox;
    select->addItems({"浏览","框选排除","框选标注","上界线以上排除","下界线以下排除"});tools->addWidget(select);
    button(tools,"撤销",[this]{if(!current||undoStack.empty())return;
      auto old=std::move(undoStack.back());undoStack.pop_back();current->excluded=std::move(old.first);current->marked=std::move(old.second);current->cache.clear();calculate();});
    for(int i=0;i<4;++i){auto* c=new QCheckBox(QStringList{"参考","分箱","散点","拟合 / 对比"}[i]);c->setChecked(true);tools->addWidget(c);
      connect(c,&QCheckBox::toggled,this,[this,i](bool on){visibleLayers[i]=on;for(auto item:layers)if(item.second==i)item.first->setVisible(on);plot->replot();});}
    density=new QCheckBox("密度着色");tools->addWidget(density);
    roseMetric=new QComboBox;roseMetric->addItems({"频率 (%)","平均风速 (m/s)","平均功率 (kW)"});tools->addWidget(roseMetric);
    tools->addStretch();
    auto* out=new QHBoxLayout;layout->addLayout(out);status=new QLabel;status->setWordWrap(true);out->addWidget(status,1);
    button(out,"导出图片",[this]{if(active)QMetaObject::invokeMethod(plot,"on_savePlotToFile",Qt::DirectConnection);});
    button(out,"分箱统计",[this]{showTable(stats,"分箱统计");});button(out,"对比报告",[this]{showTable(report,"曲线对比报告");});
    connect(kind,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int k){density->setVisible(k==M12);roseMetric->setVisible(k==M11);calculate();});
    connect(density,&QCheckBox::toggled,this,[this]{render();});
    connect(roseMetric,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{render();});
    density->hide();roseMetric->hide();
  }
  void enter(){refreshKinds();if(!current)reload();}
  ~Panel() override {delete plot;}
};
}

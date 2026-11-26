#pragma once
#include "bladescope_trend.h"
#include "bladescope_fit_xlsx.h"
#include <QHelpEvent>
#include <QToolTip>
#include "bladescope_time_display.h"
#include <QSaveFile>
#include "qwt_plot_picker.h"
#include "qwt_picker_machine.h"
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
    "桨距角 (°)","绝对风向 (°)","对风角度 (°)","转矩（单位见下方）","运行状态码","室外温度 (°C)","故障标志","限功率标志","启停过渡标志"};
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

inline void profileControls(QVBoxLayout* layout, QDialog* parent, QString id, QString title,
                            std::function<QJsonObject()> capture, std::function<void(const QJsonObject&)> apply,
                            std::function<bool(const QJsonObject&)> validate)
{
  const auto key = QStringLiteral("BladeScope/fitProfiles/") + id;
  auto profiles = std::make_shared<QJsonObject>(
      QJsonDocument::fromJson(QSettings().value(key).toByteArray()).object());
  auto* row = new QHBoxLayout;
  auto* names = new QComboBox;
  names->setEditable(true);
  names->setInsertPolicy(QComboBox::NoInsert);
  names->lineEdit()->setPlaceholderText(title);
  names->addItems(profiles->keys());
  row->addWidget(names, 1);
  layout->insertLayout(0, row);
  auto persist = [=](const QString& selected) {
    QSettings().setValue(key, QJsonDocument(*profiles).toJson(QJsonDocument::Compact));
    names->clear();
    names->addItems(profiles->keys());
    names->setEditText(selected);
  };
  auto warning = [=](const QString& message) { QMessageBox::warning(parent, title, message); };
  auto button = [&](QString label, auto callback) {
    auto* control = new QPushButton(label);
    row->addWidget(control);
    QObject::connect(control, &QPushButton::clicked, parent, callback);
  };
  button(QStringLiteral("载入"), [=] {
    const auto name = names->currentText().trimmed();
    if (profiles->contains(name)) apply(profiles->value(name).toObject());
    else warning(QStringLiteral("请选择已保存的组。"));
  });
  button(QStringLiteral("保存"), [=] {
    const auto name = names->currentText().trimmed();
    if (name.isEmpty()) { warning(QStringLiteral("请输入组名。")); return; }
    try {
      const auto values = capture();
      if (profiles->contains(name) && QMessageBox::question(parent, title,
          QStringLiteral("覆盖同名组？")) != QMessageBox::Yes) return;
      profiles->insert(name, values);
      persist(name);
    } catch (const std::exception& ex) { warning(QString::fromUtf8(ex.what())); }
  });
  button(QStringLiteral("重命名"), [=] {
    const auto name = names->currentText().trimmed();
    if (!profiles->contains(name)) return;
    bool ok = false;
    const auto next = QInputDialog::getText(parent, title, QStringLiteral("新组名"),
                                           QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || next.isEmpty() || next == name) return;
    if (profiles->contains(next)) { warning(QStringLiteral("组名已存在。")); return; }
    const auto values = profiles->take(name);
    profiles->insert(next, values);
    persist(next);
  });
  button(QStringLiteral("删除"), [=] {
    const auto name = names->currentText().trimmed();
    if (profiles->contains(name) && QMessageBox::question(parent, title,
        QStringLiteral("删除选定组？")) == QMessageBox::Yes) {
      profiles->remove(name);
      persist({});
    }
  });
  button(QStringLiteral("导入"), [=] {
    const auto path = QFileDialog::getOpenFileName(parent, title, {}, "JSON (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024*1024) {
      warning(QStringLiteral("无法读取配置文件或文件过大。")); return;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll()).object();
    const auto name = doc.value("name").toString().trimmed();
    const auto values = doc.value("values").toObject();
    if (doc.value("version").toInt() != 1 || doc.value("type").toString() != id ||
        name.isEmpty() || !doc.value("values").isObject() || !validate(values)) {
      warning(QStringLiteral("文件类型、版本或内容不符合此组的配置格式。")); return;
    }
    QString target = name;
    for (int n=2; profiles->contains(target); ++n)
      target = QStringLiteral("%1 (%2)").arg(name).arg(n);
    profiles->insert(target, values);
    persist(target);
    apply(values);
  });
  button(QStringLiteral("导出"), [=] {
    const auto name = names->currentText().trimmed();
    if (!profiles->contains(name)) { warning(QStringLiteral("请先保存并选中要导出的组。")); return; }
    auto path = QFileDialog::getSaveFileName(parent, title, id + ".json", "JSON (*.json)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".json";
    QSaveFile file(path);
    const auto bytes = QJsonDocument(QJsonObject{{"version",1},{"type",id},{"name",name},
                                                 {"values",profiles->value(name)}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      warning(QStringLiteral("无法保存配置文件。"));
  });
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
  return {"grWindSpeed;风速实时值;风速","grGridActivePower;有功功率;电网有功功率",
    "grRotorSpeed1;叶轮转速","grGeneratorSpeed;发电机转速","grPitchAngleA;桨距角",
    "绝对风向;环境风向","对风角度;偏航误差","发电机转矩百分比","机组运行模式","室外温度;环境温度","故障标志","限功率标志","启停过渡标志"};
}

class SelectionGuide : public QWidget
{
  QPoint first,last;
  bool line=false;
public:
  explicit SelectionGuide(QWidget* parent):QWidget(parent)
  {setAttribute(Qt::WA_TransparentForMouseEvents);hide();}
  void updateDrag(QPoint a,QPoint b,bool boundary)
  {first=a;last=b;line=boundary;setGeometry(parentWidget()->rect());show();raise();update();}
protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);QColor c=palette().color(QPalette::Highlight);
    painter.setPen(QPen(c,2,Qt::DashLine));
    if(line)painter.drawLine(first,last);
    else{c.setAlpha(40);painter.setBrush(c);painter.drawRect(QRect(first,last).normalized());}
  }
};

class AnalysisPlot final : public PlotWidget
{
public:
  using PlotWidget::PlotWidget;
  using PlotWidgetBase::qwtPlot;
};

class Panel : public QWidget
{
  PJ::PlotDataMapRef& source;
  PJ::PlotDataMapRef shown;
  AnalysisPlot* plot=nullptr;
  QComboBox *kind,*select,*roseMetric;
  QCheckBox *density;
  QComboBox* sampleStyle;
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
  int loadGeneration=0;
  bool dirty=true;
  std::shared_ptr<std::atomic_bool> stop;
  QTimer* refreshTimer;
  QwtPlotPicker* cursor;
  int generation=0,displayKind=-1;
  bool selecting=false;
  QPoint begin;
  SelectionGuide* rubber;
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
    p.torqueDegree=int(n("torqueDegree",2));p.normal=int(n("normal",0));p.filterState=config.value("filterState").toBool();
    p.torqueUnit=config.value("torqueUnit").toInt(Percent);
    p.ratedTorque=n("ratedTorque",0);p.rpmWidth=n("rpmWidth",0);p.torqueFitMin=n("torqueFitMin",0);
    p.torqueNonnegative=config.value("torqueNonnegative").toBool(true);
    p.rejectFlags=config.value("rejectFlags").toBool(true);
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
  bool settings()
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
    const auto groups=QJsonDocument::fromJson(saved.value("BladeScope/measurementGroups").toByteArray()).object();
    auto* groupBox=new QComboBox;groupBox->addItem(QStringLiteral("所有测点"));groupBox->addItems(groups.keys());layout.addWidget(groupBox);
    auto* pages=new QTabWidget;layout.addWidget(pages,1);
    auto* mappingPage=new QWidget;
    auto* mappingLayout=new QVBoxLayout(mappingPage);
    auto* unitRow=new QHBoxLayout;
    unitRow->addWidget(new QLabel(QStringLiteral("转矩测点输入单位")));
    auto* torqueUnit=new QComboBox;torqueUnit->addItems({"%","N·m","kN·m"});
    torqueUnit->setCurrentIndex(config.value("torqueUnit").toInt(Percent));
    unitRow->addWidget(torqueUnit);unitRow->addStretch();mappingLayout->addLayout(unitRow);
    auto* mapping=new QTableWidget(NRoles,4);
    mappingLayout->addWidget(mapping);
    pages->addTab(mappingPage,QStringLiteral("测点关联"));
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
    connect(groupBox,qOverload<int>(&QComboBox::currentIndexChanged),&d,[&](int index){
      QStringList subset;
      if(!index)subset=names;
      else for(auto v:groups.value(groupBox->currentText()).toArray())if(names.contains(v.toString()))subset<<v.toString();
      for(auto* box:selectors){auto text=box->currentText();box->clear();box->addItems(subset);box->setEditText(text);}
    });
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
    auto* formPage=new QWidget;auto* f=new QFormLayout(formPage);scroll->setWidget(formPage);
    auto* parameterPage=new QWidget;
    auto* parameterLayout=new QVBoxLayout(parameterPage);
    parameterLayout->addWidget(scroll);
    pages->addTab(parameterPage,QStringLiteral("机组与计算"));
    QMap<QString,QLineEdit*> fields;
    auto parameter=[&](QString key,QString title,QString fallback){
      auto* e=new QLineEdit(config.contains(key)?num(config.value(key).toDouble()):fallback);
      fields.insert(key,e);f->addRow(title,e);
    };
    parameter("rated","额定功率 (kW)","");parameter("diameter","叶轮直径 (m)","");
    parameter("rho","空气密度 (kg/m³)","1.225");parameter("ratio","齿轮箱变比","1");
    parameter("rpm","当前转速轴的额定转速 (rpm)","");parameter("cutIn","切入风速 (m/s)","3");
    parameter("ratedWind","额定风速 (m/s)","");parameter("cutOut","切出风速 (m/s)","25");
    parameter("efficiency","发电机效率 (0–1)","1");parameter("width","横轴分箱宽度（风速为 m/s）","0.5");
    parameter("rpmWidth","转速分箱宽度 (rpm，留空沿用原宽度)","");
    parameter("ratedTorque","发电机额定转矩 (N·m，留空按功率/效率/额定转速计算)","");
    parameter("torqueFitMin","M33 / M34 拟合最低发电机转速 (rpm)","0");
    parameter("tolerance","最大时间匹配差 (s)","1");parameter("degree","Cp–λ 多项式阶数 (4–6)","4");
    parameter("torqueDegree","M33 / M34 转矩拟合阶数 (2–3)","2");
    parameter("start","起始时间（原始秒，可留空）","");parameter("end","结束时间（原始秒，可留空）","");
    parameter("normal","正常运行状态码","0");
    auto* state=new QCheckBox(QStringLiteral("仅保留正常运行状态"));state->setChecked(config.value("filterState").toBool());f->addRow(state);
    auto* nonnegative=new QCheckBox(QStringLiteral("M33 / M34 拟合输出下限为 0（保留原始散点）"));
    nonnegative->setChecked(config.value("torqueNonnegative").toBool(true));f->addRow(nonnegative);
    auto* flags=new QCheckBox(QStringLiteral("性能曲线排除故障、限功率和启停标志（0 为正常）"));
    flags->setChecked(config.value("rejectFlags").toBool(true));f->addRow(flags);
    auto* gen=new QCheckBox(QStringLiteral("M21 / M35 使用发电机转速"));gen->setChecked(config.value("generator").toBool());f->addRow(gen);
    auto* abs=new QCheckBox(QStringLiteral("M23 使用对风偏差绝对值"));abs->setChecked(config.value("absoluteYaw").toBool(true));f->addRow(abs);
    auto* sectors=new QComboBox;sectors->addItems({"16","24"});sectors->setCurrentText(QString::number(config.value("sectors").toInt(16)));f->addRow(QStringLiteral("风玫瑰扇区"),sectors);
    QMap<QString,QPair<QCheckBox*,bool>> toggles{
      {"generator",{gen,false}}, {"absoluteYaw",{abs,true}}, {"rejectFlags",{flags,true}},
      {"filterState",{state,false}}, {"torqueNonnegative",{nonnegative,true}}};
    auto captureMapping = [&] {
      QJsonObject values;
      values["torqueUnit"]=torqueUnit->currentIndex();
      for(int i=0;i<NRoles;++i){
        values["role"+QString::number(i)]=selectors[i]->currentText().trimmed();
        const auto a=mapping->item(i,2)->text().trimmed(), b=mapping->item(i,3)->text().trimmed();
        if(a.isEmpty() && b.isEmpty()) continue;
        bool okA,okB; const double lo=a.toDouble(&okA), hi=b.toDouble(&okB);
        if(!okA || !okB || !std::isfinite(lo) || !std::isfinite(hi) || lo>hi)
          throw std::runtime_error((roles()[i]+QStringLiteral("有效范围有误")).toStdString());
        values["limit"+QString::number(i)] = QJsonArray{lo,hi};
      }
      return values;
    };
    auto captureParameters = [&] {
      QJsonObject values;
      for(auto it=fields.cbegin();it!=fields.cend();++it){
        const auto text=it.value()->text().trimmed();
        if(text.isEmpty()) continue;
        bool ok; const double value=text.toDouble(&ok);
        if(!ok || !std::isfinite(value)) throw std::runtime_error("请输入有效数字");
        values[it.key()]=value;
      }
      for(auto it=toggles.cbegin();it!=toggles.cend();++it) values[it.key()]=it.value().first->isChecked();
      values["sectors"]=sectors->currentText().toInt();
      return values;
    };
    profileControls(mappingLayout,&d,"measurement-associations",QStringLiteral("测点关联组"),captureMapping,
      [&](const QJsonObject& values) {
        torqueUnit->setCurrentIndex(values.value("torqueUnit").toInt(Percent));
        for(int i=0;i<NRoles;++i){
          selectors[i]->setEditText(values.value("role"+QString::number(i)).toString());
          const auto limits=values.value("limit"+QString::number(i)).toArray();
          for(int j=0;j<2;++j) mapping->item(i,j+2)->setText(limits.size()==2?num(limits[j].toDouble()):QString());
        }
      }, [](const QJsonObject& values) {
        if(values.contains("torqueUnit") && (!values.value("torqueUnit").isDouble() ||
           values.value("torqueUnit").toDouble()!=values.value("torqueUnit").toInt() ||
           values.value("torqueUnit").toInt()<Percent || values.value("torqueUnit").toInt()>KiloNewtonMetre)) return false;
        for(int i=0;i<NRoles;++i){
          if(!values.value("role"+QString::number(i)).isString()) return false;
          const auto key="limit"+QString::number(i);
          if(!values.contains(key)) continue;
          const auto limits=values.value(key).toArray();
          if(limits.size()!=2 || !limits[0].isDouble() || !limits[1].isDouble() ||
             limits[0].toDouble()>limits[1].toDouble()) return false;
        }
        return true;
      });
    profileControls(parameterLayout,&d,"turbine-parameters",QStringLiteral("机组参数组"),captureParameters,
      [&](const QJsonObject& values) {
        for(auto it=fields.cbegin();it!=fields.cend();++it)
          it.value()->setText(values.contains(it.key())?num(values.value(it.key()).toDouble()):QString());
        for(auto it=toggles.cbegin();it!=toggles.cend();++it)
          it.value().first->setChecked(values.value(it.key()).toBool(it.value().second));
        sectors->setCurrentText(QString::number(values.value("sectors").toInt(16)));
      }, [&](const QJsonObject& values) {
        for(auto it=values.begin();it!=values.end();++it) {
          if(toggles.contains(it.key())) { if(!it.value().isBool()) return false; }
          else if(it.key()=="sectors") {
            if(it.value()!=QJsonValue(16) && it.value()!=QJsonValue(24)) return false;
          } else if(!fields.contains(it.key()) || !it.value().isDouble()) return false;
        }
        return !values.isEmpty();
      });

    auto* theoryPage=new QWidget;
    auto* theoryLayout=new QVBoxLayout(theoryPage);
    auto* curveActions=new QHBoxLayout;
    auto* importCurve=new QPushButton(QStringLiteral("导入标准功率曲线 XLSX"));
    auto* exportCurve=new QPushButton(QStringLiteral("导出标准功率曲线 XLSX"));
    auto* curveModel=new QLabel;
    curveActions->addWidget(importCurve);curveActions->addWidget(exportCurve);curveActions->addStretch();
    theoryLayout->addLayout(curveActions);theoryLayout->addWidget(curveModel);
    auto* theory=new QPlainTextEdit;
    theoryLayout->addWidget(theory);
    pages->addTab(theoryPage,QStringLiteral("标准功率曲线"));
    auto editText=[](const QJsonArray& points) {
      QStringList lines;
      for(const auto point:points){
        const auto xy=point.toArray();
        lines << QString::number(xy[0].toDouble(),'g',17)+","+QString::number(xy[1].toDouble(),'g',17);
      }
      return lines.join('\n');
    };
    theory->setPlainText(editText(config.value("reference").toArray()));
    theory->setPlaceholderText(QStringLiteral("每行输入 风速,功率(kW)，按风速递增。留空使用机组参数估算。"));
    CurveSheet workbook;
    const auto stored=config.value("referenceWorkbook").toObject();
    workbook.header=stored.value("header").toInt(-1);
    workbook.wind=stored.value("wind").toInt(-1);
    workbook.power=stored.value("power").toInt(-1);
    for(const auto value:stored.value("rows").toArray()){
      QStringList row;
      for(const auto cell:value.toArray()) row<<cell.toString();
      workbook.rows.append(row);
    }
    auto showMetadata=[&]{
      QStringList metadata;
      for(int i=1;i<workbook.header && i<workbook.rows.size();++i)
        metadata<<workbook.rows[i].join(QStringLiteral("  |  "));
      curveModel->setText(metadata.join('\n'));
      curveModel->setVisible(!metadata.isEmpty());
    };
    showMetadata();
    auto readEditedCurve=[&] {
      CurveSheet sheet{{QStringList{"项目动态功率曲线"}, QStringList{"Turbine Type","Air Density (Kg/m3)","Turbulent Intensity"},
                        QStringList{"","",""}, QStringList{"Wind(m/s)","Power (kW)","Ct","Cp"}},3,0,1};
      for(auto line:theory->toPlainText().split('\n',Qt::SkipEmptyParts)){
        const auto fields=line.trimmed().split(QRegularExpression("[,，;\\s]+"),Qt::SkipEmptyParts);
        if(fields.isEmpty()) continue;
        if(fields.size()!=2) throw std::runtime_error("每行应填写风速和功率两个数值");
        sheet.rows.append(fields);
      }
      if(sheet.rows.size()>4){
        const auto points=curvePoints(sheet);
        if(workbook.header>=0 && curvePoints(workbook)==points) return workbook;
      }
      return sheet;
    };
    connect(importCurve,&QPushButton::clicked,&d,[&]{
      const auto path=QFileDialog::getOpenFileName(&d,QStringLiteral("导入标准功率曲线"),{},"Excel (*.xlsx)");
      if(path.isEmpty()) return;
      try {
        auto imported=readCurveSheet(path);
        const auto points=curvePoints(imported);
        workbook=std::move(imported);
        theory->setPlainText(editText(points));
        showMetadata();
      } catch(const std::exception& ex) { QMessageBox::warning(&d,d.windowTitle(),QString::fromUtf8(ex.what())); }
    });
    connect(exportCurve,&QPushButton::clicked,&d,[&]{
      try {
        const auto sheet=readEditedCurve();
        curvePoints(sheet);
        auto path=QFileDialog::getSaveFileName(&d,QStringLiteral("导出标准功率曲线"),"power-curve.xlsx","Excel (*.xlsx)");
        if(path.isEmpty()) return;
        if(!path.endsWith(".xlsx",Qt::CaseInsensitive)) path+=".xlsx";
        const auto bytes=writeCurveSheet(sheet);
        QSaveFile file(path);
        if(bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
          throw std::runtime_error("无法写入标准功率曲线文件");
      } catch(const std::exception& ex) { QMessageBox::warning(&d,d.windowTitle(),QString::fromUtf8(ex.what())); }
    });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    connect(use,&QPushButton::clicked,&d,[&]{if(library.contains(templateName->currentText())){
      config=library.value(templateName->currentText()).toObject();d.done(2);}});
    connect(buttons,&QDialogButtonBox::accepted,&d,[&]{
      auto next=config;
      auto invalid=[&](QString t){QMessageBox::warning(&d,d.windowTitle(),t);};
      try {
        for(int i=0;i<NRoles;++i) next.remove("limit"+QString::number(i));
        for(const auto& key:fields.keys()) next.remove(key);
        for(const auto& values:{captureMapping(),captureParameters()})
          for(auto it=values.begin();it!=values.end();++it) next[it.key()]=it.value();
      } catch(const std::exception& ex) { invalid(QString::fromUtf8(ex.what())); return; }
      if(next.value("width").toDouble(.5)<=0 || next.value("efficiency").toDouble(1)<=0 ||
         next.value("efficiency").toDouble(1)>1 || next.value("degree").toInt(4)<4 ||next.value("degree").toInt(4)>6){
        invalid(QStringLiteral("请检查分箱宽度、效率和拟合阶数"));return;}
      if(next.value("torqueDegree").toInt(2)<2||next.value("torqueDegree").toInt(2)>3){
        invalid(QStringLiteral("转矩拟合支持二阶或三阶"));return;}
      for(const auto& key:{"rpmWidth","ratedTorque"})
        if(next.contains(key) && next.value(key).toDouble()<=0){
          invalid(QStringLiteral("转速分箱宽度和额定转矩须为正数或留空"));return;}
      if(next.value("torqueFitMin").toDouble()<0){invalid(QStringLiteral("拟合最低转速不能为负数"));return;}
      try {
        if(theory->toPlainText().trimmed().isEmpty()){
          next["reference"]=QJsonArray();
          next.remove("referenceWorkbook");
        } else {
          const auto sheet=readEditedCurve();
          next["reference"]=curvePoints(sheet);
          QJsonArray rows;
          for(const auto& row:sheet.rows) rows.append(QJsonArray::fromStringList(row));
          next["referenceWorkbook"]=QJsonObject{{"header",sheet.header},{"wind",sheet.wind},
                                                 {"power",sheet.power},{"rows",rows}};
        }
      } catch(const std::exception& ex) { invalid(QString::fromUtf8(ex.what())); return; }
      if(keep->isChecked()){
        auto name=templateName->currentText().trimmed();if(name.isEmpty()){invalid(QStringLiteral("请输入模板名"));return;}
        library[name]=next;saved.setValue("BladeScope/fitTemplates",QJsonDocument(library).toJson(QJsonDocument::Compact));
      }
      config=next;d.accept();
    });
    const auto old=config;
    int ret=d.exec();
    if(ret==2){if(settings())return true;config=old;return false;}
    if(ret==QDialog::Accepted){persist();refreshKinds();reload();}
    else config=old;
    return ret==QDialog::Accepted;
  }
  void reload()
  {
    auto snap=std::make_shared<Snapshot>();snap->params=parameters();
    auto channels=std::make_shared<Channels>();const auto names=available();
    const auto aliases=qApp->property("bladeScopeTagAliases").toMap();
    const int ticket=++loadGeneration;
    ++generation;
    if(stop)stop->store(true);
    dirty=false;
    setProperty("bladeFitResultsReady",false);active.reset();current.reset();comparisons.clear();stats.clear();report.clear();clearPlot();plot->replot();
    status->setText(QStringLiteral("正在准备测点…"));
    struct CopyState {std::array<std::string,NRoles> names;int role=0;size_t offset=0;};
    auto copying=std::make_shared<CopyState>();
    for(int i=0;i<NRoles;++i)copying->names[i]=resolve(i,names,aliases).toStdString();
    auto* timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,[this,ticket,timer,copying,channels,snap]{
      if(ticket!=loadGeneration){timer->stop();timer->deleteLater();return;}
      QElapsedTimer clock;clock.start();
      while(copying->role<NRoles && clock.elapsed()<5){
        int i=copying->role;const auto it=source.numeric.find(copying->names[i]);
        if(it==source.numeric.end()){++copying->role;copying->offset=0;continue;}
        const auto& curve=it->second;auto& data=(*channels)[i];
        if(!copying->offset)data.reserve(curve.size());
        const size_t end=std::min<size_t>(curve.size(),copying->offset+8192);
        for(;copying->offset<end;++copying->offset){auto pt=curve.at(copying->offset);
          if(std::isfinite(pt.x))data.push_back({pt.x,pt.y});}
        if(copying->offset>=curve.size()){++copying->role;copying->offset=0;}
      }
      if(copying->role==NRoles){
        timer->stop();timer->deleteLater();snap->channels=channels;snap->name=QStringLiteral("当前数据");
        current=snap;undoStack.clear();refreshKinds();calculate();
      }
    });
    timer->start(0);
  }
  void clearPlot()
  {
    layers.clear();
    plot->removeAllCurves();
    // PlotDataMapRef::clear() leaves scatter_xy intact in PlotJuggler 3.17.0.
    shown.scatter_xy.clear();
    QMetaObject::invokeMethod(plot,"setModeXY",Qt::DirectConnection,Q_ARG(bool,true));
  }
  void calculate()
  {
    const int k=kind->currentIndex();const auto ticket=++generation;
    if(stop)stop->store(true);
    stop=std::make_shared<std::atomic_bool>(false);auto cancel=stop;
    setProperty("bladeFitResultsReady",false);active.reset();comparisons.clear();stats.clear();report.clear();clearPlot();plot->replot();
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
    BladeTrend::background<Answer>(this,[tasks,k,cancel]{
      Answer out;
      for(const auto& t:tasks){
        if(cancel->load())return out;
        try{
          auto value=t.cached;
          if(!value) value=std::make_shared<Result>(analyze(k,*t.snapshot->channels,t.snapshot->params,t.mask,
               [](double ts){return QDateTime::fromSecsSinceEpoch(qint64(std::floor(ts))).date().month();},cancel.get()));
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
    size_t markedCount = 0;
    Points means;
    for(auto& e:r.bins)means.push_back({e.second.x,e.second.y});
    if(k==M11)drawRose();
    else if(k==M02)layer(QStringLiteral("实测频率"),histogram(r,p.width),1,QColor("#599bc2"),QwtPlotCurve::Lines,true);
    else{
      if(k!=M12)layer(QStringLiteral("分箱均值"),means,1,QColor("#e69a22"),QwtPlotCurve::Dots);
      Points raw,marked;
      std::array<Points,4> colors;
      auto tile=[](const Record& a){return std::make_pair(static_cast<long long>(a.x/5),static_cast<long long>(std::clamp(a.y,-1e12,1e12)));};
      if(!current->marked.empty()){
        for(const auto& point : r.raw)
          if(current->marked.count(point.id)) {
            ++markedCount;
            if(marked.size() < 50000) marked.push_back({point.x, point.y});
          }
      }
      for(auto index:r.display){
        const auto& a=r.raw[index];
        if(k==M12&&density->isChecked()){
          auto v=r.density.at(tile(a));colors[v<5?0:v<20?1:v<100?2:3].push_back({a.x,a.y});
        }else raw.push_back({a.x,a.y});
      }
      const bool lines = sampleStyle->currentIndex() == 1;
      layer(lines ? QStringLiteral("实测折线") : QStringLiteral("实测散点"),
            raw,2,QColor("#538cbc"),lines ? QwtPlotCurve::Lines : QwtPlotCurve::Dots);
      const QColor palette[]={QColor("#6686b8"),QColor("#3ba99b"),QColor("#dc9b3c"),QColor("#d6534a")};
      for(int i=0;i<4;++i)layer(QStringLiteral("密度 %1").arg(i+1),colors[i],2,palette[i],QwtPlotCurve::Dots);
      layer(QStringLiteral("已标注"),marked,2,QColor("#d91d86"),QwtPlotCurve::Dots);
    }
    if(r.fitSegments.empty()) layer(QStringLiteral("拟合结果"),r.fit,3,QColor("#e86137"));
    else for(size_t i=0;i<r.fitSegments.size();++i)
      layer(QStringLiteral("拟合结果 %1").arg(qulonglong(i+1)),r.fitSegments[i],3,QColor("#e86137"),
            r.fitSegments[i].size()==1?QwtPlotCurve::Dots:QwtPlotCurve::Lines);
    if(k==M03&&std::isfinite(r.peakX))layer(QStringLiteral("最大 Cp %1 · λ %2").arg(num(r.peakY),num(r.peakX)),
      {{r.peakX,r.peakY}},3,QColor("#df2a8c"),QwtPlotCurve::Dots);
    for(size_t i=0;i<comparisons.size();++i){
      const auto& pair=comparisons[i]; const auto& compared=*pair.second;
      if(!compared.fitSegments.empty()){
        for(const auto& segment:compared.fitSegments)
          layer(QStringLiteral("对比 ")+pair.first,segment,3,QColor::fromHsv(int((125+67*i)%360),180,205),
                segment.size()==1?QwtPlotCurve::Dots:QwtPlotCurve::Lines);
        continue;
      }
      Points path;
      if(k==M11)path=roseOutline(compared);
      else if(k==M12){
        for(auto index:compared.display){const auto& a=compared.raw[index];path.push_back({a.x,a.y});}
      }else path=compared.fit;
      layer(QStringLiteral("对比 ")+pair.first,path,3,QColor::fromHsv(int((125+67*i)%360),180,205),
            k==M12?QwtPlotCurve::Dots:QwtPlotCurve::Lines);
    }
    plot->setKeepRatioXY(k==M11);plot->zoomOut(false);plot->replot();
    QString summary=QStringLiteral("有效 %1　已筛除 %2　标注 %3").arg(qulonglong(r.raw.size())).arg(qulonglong(r.rejected)).arg(qulonglong(markedCount));
    if(k==M02)summary+=std::isfinite(r.shape)?QStringLiteral("　Weibull k=%1，A=%2 m/s").arg(num(r.shape),num(r.scale)):QStringLiteral("　样本不足以估计 Weibull 参数");
    if(k==M41)summary+=QStringLiteral("　实测传动比 %1").arg(num(r.slope));
    if(k==M33||k==M34){
      summary+=QStringLiteral("　拟合候选样本 %1　拟合最低转速 %2 rpm　分箱 %3 rpm")
        .arg(qulonglong(r.fitSamples)).arg(num(p.torqueFitMin),num(binWidth(k,p)));
      if(r.fit.empty())summary+=QStringLiteral("　不同转速箱不足，未生成拟合线");
      if(p.torqueNonnegative)summary+=QStringLiteral("　拟合输出下限 0");
      if(k==M34)summary+=QStringLiteral("　输入 %1 → 输出 %　额定转矩 %2 N·m")
        .arg(QStringList{"%","N·m","kN·m"}.value(p.torqueUnit),num(torqueReference(p)));
    }
    if(k==M11)summary+=QStringLiteral("　正北向上，顺时针　风速分级 <4 / 4–8 / 8–12 / ≥12 m/s");
    if(k==M01)summary+=energySummary();
    makeTables();status->setText(summary);setProperty("bladeFitResultsReady",true);
  }
  static double powerAt(const Result& r,const Params& p,double x)
  {
    if(x<p.cutIn||x>p.cutOut)return 0;
    return interpolate(r.fit,x);
  }
  QString energySummary() const
  {
    double total=0,covered=0,energy=0;
    for(const auto& b:active->resource){
      const double x=(b.first+.5)*current->params.width;
      total+=b.second;const double y=powerAt(*active,current->params,x);
      if(std::isfinite(y)){covered+=b.second;energy+=b.second*y;}
    }
    return total>0&&covered>0?QStringLiteral("　覆盖风况年化电量 %1 MWh（时间覆盖 %2%）")
      .arg(num(8.76*energy/total),num(100*covered/total)):QString();
  }
  Points compareValues(const Result& r) const
  {
    if(displayKind==M11){
      Points values;
      for(int i=0;i<int(r.sectors.size());++i)values.push_back({360.*i/r.sectors.size(),roseValue(r,i)});
      return values;
    }
    if(displayKind==M12|| (displayKind==M02&&r.fit.empty())){
      Points values;
      for(auto& b:r.bins) values.push_back({b.second.x,displayKind==M02?100.*b.second.n/r.raw.size():b.second.y});
      return values;
    }
    return r.fit;
  }
  void makeTables()
  {
    const auto& r=*active;const int k=displayKind;
    stats={{"箱序号",axes(k)[0]+"均值",axes(k)[1]+"均值","样本数","样本标准差","最小值","最大值","拟合值","参考值","偏差(%)","频率(%)","扇区平均功率(kW)"}};
    if(k==M02){
      stats={{"箱序号","风速下界 (m/s)","风速上界 (m/s)","样本数","实测频率 (%)","Weibull 频率 (%)"}};
      const double width = current->params.width;
      for(const auto& entry : r.bins){
        const double lower = entry.first * width;
        const double upper = lower + width;
        const double expected = std::isfinite(r.shape)
          ? 100 * (std::exp(-std::pow(lower/r.scale, r.shape)) - std::exp(-std::pow(upper/r.scale, r.shape)))
          : missing();
        stats.append({QString::number(qlonglong(entry.first)), num(lower), num(upper),
                      QString::number(qulonglong(entry.second.n)), num(100.*entry.second.n/r.raw.size()),
                      num(expected)});
      }
    }
    else for(auto& entry:r.bins){
      const auto& b=entry.second;double ref=interpolate(r.reference,b.x),fit=fittedValue(r,b.x);
      stats.append({QString::number(qlonglong(entry.first)),num(b.x),num(b.y),QString::number(qulonglong(b.n)),num(b.sd()),num(b.lo),num(b.hi),
                   num(fit),num(ref),num(ref!=0?100*(fit-ref)/ref:missing()),num(100.*b.n/r.raw.size()),
                   k==M11?num(r.sectorPower.at(size_t(entry.first))):QString()});
    }
    report={{"当前曲线集","比较曲线集","曲线类型","共同取样点","平均差值","平均相对偏差(%)","基线峰值横坐标","对比峰值横坐标","峰值差","共同风况覆盖率(%)","覆盖区间年化电量差(kWh)"}};
    for(auto& item:comparisons){
      const auto& other=*item.second;Bin deltas,relative;
      const auto baseValues=compareValues(r),otherValues=compareValues(other);
      for(auto b:baseValues){double y=other.fitSegments.empty()?interpolate(otherValues,b.x):fittedValue(other,b.x);if(!std::isfinite(y))continue;
        deltas.add(b.x,y-b.y);if(std::abs(b.y)>1e-9)relative.add(b.x,100*(y-b.y)/std::abs(b.y));}
      double energy=0,covered=0,total=0;
      if(k==M01)for(auto& b:r.resource){
        double x=(b.first+.5)*current->params.width;total+=b.second;
        double a=powerAt(r,current->params,x);
        auto saved=std::find_if(pool.begin(),pool.end(),[&](const auto& s){return s->name==item.first;});
        double c=saved!=pool.end()?powerAt(other,(*saved)->params,x):missing();
        if(std::isfinite(a)&&std::isfinite(c)){covered+=b.second;energy+=(c-a)*b.second;}
      }
      auto peak=[](const Points& pts){return pts.empty()?Point{missing(),missing()}:
        *std::max_element(pts.begin(),pts.end(),[](Point a,Point b){return a.y<b.y;});};
      auto a=peak(baseValues),b=peak(otherValues);
      report.append({current->name,item.first,kinds()[k],QString::number(qulonglong(deltas.n)),num(deltas.n?deltas.y:missing()),
        num(relative.n?relative.y:missing()),num(a.x),num(b.x),num(b.y-a.y),total?num(100.*covered/total):QString(),
        covered?num(8760*energy/total):QString()});
    }
  }
  void showTable(const QVector<QStringList>& rows,QString name)
  {
    if(rows.isEmpty())return;
    QDialog d(this);d.setWindowTitle(name);d.resize(950,450);QVBoxLayout l(&d);
    auto* t=new QTableWidget(rows.size()-1,rows.front().size());t->setHorizontalHeaderLabels(rows.front());
    for(int i=1;i<rows.size();++i)for(int j=0;j<rows[i].size();++j)t->setItem(i-1,j,new QTableWidgetItem(rows[i][j]));
    t->setObjectName("bladeFitResultTable");t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->resizeColumnsToContents();l.addWidget(t);
    auto* save=new QPushButton(QStringLiteral("导出 CSV"));save->setObjectName("bladeFitExportCsv");l.addWidget(save);
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
    auto raw=active;
    BladeTrend::background<Masks>(this,[a,b,rect,mode,raw]{
      Masks chosen;
      for(const auto& pt:raw->raw){
        if(pt.x<rect.left()||pt.x>rect.right())continue;
        bool hit=rect.contains(QPointF(pt.x,pt.y));
        if(mode>=3){double line=a.y()+(b.y()-a.y())*(pt.x-a.x())/(b.x()-a.x());hit=mode==3?pt.y>line:pt.y<line;}
        if(hit)chosen.insert(pt.id);
      }
      return chosen;
    },[this,mode,raw](Masks chosen,QString error){
      if(!error.isEmpty()){status->setText(error);return;}
      if(raw!=active||!current||chosen.empty())return;
      if(undoStack.size()==16)undoStack.erase(undoStack.begin());
      undoStack.emplace_back(current->excluded,current->marked);
      if(mode==2){current->marked.insert(chosen.begin(),chosen.end());render();}
      else{current->excluded.insert(chosen.begin(),chosen.end());current->cache.clear();calculate();}
    });
  }
protected:
  bool eventFilter(QObject* watched,QEvent* event) override
  {
    if (watched==canvas && event->type()==QEvent::ToolTip)
    {
      auto* help=static_cast<QHelpEvent*>(event);
      const Record* nearest=nullptr;
      double best=64.0;
      if (active && !select->currentIndex() && visibleLayers[2] && displayKind!=M02 && displayKind!=M11)
      {
        auto* chart=plot->qwtPlot();
        for (const auto index : active->display)
        {
          const auto& point=active->raw[index];
          const double dx=chart->transform(QwtPlot::xBottom,point.x)-help->pos().x();
          const double dy=chart->transform(QwtPlot::yLeft,point.y)-help->pos().y();
          const double distance=dx*dx+dy*dy;
          if (distance<best) {best=distance;nearest=&point;}
        }
      }
      if (!nearest) QToolTip::hideText();
      else
      {
        const bool calendar=BladeTimeDisplay::isCalendarTime(nearest->t);
        const auto time=BladeTimeDisplay::format(nearest->t,calendar,calendar,calendar,
            BladeTimeDisplay::precision()==BladeTimeDisplay::Seconds ? 0 : 3);
        const auto names=axes(displayKind);
        QToolTip::showText(help->globalPos(),QStringLiteral("%1：%2\n%3：%4\n时间：%5")
            .arg(names.value(0), num(nearest->x), names.value(1), num(nearest->y), time),canvas);
      }
      return true;
    }
    if(watched==canvas&&select->currentIndex()&&active &&displayKind!=M02&&displayKind!=M11){
      if(event->type()==QEvent::MouseButtonPress){
        auto* e=static_cast<QMouseEvent*>(event);if(e->button()!=Qt::LeftButton)return false;
        begin=e->pos();selecting=true;rubber->updateDrag(begin,begin,select->currentIndex()>=3);return true;
      }
      if(selecting&&event->type()==QEvent::MouseMove){auto* e=static_cast<QMouseEvent*>(event);
        rubber->updateDrag(begin,e->pos(),select->currentIndex()>=3);return true;}
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
    button(top,"时序图",closePage)->setObjectName("bladeFitBack");top->addWidget(new QLabel(QStringLiteral("曲线分析")));
    kind=new QComboBox;kind->setObjectName("bladeFitKind");kind->addItems(kinds());top->addWidget(kind);
    button(top,"测点与机组参数",[this]{settings();});button(top,"获取数据",[this]{reload();})->setObjectName("bladeFitReload");
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
    plot=new AnalysisPlot(shown,this);plot->qwtPlot()->setObjectName("bladeFitPlot");layout->addWidget(plot,1);plot->activateGrid(true);plot->activateLegend(true);
    plot->setContextMenuEnabled(false);plot->qwtPlot()->setAcceptDrops(false);
    canvas=plot->qwtPlot()->canvas();rubber=new SelectionGuide(canvas);
    auto* tools=new QHBoxLayout;layout->addLayout(tools);select=new QComboBox;
    select->addItems({"浏览","框选排除","框选标注","上界线以上排除","下界线以下排除"});tools->addWidget(select);
    button(tools,"撤销",[this]{if(!current||undoStack.empty())return;
      auto old=std::move(undoStack.back());undoStack.pop_back();current->excluded=std::move(old.first);current->marked=std::move(old.second);current->cache.clear();calculate();});
    for(int i=0;i<4;++i){auto* c=new QCheckBox(QStringList{"参考","分箱","散点","拟合 / 对比"}[i]);c->setChecked(true);tools->addWidget(c);
      connect(c,&QCheckBox::toggled,this,[this,i](bool on){visibleLayers[i]=on;for(auto item:layers)if(item.second==i)item.first->setVisible(on);plot->replot();});}
    sampleStyle=new QComboBox;
    sampleStyle->addItems({QStringLiteral("实测散点"),QStringLiteral("实测折线")});
    tools->addWidget(sampleStyle);
    connect(sampleStyle,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(active) render();});
    density=new QCheckBox("密度着色");tools->addWidget(density);
    const auto updateSampleStyle=[this]{
      const auto k=kind->currentIndex();
      sampleStyle->setEnabled(k!=M02 && k!=M11 && !(k==M12 && density->isChecked()));
    };
    connect(kind,qOverload<int>(&QComboBox::currentIndexChanged),this,updateSampleStyle);
    connect(density,&QCheckBox::toggled,this,updateSampleStyle);
    roseMetric=new QComboBox;roseMetric->addItems({"频率 (%)","平均风速 (m/s)","平均功率 (kW)"});tools->addWidget(roseMetric);
    tools->addStretch();
    auto* out=new QHBoxLayout;layout->addLayout(out);status=new QLabel;status->setObjectName("bladeFitStatus");status->setWordWrap(true);out->addWidget(status,1);
    button(out,"拟合信息",[this]{
      if(!active)return;
      QVector<QStringList> rows{{"曲线集","公式（z=(x−中心)/尺度）","下限","上限","中心","尺度","系数（升幂）"}};
      auto append=[&](QString set,const Result& r){for(auto& part:r.models){
        QStringList coeff;for(auto value:part.coefficients)coeff<<num(value);
        rows.append({set,QString::fromStdString(part.formula),num(part.low),num(part.high),
                     num(part.origin),num(part.scale),coeff.join(", ")});
      }};
      append(current->name,*active);for(auto& item:comparisons)append(item.first,*item.second);
      if(rows.size()==1){QMessageBox::information(this,"拟合信息","此图使用实测分布或分组统计，详见分箱统计。");return;}
      showTable(rows,"拟合信息");
    })->setObjectName("bladeFitFormula");
    button(out,"导出图片",[this]{if(active)QMetaObject::invokeMethod(plot,"on_savePlotToFile",Qt::DirectConnection);});
    button(out,"导出拟合点",[this]{
      if(!active)return;
      QVector<QStringList> rows{{"曲线集","分析类型",axes(displayKind)[0],axes(displayKind)[1],"分段"}};
      auto append=[&](QString name,const Result& r){
        if(r.fitSegments.empty())for(auto pt:compareValues(r))rows.append({name,kinds()[displayKind],num(pt.x),num(pt.y),"1"});
        else for(size_t i=0;i<r.fitSegments.size();++i)for(auto pt:r.fitSegments[i])
          rows.append({name,kinds()[displayKind],num(pt.x),num(pt.y),QString::number(qulonglong(i+1))});
      };
      append(current->name,*active);for(auto& item:comparisons)append(item.first,*item.second);
      saveCsv(this,"导出拟合点",rows);
    });
    button(out,"分箱统计",[this]{showTable(stats,"分箱统计");})->setObjectName("bladeFitBins");button(out,"对比报告",[this]{showTable(report,"曲线对比报告");});
    connect(kind,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int k){density->setVisible(k==M12);roseMetric->setVisible(k==M11);calculate();});
    connect(density,&QCheckBox::toggled,this,[this]{render();});
    connect(roseMetric,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{render();});
    cursor=new QwtPlotPicker(QwtPlot::xBottom,QwtPlot::yLeft,QwtPicker::CrossRubberBand,QwtPicker::AlwaysOn,canvas);
    cursor->setStateMachine(new QwtPickerTrackerMachine);
    cursor->setTrackerPen(QPen(palette().color(QPalette::WindowText)));
    cursor->setRubberBandPen(QPen(palette().color(QPalette::Mid),0,Qt::DotLine));
    connect(select,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){cursor->setEnabled(i==0);});
    refreshTimer=new QTimer(this);refreshTimer->setSingleShot(true);refreshTimer->setInterval(150);
    connect(refreshTimer,&QTimer::timeout,this,[this]{if(dirty&&isVisible())reload();});
    density->hide();roseMetric->hide();
    canvas->installEventFilter(this);
  }
  void invalidate(){dirty=true;++loadGeneration;++generation;if(stop)stop->store(true);if(isVisible())refreshTimer->start();}
  void enter(){refreshKinds();if(dirty||!current)reload();}
  ~Panel() override {if(stop)stop->store(true);canvas->removeEventFilter(this);delete plot;}
};
}

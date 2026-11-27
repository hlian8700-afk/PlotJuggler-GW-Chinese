#pragma once
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace BladeUI {
// The original combo model remains authoritative; popup rows only reference indexes.
class GroupCombo final : public QComboBox {
public:
  using QComboBox::QComboBox;
  void showPopup() override {
    QDialog popup(this,Qt::Popup);auto* box=new QVBoxLayout(&popup);
    auto* search=new QLineEdit;search->setPlaceholderText(QStringLiteral("搜索测点组…"));box->addWidget(search);
    auto* list=new QListWidget;box->addWidget(list);
    const auto recent=QSettings().value("BladeScope/recentMeasurementGroups").toStringList();
    auto populate=[&]{
      list->clear();QSet<int> used;
      auto header=[&](QString text){auto* row=new QListWidgetItem(text,list);row->setFlags(Qt::NoItemFlags);};
      auto add=[&](int i){if(i<0||used.contains(i)||!itemText(i).contains(search->text(),Qt::CaseInsensitive))return;
        auto* row=new QListWidgetItem(itemText(i),list);row->setData(Qt::UserRole,i);row->setToolTip(itemText(i));used.insert(i);};
      if(search->text().isEmpty()){
        header(QStringLiteral("最近使用"));for(const auto& name:recent)add(findText(name));
      }
      header(QStringLiteral("常规 / 工况组"));
      for(int i=0;i<count();++i)if(QRegularExpression("^\\d").match(itemText(i)).hasMatch()||itemData(i).toString().isEmpty())add(i);
      header(QStringLiteral("机组 / 关联组"));for(int i=0;i<count();++i)add(i);
      for(int i=0;i<list->count();++i)if(list->item(i)->flags()&Qt::ItemIsEnabled){list->setCurrentRow(i);break;}
    };
    auto choose=[&](QListWidgetItem* row){if(!row||!row->data(Qt::UserRole).isValid())return;
      const int i=row->data(Qt::UserRole).toInt();setCurrentIndex(i);
      auto names=QSettings().value("BladeScope/recentMeasurementGroups").toStringList();
      names.removeAll(itemText(i));names.prepend(itemText(i));while(names.size()>5)names.removeLast();
      QSettings().setValue("BladeScope/recentMeasurementGroups",names);emit activated(i);popup.accept();};
    QObject::connect(search,&QLineEdit::textChanged,&popup,[&]{populate();});
    QObject::connect(list,&QListWidget::itemClicked,&popup,choose);
    QObject::connect(list,&QListWidget::itemActivated,&popup,choose);
    QObject::connect(search,&QLineEdit::returnPressed,&popup,[&]{choose(list->currentItem());});
    populate();const QRect screen=QGuiApplication::screenAt(mapToGlobal(QPoint(0,height())))?
      QGuiApplication::screenAt(mapToGlobal(QPoint(0,height())))->availableGeometry():QGuiApplication::primaryScreen()->availableGeometry();
    const int rowHeight=fontMetrics().height()+10;
    popup.resize(qMin(qMax(width(),300),qMin(520,screen.width()-16)),qMin(rowHeight*11+55,screen.height()-24));
    QPoint pos=mapToGlobal(QPoint(0,height()));pos.setX(qBound(screen.left(),pos.x(),screen.right()-popup.width()+1));
    if(pos.y()+popup.height()>screen.bottom())pos.setY(qMax(screen.top(),mapToGlobal(QPoint()).y()-popup.height()));
    popup.move(pos);search->setFocus();popup.exec();
  }
};
inline void toolbar(QWidget* widget) {
  widget->setObjectName("bladeUnifiedToolbar");
  widget->setStyleSheet(QStringLiteral(
    "#bladeUnifiedToolbar QPushButton,#bladeUnifiedToolbar QToolButton,#bladeUnifiedToolbar QComboBox {min-height:24px;padding:2px 6px;}"
    "#bladeUnifiedToolbar QPushButton:checked {background:palette(highlight);color:palette(highlighted-text);} "));
}
inline void fitDialog(QDialog& dialog) {
  const auto screen=dialog.screen()?dialog.screen():QGuiApplication::primaryScreen();
  const auto available=screen->availableGeometry().size()-QSize(24,48);
  dialog.setMinimumSize(qMin(680,available.width()),qMin(440,available.height()));
  dialog.resize(qMin(960,available.width()),qMin(700,available.height()));
}
}

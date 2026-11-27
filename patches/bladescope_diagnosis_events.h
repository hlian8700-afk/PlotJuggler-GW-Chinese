#pragma once
#include <QtCore>
#include <QTextCodec>
#include <atomic>
#include <cmath>
#include "bladescope_diagnosis_core.h"

namespace BladeDiagnosisUI {
struct Csv {QStringList headers;QVector<QStringList> rows;};
inline Csv parseCsv(QString text,QChar separator=QChar(','),const std::atomic_bool* cancel=nullptr) {
  if(text.startsWith(QChar(0xfeff)))text.remove(0,1);
  QVector<QStringList> records;QStringList row;QString cell;
  bool quoted=false,closed=false;qsizetype progress=0;
  for(int i=0;i<text.size();++i){
    if((++progress%8192)==0&&cancel&&cancel->load())throw std::runtime_error("已取消");
    const auto c=text[i];
    if(quoted){if(c=='"'){if(i+1<text.size()&&text[i+1]=='"'){cell+='"';++i;}else{quoted=false;closed=true;}}else cell+=c;continue;}
    if(c=='"'){if(!cell.isEmpty()||closed)throw std::runtime_error("CSV 引号位置无效");quoted=true;continue;}
    if(c==separator){row<<cell;cell.clear();closed=false;continue;}
    if(c=='\r'||c=='\n'){
      if(c=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;
      row<<cell;if(std::any_of(row.begin(),row.end(),[](const QString& v){return !v.trimmed().isEmpty();}))records<<row;
      row.clear();cell.clear();closed=false;continue;
    }
    if(closed&&!c.isSpace())throw std::runtime_error("CSV 闭引号后有非法字符");
    if(!closed)cell+=c;
  }
  if(quoted)throw std::runtime_error("CSV 引号未闭合");
  row<<cell;if(std::any_of(row.begin(),row.end(),[](const QString& v){return !v.trimmed().isEmpty();}))records<<row;
  if(records.empty())throw std::runtime_error("CSV 没有表头");
  Csv out;out.headers=records.takeFirst();
  for(auto& header:out.headers)header=header.trimmed();
  if(out.headers.contains(QString())||QSet<QString>(out.headers.begin(),out.headers.end()).size()!=out.headers.size())
    throw std::runtime_error("存在重复表头，请先明确各列含义");
  for(const auto& r:records)if(r.size()!=out.headers.size())throw std::runtime_error("CSV 列数不一致，请检查分隔符");
  out.rows=std::move(records);return out;
}
inline QString decodeCsv(const QByteArray& bytes,QString encoding) {
  auto* codec=QTextCodec::codecForName(encoding.toLatin1());
  if(!codec)throw std::runtime_error("不支持的字符编码");
  QTextCodec::ConverterState state;const auto text=codec->toUnicode(bytes.constData(),bytes.size(),&state);
  if(state.invalidChars)throw std::runtime_error("字符编码不匹配，请重新选择 UTF-8 / GB18030 / UTF-16");
  return text;
}
struct ImportOptions {
  int timeColumn=0,codeColumn=1,stateColumn=-1;
  bool numericMilliseconds=false;
  int utcOffsetSeconds=0;
  QString dateFormat="yyyy-MM-dd HH:mm:ss.zzz";
};
inline qint64 parseTime(QString value,const ImportOptions& o) {
  value=value.trimmed();bool numeric=false;double number=value.toDouble(&numeric);
  if(numeric){
    const long double ms=static_cast<long double>(number)*(o.numericMilliseconds?1.L:1000.L);
    if(!std::isfinite(number)||ms<-62135596800000.L||ms>253402300799999.L)
      throw std::runtime_error("时间戳超出支持范围");
    return qRound64(double(ms));
  }
  auto date=QDateTime::fromString(value,Qt::ISODateWithMs);
  if(!date.isValid())date=QDateTime::fromString(value,o.dateFormat);
  if(!date.isValid())throw std::runtime_error("时间格式不匹配");
  // Strings carrying Z or an explicit UTC offset retain their stated instant.
  const bool explicitZone=value.endsWith('Z',Qt::CaseInsensitive)||
    QRegularExpression("[+-][0-9]{2}:?[0-9]{2}$").match(value).hasMatch();
  if(!explicitZone)date=QDateTime(date.date(),date.time(),Qt::OffsetFromUTC,o.utcOffsetSeconds);
  return date.toUTC().toMSecsSinceEpoch();
}
struct EventRow {qint64 original=0,aligned=0;QString code,state,source,kind;int row=0;};
struct Imported {QVector<EventRow> events;QStringList errors;};
inline Imported importEvents(const Csv& csv,const ImportOptions& o,QString source,QString kind,
                             const std::atomic_bool* cancel=nullptr){
  if(o.timeColumn<0||o.codeColumn<0||o.timeColumn>=csv.headers.size()||o.codeColumn>=csv.headers.size()||
     o.stateColumn< -1||o.stateColumn>=csv.headers.size()||o.timeColumn==o.codeColumn)throw std::runtime_error("时间和代码列必须分别指定");
  Imported out;int row=1;
  for(const auto& values:csv.rows){
    if((row%8192)==0&&cancel&&cancel->load())throw std::runtime_error("已取消");++row;
    try{
      const auto code=values[o.codeColumn].trimmed();if(code.isEmpty())throw std::runtime_error("故障/动作代码为空");
      const auto t=parseTime(values[o.timeColumn],o);
      out.events.push_back({t,t,code,o.stateColumn>=0?values[o.stateColumn]:QStringLiteral("未提供"),source,kind,row});
    }catch(const std::exception& e){out.errors<<QString("第 %1 条数据记录：%2").arg(row-1).arg(QString::fromUtf8(e.what()));}
  }
  std::stable_sort(out.events.begin(),out.events.end(),[](const EventRow& a,const EventRow& b){return a.original<b.original;});
  return out;
}
inline QString iso(qint64 t){return QDateTime::fromMSecsSinceEpoch(t,Qt::UTC).toString("yyyy-MM-dd HH:mm:ss.zzz");}
inline QString csvCell(QString text){text.replace('"',"\"\"");return '"'+text+'"';}
}

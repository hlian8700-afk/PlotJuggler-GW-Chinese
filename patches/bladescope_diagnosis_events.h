#pragma once
#include <QtCore>
#include <QTextCodec>
#include <atomic>
#include <cmath>
#include "bladescope_diagnosis_core.h"

namespace BladeDiagnosisUI {
struct Csv {QStringList headers;QVector<QStringList> rows;QVector<int> recordNumbers;QStringList errors;int skippedPreamble=0,trimmedTrailing=0;};
inline Csv parseCsv(QString text,QChar separator=QChar(','),const std::atomic_bool* cancel=nullptr,int headerRecord=1,bool validOnly=false) {
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
  if(headerRecord<1||headerRecord>records.size())throw std::runtime_error("表头记录号超出文件范围");
  Csv out;out.skippedPreamble=headerRecord-1;
  for(int i=1;i<headerRecord;++i)records.removeFirst();
  out.headers=records.takeFirst();
  while(!out.headers.empty()&&out.headers.last().trimmed().isEmpty()){out.headers.removeLast();++out.trimmedTrailing;}
  if(out.headers.size()<2)throw std::runtime_error("未识别出至少两列；请检查编码、分隔符和表头记录号");
  for(auto& header:out.headers)header=header.trimmed();
  if(out.headers.contains(QString())||QSet<QString>(out.headers.begin(),out.headers.end()).size()!=out.headers.size())
    throw std::runtime_error("存在重复表头，请先明确各列含义");
  int record=headerRecord;
  for(auto r:records){
    ++record;
    const int actual=r.size();
    while(r.size()>out.headers.size()&&r.last().trimmed().isEmpty()){r.removeLast();++out.trimmedTrailing;}
    if(r.size()!=out.headers.size()){
      const auto message=QString("非空记录 %1：预期 %2 列，实际 %3 列；内容：%4").arg(record).arg(out.headers.size()).arg(actual).arg(r.join(separator).left(160));
      if(!validOnly)throw std::runtime_error(message.toUtf8().constData());
      out.errors<<message;continue;
    }
    out.rows<<r;out.recordNumbers<<record;
  }
  return out;
}
inline QString decodeCsv(const QByteArray& bytes,QString encoding) {
  auto* codec=QTextCodec::codecForName(encoding.toLatin1());
  if(!codec)throw std::runtime_error("不支持的字符编码");
  QTextCodec::ConverterState state;const auto text=codec->toUnicode(bytes.constData(),bytes.size(),&state);
  if(state.invalidChars||state.remainingChars)throw std::runtime_error("字符编码不匹配，请重新选择 UTF-8 / GB18030 / UTF-16");
  for(const auto c:text)if(c.unicode()==0||(c.category()==QChar::Other_Control&&c!='\r'&&c!='\n'&&c!='\t'))
    throw std::runtime_error("解码包含异常控制字符，请检查编码");
  // UTF-16 without BOM must have supporting zero-byte structure; arbitrary CSV bytes can otherwise decode as legal gibberish.
  if(encoding.startsWith("UTF-16",Qt::CaseInsensitive)&&!bytes.startsWith(QByteArray::fromHex("fffe"))&&!bytes.startsWith(QByteArray::fromHex("feff"))){
    int zeros=0;for(int i=0;i<qMin(bytes.size(),4096);++i)if(bytes[i]==0)++zeros;
    if(!zeros)throw std::runtime_error("无 BOM 且缺少 UTF-16 字节结构；请改用自动识别或正确编码");
  }
  return text;
}
struct ImportOptions {
  int timeColumn=0,codeColumn=1,stateColumn=-1,descriptionColumn=-1;
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
struct CsvDetection {Csv csv;QString encoding;QChar separator;};
inline CsvDetection detectCsv(const QByteArray& bytes,QString requested="自动",QChar separator=QChar(),int headerRecord=1,bool validOnly=false,const std::atomic_bool* cancel=nullptr){
  QStringList encodings;
  if(requested!="自动")encodings<<requested;
  else if(bytes.startsWith(QByteArray::fromHex("fffe")))encodings<<"UTF-16LE";
  else if(bytes.startsWith(QByteArray::fromHex("feff")))encodings<<"UTF-16BE";
  else if(bytes.startsWith(QByteArray::fromHex("efbbbf")))encodings<<"UTF-8";
  else encodings<<"UTF-8"<<"GB18030"<<"UTF-16LE"<<"UTF-16BE";
  QStringList failures;
  for(const auto& enc:encodings){
    QString text;try{text=decodeCsv(bytes,enc);}catch(const std::exception& e){failures<<enc+": "+QString::fromUtf8(e.what());continue;}
    const QVector<QChar> separators=separator.isNull()?QVector<QChar>{',','\t',';'}:QVector<QChar>{separator};
    QVector<CsvDetection> matches;
    for(const auto sep:separators)try{
      auto csv=parseCsv(text,sep,cancel,headerRecord,validOnly);
      // A delimiter must occur in the header and produce data of the same shape.
      if(csv.rows.empty())throw std::runtime_error("没有可预览的数据记录");
      matches.push_back({std::move(csv),enc,sep});
    }catch(const std::exception& e){failures<<enc+"/"+QString(sep)+": "+QString::fromUtf8(e.what());}
    if(matches.size()==1)return matches.front();
    if(matches.size()>1)throw std::runtime_error("分隔符识别有歧义，请手动选择并检查预览");
  }
  throw std::runtime_error(QString("无法可靠识别文件；请检查编码、分隔符、表头记录号。\n%1").arg(failures.join('\n').left(2000)).toUtf8().constData());
}
struct EventRow {qint64 original=0,aligned=0;QString code,state,source,kind;int row=0;QString description;};
struct Imported {QVector<EventRow> events;QStringList errors;};
inline Imported importEvents(const Csv& csv,const ImportOptions& o,QString source,QString kind,
                             const std::atomic_bool* cancel=nullptr){
  if(o.timeColumn<0||o.codeColumn<0||o.timeColumn>=csv.headers.size()||o.codeColumn>=csv.headers.size()||
     o.stateColumn< -1||o.stateColumn>=csv.headers.size()||o.descriptionColumn< -1||o.descriptionColumn>=csv.headers.size()||o.timeColumn==o.codeColumn)throw std::runtime_error("时间和代码列必须分别指定");
  Imported out;out.errors=csv.errors;int index=0;int row=1;
  for(const auto& values:csv.rows){
    if((index%8192)==0&&cancel&&cancel->load())throw std::runtime_error("已取消");row=csv.recordNumbers.value(index,index+2);++index;
    try{
      const auto code=values[o.codeColumn].trimmed();if(code.isEmpty())throw std::runtime_error("故障/动作代码为空");
      const auto t=parseTime(values[o.timeColumn],o);
      out.events.push_back({t,t,code,o.stateColumn>=0?values[o.stateColumn]:QStringLiteral("未提供"),source,kind,row,o.descriptionColumn>=0?values[o.descriptionColumn]:QString()});
    }catch(const std::exception& e){out.errors<<QString("非空记录 %1：%2").arg(row).arg(QString::fromUtf8(e.what()));}
  }
  std::stable_sort(out.events.begin(),out.events.end(),[](const EventRow& a,const EventRow& b){return a.original<b.original;});
  return out;
}
inline QString iso(qint64 t){return QDateTime::fromMSecsSinceEpoch(t,Qt::UTC).toString("yyyy-MM-dd HH:mm:ss.zzz");}
inline QString csvCell(QString text){text.replace('"',"\"\"");return '"'+text+'"';}
}

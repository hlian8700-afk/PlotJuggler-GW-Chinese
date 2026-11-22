#pragma once
#include <QBuffer>
#include <cmath>
#include <stdexcept>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

namespace BladeFitUI
{
struct CurveSheet
{
  QVector<QStringList> rows;
  int header = -1;
  int wind = -1;
  int power = -1;
};

inline QByteArray sheetPart(QZipReader& zip, const QString& path)
{
  for (const auto& entry : zip.fileInfoList())
    if (entry.isFile && entry.filePath == path && entry.size <= 8 * 1024 * 1024)
      return zip.fileData(path);
  return {};
}

inline QVector<QVector<QStringList>> readWorkbookRows(const QString& path, int columnCount)
{
  QZipReader zip(path);
  QStringList shared;
  QXmlStreamReader strings(sheetPart(zip, "xl/sharedStrings.xml"));
  QString value;
  while (!strings.atEnd()) {
    strings.readNext();
    if (strings.isStartElement() && strings.name() == "si") value.clear();
    if (strings.isStartElement() && strings.name() == "t") value += strings.readElementText();
    if (strings.isEndElement() && strings.name() == "si") shared.append(value);
  }
  const QRegularExpression worksheet("^xl/worksheets/sheet[0-9]+[.]xml$");
  const QRegularExpression address("^([A-Z]{1,3})[0-9]+$");
  QVector<QVector<QStringList>> sheets;
  for (const auto& entry : zip.fileInfoList()) {
    if (!worksheet.match(entry.filePath).hasMatch()) continue;
    QVector<QStringList> rows;
    QXmlStreamReader xml(sheetPart(zip, entry.filePath));
    while (!xml.atEnd() && rows.size() < 10000) {
      xml.readNext();
      if (!xml.isStartElement() || xml.name() != "row") continue;
      QStringList row;
      for(int i=0;i<columnCount;++i) row.append(QString());
      while (xml.readNextStartElement()) {
        if (xml.name() != "c") { xml.skipCurrentElement(); continue; }
        const auto cell = address.match(xml.attributes().value("r").toString());
        if (!cell.hasMatch()) { xml.skipCurrentElement(); continue; }
        int column=0;
        for(QChar letter:cell.captured(1)) column=26*column+letter.unicode()-'A'+1;
        if(column>columnCount){xml.skipCurrentElement(); continue;}
        const auto type = xml.attributes().value("t").toString();
        QString text;
        while (!xml.atEnd()) {
          xml.readNext();
          if (xml.isEndElement() && xml.name() == "c") break;
          if (xml.isStartElement() && (xml.name() == "v" || xml.name() == "t"))
            text += xml.readElementText();
        }
        if (type == "s") {
          bool ok = false;
          const int index = text.toInt(&ok);
          text = ok ? shared.value(index) : QString();
        }
        row[column-1] = text.trimmed();
      }
      rows.append(row);
    }
    if (!xml.hasError() && xml.atEnd() && !rows.isEmpty()) sheets.append(rows);
  }
  return sheets;
}

inline CurveSheet readCurveSheet(const QString& path)
{
  const QRegularExpression spaces("\\s+");
  for (const auto& rows:readWorkbookRows(path,4)) {
    for (int i=0;i<rows.size();++i) {
      int wind=-1,power=-1;
      for (int col=0;col<rows[i].size();++col) {
        const auto label=QString(rows[i][col]).toLower().remove(spaces);
        if(label=="wind(m/s)" || label==QStringLiteral("风速(m/s)")) wind=col;
        if(label=="power(kw)" || label==QStringLiteral("功率(kw)")) power=col;
      }
      if(wind>=0 && power>=0) return {rows,i,wind,power};
    }
  }
  return {};
}

inline QByteArray writeCurveSheet(const CurveSheet& sheet)
{
  QByteArray xmlBytes;
  QXmlStreamWriter xml(&xmlBytes);
  xml.writeStartDocument();
  xml.writeStartElement("worksheet");
  xml.writeDefaultNamespace("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
  xml.writeStartElement("sheetData");
  for (int r=0; r<sheet.rows.size(); ++r) {
    xml.writeStartElement("row");
    xml.writeAttribute("r", QString::number(r+1));
    const auto& row = sheet.rows[r];
    for (int c=0; c<row.size(); ++c) {
      if (row[c].isEmpty()) continue;
      xml.writeStartElement("c");
      xml.writeAttribute("r", QString(QChar(ushort('A'+c)))+QString::number(r+1));
      bool numeric = false;
      const double number = row[c].toDouble(&numeric);
      if (r>sheet.header && numeric && std::isfinite(number))
        xml.writeTextElement("v", row[c]);
      else {
        xml.writeAttribute("t", "inlineStr");
        xml.writeStartElement("is");
        xml.writeTextElement("t", row[c]);
        xml.writeEndElement();
      }
      xml.writeEndElement();
    }
    xml.writeEndElement();
  }
  xml.writeEndDocument();
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QZipWriter zip(&buffer);
  zip.addFile("[Content_Types].xml", QByteArray(R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>)"));
  zip.addFile("_rels/.rels", QByteArray(R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>)"));
  zip.addFile("xl/workbook.xml", QByteArray(R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Power Curve" sheetId="1" r:id="rId1"/></sheets></workbook>)"));
  zip.addFile("xl/_rels/workbook.xml.rels", QByteArray(R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"));
  zip.addFile("xl/worksheets/sheet1.xml", xmlBytes);
  zip.close();
  return zip.status() == QZipWriter::NoError ? bytes : QByteArray();
}

inline QJsonArray curvePoints(const CurveSheet& sheet)
{
  if (sheet.header < 0) throw std::runtime_error("找不到 Wind(m/s) 与 Power (kW) 表头");
  QJsonArray points;
  double previous = -1;
  for (int i=sheet.header+1; i<sheet.rows.size(); ++i) {
    const auto& row = sheet.rows[i];
    const auto wind = row.value(sheet.wind), power = row.value(sheet.power);
    if (wind.isEmpty() && power.isEmpty()) continue;
    bool windOk = false, powerOk = false;
    const double x = wind.toDouble(&windOk), y = power.toDouble(&powerOk);
    if (!windOk || !powerOk || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x <= previous)
      throw std::runtime_error("功率曲线含无效数据、重复风速或风速未严格递增");
    points.append(QJsonArray{x,y});
    previous = x;
  }
  if (points.size()<2) throw std::runtime_error("标准功率曲线至少需要两个数据点");
  return points;
}
}

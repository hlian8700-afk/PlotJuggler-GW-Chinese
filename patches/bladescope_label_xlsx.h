#pragma once
#include "bladescope_fit_xlsx.h"
#include <QJsonObject>

namespace BladeLabels
{
inline QJsonObject readWorkbook(const QString& path)
{
  QJsonObject result;
  for (const auto& rows : BladeFitUI::readWorkbookRows(path, 64)) {
    for (int i=0; i<rows.size(); ++i) {
      int raw=-1, chinese=-1, system=-1;
      for (int col=0; col<rows[i].size(); ++col) {
        const auto key=rows[i][col].trimmed().toLower();
        if (QStringList{"raw_tag","原始字段","测点编码","点名"}.contains(key)) raw=col;
        if (QStringList{"cn_name","中文含义","中文名称","中文字段"}.contains(key)) chinese=col;
        if (QStringList{"system","系统","系统分类","所属系统"}.contains(key)) system=col;
      }
      if (raw<0 || chinese<0) continue;
      for (int row=i+1; row<rows.size(); ++row) {
        const auto tag=rows[row][raw], name=rows[row][chinese];
        if (!tag.isEmpty() && !name.isEmpty()) {
          if (result.contains(tag))
            throw std::runtime_error((QStringLiteral("点表包含重复原始字段：")+tag).toStdString());
          result.insert(tag,QJsonArray{name,rows[row].value(system)});
        }
      }
      break;
    }
  }
  return result;
}
}

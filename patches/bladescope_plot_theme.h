#pragma once

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QVector>
#include <QtMath>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_draw.h>
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace BladePlotTheme
{
enum Mode { Solid, Linear, Mirror };
enum Ink { AxisLine, AxisText, AxisTick };

struct Stop
{
  double position;
  QColor color;
};

struct Theme
{
  Mode mode = Solid;
  QColor solid = Qt::white;
  double angle = 90;
  QVector<Stop> stops = { { 0, QColor("#121c31") }, { 1, QColor("#314960") } };
  bool verticalMirror = false;
  QColor center = QColor("#fafdff");
  QColor edge = QColor("#b8cde2");
  double spread = 1;
  std::array<std::array<QColor, 3>, 2> axes = {{
    {{ QColor("#495a6b"), QColor("#263647"), QColor("#495a6b") }},
    {{ QColor("#495a6b"), QColor("#263647"), QColor("#495a6b") }}
  }};
  std::array<bool, 2> grid = {{ true, true }};
  std::array<QColor, 2> gridColor = {{ QColor("#805a738a"), QColor("#805a738a") }};
};

inline Theme preset(int index)
{
  Theme t;
  if (index == 1)
  {
    t.mode = Linear;
    for (auto& axis : t.axes)
      axis = {{ QColor("#9cb0c8"), QColor("#edf4ff"), QColor("#9cb0c8") }};
    t.gridColor = {{ QColor("#669ab5d1"), QColor("#669ab5d1") }};
  }
  if (index == 2)
    t.mode = Mirror;
  return t;
}

inline QJsonObject encode(const Theme& t)
{
  QJsonArray stops, axes, grid;
  for (const auto& stop : t.stops)
    stops.append(QJsonObject{{ "position", stop.position }, { "color", stop.color.name(QColor::HexArgb) }});
  for (const auto& axis : t.axes)
    axes.append(QJsonArray{ axis[0].name(QColor::HexArgb), axis[1].name(QColor::HexArgb),
                            axis[2].name(QColor::HexArgb) });
  for (int i = 0; i < 2; ++i)
    grid.append(QJsonObject{{ "enabled", t.grid[i] }, { "color", t.gridColor[i].name(QColor::HexArgb) }});
  return {{ "version", 1 }, { "mode", int(t.mode) }, { "solid", t.solid.name(QColor::HexArgb) },
          { "angle", t.angle }, { "stops", stops }, { "verticalMirror", t.verticalMirror },
          { "center", t.center.name(QColor::HexArgb) }, { "edge", t.edge.name(QColor::HexArgb) },
          { "spread", t.spread }, { "axes", axes }, { "grid", grid }};
}

inline QColor readColor(QJsonValue value, QColor fallback)
{
  const QColor color(value.toString());
  return color.isValid() ? color : fallback;
}

inline bool decode(const QByteArray& bytes, Theme& result)
{
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return false;
  const auto o = doc.object();
  // Version 0 may omit fields or use RGB colors. Keep defaults for missing fields.
  if ((!o.contains("version") && !o.contains("mode") && !o.contains("solid")) ||
      (o.contains("version") && (!o.value("version").isDouble() ||
       o.value("version").toDouble() != o.value("version").toInt())) ||
      o.value("version").toInt() < 0 || o.value("version").toInt() > 1)
    return false;
  Theme t;
  t.mode = Mode(qBound(0, o.value("mode").toInt(), 2));
  t.solid = readColor(o.value("solid"), t.solid);
  t.angle = qBound(0.0, o.value("angle").toDouble(t.angle), 360.0);
  t.verticalMirror = o.value("verticalMirror").toBool(false);
  t.center = readColor(o.value("center"), t.center);
  t.edge = readColor(o.value("edge"), t.edge);
  t.spread = qBound(0.0, o.value("spread").toDouble(t.spread), 1.0);
  QVector<Stop> stops;
  for (const auto entry : o.value("stops").toArray())
  {
    const auto stop = entry.toObject();
    const QColor color = readColor(stop.value("color"), QColor());
    if (color.isValid() && stop.value("position").isDouble())
      stops.push_back({qBound(0.0, stop.value("position").toDouble(), 1.0), color});
  }
  if (stops.size() >= 2)
    t.stops = stops;
  const auto axes = o.value("axes").toArray();
  const auto grid = o.value("grid").toArray();
  for (int i = 0; i < 2; ++i)
  {
    if (i < axes.size())
    {
      const auto pens = axes.at(i).toArray();
      for (int j = 0; j < 3 && j < pens.size(); ++j)
        t.axes[i][j] = readColor(pens.at(j), t.axes[i][j]);
    }
    if (i < grid.size())
    {
      const auto g = grid.at(i).toObject();
      t.grid[i] = g.value("enabled").toBool(t.grid[i]);
      t.gridColor[i] = readColor(g.value("color"), t.gridColor[i]);
    }
  }
  result = t;
  return true;
}

inline bool save(const QString& path, const Theme& theme)
{
  const auto bytes = QJsonDocument(encode(theme)).toJson();
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

inline QString settingsPath()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/plot-theme.json";
}

inline Theme& current()
{
  static Theme theme = [] {
    Theme t;
    const bool oldGrid = QSettings().value("MainWindow.activateGrid", true).toBool();
    t.grid = {{ oldGrid, oldGrid }};
    QFile file(settingsPath());
    if (file.open(QIODevice::ReadOnly))
      decode(file.readAll(), t);
    return t;
  }();
  return theme;
}

inline void remember()
{
  const QString path = settingsPath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  if (!save(path, current()))
    qWarning("BladeScope: could not save the current plot theme");
}

inline void scheduleSave()
{
  static auto* timer = [] {
    auto* t = new QTimer(qApp);
    t->setSingleShot(true);
    QObject::connect(t, &QTimer::timeout, qApp, remember);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, t, [t] {
      if (t->isActive())
        remember();
    });
    return t;
  }();
  timer->start(250);
}

inline QBrush backgroundBrush(const Theme& t, const QSizeF& size)
{
  if (t.mode == Solid)
    return QBrush(t.solid);
  if (t.mode == Mirror)
  {
    QLinearGradient gradient(QPointF(), t.verticalMirror ? QPointF(0, size.height())
                                                         : QPointF(size.width(), 0));
    if (t.spread <= 0)
      return QBrush(t.edge);
    const double start = (1 - t.spread) / 2;
    gradient.setColorAt(start, t.edge);
    gradient.setColorAt(0.5, t.center);
    gradient.setColorAt(1 - start, t.edge);
    return QBrush(gradient);
  }

  const double radians = std::fmod(t.angle, 360.0) * std::acos(-1.0) / 180.0;
  const QPointF direction(std::cos(radians), std::sin(radians));
  const QPointF center(size.width() / 2, size.height() / 2);
  const double extent = std::abs(direction.x()) * center.x() + std::abs(direction.y()) * center.y();
  QLinearGradient gradient(center - direction * extent, center + direction * extent);
  for (const auto& stop : t.stops)
    gradient.setColorAt(stop.position, stop.color);
  return QBrush(gradient);
}

inline QJsonObject activeBackground(const Theme& theme)
{
  auto json = encode(theme);
  const QStringList keys = { "solid", "angle", "stops", "center", "edge", "spread", "verticalMirror" };
  const int first = theme.mode == Solid ? 0 : theme.mode == Linear ? 1 : 3;
  const int last = theme.mode == Solid ? 1 : theme.mode == Linear ? 3 : keys.size();
  for (int i = 0; i < keys.size(); ++i)
    if (i < first || i >= last)
      json.remove(keys[i]);
  json.remove("axes");
  json.remove("grid");
  return json;
}

inline quint64& backgroundRevision()
{
  static quint64 revision = 0;
  return revision;
}

class Background final : public QwtPlotItem
{
public:
  Background() : QwtPlotItem(QString())
  {
    setZ(-100);
    setItemAttribute(QwtPlotItem::Legend, false);
    setItemAttribute(QwtPlotItem::AutoScale, false);
  }

  void draw(QPainter* painter, const QwtScaleMap&, const QwtScaleMap&,
            const QRectF& rect) const override
  {
    if (rect.isEmpty())
      return;
    const auto transform = painter->deviceTransform();
    const double ratio = qMax(std::hypot(transform.m11(), transform.m12()),
                              std::hypot(transform.m21(), transform.m22()));
    const QSize pixels(qMax(1, qCeil(rect.width() * ratio)),
                       qMax(1, qCeil(rect.height() * ratio)));
    const auto revision = backgroundRevision();
    const bool screen = painter->device()->devType() == QInternal::Widget ||
                        painter->device()->devType() == QInternal::Pixmap;
    // Keep one cache for interactive paints and one for different export resolutions.
    auto& cache = screen && plot() && qAbs(rect.width() - plot()->canvas()->width()) < 3 &&
                  qAbs(rect.height() - plot()->canvas()->height()) < 3 ? screenCache : exportCache;
    if (cache.pixmap.size() != pixels || cache.size != rect.size() || cache.revision != revision)
    {
      cache.pixmap = QPixmap(pixels);
      cache.pixmap.fill(Qt::transparent);
      QPainter fill(&cache.pixmap);
      fill.scale(pixels.width() / rect.width(), pixels.height() / rect.height());
      fill.fillRect(QRectF(QPointF(), rect.size()), backgroundBrush(current(), rect.size()));
      cache.size = rect.size();
      cache.revision = revision;
    }
    painter->drawPixmap(rect, cache.pixmap, QRectF(cache.pixmap.rect()));
  }

private:
  struct Cache
  {
    QPixmap pixmap;
    QSizeF size;
    quint64 revision = 0;
  };
  mutable Cache screenCache, exportCache;
};

class Grid final : public QwtPlotGrid
{
public:
  Grid()
  {
    setZ(-90);
    horizontal.enableX(false);
    horizontal.enableY(true);
    horizontal.enableYMin(true);
    vertical.enableX(true);
    vertical.enableXMin(true);
    vertical.enableY(false);
  }

  void updateScaleDiv(const QwtScaleDiv& x, const QwtScaleDiv& y) override
  {
    horizontal.updateScaleDiv(x, y);
    vertical.updateScaleDiv(x, y);
  }

  void refresh()
  {
    horizontal.setPen(QPen(current().gridColor[0], 0, Qt::DotLine));
    vertical.setPen(QPen(current().gridColor[1], 0, Qt::DotLine));
  }

  void draw(QPainter* painter, const QwtScaleMap& x, const QwtScaleMap& y,
            const QRectF& rect) const override
  {
    if (current().grid[0])
      horizontal.draw(painter, x, y, rect);
    if (current().grid[1])
      vertical.draw(painter, x, y, rect);
  }

private:
  QwtPlotGrid horizontal, vertical;
};

class ScaleDraw : public QwtScaleDraw
{
public:
  explicit ScaleDraw(int axis = 0) : axis(axis) {}

  void draw(QPainter* painter, const QPalette&) const override
  {
    painter->save();
    QPen pen = painter->pen();
    pen.setWidthF(penWidthF());
    pen.setCapStyle(Qt::FlatCap);
    const auto& colors = current().axes[axis];
    if (hasComponent(Backbone))
    {
      pen.setColor(colors[AxisLine]);
      painter->setPen(pen);
      drawBackbone(painter);
    }
    const auto& division = scaleDiv();
    if (hasComponent(Ticks))
    {
      pen.setColor(colors[AxisTick]);
      painter->setPen(pen);
      for (int i = QwtScaleDiv::MinorTick; i < QwtScaleDiv::NTickTypes; ++i)
      {
        const auto type = QwtScaleDiv::TickType(i);
        const double length = tickLength(type);
        if (length <= 0)
          continue;
        for (double value : division.ticks(type))
          if (division.contains(value))
            drawTick(painter, value, length);
      }
    }
    if (hasComponent(Labels))
    {
      painter->setPen(colors[AxisText]);
      for (double value : division.ticks(QwtScaleDiv::MajorTick))
        if (division.contains(value))
          drawLabel(painter, value);
    }
    painter->restore();
  }

private:
  int axis;
};

inline void refresh(QwtPlot* plot)
{
  const auto items = plot->itemList();
  for (auto* item : items)
  {
    if (auto* grid = dynamic_cast<Grid*>(item))
      grid->refresh();
    if (auto* legend = dynamic_cast<QwtPlotLegendItem*>(item))
    {
      legend->setTextPen(QPen(current().axes[0][AxisText]));
      legend->setBackgroundBrush(QBrush(Qt::NoBrush));
      legend->setBorderPen(QPen(Qt::NoPen));
      legend->setZ(1000);
    }
  }
  for (int axis : {QwtPlot::xBottom, QwtPlot::xTop, QwtPlot::yLeft, QwtPlot::yRight})
    plot->axisWidget(axis)->update();
  plot->replot();
}

inline void refreshAll()
{
  for (auto* widget : QApplication::allWidgets())
    if (widget->property("bladeScopePlotTheme").toBool())
      refresh(static_cast<QwtPlot*>(widget));
}

inline void install(QwtPlot* plot, Grid* grid)
{
  auto* canvas = plot->canvas();
  canvas->setStyleSheet("#qwtCanvas { background: transparent; border: none; }");
  canvas->setAutoFillBackground(false);
  if (auto* raster = qobject_cast<QwtPlotCanvas*>(canvas))
  {
    raster->setPaintAttribute(QwtPlotCanvas::Opaque, false);
    raster->setPaintAttribute(QwtPlotCanvas::BackingStore, false);
    raster->setAttribute(Qt::WA_OpaquePaintEvent, false);
    raster->setFrameStyle(QFrame::NoFrame);
  }
  plot->setCanvasBackground(QBrush(Qt::transparent));
  auto* background = new Background;
  background->attach(plot);
  grid->attach(plot);
  for (int axis : {QwtPlot::xBottom, QwtPlot::xTop, QwtPlot::yLeft, QwtPlot::yRight})
  {
    const bool y = axis == QwtPlot::yLeft || axis == QwtPlot::yRight;
    plot->setAxisScaleDraw(axis, new ScaleDraw(y ? 1 : 0));
    plot->axisWidget(axis)->setStyleSheet("QwtScaleWidget { background: transparent; }");
  }
  plot->setProperty("bladeScopePlotTheme", true);
  refresh(plot);
}

inline void apply(const Theme& theme)
{
  if (activeBackground(current()) != activeBackground(theme))
    ++backgroundRevision();
  current() = theme;
  refreshAll();
  scheduleSave();
}

class StopStrip final : public QWidget
{
public:
  explicit StopStrip(QWidget* parent) : QWidget(parent)
  {
    setMinimumSize(220, 52);
    setAccessibleName(QString::fromUtf8("拖动渐变色标"));
  }

  int selected = 0;
  std::function<void(int)> select;
  std::function<void(double)> moveStop;

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);
    const QRectF bar(10, 3, width() - 20, 25);
    painter.save();
    painter.setClipRect(bar);
    for (int y = 3; y < 28; y += 6)
      for (int x = 10; x < width() - 10; x += 6)
        painter.fillRect(x, y, 6, 6, ((x - 10 + y - 3) / 6) % 2 ? Qt::white : Qt::lightGray);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    const auto& stops = current().stops;
    for (const auto& stop : stops)
      gradient.setColorAt(stop.position, stop.color);
    painter.fillRect(bar, gradient);
    painter.restore();

    for (int i = 0; i < stops.size(); ++i)
    {
      const int x = qRound(10 + stops[i].position * (width() - 20));
      painter.setPen(QPen(palette().highlight().color(), i == selected ? 3 : 1));
      painter.setBrush(stops[i].color);
      painter.drawRect(x - 5, 34, 10, 13);
    }
  }

  void mousePressEvent(QMouseEvent* event) override
  {
    if (event->button() != Qt::LeftButton)
      return;
    const double p = position(event->x());
    const auto& stops = current().stops;
    int nearest = selected;
    for (int i = 0; i < stops.size(); ++i)
      if (std::abs(stops[i].position - p) < std::abs(stops[nearest].position - p))
        nearest = i;
    select(nearest);
  }

  void mouseMoveEvent(QMouseEvent* event) override
  {
    if (event->buttons().testFlag(Qt::LeftButton))
      moveStop(position(event->x()));
  }

private:
  double position(int x) const { return qBound(0.0, (x - 10.0) / (width() - 20.0), 1.0); }
};

class Panel final : public QDialog
{
public:
  explicit Panel(QWidget* parent) : QDialog(parent)
  {
    setWindowTitle(QString::fromUtf8("绘图主题调色盘"));
    auto* layout = new QVBoxLayout(this);
    auto* presets = new QHBoxLayout;
    const QStringList names = {QString::fromUtf8("办公纯色白底"), QString::fromUtf8("深色线性渐变"),
                               QString::fromUtf8("中心聚焦镜像渐变")};
    for (int i = 0; i < names.size(); ++i)
      button(presets, names.at(i), [this, i] { load(preset(i)); });
    layout->addLayout(presets);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll);
    auto* body = new QWidget;
    scroll->setWidget(body);
    auto* form = new QFormLayout(body);
    auto* modes = new QComboBox;
    modes->addItems({QString::fromUtf8("纯色"), QString::fromUtf8("线性渐变"),
                     QString::fromUtf8("镜像对称渐变")});
    form->addRow(QString::fromUtf8("绘图底色"), modes);
    mode = modes;

    for (auto& page : pages)
    {
      page = new QWidget;
      form->addRow(page);
    }
    auto* solid = new QFormLayout(pages[Solid]);
    color(solid, QString::fromUtf8("底色"), [](Theme& t) -> QColor& { return t.solid; });

    auto* linear = new QFormLayout(pages[Linear]);
    number(linear, QString::fromUtf8("角度（°）"), 360, 1,
           [](Theme& t) -> double& { return t.angle; });
    strip = new StopStrip(pages[Linear]);
    linear->addRow(strip);
    stops = new QComboBox;
    linear->addRow(QString::fromUtf8("色标"), stops);
    stopPosition = number(linear, QString::fromUtf8("色标位置"), 1, 3,
                          [this](Theme& t) -> double& { return t.stops[strip->selected].position; });
    color(linear, QString::fromUtf8("色标颜色"),
          [this](Theme& t) -> QColor& { return t.stops[strip->selected].color; });
    strip->select = [this](int i) { stops->setCurrentIndex(i); };
    strip->moveStop = [this](double p) { stopPosition->setValue(p); };
    connect(stops, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
      if (i >= 0)
      {
        strip->selected = i;
        sync();
      }
    });
    auto* stopButtons = new QHBoxLayout;
    button(stopButtons, QString::fromUtf8("增加色标"), [this] {
      Theme t = current();
      QVector<double> positions{ 0, 1 };
      for (const auto& stop : t.stops)
        positions.push_back(stop.position);
      std::sort(positions.begin(), positions.end());
      double gap = 0;
      double position = 0.5;
      for (int i = 1; i < positions.size(); ++i)
      {
        const double width = positions[i] - positions[i - 1];
        if (width > gap)
        {
          gap = width;
          position = (positions[i] + positions[i - 1]) / 2;
        }
      }
      t.stops.push_back({ position, t.stops[strip->selected].color });
      strip->selected = t.stops.size() - 1;
      load(t, false);
    });
    removeStop = button(stopButtons, QString::fromUtf8("删除色标"), [this] {
      Theme t = current();
      if (t.stops.size() <= 2)
        return;
      t.stops.removeAt(strip->selected);
      strip->selected = qMin(strip->selected, t.stops.size() - 1);
      load(t, false);
    });
    linear->addRow(stopButtons);

    auto* mirror = new QFormLayout(pages[Mirror]);
    auto* orientation = new QComboBox;
    orientation->addItems({QString::fromUtf8("水平镜像（左右对称）"),
                           QString::fromUtf8("垂直镜像（上下对称）")});
    mirror->addRow(QString::fromUtf8("镜像方向"), orientation);
    readers.push_back([orientation] {
      const QSignalBlocker blocked(orientation);
      orientation->setCurrentIndex(current().verticalMirror ? 1 : 0);
    });
    connect(orientation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
      edit([i](Theme& t) { t.verticalMirror = i == 1; });
    });
    color(mirror, QString::fromUtf8("中心色"), [](Theme& t) -> QColor& { return t.center; });
    color(mirror, QString::fromUtf8("边缘色"), [](Theme& t) -> QColor& { return t.edge; });
    number(mirror, QString::fromUtf8("渐变扩散比例"), 1, 2,
           [](Theme& t) -> double& { return t.spread; });

    const QStringList axes = {QString::fromUtf8("X 轴"), QString::fromUtf8("Y 轴")};
    const QStringList inks = {QString::fromUtf8("轴线"), QString::fromUtf8("刻度文字"),
                              QString::fromUtf8("刻度短线")};
    for (int i = 0; i < axes.size(); ++i)
      for (int j = 0; j < inks.size(); ++j)
        color(form, axes.at(i) + ' ' + inks.at(j),
              [i, j](Theme& t) -> QColor& { return t.axes[i][j]; });

    const QStringList grids = {QString::fromUtf8("横向网格"), QString::fromUtf8("纵向网格")};
    for (int i = 0; i < grids.size(); ++i)
    {
      auto* enabled = new QCheckBox(QString::fromUtf8("显示"));
      form->addRow(grids.at(i), enabled);
      readers.push_back([enabled, i] {
        const QSignalBlocker blocked(enabled);
        enabled->setChecked(current().grid[i]);
      });
      connect(enabled, &QCheckBox::toggled, this, [this, i](bool on) {
        edit([i, on](Theme& t) { t.grid[i] = on; });
      });
      color(form, grids.at(i) + QString::fromUtf8("颜色"),
            [i](Theme& t) -> QColor& { return t.gridColor[i]; });
    }

    auto* actions = new QHBoxLayout;
    button(actions, QString::fromUtf8("保存主题"), [this] {
      QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8("保存主题"), {}, "JSON (*.json)");
      if (path.isEmpty())
        return;
      if (QFileInfo(path).suffix().isEmpty())
        path += ".json";
      if (!save(path, current()))
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8("无法保存主题文件。"));
    });
    button(actions, QString::fromUtf8("加载主题"), [this] {
      const auto path = QFileDialog::getOpenFileName(this, QString::fromUtf8("加载主题"), {}, "JSON (*.json)");
      if (path.isEmpty())
        return;
      QFile file(path);
      Theme theme;
      if (file.open(QIODevice::ReadOnly) && decode(file.readAll(), theme))
        load(theme);
      else
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8("主题文件无法读取，或版本不受支持。"));
    });
    button(actions, QString::fromUtf8("恢复默认"), [this] { load(preset(0)); });
    button(actions, QString::fromUtf8("关闭"), [this] { close(); });
    layout->addLayout(actions);
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int m) {
      edit([m](Theme& t) { t.mode = Mode(m); });
    });
    resize(570, 635);
    sync();
  }

private:
  QPushButton* button(QHBoxLayout* layout, const QString& label, std::function<void()> click)
  {
    auto* b = new QPushButton(label);
    layout->addWidget(b);
    connect(b, &QPushButton::clicked, this, std::move(click));
    return b;
  }

  QDoubleSpinBox* number(QFormLayout* form, const QString& label, double maximum, int decimals,
                         std::function<double&(Theme&)> get)
  {
    auto* input = spin(maximum, decimals);
    form->addRow(label, input);
    readers.push_back([input, get] {
      const QSignalBlocker blocked(input);
      input->setValue(get(current()));
    });
    connect(input, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, get](double v) {
      edit([get, v](Theme& t) { get(t) = v; });
    });
    return input;
  }

  QDoubleSpinBox* spin(double max, int decimals)
  {
    auto* input = new QDoubleSpinBox;
    input->setRange(0, max);
    input->setDecimals(decimals);
    input->setSingleStep(max > 1 ? 1 : 0.01);
    input->setKeyboardTracking(true);
    return input;
  }

  void color(QFormLayout* form, const QString& label, std::function<QColor&(Theme&)> get)
  {
    auto* row = new QHBoxLayout;
    auto* swatch = button(row, {}, [this, get] {
      const QColor original = get(current());
      QColorDialog dialog(original, this);
      dialog.setOption(QColorDialog::ShowAlphaChannel);
      dialog.setOption(QColorDialog::DontUseNativeDialog);
      connect(&dialog, &QColorDialog::currentColorChanged, this, [this, get](const QColor& c) {
        if (c.isValid())
          edit([get, c](Theme& t) { get(t) = c; });
      });
      if (dialog.exec() != QDialog::Accepted)
        edit([get, original](Theme& t) { get(t) = original; });
    });
    auto* opacity = spin(1, 3);
    opacity->setAccessibleName(label + QString::fromUtf8("透明度"));
    row->addWidget(new QLabel(QString::fromUtf8("透明度")));
    row->addWidget(opacity);
    form->addRow(label, row);
    readers.push_back([swatch, opacity, get] {
      const QColor c = get(current());
      swatch->setText(c.name(QColor::HexRgb).toUpper());
      swatch->setStyleSheet(QString("QPushButton { border-left: 12px solid %1; }")
                                .arg(c.name(QColor::HexRgb)));
      const QSignalBlocker blocked(opacity);
      opacity->setValue(c.alphaF());
    });
    connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, get](double a) {
      edit([get, a](Theme& t) { get(t).setAlphaF(a); });
    });
  }

  void edit(const std::function<void(Theme&)>& change)
  {
    Theme theme = current();
    change(theme);
    load(theme, false);
  }

  void load(const Theme& theme, bool resetSelection = true)
  {
    if (resetSelection)
      strip->selected = 0;
    apply(theme);
    sync();
  }

  void sync()
  {
    const QSignalBlocker modeBlocked(mode);
    const QSignalBlocker stopsBlocked(stops);
    mode->setCurrentIndex(current().mode);
    for (int i = 0; i < int(pages.size()); ++i)
      pages[i]->setVisible(i == current().mode);
    if (stops->count() != current().stops.size())
    {
      stops->clear();
      for (int i = 0; i < current().stops.size(); ++i)
        stops->addItem(QString::fromUtf8("色标 %1").arg(i + 1));
    }
    stops->setCurrentIndex(strip->selected);
    removeStop->setEnabled(current().stops.size() > 2);
    for (const auto& update : readers)
      update();
    strip->update();
  }

  QComboBox* mode = nullptr;
  QComboBox* stops = nullptr;
  StopStrip* strip = nullptr;
  QDoubleSpinBox* stopPosition = nullptr;
  QPushButton* removeStop = nullptr;
  std::array<QWidget*, 3> pages = {};
  QVector<std::function<void()>> readers;
};

inline void openPanel(QWidget* parent)
{
  Panel panel(parent);
  panel.exec();
}
}

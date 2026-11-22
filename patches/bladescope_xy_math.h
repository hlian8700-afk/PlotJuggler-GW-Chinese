#pragma once
#include "bladescope_fit_math.h"

namespace BladeXY
{
using BladeFit::Point;
using BladeFit::Points;

struct Pair { double time, x, y; };

inline std::vector<Pair> pair(const Points& x, const Points& y, double tolerance)
{
  std::vector<Pair> result;
  result.reserve(x.size());
  size_t right = 0;
  for (auto a : x)
  {
    if (!std::isfinite(a.x) || !std::isfinite(a.y)) continue;
    while (right < y.size() && y[right].x < a.x) ++right;
    size_t match = right;
    if (right && (right == y.size() || a.x - y[right - 1].x <= y[right].x - a.x))
      match = right - 1;
    if (match < y.size() && std::abs(y[match].x - a.x) <= tolerance && std::isfinite(y[match].y))
      result.push_back({a.x, a.y, y[match].y});
  }
  return result;
}

struct Result
{
  std::vector<Pair> observed;
  Points fitted;
  BladeFit::Polynomial polynomial{};
  double maximum = 0, mean = 0, deviation = 0;
};

inline Result analyze(const Points& x, const Points& y, double tolerance, int degree, size_t neighbors)
{
  Result result;
  result.observed = pair(x, y, tolerance);
  if (result.observed.empty()) throw std::runtime_error("没有符合时间容差的有效配对");
  size_t count = 0;
  long double mean = 0, m2 = 0;
  result.maximum = -std::numeric_limits<double>::infinity();
  Points data;
  data.reserve(result.observed.size());
  for (auto row : result.observed)
  {
    data.push_back({row.x, row.y});
    const auto difference = static_cast<long double>(row.y) - row.x;
    const auto delta = difference - mean;
    mean += delta / ++count;
    m2 += delta * (difference - mean);
    result.maximum = std::max(result.maximum, double(difference));
  }
  result.mean = double(mean);
  result.deviation = std::sqrt(double(std::max(0.L, m2 / count)));
  if (!degree && !neighbors) return result;
  std::stable_sort(data.begin(), data.end(), [](Point a, Point b) { return a.x < b.x; });
  if (degree)
  {
    result.polynomial = BladeFit::regress(data, degree);
    result.fitted = BladeFit::sample(data.front().x, data.back().x, result.polynomial);
    return result;
  }
  const size_t window = std::min(neighbors, data.size());
  result.fitted = BladeFit::sample(data.front().x, data.back().x, [&](double location) {
    const auto pivot = std::lower_bound(data.begin(), data.end(), location,
                                       [](Point point, double value) { return point.x < value; });
    size_t left = size_t(pivot - data.begin()), right = left;
    while (right - left < window)
    {
      if (left && (right == data.size() || location - data[left-1].x <= data[right].x - location)) --left;
      else ++right;
    }
    const double radius = std::max(location - data[left].x, data[right-1].x - location);
    long double weight = 0, x1 = 0, x2 = 0, y1 = 0, xy = 0;
    for (size_t i = left; i < right; ++i)
    {
      const double z = radius > 0 ? (data[i].x - location) / radius : 0;
      const double w = 1 / (1 + z * z);
      weight += w;
      x1 += w * z;
      x2 += w * z * z;
      y1 += w * data[i].y;
      xy += w * z * data[i].y;
    }
    const long double determinant = weight * x2 - x1 * x1;
    return double(determinant > 1e-12L * weight * weight ? (x2 * y1 - x1 * xy) / determinant : y1 / weight);
  });
  return result;
}
}

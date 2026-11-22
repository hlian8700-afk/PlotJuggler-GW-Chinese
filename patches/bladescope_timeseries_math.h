#pragma once
#include "bladescope_fit_math.h"

namespace BladeTimeSeries
{
using BladeFit::Point;
using BladeFit::Points;

inline Points reduce(const Points& input, size_t budget)
{
  if (input.size() <= budget) return input;
  if (budget < 4) return {};
  Points output{input.front()};
  const size_t buckets = (budget - 2) / 2;
  const size_t interior = input.size() - 2;
  for (size_t bucket = 0; bucket < buckets; ++bucket)
  {
    const auto begin = input.begin() + 1 + bucket * interior / buckets;
    const auto end = input.begin() + 1 + (bucket + 1) * interior / buckets;
    const auto extremes = std::minmax_element(begin, end, [](Point a, Point b) { return a.y < b.y; });
    output.push_back(*std::min(extremes.first, extremes.second));
    if (extremes.first != extremes.second)
      output.push_back(*std::max(extremes.first, extremes.second));
  }
  output.push_back(input.back());
  return output;
}

struct Condition
{
  size_t channel;
  double lower, upper;
};

inline const Point* nearest(const Points& series, double timestamp, double tolerance)
{
  const auto next = std::lower_bound(series.begin(), series.end(), timestamp,
                                    [](Point p, double t) { return p.x < t; });
  auto best = next;
  if (next != series.begin() &&
      (next == series.end() || timestamp - (next - 1)->x <= next->x - timestamp))
    best = next - 1;
  return best != series.end() && std::abs(best->x - timestamp) <= tolerance ? &*best : nullptr;
}

inline std::vector<Points> prepare(const std::vector<Points>& channels,
                                   const std::vector<size_t>& visible,
                                   const std::vector<Condition>& conditions, double tolerance)
{
  std::vector<Points> output;
  if (visible.empty()) return output;
  size_t total = 0;
  for (auto channel : visible) total += channels.at(channel).size();
  const size_t budget = total <= 50000 ? 50000 : 50000 / visible.size();
  for (auto channel : visible)
  {
    auto points = reduce(channels.at(channel), budget);
    for (auto& point : points)
      for (const auto& condition : conditions)
      {
        const auto* value = condition.channel == channel ? &point :
            nearest(channels.at(condition.channel), point.x, tolerance);
        if (!value || !(value->y >= condition.lower && value->y <= condition.upper))
        {
          point.y = BladeFit::missing(); // Preserve gaps for the line renderer.
          break;
        }
      }
    output.push_back(std::move(points));
  }
  return output;
}

struct Statistics
{
  size_t count = 0;
  double min, max, mean, sd, p05, p25, median, p75, p95;
};

inline Statistics statistics(std::vector<double> values)
{
  values.erase(std::remove_if(values.begin(), values.end(),
                             [](double value) { return !std::isfinite(value); }), values.end());
  if (values.empty()) return {};
  std::sort(values.begin(), values.end());
  long double mean = 0, squares = 0;
  size_t count = 0;
  for (double value : values)
  {
    const auto delta = value - mean;
    mean += delta / ++count;
    squares += delta * (value - mean);
  }
  const auto quantile = [&](double fraction) {
    const auto position = fraction * (count - 1);
    const auto index = size_t(position);
    return values[index] + (position - index) * (values[std::min(index + 1, count - 1)] - values[index]);
  };
  return {count, values.front(), values.back(), double(mean), std::sqrt(double(squares / count)),
          quantile(.05), quantile(.25), quantile(.50), quantile(.75), quantile(.95)};
}

inline BladeFit::Points exponential(const Points& points)
{
  if (points.size() < 3 || points.front().x >= points.back().x)
    throw std::runtime_error("指数拟合需要至少三个有效样本和非零时间跨度");
  const double start = points.front().x, span = points.back().x - start;
  double bestError = std::numeric_limits<double>::infinity(), bestRate = 0;
  BladeFit::Polynomial best{};
  auto evaluate = [&](double rate) {
    BladeFit::Points transformed;
    transformed.reserve(points.size());
    for (auto p : points) transformed.push_back({std::expm1(rate * ((p.x - start) / span)), p.y});
    if (transformed.front().x == transformed[1].x &&
        transformed.front().x == transformed.back().x) return;
    const auto model = BladeFit::regress(transformed, 1);
    double error = 0;
    for (auto p : transformed) error += std::pow(model(p.x) - p.y, 2);
    if (error < bestError)
    {
      bestError = error;
      bestRate = rate;
      best = model;
    }
  };
  for (int k = -32; k <= 32; ++k) if (k) evaluate(k * .5);
  for (double step : {.1, .02, .004})
  {
    const double center = bestRate;
    for (int k = -5; k <= 5; ++k)
      if (std::abs(center + k * step) >= .004) evaluate(center + k * step);
  }
  if (!std::isfinite(bestError)) throw std::runtime_error("指数拟合未收敛");
  return BladeFit::sample(start, start + span, [&](double t) {
    return best(std::expm1(bestRate * ((t - start) / span)));
  });
}
}

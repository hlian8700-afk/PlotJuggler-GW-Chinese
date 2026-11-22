#pragma once
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace BladeTimeView
{
inline constexpr std::size_t maxPoints = 50000;

// Emit source indices in time order, keeping both endpoints and bucket extrema.
template<class Value, class Emit>
void select(std::size_t first, std::size_t end, bool detail, Value value, Emit append)
{
  if (first >= end) return;
  if (detail || end - first <= maxPoints)
  {
    for (auto i = first; i < end; ++i) append(i);
    return;
  }
  append(first);
  const auto interior = end - first - 2;
  const auto buckets = (maxPoints - 2) / 2;
  const auto width = interior / buckets;
  const auto remainder = interior % buckets;
  auto begin = first + 1;
  for (std::size_t bucket = 0; bucket < buckets; ++bucket)
  {
    const auto stop = begin + width + (bucket < remainder ? 1 : 0);
    auto low = begin, high = begin;
    double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
    for (auto i = begin; i < stop; ++i)
    {
      const auto y = value(i);
      if (!std::isfinite(y)) continue;
      if (y < minimum) { minimum = y; low = i; }
      if (y > maximum) { maximum = y; high = i; }
    }
    if (low > high) std::swap(low, high);
    append(low);
    if (high != low) append(high);
    begin = stop;
  }
  append(end - 1);
}
}

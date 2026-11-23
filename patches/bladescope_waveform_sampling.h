#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace BladeWaveform
{
constexpr std::size_t overviewPointLimit = 50000;

// Keep the endpoints and each bucket's extrema in their original sample order.
template <typename ValueAt, typename Emit>
void forEachDisplayIndex(std::size_t first, std::size_t end, bool detail,
                         ValueAt valueAt, Emit append)
{
  if (first >= end) return;
  if (detail || end - first <= overviewPointLimit)
  {
    for (auto i = first; i < end; ++i) append(i);
    return;
  }

  constexpr auto bucketCount = (overviewPointLimit - 2) / 2;
  const auto interior = end - first - 2;
  const auto bucketWidth = interior / bucketCount;
  const auto widerBuckets = interior % bucketCount;
  append(first);
  auto begin = first + 1;
  for (std::size_t bucket = 0; bucket < bucketCount; ++bucket)
  {
    const auto stop = begin + bucketWidth + (bucket < widerBuckets ? 1 : 0);
    auto low = begin;
    auto high = begin;
    double minValue = 0.0, maxValue = 0.0;
    bool hasValue = false;
    for (auto i = begin; i < stop; ++i)
    {
      const double value = valueAt(i);
      if (!std::isfinite(value)) continue;
      if (!hasValue || value < minValue) { low = i; minValue = value; }
      if (!hasValue || value > maxValue) { high = i; maxValue = value; }
      hasValue = true;
    }
    append(std::min(low, high));
    if (low != high) append(std::max(low, high));
    begin = stop;
  }
  append(end - 1);
}
}

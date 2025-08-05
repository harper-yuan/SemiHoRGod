#pragma once

#include <io/netmp.h>
#include <array>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <SemiHoRGod/types.h>
struct TimePoint {
  using timepoint_t = std::chrono::high_resolution_clock::time_point;
  using timeunit_t = std::chrono::duration<double, std::milli>;

  TimePoint();
  double operator-(const TimePoint& rhs) const;

  timepoint_t time;
};

struct CommPoint {
  std::array<uint64_t, NUM_PARTIES> stats;

  explicit CommPoint(io::NetIOMP<NUM_PARTIES>& network);
  std::array<uint64_t, NUM_PARTIES> operator-(const CommPoint& rhs) const;
};

class StatsPoint {
  TimePoint tpoint_;
  CommPoint cpoint_;

 public:
  explicit StatsPoint(io::NetIOMP<NUM_PARTIES>& network);
  nlohmann::json operator-(const StatsPoint& rhs);
};

bool saveJson(const nlohmann::json& data, const std::string& fpath);
int64_t peakVirtualMemory();
int64_t peakResidentSetSize();
void initNTL(size_t num_threads);

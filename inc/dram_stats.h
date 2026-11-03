#ifndef DRAM_STATS_H
#define DRAM_STATS_H

#include <cstdint>
#include <string>

struct dram_stats {
  std::string name{};
  long dbus_cycle_congested{};
  uint64_t dbus_count_congested = 0;
  uint64_t refresh_cycles = 0;
  unsigned WQ_ROW_BUFFER_HIT = 0, WQ_ROW_BUFFER_MISS = 0, RQ_ROW_BUFFER_HIT = 0, RQ_ROW_BUFFER_MISS = 0, WQ_FULL = 0;

  // Performance-counter style events, in memory controller cycles
  uint64_t RQ_FULL = 0;                // read requests turned away because the RQ was full (they are retried)
  uint64_t cycles = 0;                 // cycles sampled for the queue occupancies
  uint64_t rq_occupancy_sum = 0;       // RQ entries in use, summed over cycles
  uint64_t wq_occupancy_sum = 0;       // WQ entries in use, summed over cycles
  uint64_t write_mode_switches = 0;    // times the channel switched between read and write mode
  uint64_t reads_completed = 0;        // reads returned over the data bus
  uint64_t total_read_latency = 0;     // from entering the RQ to the data returning, summed over those reads
};

dram_stats operator-(dram_stats lhs, dram_stats rhs);

#endif

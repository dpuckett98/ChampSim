#include "dram_stats.h"

dram_stats operator-(dram_stats lhs, dram_stats rhs)
{
  lhs.dbus_cycle_congested -= rhs.dbus_cycle_congested;
  lhs.dbus_count_congested -= rhs.dbus_count_congested;
  lhs.WQ_ROW_BUFFER_HIT -= rhs.WQ_ROW_BUFFER_HIT;
  lhs.WQ_ROW_BUFFER_MISS -= rhs.WQ_ROW_BUFFER_MISS;
  lhs.RQ_ROW_BUFFER_HIT -= rhs.RQ_ROW_BUFFER_HIT;
  lhs.RQ_ROW_BUFFER_MISS -= rhs.RQ_ROW_BUFFER_MISS;
  lhs.WQ_FULL -= rhs.WQ_FULL;
  lhs.RQ_FULL -= rhs.RQ_FULL;
  lhs.cycles -= rhs.cycles;
  lhs.rq_occupancy_sum -= rhs.rq_occupancy_sum;
  lhs.wq_occupancy_sum -= rhs.wq_occupancy_sum;
  lhs.write_mode_switches -= rhs.write_mode_switches;
  lhs.reads_completed -= rhs.reads_completed;
  lhs.total_read_latency -= rhs.total_read_latency;
  return lhs;
}

/*
 * Registers the test-only wrong-path workload source (see test/cpp/src/wp_source_mock.hpp) as "MOCK_WP_SOURCE".
 *
 * Linked only into the test binary; not part of bin/champsim.
 */

#include "wp_source_mock.hpp"

namespace
{
static champsim::modules::workload_source::register_module<champsim::test::mock_wp_source> mock_wp_source_reg("MOCK_WP_SOURCE");
} // anonymous namespace

#include <catch.hpp>
#include <vector>

#include "../../../branch/bimodal/bimodal.h"
#include "../../../branch/gshare/gshare.h"
#include "../../../branch/hashed_perceptron/hashed_perceptron.h"
#include "../../../branch/perceptron/perceptron.h"

TEST_CASE("The default hashed_perceptron history lengths are the original geometric series")
{
  const std::vector<std::size_t> original{0, 3, 4, 6, 8, 10, 14, 19, 26, 36, 49, 67, 91, 125, 170, 232};
  hashed_perceptron uut{champsim::modules::ModuleBuilder{"t176_hp_default", "hashed_perceptron"}};
  CHECK(uut.get_history_lengths() == original);
  CHECK(hashed_perceptron::geometric_history_lengths(16, 3, 232) == original);
}

TEST_CASE("The hashed_perceptron takes its history lengths from the config")
{
  SECTION("An explicit list sets the number of tables and their lengths")
  {
    const std::vector<std::size_t> lengths{0, 5, 20, 80};
    hashed_perceptron uut{champsim::modules::ModuleBuilder{"t176_hp_list", "hashed_perceptron"}.add_parameter("history_lengths", lengths)};
    CHECK(uut.get_history_lengths() == lengths);
  }

  SECTION("num_tables, min_history and max_history generate a geometric series")
  {
    hashed_perceptron uut{champsim::modules::ModuleBuilder{"t176_hp_geo", "hashed_perceptron"}
                              .add_parameter("num_tables", std::size_t{4})
                              .add_parameter("min_history", std::size_t{4})
                              .add_parameter("max_history", std::size_t{64})};
    CHECK(uut.get_history_lengths() == std::vector<std::size_t>{0, 4, 16, 64});
  }
}

TEST_CASE("A 1-bit bimodal counter flips on every outcome")
{
  bimodal uut{champsim::modules::ModuleBuilder{"t176_bimodal", "bimodal"}.add_parameter("counter_bits", std::size_t{1})};
  champsim::address ip{0xdeadbeef};

  for (int i = 0; i < 10; ++i)
    uut.last_branch_result(ip, champsim::address{}, true, 0);
  CHECK(uut.predict_branch(ip, champsim::address{}, false, 0));

  // with the default 2-bit counters one not-taken outcome would not be enough to flip the prediction
  uut.last_branch_result(ip, champsim::address{}, false, 0);
  CHECK_FALSE(uut.predict_branch(ip, champsim::address{}, false, 0));
}

TEST_CASE("A 1-entry bimodal table makes every branch share one counter")
{
  bimodal uut{champsim::modules::ModuleBuilder{"t176_bimodal_1", "bimodal"}.add_parameter("table_size", std::size_t{1})};
  for (int i = 0; i < 10; ++i)
    uut.last_branch_result(champsim::address{0x1000}, champsim::address{}, true, 0);
  CHECK(uut.predict_branch(champsim::address{0x2004}, champsim::address{}, false, 0));
}

TEST_CASE("gshare and perceptron accept configured sizes")
{
  gshare gs{champsim::modules::ModuleBuilder{"t176_gshare", "gshare"}
                .add_parameter("history_length", std::size_t{20})
                .add_parameter("table_size", std::size_t{1024})};
  CHECK(gs.history_length == 20);
  CHECK(std::size(gs.gs_history_table) == 1024);

  perceptron p{champsim::modules::ModuleBuilder{"t176_perceptron", "perceptron"}
                   .add_parameter("history_length", std::size_t{40})
                   .add_parameter("num_perceptrons", std::size_t{64})};
  CHECK(std::size(p.weights) == 64 * 41);
  CHECK(p.theta == 91); // round(1.93 * 40 + 14)

  // a strongly biased branch is still learned with the larger history
  champsim::address ip{0x4000};
  for (int i = 0; i < 50; ++i) {
    p.predict_branch(ip, champsim::address{}, false, 0);
    p.last_branch_result(ip, champsim::address{}, true, 0);
  }
  CHECK(p.predict_branch(ip, champsim::address{}, false, 0));
}

TEST_CASE("Invalid branch predictor sizes are rejected")
{
  using champsim::modules::ModuleBuilder;
  CHECK_THROWS(hashed_perceptron{ModuleBuilder{"t176_bad_hp", "hashed_perceptron"}.add_parameter("table_size", std::size_t{1000})});
  CHECK_THROWS(gshare{ModuleBuilder{"t176_bad_gs", "gshare"}.add_parameter("history_length", std::size_t{65})});
  CHECK_THROWS(perceptron{ModuleBuilder{"t176_bad_p", "perceptron"}.add_parameter("weight_bits", std::size_t{1})});
  CHECK_THROWS(bimodal{ModuleBuilder{"t176_bad_b", "bimodal"}.add_parameter("table_size", std::size_t{16}).add_parameter("hash_modulus", std::size_t{17})});
}

TEST_CASE("By default bimodal indexes with ip % table_size, so IPs table_size apart share a counter")
{
  bimodal uut{champsim::modules::ModuleBuilder{"t176_bimodal_mod", "bimodal"}.add_parameter("table_size", std::size_t{1024})};
  const champsim::address trained{0x4000};
  const champsim::address alias{0x4000 + 1024};
  const champsim::address neighbor{0x4000 + 1};

  for (int i = 0; i < 10; ++i)
    uut.last_branch_result(trained, champsim::address{}, true, 0);

  CHECK(uut.predict_branch(alias, champsim::address{}, false, 0));
  CHECK_FALSE(uut.predict_branch(neighbor, champsim::address{}, false, 0));
}

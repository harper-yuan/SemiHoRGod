#define BOOST_TEST_MODULE offline
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <SemiHoRGod/helpers.h>
#include <SemiHoRGod/offline_evaluator.h>
#include <SemiHoRGod/ot_provider.h>
#include <SemiHoRGod/rand_gen_pool.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/included/unit_test.hpp>
#include <future>
#include <memory>
#include <random>
#include <vector>

using namespace SemiHoRGod;
namespace bdata = boost::unit_test::data;

constexpr int TEST_DATA_MAX_VAL = 1000;
constexpr int SECURITY_PARAM = 128;
constexpr int SEED = 200;

Ring compute_sum_alpha(int i, int j, const PreprocCircuit<Ring> & preproc_i, const PreprocCircuit<Ring> & preproc_j, int k) {
  Ring sum_alpha = 0;
  int pos = 0;
  for(int alpha_index_count = 0; alpha_index_count < 5; alpha_index_count++) {
    if (alpha_index_count == i) { //还需要一个x_i，这个值参与方i没有，需要找j要
      sum_alpha += preproc_j.gates[k]->mask[idxFromSenderAndReceiver(j, i)];
    }
    else {
      sum_alpha += preproc_i.gates[k]->mask[pos++];
    }
  }
  return sum_alpha;
}
BOOST_AUTO_TEST_SUITE(dummy_offline)

BOOST_AUTO_TEST_SUITE_END()
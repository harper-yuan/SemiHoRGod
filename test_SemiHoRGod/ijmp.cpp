#define BOOST_TEST_MODULE jump
#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <SemiHoRGod/ijmp.h>
#include <boost/test/included/unit_test.hpp>
#include <future>
#include <string>
#include <vector>

using namespace SemiHoRGod;
using namespace SemiHoRGod;

BOOST_AUTO_TEST_SUITE(ijmp_provider)

BOOST_AUTO_TEST_CASE(all_combinations) {
  std::string message("A test string.");
  // std::vector<uint8_t> input(message.begin(), message.end());

  std::vector<std::future<void>> parties;
  for (int i = 0; i < NUM_PARTIES ; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      io::NetIOMP<NUM_PARTIES > network(i, 10000, nullptr, true);
      ImprovedJmp jump(i);
      ThreadPool tpool(1);

      for (int sender1 = 0; sender1 < NUM_PARTIES ; ++sender1) {
        for (int sender2 = sender1 + 1; sender2 < NUM_PARTIES ; ++sender2) {
            for (int sender3 = sender2 + 1; sender2 < NUM_PARTIES ; ++sender2) {
                for (int receiver = 0; receiver < NUM_PARTIES ; ++receiver) {
                    if (receiver == sender1 || receiver == sender2 || receiver == sender3) {
                    continue;
                    }
                    std::vector<uint8_t> input= {static_cast<uint8_t>(sender1),static_cast<uint8_t>(sender2),
                                                static_cast<uint8_t>(sender3),static_cast<uint8_t>(i)};
                    jump.jumpUpdate(sender1, sender2, sender3, receiver, input.size(), input.data()); 
                }
            }
        }
      }

      jump.communicate(network, tpool);

      for (int sender1 = 0; sender1 < NUM_PARTIES ; ++sender1) {
        for (int sender2 = sender1 + 1; sender2 < NUM_PARTIES ; ++sender2) {
            for (int sender3 = sender2 + 1; sender2 < NUM_PARTIES ; ++sender2) {
                if (i == sender1 || i == sender2 || i == sender3) {
                    continue;
                }
                std::vector<uint8_t> input= {static_cast<uint8_t>(sender1),static_cast<uint8_t>(sender2),
                                                static_cast<uint8_t>(sender3),static_cast<uint8_t>(i)};
                BOOST_TEST(jump.getValues(sender1, sender2, sender3) == input);
            }
        }
      }
    }));
  }

  for (auto& p : parties) {
    p.wait();
  }
}

BOOST_AUTO_TEST_SUITE_END()

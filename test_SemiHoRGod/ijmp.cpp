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

// BOOST_AUTO_TEST_CASE(uint64_t_test) {
//   std::string message("A test string.");
//   // std::vector<uint64_t> input = {1,2,3,4,NUM_PARTIES };

//   // std::future<void>作用：表示一个异步操作的结果（来自 std::async、std::promise 或线程池任务）。
//   std::vector<std::future<void>> parties;
//   for (int i = 0; i < NUM_PARTIES ; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       io::NetIOMP<NUM_PARTIES > network(i, 10000, nullptr, true);
//       ImprovedJmp jump(i);
//       ThreadPool tpool(1);

//       jump.jumpUpdate(1, 2, 3, 4, input.size(), input.data());
//       if(i == 4){
//         jump.communicate(network, tpool);
//       }
//       else{
//         jump.communicate(network, tpool);
//       }
      

//       if (i == 4) {
//         std::cout<<"Received the message: ";
//         auto& vec = jump.getValues(1, 2, 3);
//         for(auto x : vec) {
//             std::cout<<x;
//         }
//         std::cout<<endl;
//         BOOST_TEST(jump.getValues(1, 2, 3) == input);
//       }
//     }));
//   }

//   for (auto& p : parties) {
//     p.wait();
//   }
// }

BOOST_AUTO_TEST_CASE(receiver_offset_2) {
  std::string message("A test string.");
  std::vector<uint8_t> input(message.begin(), message.end());

  // std::future<void>作用：表示一个异步操作的结果（来自 std::async、std::promise 或线程池任务）。
  std::vector<std::future<void>> parties;
  for (int i = 0; i < NUM_PARTIES ; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      io::NetIOMP<NUM_PARTIES > network(i, 10000, nullptr, true);
      ImprovedJmp jump(i);
      ThreadPool tpool(1);

      jump.jumpUpdate(1, 2, 3, 4, input.size(), input.data());
      if(i == 4){
        jump.communicate(network, tpool);
      }
      else{
        jump.communicate(network, tpool);
      }
      

      if (i == 4) {
        std::cout<<"Received the message: ";
        auto& vec = jump.getValues(1, 2, 3);
        for(auto x : vec) {
            std::cout<<x;
        }
        std::cout<<endl;
        BOOST_TEST(jump.getValues(1, 2, 3) == input);
      }
    }));
  }

  for (auto& p : parties) {
    p.wait();
  }
}

// BOOST_AUTO_TEST_CASE(jmp_test) {
//   std::string message("A test string.");
//   std::vector<uint8_t> input(message.begin(), message.end());

//   std::vector<std::future<void>> parties;

//   for (int i = 0; i < 4; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       io::NetIOMP<4> network(i, 10000, nullptr, true);
//       // JumpProvider jump(i);
//       // ThreadPool tpool(1);
//       // jump.jumpUpdate(0, 1, 3, input.size(), input.data());
//       // jump.communicate(network, tpool);

//       // if (i == 3) {
//       //   BOOST_TEST(jump.getValues(0, 1) == input);
//       // }
//     }));
//   }

//   for (auto& p : parties) {
//     p.wait();
//   }
//   int i = 0;
//   JumpProvider jump(i);
//   ThreadPool tpool(1);
//   jump.jumpUpdate(0, 1, 3, input.size(), input.data());
//   jump.communicate(network, tpool);

//   if (i == 3) {
//     BOOST_TEST(jump.getValues(0, 1) == input);
//   }
// }
// BOOST_AUTO_TEST_CASE(receiver_offset_3) {
//   std::string message("A test string.");
//   std::vector<uint8_t> input(message.begin(), message.end());

//   std::vector<std::future<void>> parties;
//   for (int i = 0; i < 4; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       io::NetIOMP<4> network(i, 10000, nullptr, true);
//       JumpProvider jump(i);
//       ThreadPool tpool(1);

//       jump.jumpUpdate(0, 1, 3, input.size(), input.data());
//       jump.communicate(network, tpool);

//       if (i == 3) {
//         BOOST_TEST(jump.getValues(0, 1) == input);
//       }
//     }));
//   }

//   for (auto& p : parties) {
//     p.wait();
//   }
// }

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

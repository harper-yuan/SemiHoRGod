#define BOOST_TEST_MODULE io
#include <io/netmp.h>
#include <bits/stdc++.h>
#include <boost/test/included/unit_test.hpp>
#include <future>
#include <random>
#include <vector>
BOOST_AUTO_TEST_SUITE(netmp)
BOOST_AUTO_TEST_CASE(test_harper_2PC) {
  // std::vector<uint8_t> message = {31,21,11};
  std::string message("1 to 0");
  std::string message1("0 to 1");
  auto party = std::async(std::launch::async, [=](){
    io::NetIOMP<2> network(1, 10000, nullptr, true);
    std::cout<<"Network1 has been build"<<endl;
    std::vector<uint8_t> data(message.size());
    
    
    // std::this_thread::sleep_for(std::chrono::seconds(10));
    // network.sync(); // 添加同步
    // std::cout<<"party 1完成同步"<<endl;

    std::cout<<"party 1 is preparing for recieving the data: "<<endl;
    network.recv(0, data.data(), data.size());
    
    std::cout<<"party 1 has received the data: ";
    for (auto x : data) std::cout << x << "";
    std::cout<<endl;
    // network.send(0, message.data(), message.size());
    // std::cout<<"party 1 has send the data: ";
    // for (auto x : message) std::cout << x << "";
    // std::cout<<endl;
  });
  
  io::NetIOMP<2> network(0, 10000, nullptr, true);
  
  std::cout<<"Network0 has been build"<<endl;
  for(int i = 0;i<1;i++)
  {
    // network.sync(); // 添加同步
    // std::cout<<"party 0完成同步"<<endl;
    network.send(1, message1.data(), message1.size());
    network.flush(1);
    
    std::cout<<"party 0 has send the data: ";
    for (auto x : message1) std::cout << x << "";
      std::cout<<endl;
  }
  
  // std::vector<uint8_t> received_data(message.size());
  // network.recv(1, received_data.data(), received_data.size());
  // party.wait();//必须放在这里，否则会死锁

  // std::cout<<"party 0 has received the data: ";
  // for(int i=0; i<received_data.size();i++)
  // {
  //   std::cout<<received_data[i]<<"";
  // }
  
  std::cout<<endl;
}

// BOOST_AUTO_TEST_CASE(echo_2P) {
//   std::string message("A test string.");
//   auto party = std::async(std::launch::async, [=]() {
//     io::NetIOMP<2> net(1, 10000, nullptr, true);
//     std::vector<uint8_t> data(message.size());
//     for (auto x : data) std::cout << x << "";
//     std::cout<<endl;
//     net.recv(0, data.data(), data.size());
//     std::cout<<"party 1 is preparing for receiveing the data"<<endl;
//     for (auto x : data) std::cout << x << "";
//     std::cout<<endl;
//     net.send(0, data.data(), data.size());
//     std::cout<<"party 1 has send the data"<<endl;
//   });

//   io::NetIOMP<2> net(0, 10000, nullptr, true);
//   net.send(1, message.data(), message.size());
//   std::cout<<"party 0 has send the data"<<endl;

//   std::vector<uint8_t> received_message(message.size());
//   net.recv(1, received_message.data(), received_message.size());
//   std::cout<<"party 0 is preparing for receiveing the data"<<endl;
//   party.wait();

//   std::cout<<"received message"<<endl;
//   for(int i=0; i<received_message.size();i++)
//   {
//     std::cout<<received_message[i]<<"";
//   }
//   BOOST_TEST(received_message ==
//              std::vector<uint8_t>(message.begin(), message.end()));
// }

BOOST_AUTO_TEST_CASE(mssg_pass_4P) {
  std::string message("A test string.");

  std::vector<std::future<void>> parties;
  for (size_t i = 1; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [=]() {
      io::NetIOMP<4> net(i, 10000, nullptr, true);
      std::vector<uint8_t> data(message.size());
      net.recvRelative(-1, data.data(), data.size());
      net.sendRelative(1, data.data(), data.size());
      net.flush();
    }));
  }

  io::NetIOMP<4> net(0, 10000, nullptr, true);
  net.sendRelative(1, message.data(), message.size());
  net.flush();

  std::vector<uint8_t> received_message(message.size());
  net.recvRelative(-1, received_message.data(), received_message.size());

  for (auto& p : parties) {
    p.wait();
  }

  BOOST_TEST(received_message ==
             std::vector<uint8_t>(message.begin(), message.end()));
}

BOOST_AUTO_TEST_CASE(echo_bool) {
  const size_t len = 65;

  std::mt19937 gen(200);
  std::bernoulli_distribution distrib;

  bool message[len];
  for (bool& i : message) {
    i = distrib(gen);
  }

  auto party = std::async(std::launch::async, [=]() {
    io::NetIOMP<2> net(1, 10000, nullptr, true);
    bool data[len];
    net.recvBool(0, static_cast<bool*>(data), len);
    net.sendBool(0, static_cast<bool*>(data), len);
  });

  io::NetIOMP<2> net(0, 10000, nullptr, true);
  net.sendBool(1, static_cast<bool*>(message), len);

  bool received_message[len];
  net.recvBool(1, static_cast<bool*>(received_message), len);

  party.wait();

  for (size_t i = 0; i < len; ++i) {
    BOOST_TEST(received_message[i] == message[i]);
  }
}

BOOST_AUTO_TEST_SUITE_END()

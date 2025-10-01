add_library(SemiHoRGod
    utils/circuit.cpp
    SemiHoRGod/types.cpp
    SemiHoRGod/helpers.cpp
    SemiHoRGod/rand_gen_pool.cpp
    SemiHoRGod/ijmp.cpp
    SemiHoRGod/offline_evaluator.cpp
    SemiHoRGod/online_evaluator.cpp)
target_include_directories(SemiHoRGod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SemiHoRGod PUBLIC Boost::system EMPTool EMP_OT NTL GMP)

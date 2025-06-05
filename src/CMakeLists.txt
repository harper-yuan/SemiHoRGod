# add_library(QuadSquad
#     utils/circuit.cpp
#     quadsquad/types.cpp
#     quadsquad/sharing.cpp
#     quadsquad/helpers.cpp
#     quadsquad/rand_gen_pool.cpp
#     quadsquad/offline_evaluator.cpp
#     quadsquad/jump_provider.cpp
#     quadsquad/online_evaluator.cpp
#     quadsquad/ot_provider.cpp)
# target_include_directories(QuadSquad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# target_link_libraries(QuadSquad PUBLIC Boost::system EMPTool EMP_OT NTL GMP)

add_library(HoRGod
    utils/circuit.cpp
    HoRGod/types.cpp
    HoRGod/sharing.cpp
    HoRGod/helpers.cpp
    HoRGod/rand_gen_pool.cpp
    HoRGod/offline_evaluator.cpp
    HoRGod/ijmp.cpp
    HoRGod/jump_provider.cpp
    HoRGod/online_evaluator.cpp
    HoRGod/ot_provider.cpp)
target_include_directories(HoRGod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(HoRGod PUBLIC Boost::system EMPTool EMP_OT NTL GMP)

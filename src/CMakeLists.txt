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

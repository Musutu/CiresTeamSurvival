cmake_minimum_required(VERSION 3.16)
project(CiresRulesTests LANGUAGES CXX)
enable_testing()
add_executable(CiresRulesTests
    RulesTests.cpp
    ../Source/CiresTeamSurvival/Rules/CiresRules.cpp)
target_include_directories(CiresRulesTests PRIVATE ../Source/CiresTeamSurvival/Rules)
target_compile_features(CiresRulesTests PRIVATE cxx_std_17)
set_target_properties(CiresRulesTests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(CiresRulesTests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(CiresRulesTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME gameplay_rules COMMAND CiresRulesTests)
# scaling-kits: primary-stat scaling, inheritance, shield block, level-15, Headshot, Artillery, Mech Tank.
add_executable(CireKitRulesTests
    KitRulesTests.cpp
    ../Source/CiresTeamSurvival/Rules/CireKitRules.cpp)
target_include_directories(CireKitRulesTests PRIVATE ../Source/CiresTeamSurvival/Rules)
target_compile_features(CireKitRulesTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(CireKitRulesTests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(CireKitRulesTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME kit_rules COMMAND CireKitRulesTests)

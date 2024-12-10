cmake_minimum_required(VERSION 3.10)
project(lm-pull)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(CURL REQUIRED)

add_executable(lm-pull lm-pull.cpp)
target_link_libraries(lm-pull PRIVATE ${CURL_LIBRARIES})

# Add install rules
install(TARGETS lm-pull
        RUNTIME DESTINATION bin)

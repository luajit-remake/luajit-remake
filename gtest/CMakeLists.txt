add_library(gtest 
  src/gtest-all.cc
)

target_include_directories(gtest PUBLIC 
                           ".")


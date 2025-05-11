# Core examples

# Example 1: Simple actor example
add_executable(example1_simple_actor example1_simple_actor.cpp)
target_link_libraries(example1_simple_actor PRIVATE qb-core)

# Example 2: Basic actor communication
add_executable(example2_basic_actors example2_basic_actors.cpp)
target_link_libraries(example2_basic_actors PRIVATE qb-core)

# Example 3: Multiple cores and event priorities
add_executable(example3_multicore example3_multicore.cpp)
target_link_libraries(example3_multicore PRIVATE qb-core)

# Example 4: Actor lifecycle and callbacks
add_executable(example4_lifecycle example4_lifecycle.cpp)
target_link_libraries(example4_lifecycle PRIVATE qb-core)

# Example 5: Timer usage with the latest API
add_executable(example5_timers example5_timers.cpp)
target_link_libraries(example5_timers PRIVATE qb-core)

# Example 6: Shared queue between actors
add_executable(example6_shared_queue example6_shared_queue.cpp)
target_link_libraries(example6_shared_queue PRIVATE qb-core)

# Example 7: Publisher/Subscriber pattern
add_executable(example7_pub_sub example7_pub_sub.cpp)
target_link_libraries(example7_pub_sub PRIVATE qb-core)

# Example 8: State machine implementation
add_executable(example8_state_machine example8_state_machine.cpp)
target_link_libraries(example8_state_machine PRIVATE qb-core)

# Example 9: Trading system simulation
add_executable(example9_trading_system example9_trading_system.cpp)
target_link_libraries(example9_trading_system PRIVATE qb-core)

# Example 10: Distributed computing system
add_executable(example10_distributed_computing example10_distributed_computing.cpp)
target_link_libraries(example10_distributed_computing PRIVATE qb-core) 
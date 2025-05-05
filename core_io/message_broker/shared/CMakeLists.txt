add_library(broker_shared STATIC
    Protocol.h
    Protocol.cpp
    Events.h
)

target_include_directories(broker_shared 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(broker_shared 
    PUBLIC 
    qb-core
) 
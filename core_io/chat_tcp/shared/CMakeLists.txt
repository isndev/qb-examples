add_library(chat_shared STATIC
    Protocol.h
    Protocol.cpp
    Events.h
)

target_include_directories(chat_shared 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_shared 
    PUBLIC 
    qb-core
) 
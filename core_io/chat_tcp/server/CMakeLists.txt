add_executable(chat_server
    main.cpp
    AcceptActor.cpp
    ServerActor.cpp
    ChatSession.cpp
    ChatRoomActor.cpp
)

target_link_libraries(chat_server 
    PRIVATE 
        chat_shared
        qb-core
) 
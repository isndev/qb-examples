add_executable(chat_client
    main.cpp
    ClientActor.cpp
    InputActor.cpp
)

target_link_libraries(chat_client 
    PRIVATE 
        chat_shared
        qb-core
) 
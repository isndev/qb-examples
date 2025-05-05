add_executable(broker_client
    main.cpp
    ClientActor.cpp
    InputActor.cpp
)

target_link_libraries(broker_client 
    PRIVATE 
        broker_shared
        qb-core
) 
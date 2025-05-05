add_executable(broker_server
    main.cpp
    AcceptActor.cpp
    ServerActor.cpp
    BrokerSession.cpp
    TopicManagerActor.cpp
)

target_link_libraries(broker_server 
    PRIVATE 
        broker_shared
        qb-core
) 
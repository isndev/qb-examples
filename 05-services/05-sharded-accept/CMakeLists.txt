project(qb_examples_05_sharded_accept
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "One listener per core on one port, the accept sharded by the kernel (SO_REUSEPORT)"
)

qb_status_message("Building qb examples — 05-services/05-sharded-accept")

qb_example(
        SOURCE
        main.cpp
        DEPENDS qb-core
)

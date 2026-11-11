# Pure C, no ESP-IDF calls: test_host/ builds the same rules.c with the host compiler.
idf_component_register(
    SRCS "rules.c"
    INCLUDE_DIRS "include"
    REQUIRES "protocol"
)

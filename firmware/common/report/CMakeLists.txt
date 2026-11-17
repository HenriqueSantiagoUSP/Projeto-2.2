# Pure C, no ESP-IDF calls: test_host/ builds the same report.c with the host compiler.
idf_component_register(
    SRCS "report.c"
    INCLUDE_DIRS "include"
)

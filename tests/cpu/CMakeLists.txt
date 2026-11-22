add_executable(test_ops test_ops.c)
target_include_directories(test_ops PRIVATE ${PROJECT_SOURCE_DIR}/runtime/cpu)
target_link_libraries(test_ops PRIVATE m)
add_test(NAME test_ops COMMAND test_ops)

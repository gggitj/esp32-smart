add_executable(mock_pogo_control main.cpp ../../src/protocol/pibridge_protocol.cpp)
target_include_directories(mock_pogo_control PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mock_pogo_control PRIVATE Qt6::Core Qt6::SerialPort)
target_compile_features(mock_pogo_control PRIVATE cxx_std_17)

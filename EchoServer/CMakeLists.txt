project(EchoServer)
set(CMAKE_CXX_STANDARD 17)

add_executable(EchoServer EchoServer/EchoServer/main.cpp EchoServer/EchoServer/TcpServer.cpp)
add_executable(Autotest Autotest/main.cpp)
target_link_libraries(EchoServer uring)

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

#define PORT 7050

void test_tcp_echo_server()
{
    auto testFunction = [] (int instanceNumber){
        int sock = 0, valread;
        struct sockaddr_in serv_addr;
        char buffer[1024] = {0};

        // Создание сокета
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket creation error");
            exit(EXIT_FAILURE);
        }

        // Установка параметров сервера
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);

        // Преобразование адреса из текстового в бинарный формат
        if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
        {
            perror("invalid address");
            exit(EXIT_FAILURE);
        }

        // Подключение к серверу
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            perror("connection failed");
            exit(EXIT_FAILURE);
        }

        for(int i = 0; i < 3; ++i)
        {
            auto hello = std::string("Hello, world! #") + std::to_string(i + 1) + '(' + std::to_string(instanceNumber) + ')';
            // Отправка сообщения
            send(sock, hello.c_str(), hello.length(), 0);
            std::cout << "Sent: " << hello << std::endl;

            // Получение ответа
            valread = read(sock, buffer, 1024);
            std::cout << "Received: " << buffer << std::endl;

            // Проверка ответа
            if (hello != buffer)
            {
                std::cerr << "Test failed: \"" << buffer << "\" received" << std::endl;
                exit(EXIT_FAILURE);
            }
            else
            {
                std::cout << "Test passed" << std::endl;
            }
        }
        close(sock);
    };
    std::thread t1([testFunction]{ testFunction(1); });
    std::thread t2([testFunction]{ testFunction(2); });
    std::thread t3([testFunction]{ testFunction(3); });

    t1.join();
    t2.join();
    t3.join();
}

int main()
{
    test_tcp_echo_server();
    return 0;
}

#include <iostream>
#include <unistd.h>

int main()
{
    std::cout << "Hello from ARM64 program!" << std::endl;
    std::cout << "This program is built in Docker and runs on Orange Pi CM4." << std::endl;
    std::cout << "Process ID: " << getpid() << std::endl;
    return 0;
}
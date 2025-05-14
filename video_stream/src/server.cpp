#include "neolux/nxserial.hpp"
#include "neolux/nxlog.hpp"
#include <iostream>
#include <atomic>
#include <thread>

void serial_test()
{
    try
    {
        neolux::NxSerial serial("/dev/ttyACM0", 115200, 8, 1, neolux::NxSerial::Parity::NONE);
        serial.set_non_blocking(true);

        char write_buffer[] = "Hello Serial!\n";

        while (true)
        {
            // 写数据到串口
            serial.write_data(write_buffer, sizeof(write_buffer) - 1);

            // 读取一行数据
            std::string response = serial.readline(1000);
            if (!response.empty())
            {
                std::cout << "Received: " << response << "\n";
            }

            std::this_thread::sleep_for(std::chrono::seconds(1)); // 每秒发送一次数据
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

int main()
{
    neolux::get_logger().info("Serial communication test started.");
    serial_test();
    return 0;
}

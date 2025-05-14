#ifndef __NEOLUX_NXSERIAL_HPP__
#define __NEOLUX_NXSERIAL_HPP__

#include <string>
#include <atomic>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include "neolux/nxlog.hpp"

namespace neolux
{

    class NxSerial
    {
    public:
        enum class Parity
        {
            NONE,
            ODD,
            EVEN
        };

        NxSerial(const std::string &port, int baudrate, int data_bits = 8, int stop_bits = 1, Parity parity = Parity::NONE);
        ~NxSerial();

        bool open_port();
        void close_port();
        ssize_t read_data(char *buffer, size_t size, int timeout_ms = 1000);
        ssize_t write_data(const char *buffer, size_t size);
        void set_non_blocking(bool non_blocking);
        std::string readline(int timeout_ms = 1000); // 新增的 readline 方法

    private:
        std::string port_;
        int baudrate_;
        int data_bits_;
        int stop_bits_;
        Parity parity_;
        int serial_fd_;
        struct termios tty_;
        neolux::NxLogger &logger_;

        void configure_serial();
        speed_t get_baud_rate(int baudrate);
        void set_parity(Parity parity);
        void set_data_bits(int data_bits);
        void set_stop_bits(int stop_bits);
    };

} // namespace neolux

#endif // __NEOLUX_NXSERIAL_HPP__

#include "neolux/nxserial.hpp"
#include <stdexcept>
#include <iostream>

namespace neolux
{

    // 构造函数
    NxSerial::NxSerial(const std::string &port, int baudrate, int data_bits, int stop_bits, Parity parity)
        : port_(port), baudrate_(baudrate), data_bits_(data_bits), stop_bits_(stop_bits), parity_(parity), serial_fd_(-1), logger_(neolux::get_logger())
    {
        if (!open_port())
        {
            logger_.error("Failed to open serial port: " + port_);
            throw std::runtime_error("Failed to open serial port: " + port_);
        }
        logger_.info("Serial port " + port_ + " opened successfully.");
        configure_serial();
    }

    // 析构函数
    NxSerial::~NxSerial()
    {
        if (serial_fd_ != -1)
        {
            close_port();
        }
    }

    // 打开串口
    bool NxSerial::open_port()
    {
        serial_fd_ = open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (serial_fd_ < 0)
        {
            logger_.error("Failed to open serial port: " + port_ + " (" + strerror(errno) + ")");
            return false;
        }
        return true;
    }

    // 关闭串口
    void NxSerial::close_port()
    {
        if (serial_fd_ != -1)
        {
            close(serial_fd_);
            serial_fd_ = -1;
            logger_.info("Serial port " + port_ + " closed.");
        }
    }

    // 配置串口
    void NxSerial::configure_serial()
    {
        if (tcgetattr(serial_fd_, &tty_) != 0)
        {
            logger_.error("Failed to get serial attributes: " + std::string(strerror(errno)));
            close(serial_fd_);
            throw std::runtime_error("Failed to get serial attributes");
        }

        // 设置波特率
        speed_t baud = get_baud_rate(baudrate_);
        cfsetospeed(&tty_, baud);
        cfsetispeed(&tty_, baud);

        // 设置奇偶校验、数据位、停止位
        set_parity(parity_);
        set_data_bits(data_bits_);
        set_stop_bits(stop_bits_);

        // 设置为原始模式
        cfmakeraw(&tty_);

        // 设置串口参数
        tty_.c_cflag |= (CLOCAL | CREAD);                // 启用串口接收
        tty_.c_iflag &= ~(IXON | IXOFF | IXANY);         // 禁用软件流控制
        tty_.c_oflag &= ~OPOST;                          // 原始输出
        tty_.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // 禁用终端模式

        // 设置串口属性
        if (tcsetattr(serial_fd_, TCSANOW, &tty_) != 0)
        {
            logger_.error("Failed to set serial attributes: " + std::string(strerror(errno)));
            close(serial_fd_);
            throw std::runtime_error("Failed to set serial attributes");
        }
        logger_.info("Serial port configured: " + port_);
    }

    // 获取波特率
    speed_t NxSerial::get_baud_rate(int baudrate)
    {
        switch (baudrate)
        {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        default:
            logger_.error("Unsupported baud rate: " + std::to_string(baudrate));
            throw std::invalid_argument("Unsupported baud rate");
        }
    }

    // 设置奇偶校验
    void NxSerial::set_parity(Parity parity)
    {
        switch (parity)
        {
        case Parity::NONE:
            tty_.c_cflag &= ~PARENB; // 无奇偶校验
            break;
        case Parity::ODD:
            tty_.c_cflag |= PARENB; // 设置奇校验
            tty_.c_cflag |= PARODD; // 奇校验
            break;
        case Parity::EVEN:
            tty_.c_cflag |= PARENB;  // 设置偶校验
            tty_.c_cflag &= ~PARODD; // 偶校验
            break;
        }
    }

    // 设置数据位
    void NxSerial::set_data_bits(int data_bits)
    {
        switch (data_bits)
        {
        case 7:
            tty_.c_cflag |= CS7; // 7 数据位
            break;
        case 8:
            tty_.c_cflag |= CS8; // 8 数据位
            break;
        default:
            logger_.error("Unsupported data bits: " + std::to_string(data_bits));
            throw std::invalid_argument("Unsupported data bits");
        }
    }

    // 设置停止位
    void NxSerial::set_stop_bits(int stop_bits)
    {
        switch (stop_bits)
        {
        case 1:
            tty_.c_cflag &= ~CSTOPB; // 1 停止位
            break;
        case 2:
            tty_.c_cflag |= CSTOPB; // 2 停止位
            break;
        default:
            logger_.error("Unsupported stop bits: " + std::to_string(stop_bits));
            throw std::invalid_argument("Unsupported stop bits");
        }
    }

    // 读取数据
    ssize_t NxSerial::read_data(char *buffer, size_t size, int timeout_ms)
    {
        fd_set set;
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        FD_ZERO(&set);
        FD_SET(serial_fd_, &set);

        int rv = select(serial_fd_ + 1, &set, nullptr, nullptr, &timeout);
        if (rv == -1)
        {
            logger_.error("Select error: " + std::string(strerror(errno)));
            return -1;
        }
        else if (rv == 0)
        {
            logger_.warning("Read timeout");
            return 0;
        }
        else
        {
            ssize_t bytes_read = read(serial_fd_, buffer, size);
            logger_.debug("Read " + std::to_string(bytes_read) + " bytes from serial.");
            return bytes_read;
        }
    }

    // 写入数据
    ssize_t NxSerial::write_data(const char *buffer, size_t size)
    {
        ssize_t bytes_written = write(serial_fd_, buffer, size);
        logger_.debug("Wrote " + std::to_string(bytes_written) + " bytes to serial.");
        return bytes_written;
    }

    // 设置为非阻塞模式
    void NxSerial::set_non_blocking(bool non_blocking)
    {
        int flags = fcntl(serial_fd_, F_GETFL, 0);
        if (flags == -1)
        {
            logger_.error("Failed to get file status flags: " + std::string(strerror(errno)));
            return;
        }

        if (non_blocking)
        {
            flags |= O_NONBLOCK;
        }
        else
        {
            flags &= ~O_NONBLOCK;
        }

        if (fcntl(serial_fd_, F_SETFL, flags) == -1)
        {
            logger_.error("Failed to set file status flags: " + std::string(strerror(errno)));
        }
    }

    // 读取一行数据直到换行符
    std::string NxSerial::readline(int timeout_ms)
    {
        char buffer[256];
        int idx = 0;
        size_t total_bytes_read = 0; // 记录总共读取的字节数

        while (true)
        {
            ssize_t bytes_read = read_data(&buffer[idx], 1, timeout_ms);
            if (bytes_read > 0)
            {
                total_bytes_read += bytes_read; // 累加读取的字节数
                if (buffer[idx] == '\n')
                {
                    buffer[idx] = '\0'; // 终止符
                    logger_.debug("Read " + std::to_string(total_bytes_read) + " bytes in total.");
                    return std::string(buffer);
                }
                idx++;
            }
            else if (bytes_read == 0)
            {
                // Timeout or no data
                break;
            }
            else
            {
                logger_.error("Error while reading data: " + std::string(strerror(errno)));
                break;
            }
        }
        return "";
    }

} // namespace neolux

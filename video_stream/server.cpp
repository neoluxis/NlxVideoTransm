#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <getopt.h>
#include <stdexcept>
#include <ctime>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <atomic>
#include <netinet/tcp.h>
#include <poll.h>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

// Global state
std::atomic<bool> running(true);
std::atomic<bool> snapshot_signal(false);
int current_client = -1;

// Thread-safe queue for frames
struct Frame {
    cv::Mat image;
    bool is_snapshot;
    Frame() : is_snapshot(false) {}
    explicit Frame(cv::Mat&& img, bool snap = false) : image(std::move(img)), is_snapshot(snap) {}
};

class FrameQueue {
private:
    std::queue<Frame> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    const size_t max_size_ = 5;

public:
    bool push(Frame&& frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) {
            queue_.pop();
        }
        queue_.push(std::move(frame));
        lock.unlock();
        cond_.notify_one();
        return true;
    }

    bool pop(Frame& frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || !running; });
        if (queue_.empty()) return false;
        frame = std::move(queue_.front());
        queue_.pop();
        return true;
    }
};

// Thread-safe queue for encoded frames
struct EncodedFrame {
    std::vector<uchar> data;
    EncodedFrame() = default;
    explicit EncodedFrame(std::vector<uchar>&& d) : data(std::move(d)) {}
};

class EncodedFrameQueue {
private:
    std::queue<EncodedFrame> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    const size_t max_size_ = 5;

public:
    bool push(EncodedFrame&& frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) {
            queue_.pop();
        }
        queue_.push(std::move(frame));
        lock.unlock();
        cond_.notify_one();
        return true;
    }

    bool pop(EncodedFrame& frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || !running; });
        if (queue_.empty()) return false;
        frame = std::move(queue_.front());
        queue_.pop();
        return true;
    }
};

// Get current timestamp
std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::string ts = std::ctime(&now);
    ts.pop_back();
    return ts;
}

// Photo capture thread
void photo_capture_thread(cv::VideoCapture& cap, FrameQueue& frame_queue, int width, int height) {
    try {
        while (running) {
            auto start = std::chrono::high_resolution_clock::now();
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty()) {
                std::cerr << "[" << get_timestamp() << "] Failed to capture frame\n";
                continue;
            }
            frame_queue.push(Frame(std::move(frame), snapshot_signal));
            if (snapshot_signal) snapshot_signal = false;
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            std::cout << "[" << get_timestamp() << "] Capture time: " << duration << " us\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << get_timestamp() << "] Photo capture thread exception: " << e.what() << "\n";
    }
}

// Resize thread
void resize_thread(FrameQueue& input_queue, EncodedFrameQueue& output_queue, int width, int height) {
    std::vector<int> encode_params = {cv::IMWRITE_JPEG_QUALITY, 50};
    std::vector<uchar> buffer(100000);
    try {
        while (running) {
            Frame frame;
            if (!input_queue.pop(frame)) break;
            auto start = std::chrono::high_resolution_clock::now();
            cv::Mat processed;
            if (!frame.is_snapshot) {
                cv::resize(frame.image, processed, cv::Size(width, height));
            } else {
                processed = std::move(frame.image);
            }
            cv::imencode(".jpg", processed, buffer, encode_params);
            output_queue.push(EncodedFrame(std::move(buffer)));
            buffer = std::vector<uchar>(100000);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            std::cout << "[" << get_timestamp() << "] Resize+encode time: " << duration << " us\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << get_timestamp() << "] Resize thread exception: " << e.what() << "\n";
    }
}

// Serial communication thread
void serial_thread(int serial_fd) {
    char buffer[1];
    while (running) {
        ssize_t bytes_read = read(serial_fd, buffer, 1);
        if (bytes_read > 0) {
            unsigned char byte = buffer[0];
            std::cout << "[" << get_timestamp() << "] Serial received byte: " << (int)byte
                      << " (char: " << (isprint(byte) ? std::string(1, byte) : "non-printable") << ")\n";
            if (byte == 'S') {
                snapshot_signal = true;
                std::cout << "[" << get_timestamp() << "] Snapshot signal received\n";
            }
        } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "[" << get_timestamp() << "] Serial read error: " << strerror(errno) << "\n";
        }
    }
}

// TCP posting thread
void tcp_posting_thread(int client_socket, EncodedFrameQueue& frame_queue) {
    int sndbuf = 1024 * 1024;
    setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    try {
        while (running && client_socket == current_client) {
            auto start = std::chrono::high_resolution_clock::now();
            EncodedFrame frame;
            if (!frame_queue.pop(frame)) break;
            uint32_t size = htonl(frame.data.size());
            if (send(client_socket, &size, sizeof(size), MSG_NOSIGNAL) < 0) {
                std::cerr << "[" << get_timestamp() << "] Send failed (size)\n";
                break;
            }
            if (send(client_socket, frame.data.data(), frame.data.size(), MSG_NOSIGNAL) < 0) {
                std::cerr << "[" << get_timestamp() << "] Send failed (data)\n";
                break;
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            std::cout << "[" << get_timestamp() << "] Send time: " << duration << " us\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << get_timestamp() << "] TCP posting thread exception: " << e.what() << "\n";
    }
    if (client_socket == current_client) {
        current_client = -1;
    }
    close(client_socket);
    std::cout << "[" << get_timestamp() << "] Client disconnected\n";
}

// Initialize serial port
int init_serial(const std::string& serial, int baudrate) {
    int fd = open(serial.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[" << get_timestamp() << "] Failed to open serial: " << serial << " (" << strerror(errno) << ")\n";
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "[" << get_timestamp() << "] Failed to get serial attributes: " << strerror(errno) << "\n";
        close(fd);
        return -1;
    }
    speed_t baud;
    switch (baudrate) {
    case 9600: baud = B9600; break;
    case 19200: baud = B19200; break;
    case 38400: baud = B38400; break;
    case 57600: baud = B57600; break;
    case 115200: baud = B115200; break;
    default:
        std::cerr << "[" << get_timestamp() << "] Unsupported baud rate: " << baudrate << "\n";
        close(fd);
        return -1;
    }
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    cfmakeraw(&tty);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "[" << get_timestamp() << "] Failed to set serial attributes: " << strerror(errno) << "\n";
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

// Signal handler
void signal_handler(int sig) {
    running = false;
}

// Print usage
void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name
              << " [options]\n"
              << "Options:\n"
              << "  --device <device>    Video device (default: /dev/video0)\n"
              << "  --width <width>      Frame width (default: 1280)\n"
              << "  --height <height>    Frame height (default: 480)\n"
              << "  --fps <fps>          Frames per second (default: 30)\n"
              << "  --host <host>        Host address (default: 0.0.0.0)\n"
              << "  --port <port>        Port number (default: 40917)\n"
              << "  --serial <serial>    Serial device (default: empty)\n"
              << "  --baudrate <baud>    Baud rate (default: 115200)\n"
              << "  --help               Show this help\n";
}

int main(int argc, char* argv[]) {
    // Default parameters
    std::string device = "/dev/video0";
    int fwidth = 640, fheight = 480;
    int fps = 30;
    std::string host = "0.0.0.0";
    int port = 40917;
    std::string serial = "";
    int baudrate = 115200;

    // Parse arguments
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"fps", required_argument, 0, 'f'},
        {"host", required_argument, 0, 'H'},
        {"port", required_argument, 0, 'p'},
        {"serial", required_argument, 0, 's'},
        {"baudrate", required_argument, 0, 'b'},
        {"help", no_argument, 0, 'x'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
        try {
            switch (opt) {
            case 'd': device = optarg; break;
            case 'w': fwidth = std::stoi(optarg); break;
            case 'h': fheight = std::stoi(optarg); break;
            case 'f': fps = std::stoi(optarg); break;
            case 'H': host = optarg; break;
            case 'p': port = std::stoi(optarg); break;
            case 's': serial = optarg; break;
            case 'b': baudrate = std::stoi(optarg); break;
            case 'x': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return -1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid argument: " << e.what() << "\n";
            print_usage(argv[0]);
            return -1;
        }
    }

    // Initialize video capture
    cv::VideoCapture cap(device, cv::CAP_V4L2);
    if (!cap.isOpened()) {
        std::cerr << "[" << get_timestamp() << "] Failed to open video device: " << device << "\n";
        return -1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, fwidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, fheight);
    cap.set(cv::CAP_PROP_FPS, fps);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    std::cout << "[" << get_timestamp() << "] Video: " << fwidth << "x" << fheight << "@" << fps << "fps\n";

    // Initialize queues
    FrameQueue capture_queue;
    EncodedFrameQueue encode_queue;

    // Start threads
    std::thread capture_thread(photo_capture_thread, std::ref(cap), std::ref(capture_queue), fwidth, fheight);
    std::thread resize_thr(resize_thread, std::ref(capture_queue), std::ref(encode_queue), fwidth / 4, fheight / 4);

    // Initialize serial
    int serial_fd = -1;
    std::thread serial_thr;
    if (!serial.empty()) {
        serial_fd = init_serial(serial, baudrate);
        if (serial_fd < 0) {
            running = false;
            capture_thread.join();
            resize_thr.join();
            return -1;
        }
        serial_thr = std::thread(serial_thread, serial_fd);
        std::cout << "[" << get_timestamp() << "] Serial: " << serial << "@" << baudrate << "\n";
    }

    // Setup signal handling
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0) {
        std::cerr << "[" << get_timestamp() << "] Failed to create socket\n";
        running = false;
        capture_thread.join();
        resize_thr.join();
        if (serial_fd >= 0) {
            close(serial_fd);
            if (serial_thr.joinable()) serial_thr.join();
        }
        return -1;
    }

    int sock_opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));
    int sndbuf = 1024 * 1024;
    setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[" << get_timestamp() << "] Invalid host: " << host << "\n";
        close(server_fd);
        running = false;
        capture_thread.join();
        resize_thr.join();
        if (serial_fd >= 0) {
            close(serial_fd);
            if (serial_thr.joinable()) serial_thr.join();
        }
        return -1;
    }

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[" << get_timestamp() << "] Bind failed\n";
        close(server_fd);
        running = false;
        capture_thread.join();
        resize_thr.join();
        if (serial_fd >= 0) {
            close(serial_fd);
            if (serial_thr.joinable()) serial_thr.join();
        }
        return -1;
    }

    if (listen(server_fd, 1) < 0) {
        std::cerr << "[" << get_timestamp() << "] Listen failed\n";
        close(server_fd);
        running = false;
        capture_thread.join();
        resize_thr.join();
        if (serial_fd >= 0) {
            close(serial_fd);
            if (serial_thr.joinable()) serial_thr.join();
        }
        return -1;
    }
    std::cout << "[" << get_timestamp() << "] Server: " << host << ":" << port << "\n";

    // Main loop with poll
    struct pollfd pfd;
    pfd.fd = server_fd;
    pfd.events = POLLIN;

    while (running) {
        int ret = poll(&pfd, 1, 100);
        if (ret < 0) {
            if (running) std::cerr << "[" << get_timestamp() << "] Poll error\n";
            continue;
        }
        if (ret == 0) continue;

        if (pfd.revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (running) std::cerr << "[" << get_timestamp() << "] Accept failed\n";
                continue;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::cout << "[" << get_timestamp() << "] New client: " << client_ip << "\n";

            if (current_client != -1) {
                close(current_client);
                current_client = -1;
                std::cout << "[" << get_timestamp() << "] Closed previous client\n";
            }

            current_client = client_fd;
            std::thread tcp_thread(tcp_posting_thread, client_fd, std::ref(encode_queue));
            tcp_thread.detach();
        }
    }

    // Cleanup
    std::cout << "[" << get_timestamp() << "] Shutting down...\n";
    if (current_client != -1) {
        close(current_client);
        current_client = -1;
    }
    close(server_fd);
    if (serial_fd >= 0) {
        close(serial_fd);
        if (serial_thr.joinable()) serial_thr.join();
    }
    running = false;
    capture_queue.push(Frame());
    encode_queue.push(EncodedFrame());
    if (capture_thread.joinable()) capture_thread.join();
    if (resize_thr.joinable()) resize_thr.join();
    cap.release();
    std::cout << "[" << get_timestamp() << "] Shutdown complete\n";
    return 0;
}
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

// Global state
std::atomic<bool> running(true);
int current_client = -1; // Single client socket

// Get current timestamp
std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::string ts = std::ctime(&now);
    ts.pop_back();
    return ts;
}

// Handle serial communication
void handle_serial(int serial_fd, std::atomic<bool>& snapshot_signal) {
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

// Handle single client
void handle_client(int client_socket, cv::VideoCapture& cap, std::atomic<bool>& snapshot_signal,
                   int width, int height, int snapw, int snaph) {
    // Enable TCP_NODELAY
    int flag = 1;
    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Pre-allocate buffer
    std::vector<uchar> buffer(100000);
    std::vector<int> encode_params = {cv::IMWRITE_JPEG_QUALITY, 70};

    try {
        while (running && client_socket == current_client) {
            cv::Mat frame;
            if (snapshot_signal) {
                cap.set(cv::CAP_PROP_FRAME_WIDTH, snapw);
                cap.set(cv::CAP_PROP_FRAME_HEIGHT, snaph);
                if (!cap.read(frame) || frame.empty()) {
                    std::cerr << "[" << get_timestamp() << "] Failed to capture snapshot\n";
                    break;
                }
                snapshot_signal = false;
                cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
                cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
            } else {
                if (!cap.read(frame) || frame.empty()) {
                    std::cerr << "[" << get_timestamp() << "] Failed to capture frame\n";
                    break;
                }
            }

            // Encode frame
            cv::imencode(".jpg", frame, buffer, encode_params);

            // Send frame size
            uint32_t size = htonl(buffer.size());
            if (send(client_socket, &size, sizeof(size), MSG_NOSIGNAL) < 0) {
                std::cerr << "[" << get_timestamp() << "] Send failed (size)\n";
                break;
            }

            // Send frame data
            if (send(client_socket, buffer.data(), buffer.size(), MSG_NOSIGNAL) < 0) {
                std::cerr << "[" << get_timestamp() << "] Send failed (data)\n";
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << get_timestamp() << "] Client thread exception: " << e.what() << "\n";
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

                       // Flush the port to clear any residual data
                       tcflush(fd, TCIOFLUSH);

                       struct termios tty;
                       if (tcgetattr(fd, &tty) != 0) {
                           std::cerr << "[" << get_timestamp() << "] Failed to get serial attributes: " << strerror(errno) << "\n";
                           close(fd);
                           return -1;
                       }

                       // Set baud rate
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

                       // Set raw mode
                       cfmakeraw(&tty); // Sets raw mode: disables canonical processing, echo, signals, etc.

                       // Explicitly configure for 8N1
                       tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8 data bits
                       tty.c_cflag &= ~(PARENB | PARODD);          // No parity
                       tty.c_cflag &= ~CSTOPB;                     // 1 stop bit
                       tty.c_cflag &= ~CRTSCTS;                    // No hardware flow control
                       tty.c_cflag |= CLOCAL | CREAD;              // Ignore modem controls, enable reading

                       // Disable software flow control and other input processing
                       tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

                       // Set timeout for non-blocking read
                       tty.c_cc[VMIN] = 0;  // Non-blocking
                       tty.c_cc[VTIME] = 1; // 0.1s timeout

                       if (tcsetattr(fd, TCSANOW, &tty) != 0) {
                           std::cerr << "[" << get_timestamp() << "] Failed to set serial attributes: " << strerror(errno) << "\n";
                           close(fd);
                           return -1;
                       }

                       // Flush again after configuration
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
                       << "  --device <device>    Video device (default: /dev/video8)\n"
                       << "  --width <width>      Frame width (default: 320)\n"
                       << "  --height <height>    Frame height (default: 240)\n"
                       << "  --snaph <height>     Snapshot height (default: 480)\n"
                       << "  --snapw <width>      Snapshot width (default: 640)\n"
                       << "  --fps <fps>          Frames per second (default: 30)\n"
                       << "  --host <host>        Host address (default: 0.0.0.0)\n"
                       << "  --port <port>        Port number (default: 40917)\n"
                       << "  --serial <serial>    Serial device (default: empty)\n"
                       << "  --baudrate <baud>    Baud rate (default: 115200)\n"
                       << "  --help               Show this help\n";
                   }

                   int main(int argc, char* argv[]) {
                       // Default parameters
                       std::string device = "/dev/video8";
                       int fwidth = 320, fheight = 240, snaph = 480, snapw = 640;
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
                           {"snaph", required_argument, 0, 'P'},
                           {"snapw", required_argument, 0, 'O'},
                           {"fps", required_argument, 0, 'f'},
                           {"host", required_argument, 0, 'H'},
                           {"port", required_argument, 0, 'p'},
                           {"serial", required_argument, 0, 's'},
                           {"baudrate", required_argument, 0, 'b'},
                           {"help", no_argument, 0, 'x'},
                           {0, 0, 0, 0}
                       };

                       int opt;
                       while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
                           try {
                               switch (opt) {
                                   case 'd': device = optarg; break;
                                   case 'w': fwidth = std::stoi(optarg); break;
                                   case 'h': fheight = std::stoi(optarg); break;
                                   case 'P': snaph = std::stoi(optarg); break;
                                   case 'O': snapw = std::stoi(optarg); break;
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

                       // Initialize serial
                       int serial_fd = -1;
                       std::thread serial_thread;
                       std::atomic<bool> snapshot_signal(false);
                       if (!serial.empty()) {
                           serial_fd = init_serial(serial, baudrate);
                           if (serial_fd < 0) return -1;
                           serial_thread = std::thread(handle_serial, serial_fd, std::ref(snapshot_signal));
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
                           return -1;
                       }

                       int sock_opt = 1;
                       setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));

                       struct sockaddr_in server_addr;
                       server_addr.sin_family = AF_INET;
                       server_addr.sin_port = htons(port);
                       if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
                           std::cerr << "[" << get_timestamp() << "] Invalid host: " << host << "\n";
                           close(server_fd);
                           return -1;
                       }

                       if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                           std::cerr << "[" << get_timestamp() << "] Bind failed\n";
                           close(server_fd);
                           return -1;
                       }

                       if (listen(server_fd, 1) < 0) {
                           std::cerr << "[" << get_timestamp() << "] Listen failed\n";
                           close(server_fd);
                           return -1;
                       }
                       std::cout << "[" << get_timestamp() << "] Server: " << host << ":" << port << "\n";

                       // Main loop with poll
                       struct pollfd pfd;
                       pfd.fd = server_fd;
                       pfd.events = POLLIN;

                       while (running) {
                           int ret = poll(&pfd, 1, 100); // 100ms timeout
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

                               // Close existing client
                               if (current_client != -1) {
                                   close(current_client);
                                   current_client = -1;
                                   std::cout << "[" << get_timestamp() << "] Closed previous client\n";
                               }

                               // Set new client
                               current_client = client_fd;

                               // Start client thread
                               std::thread client_thread(handle_client, client_fd, std::ref(cap), std::ref(snapshot_signal),
                                                         fwidth, fheight, snapw, snaph);
                               client_thread.detach();
                           }
                       }

                       // Cleanup
                       std::cout << "[" << get_timestamp() << "] Shutting down...\n";
                       if (current_client != -1) {
                           close(current_client);
                           current_client = -1;
                       }
                       if (serial_fd >= 0) {
                           close(serial_fd);
                           if (serial_thread.joinable()) serial_thread.join();
                       }
                       close(server_fd);
                       cap.release();
                       std::cout << "[" << get_timestamp() << "] Shutdown complete\n";
                       return 0;
                   }

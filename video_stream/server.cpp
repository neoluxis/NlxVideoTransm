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

// Get current timestamp as string
std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::string ts = std::ctime(&now);
    ts.pop_back(); // Remove trailing newline
    return ts;
}

void handle_client(int client_socket, cv::VideoCapture& cap) {
    try {
        while (true) {
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty()) {
                std::cerr << "[" << get_timestamp() << "] Failed to capture frame" << std::endl;
                break;
            }

            // Encode frame as JPEG
            std::vector<uchar> buffer;
            cv::imencode(".jpg", frame, buffer);

            // Send frame size
            uint32_t size = buffer.size();
            size = htonl(size);
            if (send(client_socket, &size, sizeof(size), MSG_NOSIGNAL) < 0) {
                std::cerr << "[" << get_timestamp() << "] Client disconnected or send failed (frame size)" << std::endl;
                break;
            }

            // Send frame data
            if (send(client_socket, buffer.data(), buffer.size(), MSG_NOSIGNAL) < 0) {
                std::cerr << "[" << get_timestamp() << "] Client disconnected or send failed (frame data)" << std::endl;
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << get_timestamp() << "] Exception in client thread: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[" << get_timestamp() << "] Unknown exception in client thread" << std::endl;
    }

    // Close the client socket
    close(client_socket);
    std::cout << "[" << get_timestamp() << "] Client thread terminated" << std::endl;
}

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name 
                << " [options]\n"
                << "Options:\n"
                << "  -d, --device <device>    Video device (default: /dev/video8)\n"
                << "  -w, --width <width>      Frame width (default: 640)\n"
                << "  -h, --height <height>    Frame height (default: 480)\n"
                << "  -f, --fps <fps>          Frames per second (default: 30)\n"
                << "  -H, --host <host>        Host address (default: 0.0.0.0)\n"
                << "  -P, --port <port>        Port number (default: 40917)\n"
                << "  -s, --serial <serial>    Serial device (default: empty)\n"
                << "  -b, --baudrate <baudrate> Baud rate (default: 115200)\n"
                << "  -x, --help               Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Default values
    std::string device = "/dev/video8";
    int fwidth = 640;
    int fheight = 480;
    int fps = 30;
    std::string host = "0.0.0.0";
    int port = 40917;
    std::string serial = "";
    int baudrate = 115200;

    // Define long options
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"fps", required_argument, 0, 'f'},
        {"host", required_argument, 0, 'H'},
        {"port", required_argument, 0, 'P'},
        {"serial", required_argument, 0, 's'},
        {"baudrate", required_argument, 0, 'b'},
        {"help", no_argument, 0, 'x'},
        {0, 0, 0, 0} // End of options
    };

    // Parse command-line arguments
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'd':
                device = optarg;
                break;
            case 'w':
                try {
                    fwidth = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid width" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'h':
                try {
                    fheight = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid height" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'f':
                try {
                    fps = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid FPS" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'H':
                host = optarg;
                break;
            case 'P':
                try {
                    port = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid port number" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 's':
                serial = optarg;
                break;
            case 'b':
                try {
                    baudrate = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid baud rate" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'x':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // Initialize video capture
    cv::VideoCapture cap(device);
    if (!cap.isOpened()) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not open video device " << device << std::endl;
        return -1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, fwidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, fheight);
    cap.set(cv::CAP_PROP_FPS, fps);
    

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not create socket" << std::endl;
        return -1;
    }

    // Set socket options to reuse address
    int sock_opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));

    // Configure server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Invalid host address" << std::endl;
        close(server_fd);
        return -1;
    }
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Bind failed" << std::endl;
        close(server_fd);
        return -1;
    }

    // Listen for connections
    if (listen(server_fd, 1) < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Listen failed" << std::endl;
        close(server_fd);
        return -1;
    }

    std::cout << "[" << get_timestamp() << "] Server listening on " << host << ":" << port << std::endl;

    int current_client = -1;
    while (true) {
        try {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            // Accept new connection
            int new_client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (new_client < 0) {
                std::cerr << "[" << get_timestamp() << "] Error: Accept failed" << std::endl;
                continue;
            }

            // Get client IP address
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

            // Log new connection
            std::cout << "[" << get_timestamp() << "] New connection from " << client_ip << std::endl;

            // Close existing connection if any
            if (current_client != -1) {
                close(current_client);
                std::cout << "[" << get_timestamp() << "] Closed previous connection" << std::endl;
            }

            // Update current client
            current_client = new_client;

            // Handle client in a new thread
            std::thread client_thread(handle_client, current_client, std::ref(cap));
            client_thread.detach();
        } catch (const std::exception& e) {
            std::cerr << "[" << get_timestamp() << "] Exception in main loop: " << e.what() << std::endl;
            continue;
        } catch (...) {
            std::cerr << "[" << get_timestamp() << "] Unknown exception in main loop" << std::endl;
            continue;
        }
    }

    // Cleanup
    close(server_fd);
    cap.release();
    return 0;
}

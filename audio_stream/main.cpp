#include <portaudio.h>
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

// Audio configuration
#define SAMPLE_RATE (44100)
#define CHANNELS (1)
#define SAMPLE_FORMAT (paInt16)
#define FRAMES_PER_BUFFER (1024)

// Get current timestamp as string
std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::string ts = std::ctime(&now);
    ts.pop_back(); // Remove trailing newline
    return ts;
}

void handle_client(int client_socket, PaStream* stream) {
    try {
        std::vector<int16_t> buffer(FRAMES_PER_BUFFER);
        while (true) {
            // Read audio data
            PaError err = Pa_ReadStream(stream, buffer.data(), FRAMES_PER_BUFFER);
            if (err != paNoError) {
                std::cerr << "[" << get_timestamp() << "] Failed to read audio: " 
                          << Pa_GetErrorText(err) << std::endl;
                break;
            }

            // Send audio data
            ssize_t bytes_sent = send(client_socket, buffer.data(), 
                                   FRAMES_PER_BUFFER * sizeof(int16_t), MSG_NOSIGNAL);
            if (bytes_sent < 0) {
                std::cerr << "[" << get_timestamp() << "] Client disconnected or send failed" << std::endl;
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
              << " [--device <device_id>] [--host <host>] [--port <port>]\n"
              << "Defaults: --device 0 --host 0.0.0.0 --port 40918\n";
}

int main(int argc, char* argv[]) {
    // Default values
    int device_id = 0;
    std::string host = "0.0.0.0";
    int port = 40918;

    // Define long options
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    // Parse command-line arguments
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'd':
                try {
                    device_id = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid device ID" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'h':
                host = optarg;
                break;
            case 'p':
                try {
                    port = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid port number" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Failed to initialize PortAudio: " 
                  << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    // Open audio stream
    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        &PaStreamParameters{device_id, CHANNELS, SAMPLE_FORMAT, 0.0, nullptr},
        nullptr, // No output
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        nullptr, // No callback
        nullptr
    );
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not open audio device " 
                  << device_id << ": " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Start audio stream
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not start audio stream: " 
                  << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[" << get_timestamp  << "] Error: Could not create socket" << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
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
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Bind failed" << std::endl;
        close(server_fd);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }

    // Listen for connections
    if (listen(server_fd, 1) < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Listen failed" << std::endl;
        close(server_fd);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }

    std::cout << "[" << get_timestamp() << "] Server listening on " << host << ":" << port << std::endl;

    int current_client = -1;
    while (true) {
        try {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            // Accept

System: It looks like the provided C++ code for the audio streaming server (`audio_stream.cpp`) was cut off mid-sentence in the `main` function, specifically at the `// Accept` comment. I'll complete the code by adding the remaining logic for accepting client connections, handling single-connection logic, logging client IPs, and ensuring proper cleanup. The completed code will maintain all specified requirements: recording audio from a device (default ID 0), streaming to host (default `0.0.0.0`) and port (default `40918`), keeping only one connection, and staying robust against client disconnections.

### Completing the Code
The missing part should include:
- Accepting new client connections.
- Logging the client’s IP address with a timestamp.
- Closing the previous connection if a new one is accepted.
- Spawning a client handling thread.
- Robust error handling to keep the server running.

The completed code will be consistent with the previous video streaming server (`main.cpp`, artifact ID `ceb02e3d-261b-40f7-aa2f-44c6cce7ec8d`) in terms of structure, logging, and error handling.

<xaiArtifact artifact_id="53a32594-0ab8-4cc1-9325-6772fdd54691" artifact_version_id="254639e5-36ec-49ef-8bc2-ada9a419dec5" title="audio_stream.cpp" contentType="text/x-c++src">
#include <portaudio.h>
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

// Audio configuration
#define SAMPLE_RATE (44100)
#define CHANNELS (1)
#define SAMPLE_FORMAT (paInt16)
#define FRAMES_PER_BUFFER (1024)

// Get current timestamp as string
std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::string ts = std::ctime(&now);
    ts.pop_back(); // Remove trailing newline
    return ts;
}

void handle_client(int client_socket, PaStream* stream) {
    try {
        std::vector<int16_t> buffer(FRAMES_PER_BUFFER);
        while (true) {
            // Read audio data
            PaError err = Pa_ReadStream(stream, buffer.data(), FRAMES_PER_BUFFER);
            if (err != paNoError) {
                std::cerr << "[" << get_timestamp() << "] Failed to read audio: " 
                          << Pa_GetErrorText(err) << std::endl;
                break;
            }

            // Send audio data
            ssize_t bytes_sent = send(client_socket, buffer.data(), 
                                   FRAMES_PER_BUFFER * sizeof(int16_t), MSG_NOSIGNAL);
            if (bytes_sent < 0) {
                std::cerr << "[" << get_timestamp() << "] Client disconnected or send failed" << std::endl;
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
              << " [--device <device_id>] [--host <host>] [--port <port>]\n"
              << "Defaults: --device 0 --host 0.0.0.0 --port 40918\n";
}

int main(int argc, char* argv[]) {
    // Default values
    int device_id = 0;
    std::string host = "0.0.0.0";
    int port = 40918;

    // Define long options
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    // Parse command-line arguments
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'd':
                try {
                    device_id = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid device ID" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'h':
                host = optarg;
                break;
            case 'p':
                try {
                    port = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid port number" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Failed to initialize PortAudio: " 
                  << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    // Open audio stream
    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        &PaStreamParameters{device_id, CHANNELS, SAMPLE_FORMAT, 0.0, nullptr},
        nullptr, // No output
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        nullptr, // No callback
        nullptr
    );
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not open audio device " 
                  << device_id << ": " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Start audio stream
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not start audio stream: " 
                  << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Could not create socket" << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
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
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Bind failed" << std::endl;
        close(server_fd);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }

    // Listen for connections
    if (listen(server_fd, 1) < 0) {
        std::cerr << "[" << get_timestamp() << "] Error: Listen failed" << std::endl;
        close(server_fd);
        Pa_CloseStream(stream);
        Pa_Terminate();
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
            std::thread client_thread(handle_client, current_client, stream);
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
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}

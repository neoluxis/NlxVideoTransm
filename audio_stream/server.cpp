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
#include <sstream>

// Audio configuration
#define DEFAULT_SAMPLE_RATE (44100)
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
    << " [--device <device_id>] [--host <host>] [--port <port>] [--sample-rate <rate>] [--list-device]\n"
    << "Defaults: --device 0 --host 0.0.0.0 --port 40918 --sample-rate 44100\n"
    << "  --list-device: List available audio input devices and exit\n";
}

void list_devices() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Error: Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    int num_devices = Pa_GetDeviceCount();
    if (num_devices < 0) {
        std::cerr << "Error: Failed to get device count: " << Pa_GetErrorText(num_devices) << std::endl;
        Pa_Terminate();
        return;
    }

    // Common sample rates to check
    const double sample_rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 96000};
    const int num_rates = sizeof(sample_rates) / sizeof(sample_rates[0]);

    std::cout << "Available audio input devices:\n";
    std::cout << "ID\tName\tMax Input Channels\tSupported Sample Rates (Hz)\n";
    std::cout << "------------------------------------------------------------\n";
    for (int i = 0; i < num_devices; ++i) {
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info && device_info->maxInputChannels > 0) {
            // Check supported sample rates
            std::vector<double> supported_rates;
            PaStreamParameters input_params = {i, CHANNELS, SAMPLE_FORMAT, 0.0, nullptr};
            for (int j = 0; j < num_rates; ++j) {
                err = Pa_IsFormatSupported(&input_params, nullptr, sample_rates[j]);
                if (err == paNoError) {
                    supported_rates.push_back(sample_rates[j]);
                }
            }

            // Format supported sample rates
            std::ostringstream rates_str;
            for (size_t j = 0; j < supported_rates.size(); ++j) {
                rates_str << supported_rates[j];
                if (j < supported_rates.size() - 1) rates_str << ", ";
            }

            std::cout << i << "\t" << device_info->name << "\t"
            << device_info->maxInputChannels << "\t\t"
            << (supported_rates.empty() ? "None" : rates_str.str()) << "\n";
        }
    }

    Pa_Terminate();
}

int main(int argc, char* argv[]) {
    // Default values
    int device_id = 0;
    std::string host = "0.0.0.0";
    int port = 40918;
    double sample_rate = DEFAULT_SAMPLE_RATE;
    bool list_device = false;

    // Define long options
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"sample-rate", required_argument, 0, 's'},
        {"list-device", no_argument, 0, 'l'},
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
            case 's':
                try {
                    sample_rate = std::stod(optarg);
                    if (sample_rate <= 0) {
                        throw std::invalid_argument("Sample rate must be positive");
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid sample rate" << std::endl;
                    print_usage(argv[0]);
                    return -1;
                }
                break;
            case 'l':
                list_device = true;
                break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // List devices and exit if requested
    if (list_device) {
        list_devices();
        return 0;
    }

    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Failed to initialize PortAudio: "
        << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    // Validate sample rate
    PaStreamParameters input_params;
    input_params.device = device_id;
    input_params.channelCount = CHANNELS;
    input_params.sampleFormat = SAMPLE_FORMAT;
    input_params.suggestedLatency = 0.0;
    input_params.hostApiSpecificStreamInfo = nullptr;

    err = Pa_IsFormatSupported(&input_params, nullptr, sample_rate);
    if (err != paNoError) {
        std::cerr << "[" << get_timestamp() << "] Error: Sample rate " << sample_rate
        << " not supported by device " << device_id << ": "
        << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Open audio stream
    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        &input_params,
        nullptr, // No output
        sample_rate,
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

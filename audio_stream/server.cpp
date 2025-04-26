#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <ctime>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <getopt.h>

std::atomic<bool> running(true);
std::atomic<bool> cleaned_up(false); // Prevent double cleanup
snd_pcm_t* global_capture_handle = nullptr;
int global_server_fd = -1;

void log_message(const std::string& message) {
    time_t now = time(nullptr);
    char time_str[26];
    ctime_r(&now, time_str);
    time_str[strlen(time_str) - 1] = '\0'; // Remove newline
    std::cout << "[" << time_str << "] " << message << std::endl;
}

void handle_client(int client_socket, snd_pcm_t* capture_handle, unsigned int buffer_size, const std::string& client_ip) {
    log_message("Client connected from " + client_ip);

    std::vector<int16_t> buffer(buffer_size / 2); // buffer_size is in bytes, samples are 2 bytes
    int retries = 0;
    const int max_retries = 5;

    // Set client socket to non-blocking
    fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL) | O_NONBLOCK);

    while (running) {
        // Check running before blocking call
        if (!running) break;

        int err = snd_pcm_readi(capture_handle, buffer.data(), buffer_size / 2);
        if (err == -EPIPE || err == -EOVERFLOW) {
            log_message("Audio buffer overflow detected, attempting recovery");
            snd_pcm_recover(capture_handle, err, true);
            retries++;
            if (retries >= max_retries) {
                log_message("Max retries reached, terminating client thread");
                break;
            }
            continue;
        } else if (err < 0) {
            log_message("Failed to read audio: " + std::string(snd_strerror(err)));
            retries++;
            if (retries >= max_retries) {
                log_message("Max retries reached, terminating client thread");
                break;
            }
            continue;
        } else if (err != static_cast<int>(buffer_size / 2)) {
            log_message("Short read: " + std::to_string(err) + " frames");
            continue;
        }

        retries = 0; // Reset retries on successful read

        // Non-blocking send
        ssize_t sent = send(client_socket, buffer.data(), buffer_size, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // Retry if non-blocking send would block
            }
            log_message("Failed to send data to client: " + std::string(strerror(errno)));
            break;
        } else if (sent != static_cast<ssize_t>(buffer_size)) {
            log_message("Incomplete send: " + std::to_string(sent) + " bytes");
        }
    }

    close(client_socket);
    log_message("Client thread terminated and disconnected from " + client_ip);
}

void list_alsa_devices() {
    snd_ctl_t *ctl;
    snd_ctl_card_info_t *info;
    snd_pcm_info_t *pcminfo;
    int card = -1;
    int err;

    snd_ctl_card_info_alloca(&info);
    snd_pcm_info_alloca(&pcminfo);

    log_message("Listing ALSA capture devices:");
    while (snd_card_next(&card) >= 0 && card >= 0) {
        std::string card_name = "hw:" + std::to_string(card);
        if ((err = snd_ctl_open(&ctl, card_name.c_str(), 0)) < 0) {
            log_message("Cannot open card " + card_name + ": " + snd_strerror(err));
            continue;
        }

        if ((err = snd_ctl_card_info(ctl, info)) < 0) {
            log_message("Cannot get info for card " + card_name + ": " + snd_strerror(err));
            snd_ctl_close(ctl);
            continue;
        }

        std::cout << "Card " << card << ": " << snd_ctl_card_info_get_name(info) << " [" << card_name << "]" << std::endl;

        int device = -1;
        while (snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0) {
            snd_pcm_info_set_device(pcminfo, device);
            snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);
            if ((err = snd_ctl_pcm_info(ctl, pcminfo)) < 0) {
                continue;
            }
            std::cout << "  Device hw:" << card << "," << device << ": "
            << snd_pcm_info_get_name(pcminfo) << std::endl;

            // Query supported sample rates
            snd_pcm_t *pcm;
            snd_pcm_hw_params_t *hwparams;
            snd_pcm_hw_params_alloca(&hwparams);
            if (snd_pcm_open(&pcm, ("hw:" + std::to_string(card) + "," + std::to_string(device)).c_str(),
                SND_PCM_STREAM_CAPTURE, 0) >= 0) {
                snd_pcm_hw_params_any(pcm, hwparams);
            unsigned int rmin, rmax;
            snd_pcm_hw_params_get_rate_min(hwparams, &rmin, nullptr);
            snd_pcm_hw_params_get_rate_max(hwparams, &rmax, nullptr);
            std::cout << "    Supported sample rates: " << rmin << " - " << rmax << " Hz" << std::endl;
            snd_pcm_close(pcm);
                }
        }
        snd_ctl_close(ctl);
    }
}

void cleanup_resources() {
    if (!cleaned_up) {
        cleaned_up = true;
        log_message("Cleaning up resources");
        if (global_server_fd >= 0) {
            log_message("Closing server socket");
            close(global_server_fd);
            global_server_fd = -1;
        }
        if (global_capture_handle) {
            log_message("Stopping and closing ALSA capture");
            snd_pcm_drop(global_capture_handle);
            snd_pcm_close(global_capture_handle);
            global_capture_handle = nullptr;
        }
    }
}

int main(int argc, char* argv[]) {
    int port = 40918;
    unsigned int sample_rate = 44100;
    unsigned int buffer_size = 1024 * 2; // Bytes (1024 samples * 2 bytes)
    unsigned int period_size = 256; // ALSA period size (frames)
    unsigned int n_periods = 4; // Number of periods in buffer
    std::string device = "hw:0,0";
    bool list_devices = false;

    // Parse command-line arguments
    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"sample-rate", required_argument, 0, 's'},
        {"device", required_argument, 0, 'd'},
        {"list-device", no_argument, 0, 'l'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:s:d:l", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'p':
                port = std::stoi(optarg);
                break;
            case 's':
                sample_rate = std::stoi(optarg);
                break;
            case 'd':
                device = optarg;
                break;
            case 'l':
                list_devices = true;
                break;
            default:
                std::cerr << "Usage: " << argv[0]
                << " [--port <port>] [--sample-rate <rate>] [--device <device>] [--list-device]" << std::endl;
                return 1;
        }
    }

    if (list_devices) {
        list_alsa_devices();
        return 0;
    }

    // Setup ALSA
    snd_pcm_t* capture_handle;
    snd_pcm_hw_params_t* hw_params;
    int err;

    if ((err = snd_pcm_open(&capture_handle, device.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        log_message("Cannot open audio device " + device + ": " + snd_strerror(err));
        return 1;
    }
    global_capture_handle = capture_handle;

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture_handle, hw_params);

    if ((err = snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        log_message("Cannot set access type: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
        log_message("Cannot set sample format: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    unsigned int actual_rate = sample_rate;
    if ((err = snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, &actual_rate, 0)) < 0) {
        log_message("Cannot set sample rate: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }
    if (actual_rate != sample_rate) {
        log_message("Warning: Actual sample rate is " + std::to_string(actual_rate) + " Hz");
        sample_rate = actual_rate;
    }

    if ((err = snd_pcm_hw_params_set_channels(capture_handle, hw_params, 1)) < 0) {
        log_message("Cannot set channel count: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_period_size(capture_handle, hw_params, period_size, 0)) < 0) {
        log_message("Cannot set period size: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_periods(capture_handle, hw_params, n_periods, 0)) < 0) {
        log_message("Cannot set periods: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    if ((err = snd_pcm_hw_params(capture_handle, hw_params)) < 0) {
        log_message("Cannot set parameters: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    snd_pcm_uframes_t actual_buffer_size;
    snd_pcm_hw_params_get_buffer_size(hw_params, &actual_buffer_size);
    log_message("ALSA buffer size: " + std::to_string(actual_buffer_size) + " frames");
    log_message("ALSA period size: " + std::to_string(period_size) + " frames");
    log_message("ALSA sample rate: " + std::to_string(sample_rate) + " Hz");

    if ((err = snd_pcm_prepare(capture_handle)) < 0) {
        log_message("Cannot prepare audio interface: " + std::string(snd_strerror(err)));
        cleanup_resources();
        return 1;
    }

    // Setup socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_message("Socket creation failed: " + std::string(strerror(errno)));
        cleanup_resources();
        return 1;
    }
    global_server_fd = server_fd;

    // Set server socket to non-blocking
    fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK);

    opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_message("Setsockopt failed: " + std::string(strerror(errno)));
        cleanup_resources();
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        log_message("Bind failed: " + std::string(strerror(errno)));
        cleanup_resources();
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        log_message("Listen failed: " + std::string(strerror(errno)));
        cleanup_resources();
        return 1;
    }

    log_message("Server listening on port " + std::to_string(port));

    // Signal handler for clean shutdown
    signal(SIGINT, [](int) {
        running = false;
        log_message("Received SIGINT, shutting down...");
        cleanup_resources();
    });

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (running) {
                log_message("Accept failed: " + std::string(strerror(errno)));
            }
            continue;
        }

        // Get client IP
        std::string client_ip = inet_ntoa(client_addr.sin_addr);

        // Close previous connection if any
        static std::thread client_thread;
        if (client_thread.joinable()) {
            log_message("Closed previous connection from " + client_ip);
            running = false; // Signal previous thread to exit
            client_thread.join();
            running = true;
        }

        // Start new client thread
        client_thread = std::thread(handle_client, client_socket, capture_handle, buffer_size, client_ip);
        client_thread.detach();
    }

    cleanup_resources();
    log_message("Server shutdown complete");
    return 0;
}

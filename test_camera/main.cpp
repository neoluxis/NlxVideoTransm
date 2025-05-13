#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <map>
#include <sstream>

void printHelp(const std::string &program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n"
    << "Options:\n"
    << "  -d, --device DEV      Camera device (default: 0 or /dev/video0)\n"
    << "  -w, --width WIDTH     Frame width (default: 640)\n"
    << "  -h, --height HEIGHT   Frame height (default: 480)\n"
    << "  -f, --format FOURCC   Pixel format (e.g., MJPG, YUYV; default: MJPG)\n"
    << "  -r, --resolution RES  Resolution and FPS in format WIDTHxHEIGHT@FPS (e.g., 640x480@60)\n"
    << "  -s, --fps FPS         Target frames per second (default: 30)\n"
    << "  -h, --help            Show this help message\n";
}

int fourccFromString(const std::string &format) {
    if (format.length() != 4) return 0;
    return cv::VideoWriter::fourcc(format[0], format[1], format[2], format[3]);
}

std::map<std::string, std::string> parseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];

        if (key == "-h" || key == "--help") {
            args["help"] = "1";
            return args;
        }

        // 处理短参数
        if (key == "-d" || key == "--device") {
            args["device"] = argv[++i];
        }
        else if (key == "-w" || key == "--width") {
            args["width"] = argv[++i];
        }
        else if (key == "-h" || key == "--height") {
            args["height"] = argv[++i];
        }
        else if (key == "-f" || key == "--format") {
            args["format"] = argv[++i];
        }
        else if (key == "-r" || key == "--resolution") {
            args["resolution"] = argv[++i];
        }
        else if (key == "-s" || key == "--fps") {
            args["fps"] = argv[++i];
        }
    }
    return args;
}

// 解析形如 640x480@60 的字符串
bool parseResolution(const std::string &res, int &width, int &height, int &fps) {
    std::stringstream ss(res);
    char x, at;
    if (ss >> width >> x >> height >> at >> fps) {
        return (x == 'x' && at == '@');
    }
    return false;
}

int main(int argc, char** argv) {
    auto args = parseArgs(argc, argv);

    if (args.count("help")) {
        printHelp(argv[0]);
        return 0;
    }

    std::string device_str = args.count("device") ? args["device"] : "0";
    int device;
    if (device_str.rfind("/dev/video", 0) == 0)
        device = std::stoi(device_str.substr(10));
    else
        device = std::stoi(device_str);

    int width = 640, height = 480, fps = 30;
    std::string format = args.count("format") ? args["format"] : "MJPG";

    if (args.count("resolution")) {
        std::string res = args["resolution"];
        if (!parseResolution(res, width, height, fps)) {
            std::cerr << "Invalid resolution format. Use WIDTHxHEIGHT@FPS (e.g., 640x480@60).\n";
            return 1;
        }
    }

    int fourcc = fourccFromString(format);
    if (fourcc == 0) {
        std::cerr << "Invalid format: " << format << std::endl;
        return 1;
    }

    cv::VideoCapture cap(device, cv::CAP_V4L2);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera device " << device << std::endl;
        return 1;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap.set(cv::CAP_PROP_FOURCC, fourcc);
    cap.set(cv::CAP_PROP_FPS, fps);

    std::cout << "Testing camera: " << width << "x" << height
    << " @" << fps << "fps, format=" << format << "\n";

    const int test_duration_sec = 5;
    int total_frames = 0;
    int frames_last_second = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;

    while (true) {
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Frame capture failed.\n";
            break;
        }

        total_frames++;
        frames_last_second++;

        auto now = std::chrono::steady_clock::now();
        auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        auto elapsed_since_report = std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count();

        if (elapsed_since_report >= 1) {
            std::cout << "Current FPS: " << frames_last_second << std::endl;
            frames_last_second = 0;
            last_report = now;
        }

        if (total_elapsed >= test_duration_sec) {
            double avg_fps = total_frames / (double)total_elapsed;
            std::cout << "Captured " << total_frames << " frames in "
            << total_elapsed << " seconds. Average FPS: "
            << avg_fps << std::endl;
            break;
        }
    }

    cap.release();
    return 0;
}

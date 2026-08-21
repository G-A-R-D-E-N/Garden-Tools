#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    int exitCode = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--stdout" && i + 1 < argc) {
            std::cout << argv[++i];
        } else if (arg == "--stderr" && i + 1 < argc) {
            std::cerr << argv[++i];
        } else if (arg == "--exit" && i + 1 < argc) {
            exitCode = std::stoi(argv[++i]);
        } else if (arg == "--sleep" && i + 1 < argc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[++i])));
        } else if (arg == "--cwd") {
            std::cout << std::filesystem::current_path().string();
        } else if (arg == "--env" && i + 1 < argc) {
            const char* value = std::getenv(argv[++i]);
            if (value) std::cout << value;
        } else if (arg == "--echo-arg" && i + 1 < argc) {
            std::cout << argv[++i];
        }
    }
    return exitCode;
}

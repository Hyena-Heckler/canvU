#include "raylib.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <glob.h>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 560;
constexpr int kMinimumRssi = -100;
constexpr int kMaximumRssi = -30;
constexpr double kSignalTimeoutSeconds = 2.0;

const Color kBackground{16, 20, 30, 255};
const Color kPanel{25, 32, 46, 255};
const Color kGrid{54, 65, 83, 255};
const Color kText{226, 232, 240, 255};
const Color kMuted{144, 158, 178, 255};
const Color kSignal{71, 220, 170, 255};

bool parseRssiLine(const char *line, int &rssi) {
    constexpr char prefix[] = "signal A:";
    if (std::strncmp(line, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    const char *value = line + sizeof(prefix) - 1;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed < -127 || parsed > 0) {
        return false;
    }

    while (*end == ' ' || *end == '\t' || *end == '\r') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }

    rssi = static_cast<int>(parsed);
    return true;
}

float rssiToX(int rssi, float axisLeft, float axisRight) {
    const int clamped = std::clamp(rssi, kMinimumRssi, kMaximumRssi);
    const float fraction = static_cast<float>(clamped - kMinimumRssi) /
                           static_cast<float>(kMaximumRssi - kMinimumRssi);
    return axisLeft + fraction * (axisRight - axisLeft);
}

class SerialReader {
public:
    explicit SerialReader(const char *device)
        : requestedDevice_(device == nullptr ? "" : device) {
        reconnect();
    }

    ~SerialReader() {
        closePort();
    }

    SerialReader(const SerialReader &) = delete;
    SerialReader &operator=(const SerialReader &) = delete;

    bool isOpen() const {
        return fd_ >= 0;
    }

    const std::string &error() const {
        return error_;
    }

    const std::string &device() const {
        return device_;
    }

    bool reconnect() {
        if (isOpen()) {
            return true;
        }

        if (!requestedDevice_.empty()) {
            return connectTo(requestedDevice_);
        }

        glob_t matches{};
        if (glob("/dev/cu.*", 0, nullptr, &matches) != 0) {
            error_ = "No USB serial device found. Connect the hub over USB.";
            return false;
        }

        bool connected = false;
        for (size_t i = 0; i < matches.gl_pathc && !connected; ++i) {
            const std::string candidate = matches.gl_pathv[i];
            const std::string name = candidate.substr(candidate.find_last_of('/') + 1);
            if (name.rfind("usb", 0) == 0 ||
                name.rfind("SLAB_USBtoUART", 0) == 0 ||
                name.rfind("wchusbserial", 0) == 0) {
                connected = connectTo(candidate);
            }
        }
        globfree(&matches);
        if (!connected && error_.empty()) {
            error_ = "No USB serial device found. Connect the hub over USB.";
        }
        return connected;
    }

    bool readRssi(int &rssi) {
        if (fd_ < 0) {
            return false;
        }

        char bytes[256];
        const ssize_t count = read(fd_, bytes, sizeof(bytes));
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                error_ = std::string("Serial connection lost: ") + std::strerror(errno);
                closePort();
            }
            return false;
        }
        if (count == 0) {
            return false;
        }

        bool updated = false;
        for (ssize_t i = 0; i < count; ++i) {
            const char ch = bytes[i];
            if (ch == '\n') {
                line_[lineLength_] = '\0';
                updated = parseRssiLine(line_, rssi) || updated;
                lineLength_ = 0;
            } else if (ch != '\r') {
                if (lineLength_ + 1 < sizeof(line_)) {
                    line_[lineLength_++] = ch;
                } else {
                    lineLength_ = 0;
                }
            }
        }
        return updated;
    }

private:
    bool connectTo(const std::string &device) {
        const int candidateFd = open(device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (candidateFd < 0) {
            error_ = std::string("Could not open ") + device + ": " + std::strerror(errno);
            return false;
        }

        termios settings{};
        if (tcgetattr(candidateFd, &settings) != 0) {
            error_ = std::string("Could not configure ") + device + ": " + std::strerror(errno);
            close(candidateFd);
            return false;
        }

        cfmakeraw(&settings);
        cfsetispeed(&settings, B115200);
        cfsetospeed(&settings, B115200);
        settings.c_cflag |= CLOCAL | CREAD;
        settings.c_cflag &= ~CSTOPB;
        settings.c_cflag &= ~PARENB;
        settings.c_cflag &= ~CSIZE;
        settings.c_cflag |= CS8;
        settings.c_cc[VMIN] = 0;
        settings.c_cc[VTIME] = 0;
        if (tcsetattr(candidateFd, TCSANOW, &settings) != 0) {
            error_ = std::string("Could not apply serial settings to ") + device +
                     ": " + std::strerror(errno);
            close(candidateFd);
            return false;
        }

        fd_ = candidateFd;
        device_ = device;
        error_.clear();
        return true;
    }

    void closePort() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
        device_.clear();
    }

    int fd_ = -1;
    std::string error_;
    std::string requestedDevice_;
    std::string device_;
    char line_[128]{};
    size_t lineLength_ = 0;
};

void drawAxis(float left, float right, float top, float bottom) {
    const float axisY = (top + bottom) * 0.5F;
    for (int value = kMinimumRssi; value <= kMaximumRssi; value += 10) {
        const float x = rssiToX(value, left, right);
        DrawLineEx({x, top}, {x, bottom}, 1.0F, kGrid);
        const char *label = TextFormat("%d", value);
        DrawText(label, static_cast<int>(x - MeasureText(label, 16) / 2.0F),
                 static_cast<int>(bottom + 18), 16, kMuted);
    }
    DrawLineEx({left, axisY}, {right, axisY}, 4.0F, kMuted);
    DrawText("WEAKER", static_cast<int>(left), static_cast<int>(bottom + 48), 14, kMuted);
    const char *stronger = "STRONGER";
    DrawText(stronger, static_cast<int>(right - MeasureText(stronger, 14)),
             static_cast<int>(bottom + 48), 14, kMuted);
    DrawText("RSSI (dBm)", static_cast<int>((left + right) * 0.5F - MeasureText("RSSI (dBm)", 16) / 2.0F),
             static_cast<int>(bottom + 76), 16, kMuted);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [serial-device]\n", argv[0]);
        std::fprintf(stderr, "Without a device path, the app finds the hub automatically.\n");
        return 2;
    }

    SerialReader serial(argc == 2 ? argv[1] : nullptr);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(kWindowWidth, kWindowHeight, "RSSI One-Axis Canvas");
    SetWindowMinSize(560, 400);
    SetTargetFPS(60);

    int rssi = kMinimumRssi;
    bool hasSignal = false;
    double lastSignalAt = 0.0;
    double lastConnectionAttempt = GetTime();

    while (!WindowShouldClose()) {
        if (!serial.isOpen() && GetTime() - lastConnectionAttempt >= 1.0) {
            serial.reconnect();
            lastConnectionAttempt = GetTime();
        }
        if (serial.readRssi(rssi)) {
            hasSignal = true;
            lastSignalAt = GetTime();
        }

        const bool signalIsFresh = hasSignal && GetTime() - lastSignalAt <= kSignalTimeoutSeconds;
        const int width = GetScreenWidth();
        const int height = GetScreenHeight();
        const float axisLeft = 110.0F;
        const float axisRight = static_cast<float>(width) - 110.0F;
        const float axisTop = static_cast<float>(height) * 0.35F;
        const float axisBottom = static_cast<float>(height) * 0.68F;
        const float axisY = (axisTop + axisBottom) * 0.5F;

        BeginDrawing();
        ClearBackground(kBackground);
        DrawText("RSSI POSITION", 52, 42, 16, kMuted);
        DrawText("Live signal strength", 52, 68, 32, kText);

        DrawRectangleRounded({36, axisTop - 48, static_cast<float>(width) - 72,
                              axisBottom - axisTop + 164}, 0.08F, 12, kPanel);
        drawAxis(axisLeft, axisRight, axisTop, axisBottom);
        if (serial.isOpen()) {
            DrawText(TextFormat("PORT  %s", serial.device().c_str()), 52, height - 34, 14, kMuted);
        }

        if (signalIsFresh) {
            const float markerX = rssiToX(rssi, axisLeft, axisRight);
            DrawLineEx({markerX, axisY - 30}, {markerX, axisY + 30}, 2.0F, kSignal);
            DrawCircleV({markerX, axisY}, 13.0F, kSignal);
            DrawCircleV({markerX, axisY}, 5.0F, kPanel);

            const char *value = TextFormat("%d dBm", rssi);
            const int valueWidth = MeasureText(value, 28);
            DrawText(value, static_cast<int>(markerX - valueWidth / 2.0F),
                     static_cast<int>(axisY - 76), 28, kSignal);
            DrawText("LIVE", width - 98, 52, 14, kSignal);
        } else {
            const char *message = !serial.isOpen()
                                      ? "Connect the ESP32 hub over USB"
                                      : (hasSignal ? "Signal timed out" : "Waiting for hub data...");
            DrawText(message, 52, height - 58, 16, kMuted);
            DrawText("NO SIGNAL", width - 138, 52, 14, kMuted);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

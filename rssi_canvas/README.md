# RSSI One-Axis Canvas

A Raylib desktop window that reads the hub's serial output and moves a marker
along one horizontal axis. Lower (more negative) RSSI values appear on the left;
higher values appear on the right. The display range is -100 to -30 dBm.

## Build and run on macOS

Install Raylib and CMake if needed, then build:

```sh
brew install raylib cmake
cmake -S rssi_canvas -B rssi_canvas/build
cmake --build rssi_canvas/build
```

Connect the ESP32-S3 hub over USB. To upload the hub and open the canvas
automatically, use the VS Code **Upload Hub and Open RSSI Canvas** task or the
**Upload** launch configuration. The canvas scans for the hub's USB serial port;
close any serial monitor that might already be using it.

To start the canvas without uploading firmware:

```sh
./rssi_canvas/run.sh
```

The app opens even when the hub is not connected and keeps looking for it. It
listens at 115200 baud for the hub's `signal A: <dBm>` output. An optional
serial-device path can be passed directly to the built executable to skip
automatic port detection.

# SuperCAN plugin for python-can

This plugin is required on Windows to get access to SuperCAN devices from Python. On Linux, simply use `can.Bus(interface='socketcan', ...)`.

## Installation

### Visual Studio (Build Tools)

Open a native command prompt.

### MingGW / MSYS2

1. Open a MinGW shell suitable for your system (MinGW64, typically).
2. Ensure you have the necessary packages installed, i.e

    ```sh
    pacman -S mingw-w64-x86_64-toolchain mingw64/mingw-w64-x86_64-python-pip
    ``` 

### Common Steps

1. Navigate to this directory

2. Run

    ```console
    python -m pip install .
    ```

## Usage from Python

1. Ensure your device is upgraded to firmware version 0.6.0 or better.
2. Optionally, register the COM server (`supercan_srv64.exe`) for shared channel access. If you used the installer, this step is already done. Otherwise, from an _admin_ console, run
     ```console
    supercan_srv64.exe /RegServer
    ```

3. Access the CAN channel from Python


    ```python
    import can

    e = can.Bus(
        channel=0,  # channel index (0-based)
        filters=None,
        interface="supercan",
        serial="12345678",  # device serial
        bitrate=500000)
    ```

    You can force _shared_ mode like so:

    ```python
    import can

    e = can.Bus(
        channel=0,  # channel index (0-based)
        filters=None,
        interface="supercan",
        serial="12345678",  # device serial
        bitrate=500000,
        shared=True)  # request shared mode, fail if unavailable
    ```

    Note, exclusive mode is always available. This should always work:

    ```python
    import can

    e = can.Bus(
        channel=0,  # channel index (0-based)
        filters=None,
        interface="supercan",
        serial="12345678",  # device serial
        bitrate=500000,
        shared=False)  # request exclusive mode
    ```

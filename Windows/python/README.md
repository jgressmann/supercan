# SuperCAN plugin for python-can

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

```python
import can

e = can.Bus(
    channel=0,  # channel index (0-based)
    filters=None,
    interface="supercan-exclusive",  # exclusive access to the channel
    serial="12345678",  # device serial
    bitrate=500000)
```

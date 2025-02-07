# SuperCAN plugin for python-can

## Installation

Assuming you have Visual Studio (Build Tools) installed, navigate
to this directory and then run

```console
python -m pip install setuptools
```

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

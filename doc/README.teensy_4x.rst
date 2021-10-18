PJRC Teensy 4.x
===============

**Attention: for this board to interface with CAN bus additional electronic components such as a CAN tranceiver are required!**

CAN mapping
===========

+-----------+------+---------------------+----------------------------------------------------------------+
| Board CAN | FD?  | CAN Channel on Host | `Pins Teensy 4.0 <https://www.pjrc.com/store/teensy40.html>`_  |
+===========+======+=====================+================================================================+
| CAN3      | yes  | channel 0           | TX: EMC_36 (pin 30), RX: EMC_37 (pin 31)                       |
+-----------+------+---------------------+----------------------------------------------------------------+
| CAN1      | no   | channel 1           | TX: AD_B1_08 (pin 22), RX: AD_B1_09 (pin 23)                   |
+-----------+------+---------------------+----------------------------------------------------------------+

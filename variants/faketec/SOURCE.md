# fakeTec (ProMicro nRF52840) variant

Derived from MeshCore's `variants/promicro` (upstream/meshcore-stock, v1.17.1; Seeed header kept). MeshCore
numbers the pins through its own 23-entry map; here the Arduino pin numbers are the nRF52840 GPIO numbers
(`variant.cpp`, the identity map of MeshCore's `heltec_t096` variant, LGPL-2.1 from the Adafruit nRF52
core), so `variant.h` lists the same pins translated to GPIO numbers. `boards/meshmesh_faketec.json` points here.

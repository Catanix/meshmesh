# Heltec Mesh Node T096 variant

`variant.h` and `variant.cpp` are taken unchanged from MeshCore (`variants/heltec_t096`,
upstream/meshcore-stock, v1.17.1), which derives them from the Adafruit nRF52 Arduino core (LGPL-2.1,
headers kept). The Adafruit core fork MeshCore pins has no T096 variant, so the board file
`boards/meshmesh_heltec_t096.json` points here. `PIN_SPI1_MISO (0)` maps to 0xFF in the pin map: SPI1
(the TFT) has no MISO.

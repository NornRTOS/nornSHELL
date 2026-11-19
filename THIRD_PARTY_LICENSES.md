# Third-party components

nornSHELL contains no third-party code. Everything in this repository is part of the
Licensed Work under LICENSE.md.

When linked into an ESP32 or AVR image, the toolchain's libc is used for nothing beyond
what nornCore already needs; nosh does its own number formatting and string handling.
The toolchain's terms are listed in the nornRTOS repository's THIRD_PARTY_LICENSES.md.

# Firmware Tool
Tool for generating firmware files from one or multiple input binaries

## Features
* Firmware files with multiple sections

## Firmware File Structure

Field                                      | Type
-------------------------------------------|---------------------------------
Device identifier                          | 0-padded string of 16 characters
Flags for future extensions                | uint32_t
Size of all sections (without file header) | uint32_t
Per section:                               |
Section Identifier                         | uint32_t
Section CRC                                | uint32_t
Section size (without section header)      | uint32_t
Section data                               | Byte data

Section identifiers can be defined by the application, e.g. 1 for application, 2 for configuration, 3 for external flash, 4 for flash of a display etc.

## Build/Develop

Use [conan](README-conan.md)

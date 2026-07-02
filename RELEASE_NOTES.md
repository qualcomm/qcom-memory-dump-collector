# QMDC Release Notes

## v1.2.2

### Bug Fixes
- Filter out devices that aren't in true crash mode from device list

---

## v1.2.1

> **Note:** Since this version, CLI only works with user space driver 1.0.2.2 (Windows) or 1.0.1.8 (Linux).

### Features
- Filter out devices with no protocol available from device list
- Add MCP Server for QMDC with sample client and PowerPoint presentation generator
- Add Windows 32-bit (Win32/x86) build support
- Add ACPI condition for identifying ARM64 processor
- Support open-source userspace installer 1.0.2.2 for libusb dynamic loader
- Add ptrace logging for device tracing
- Sync-up QDS platform library

### Bug Fixes
- Fix ghost device and false Sahara detection after crash dump reset
- Fix port mismatch issue on Windows ARM64 — new port number parser for ARM64
- Fix logger permission errors in multi-user scenarios
- Fix incorrect boolean return value in QDS
- Fix progress bar updates during crash collection
- Fix enumeration issue with Linux kernel driver
- Skip USB interface node to prevent incorrect device matching

### Improvements
- Phase II code cleanup — remove dead code, rename rpc to service layer, update license headers
- Remove firehose protocol, download build, and cascading dead code
- Update device detection logic
- Detect crash mode on device connection
- Use SerNumMSM as device context map key on Windows
- Replace CM_Get_DevNode_PropertyW with CM_Get_DevNode_Registry_Property for broader compatibility

### Documentation & Compliance
- Add/update source license headers across all files
- Add LGPL compliance artifacts for bundled libusb static link
- Update User Guide PDF and Docker README
- User guide updates

---

## v1.1.5

### Features
- Initial QMDC CLI release — Qualcomm Memory Dump Collector
- Cross-platform support: Windows (x64, ARM64) and Linux (x86_64)
- USB-based crash dump collection via Sahara protocol
- ADB device enumeration
- Verbose logging mode with configurable log output
- Version and sequencing in output file names
- JSON output format support
- Add Docker file and README for containerized deployment
- Error out if ADB server is running (prevents port conflicts)

### Bug Fixes
- Fix thread timing issue that leads to crash during collection
- Fix pushback issue on HLOS
- Reset device in case of crash collection failure
- Optimize device detection

### Improvements
- Display correct tool name in help menu
- Logger size limit initialized to 100MB
- Add line number to log output
- Debug logs print only when verbose mode is enabled
- Verbose log support
- Custom log file location
- Print error messages from DLL
- Help menu update — support `qmdc -h`

### Documentation
- Initial user guide and README
- QMDC documentation
- Add open-source license files

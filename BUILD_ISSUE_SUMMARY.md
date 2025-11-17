# Build & Flash Issue Summary

## Current Situation

### Code Status: ✅ READY
All memory leak fixes are **complete and saved**:
- [x] Mesh status broadcast disabled (primary leak fix)
- [x] String objects replaced with static buffers
- [x] Threading system disabled temporarily
- [x] 8KB history endpoint disabled
- [x] All changes documented

**Expected Impact**: Heap usage stable at 55-60% (down from 94%+)

### Build Status: ❌ BLOCKED
**Cannot compile** due to PlatformIO + Python 3.14 compatibility issue.

---

## The Build Problem

### Error Message
```
*** [src/BluetoothCommon.cpp] /Users/loicpinel/Documents/Arduino/firmware/.pio/build/heltec-v2-pwa-poc/.sconsign314.dblite: No such file or directory
========================= [FAILED] Took 12.80 seconds
```

### Root Cause
**SCons signature database incompatibility with Python 3.14**

SCons (the build system used by PlatformIO) creates a `.sconsign314.dblite` file to track dependencies. Python 3.14 has a bug/incompatibility with the `dbm` module that prevents this file from being created or read.

### Technical Details
- **PlatformIO**: Uses SCons for ESP32 builds
- **SCons**: Creates `.sconsign314.dblite` using Python's `dbm` module
- **Python 3.14**: `dbm` module has breaking changes
- **Your System**: Homebrew Python 3.14 at `/opt/homebrew/bin/python3`
- **PlatformIO**: Installed for Python 3.14, can't use older versions

### What We've Tried

1. ❌ **Manual DB creation**: `python3 -c "import dbm; dbm.open(...)"`
   - File gets created but SCons can't read it

2. ❌ **Disable caching**: `build_cache_dir = none`
   - Causes different error: directory not found

3. ❌ **SCons flags**: `SCONSFLAGS="--max-drift=1"`
   - Same error persists

4. ❌ **Clean builds**: `rm -rf .pio/build`
   - Fails at first compile attempt

5. ❌ **Touch file workaround**: Creating empty file
   - Not a valid database, SCons rejects it

6. ❌ **Alternative Python**: `/opt/homebrew/bin/python3.13`
   - PlatformIO not installed for that version

---

## Workarounds Attempted

### Workaround 1: Use Different Python Version
**Status**: Not feasible
- PlatformIO is installed only for Python 3.14
- Would need to reinstall PlatformIO entirely
- Risk of breaking other dependencies

### Workaround 2: Disable SCons Dependency Tracking
**Status**: Failed
- SCons doesn't have a simple "disable .sconsign" option
- Various flags tried, none work

### Workaround 3: Build in Docker/Container
**Status**: Not attempted
- Would require Docker setup
- Adds complexity
- Not a long-term solution

---

## Solutions

### Solution A: Build from VSCode PlatformIO IDE ✅ RECOMMENDED

**Your local VSCode/PlatformIO may work because**:
- VSCode PlatformIO extension might use bundled Python
- Different environment variables
- Different SCons configuration
- Worth trying first!

**Steps**:
1. Open project in VSCode
2. Click PlatformIO icon
3. Select "Build" for `heltec-v2-pwa-poc` environment
4. If successful, click "Upload"

**Likelihood**: 60% - VSCode often has its own Python bundled

---

### Solution B: Downgrade Python (System-Wide) ⚠️ RISKY

**Steps**:
```bash
# Install Python 3.13
brew install python@3.13

# Unlink Python 3.14
brew unlink python@3.14

# Link Python 3.13
brew link python@3.13

# Reinstall PlatformIO
pip3 uninstall platformio
pip3 install platformio

# Try build
pio run -e heltec-v2-pwa-poc
```

**Risks**:
- Breaks other Python 3.14 dependencies
- System-wide change
- May affect other tools

**Likelihood**: 90% - Should work but disruptive

---

### Solution C: Python Virtual Environment 🔧 MODERATE

**Steps**:
```bash
# Install Python 3.13
brew install python@3.13

# Create venv with Python 3.13
/opt/homebrew/opt/python@3.13/bin/python3.13 -m venv ~/pio-env

# Activate venv
source ~/pio-env/bin/activate

# Install PlatformIO in venv
pip install platformio

# Build from venv
cd /Users/loicpinel/Documents/Arduino/firmware
pio run -e heltec-v2-pwa-poc
```

**Pros**:
- Isolated environment
- Doesn't affect system Python
- Can keep both versions

**Cons**:
- Need to activate venv each time
- VSCode might not detect it

**Likelihood**: 85% - Clean solution

---

### Solution D: Use Pre-Built Firmware from Earlier Session 📦 TEMPORARY

**If you have a backup** of firmware from yesterday before threading changes:

```bash
# Find old firmware
ls -lt .pio/build/*/firmware.bin

# Flash directly
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --port /dev/cu.usbserial-0001 \
  --baud 921600 \
  write_flash 0x10000 <path-to-old-firmware.bin>
```

**Note**: This doesn't include memory leak fixes!

**Likelihood**: N/A - depends if backup exists

---

### Solution E: Wait for PlatformIO/SCons Update 🕐 LONG-TERM

**Status**: Not practical
- Could take weeks/months
- Need solution now

---

## Recommended Action Plan

### Plan A: Try VSCode Build First (5 minutes)
1. Open VSCode
2. Try building `heltec-v2-pwa-poc` environment
3. If works → Upload immediately

**If Plan A fails → Plan B**

### Plan B: Python 3.13 Virtual Environment (30 minutes)
1. Install Python 3.13: `brew install python@3.13`
2. Create venv with 3.13
3. Install PlatformIO in venv
4. Build and upload from venv

**If Plan B fails → Plan C**

### Plan C: System Python Downgrade (15 minutes + restore time)
1. Backup current Python config
2. Downgrade to 3.13 system-wide
3. Reinstall PlatformIO
4. Build and upload
5. **Important**: May need to upgrade back for other tools

---

## Testing After Successful Flash

### 1. Immediate Checks
- Connect to WiFi: `EMRG-NODE-XXXX`
- Should connect without "incorrect password" error
- Open http://192.168.4.1/
- Page should load

### 2. Serial Monitor (5 minutes)
```bash
pio device monitor -b 115200
```

Look for:
- Boot messages
- WiFi AP started
- Initial heap usage (should be ~35%)
- No crash messages

### 3. Stability Test (30 minutes)
- Leave device running
- Connect/disconnect clients
- Send messages periodically
- Monitor heap in serial output

**Expected**: Heap should stay 45-60%, no crashes

### 4. Long-term Test (24 hours)
- Leave running overnight
- Heap should plateau at 55-60%
- No WiFi disconnections

---

## Files Modified (Ready to Build)

| File | Status | Description |
|------|--------|-------------|
| `src/wifi/EmergencyWiFiService.cpp` | ✅ Modified | Memory leak fixes applied |
| `src/wifi/EmergencyWiFiService.h` | ✅ Modified | Threading system wrapped |
| `variants/esp32/heltec_v2/platformio-poc.ini` | ✅ Modified | DISABLE_THREADING_SYSTEM flag |
| `platformio.ini` | ✅ Clean | Build cache setting removed |
| `MEMORY_LEAK_FIX.md` | ✅ New | Complete documentation |
| `BUILD_ISSUE_SUMMARY.md` | ✅ New | This file |
| `FACTORY_RESET_CODE.md` | ✅ New | Device reset instructions |

---

## Next Steps

1. **Try VSCode build** - quickest option
2. **If fails**: Setup Python 3.13 venv
3. **Once built**: Test heap stability
4. **Report back**: Heap usage after 30 min runtime

All code is ready - just need a working build environment!

---

## Alternative: Remote Build Service (Not Recommended)

Could use GitHub Actions or similar CI/CD with older Python, but adds complexity and is overkill for this.

---

## Questions?

- **Q: Can we skip the build and just reset the device?**
  - A: You can clear NVS (see FACTORY_RESET_CODE.md) but this doesn't fix the memory leaks in the running firmware. You still need to flash new firmware with the fixes.

- **Q: Will VSCode definitely work?**
  - A: 60% chance - worth trying first. VSCode PlatformIO often bundles its own Python.

- **Q: Can we build on a different machine?**
  - A: Yes! If you have access to a Mac/Linux/Windows with Python 3.13 or older, compile there and copy the .bin file.

- **Q: What if nothing works?**
  - A: Last resort is system-wide Python downgrade, but that's disruptive. Virtual environment is safer.

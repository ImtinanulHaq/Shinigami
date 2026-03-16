# Shinigami Terminal - Running Guide

## ✅ Fixed Issues

- ✅ Colors are now **optional** (terminal bina colors support ke bhi chal jayay ga)
- ✅ Added better error messages for debugging
- ✅ Test script created to validate environment

## 🚀 How to Run

### Option 1: Using Test Script (Recommended)

```bash
cd /home/muhammad-imtinan-ul-haq/Desktop/middleware
./test_shinigami.sh
```

This script:
- Checks terminal size (minimum 100 columns × 28 rows)
- Sets proper UTF-8 and color environment
- Shows diagnostic information
- Warns if terminal is too small

### Option 2: Direct Execution

```bash
cd /home/muhammad-imtinan-ul-haq/Desktop/middleware
./build/relwithdebinfo/bin/RelWithDebInfo/shinigami-terminal
```

## 📋 Requirements

**Terminal Size (MUST):**
- Minimum: **100 columns × 28 rows**
- If terminal is smaller, it will fail with error message

**Setting Terminal Size:**

If your terminal is too small, you have several options:

```bash
# Option 1: Maximize your terminal window in the GUI first!
# Option 2: Use resize command
resize

# Option 3: Manually set size
stty rows 30 cols 120

# Option 4: Check current size
tput cols  # columns
tput lines # rows
```

## 🎮 Keyboard Controls Once Running

| Key | Action |
|-----|--------|
| **1-8** | Jump to panel (Dashboard, Services, Proxies, etc.) |
| **Tab** | Next panel |
| **Shift+Tab** | Previous panel |
| **:** | Open command palette |
| **ESC** or **Ctrl+C** | Quit |

## 📊 Available Panels

1. **Dashboard** - System overview (CPU, memory, uptime)
2. **Services** - Control services (start/stop/restart)
3. **Proxies** - Live proxy metrics
4. **Security** - Security violations & audit trail
5. **HAL** - Hardware device control
6. **Logs** - Live log tailing
7. **Monitor** - Real-time metrics from monitord
8. **Help** - Keyboard shortcuts

## ⚠️ Troubleshooting

### Error: "Terminal too small"
```
❌ ERROR: Terminal too small: 80x20 (need 100x28)
```
**Fix:** Resize your terminal to at least 100×28
```bash
stty rows 30 cols 120
./test_shinigami.sh
```

### Error: "Failed to initialize terminal engine"
```
❌ ERROR: initscr() failed
```
**Fix:** Your terminal doesn't support ncurses. Try:
```bash
export TERM=xterm-256color
./test_shinigami.sh
```

### Error: "Terminal does not support colors"
```
⚠️  WARNING: Terminal does not support colors (continuing anyway)
```
**Note:** This is just a warning - the application will still run fine in monochrome mode

## 🔧 Build Command (if needed)

```bash
cd /home/muhammad-imtinan-ul-haq/Desktop/middleware/build/relwithdebinfo
ninja shinigami-terminal
```

## 📝 What's Next

The terminal is now ready for:
- Dashboard implementation (real CPU/memory data)
- Command execution (SM socket integration)
- Log watching (inotify integration)
- Full panel implementations

#!/bin/bash

# Test script to run shinigami-terminal with proper environment

echo "📋 Shinigami Terminal Test Script"
echo "=================================="
echo ""

# Set proper terminal environment
export TERM=xterm-256color
export LC_ALL=en_US.UTF-8

# Get current terminal size
cols=$(tput cols 2>/dev/null || echo "0")
rows=$(tput lines 2>/dev/null || echo "0")

echo "Current Terminal:"
echo "  - Columns: $cols"
echo "  - Rows: $rows"
echo "  - Terminal: $TERM"
echo ""

# Check if terminal is large enough
MIN_COLS=100
MIN_ROWS=28

if [ "$cols" -lt "$MIN_COLS" ] || [ "$rows" -lt "$MIN_ROWS" ]; then
    echo "⚠️  WARNING: Terminal is too small!"
    echo "   Minimum required: ${MIN_COLS}x${MIN_ROWS}"
    echo "   Current size: ${cols}x${rows}"
    echo ""
    echo "💡 To resize your terminal:"
    echo "   - Maximize your terminal window"
    echo "   - Run: resize"
    echo ""
    exit 1
fi

echo "✅ Terminal size is OK"
echo ""
echo "🚀 Starting shinigami-terminal..."
echo ""

# Run the application
./build/relwithdebinfo/bin/RelWithDebInfo/shinigami-terminal

echo ""
echo "👋 Shinigami Terminal exited"

#!/bin/bash
# Interactive Phase 3 CLI Middleware Demo

cd "$(dirname "$0")" || exit

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                                                                ║"
echo "║      Phase 3 Event-Driven Middleware Platform - CLI            ║"
echo "║                                                                ║"
echo "║      Professional Command-Line Interface                       ║"
echo "║      Version 3.0.0 - Production Release                        ║"
echo "║                                                                ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "Starting interactive CLI session..."
echo ""

# Check if compiled binary exists
if [ ! -f "./middleware-cli-x86" ]; then
    echo "❌ Middleware CLI not compiled!"
    echo ""
    echo "Compile with:"
    echo "  g++ -std=c++17 -O2 middleware-cli.cpp -o middleware-cli-x86"
    echo ""
    echo "For ARM64 cross-compilation:"
    echo "  aarch64-linux-gnu-g++ -std=c++17 -O2 middleware-cli.cpp -o middleware-cli"
    exit 1
fi

echo "Commands Available:"
echo "  • help              - Show all commands"
echo "  • status            - Service status"
echo "  • list              - List services"
echo "  • start <service>   - Start service"
echo "  • stop <service>    - Stop service"
echo "  • restart <service> - Restart service"
echo "  • version           - Middleware version"
echo "  • capability        - Manage capabilities"
echo "  • logs <service>    - View logs"
echo "  • stats             - Performance statistics"
echo "  • monitor <service> - Monitor service"
echo "  • register <app>    - Register app"
echo "  • discover          - Service discovery"
echo "  • exit              - Quit"
echo ""

# Launch CLI
./middleware-cli-x86

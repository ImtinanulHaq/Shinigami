# Service Manager Testing Framework (Levels 1-5)

Professional, industry-grade testing infrastructure for the Service Manager codebase.

## Overview

This testing framework implements comprehensive testing across 5 levels:

- **Level 1: Static Analysis** - Code quality and static verification
- **Level 2: Unit Tests** - Individual module testing
- **Level 3: Integration Tests** - Module interaction testing
- **Level 4: End-to-End Tests** - Full system lifecycle testing
- **Level 5: Fuzz Testing** - Memory safety and edge case discovery

## Quick Start

### Run All Tests

```bash
cd testing/service_manager_testing
./run_tests_new.sh --all
```

### Run Specific Level

```bash
# Level 1: Static Analysis
./run_tests_new.sh --static

# Level 2: Unit Tests
./run_tests_new.sh --unit

# Level 3: Integration Tests
./run_tests_new.sh --integration

# Level 4: E2E Tests
./run_tests_new.sh --e2e

# Level 5: Fuzz Testing
./run_tests_new.sh --fuzz
```

## Using Make Directly

```bash
make -f Makefile help              # Show all targets
make -f Makefile test              # Run all levels (1-5)
make -f Makefile test-unit         # Level 2 only
make -f Makefile test-integration  # Level 3 only
make -f Makefile test-e2e          # Level 4 only
make -f Makefile test-fuzz         # Level 5 only
make -f Makefile test-asan         # AddressSanitizer
make -f Makefile test-ubsan        # UndefinedBehaviorSanitizer
make -f Makefile clean             # Cleanup
```

## Level 1: Static Analysis

Automated code quality checks using industry tools.

### Tools

- **cppcheck** - Deep static analysis for C
- **clang-tidy** - Clang-based linter and static analyzer
- **GCC Warnings** - Strict compiler warning levels
- **valgrind** - Memory error detection

### Run

```bash
make test-static
# Or individual tools:
make test-cppcheck
make test-clang-tidy
make test-gcc-warnings
make test-valgrind
```

### What It Checks

- Memory leaks and corruption
- Uninitialised variables
- Buffer overflows
- Dead code
- Logic errors
- Type consistency

## Level 2: Unit Tests

Isolated testing of individual modules using Unity framework.

### Test Files

- `test_sm_protocol.c` - Protocol validation (10 tests)
  - Valid/invalid headers
  - Payload size validation
  - Service name validation
  - Path validation
  - Message type constants

- `test_sm_registry.c` - Registry operations (11 tests)
  - Service registration/lookup
  - Duplicate detection
  - Capacity limits
  - Heartbeat updates
  - Null parameter handling

- `test_sm_crypto.c` - Cryptography (10 tests)
  - HMAC-SHA256 computation
  - Verification (valid/modified/wrong key)
  - Empty message handling
  - Large message support
  - Timing-safe verification

- `test_sm_rate_limit.c` - Rate limiting (10 tests)
  - Quota tracking
  - Per-PID isolation
  - Window refill
  - Concurrent stress
  - Edge cases

### Run

```bash
make test-unit
# Run individual tests:
./build/test_sm_protocol
./build/test_sm_registry
./build/test_sm_crypto
./build/test_sm_rate_limit
```

### Test Framework

Uses lightweight Unity C testing framework (minimal dependencies):

```c
TEST_BEGIN(test_name)
{
    /* Test code */
    TEST_ASSERT_EQUAL_INT(actual, expected);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_TRUE(condition);
}
```

## Level 3: Integration Tests

Modules working together with real sockets/server.

### Test Script: `integration_test.sh`

Tests:
1. Server startup
2. Config reload on SIGHUP
3. Health monitoring integration
4. Persistence/autosave
5. Rate limiter enforcement
6. Audit logging
7. Signal safety (signalfd)
8. Registry + Protocol + Handlers
9. Cleanup and recovery
10. (Plus 2 more comprehensive tests)

### Run

```bash
./integration_test.sh
```

### What It Verifies

- Server lifecycle (start/stop)
- Signal handling (SIGHUP, SIGTERM)
- Background thread operations
- Config reloading
- Graceful shutdown
- Resource cleanup
- File persistence

## Level 4: End-to-End Tests

Full system lifecycle with client interactions.

### Test Script: `e2e_test.sh`

Tests:
1. Service registration and lookup
2. Concurrent clients
3. Service heartbeat
4. Unregistration
5. Authentication/HMAC
6. Rate limiting
7. Audit trail
8. Request ID tracking
9. Metrics collection
10. Graceful shutdown
11. Crash recovery
12. Dependency-aware shutdown
13. Management API
14. Multi-process interop
15. Error handling

### Run

```bash
./e2e_test.sh
```

### What It Verifies

- Full service lifecycle
- Authentication enforcement
- Rate limiting across requests
- Graceful vs forced shutdown
- State recovery after crash
- Multi-process interaction
- Error handling and recovery

## Level 5: Fuzz Testing

Memory safety and edge case discovery.

### Targets

- `fuzz_target` - Standard fuzz target
- `fuzz_target_asan` - AddressSanitizer instrumented
- `fuzz_target_ubsan` - UndefinedBehaviorSanitizer instrumented

### Build

```bash
make fuzz_target           # Standard
make fuzz_target_asan      # Memory safety
make fuzz_target_ubsan     # Undefined behavior
```

### Run with AFL++

```bash
# Install AFL++ first:
# brew install afl++ (macOS)
# apt install afl++ (Linux)

# Create input directory with seed
mkdir -p fuzz_input
echo -ne '\x53\x4d\x01\x00' > fuzz_input/seed.bin

# Run fuzzing with ASAN
afl-fuzz -i fuzz_input -o fuzz_output ./build/fuzz_target_asan

# Or with UBSAN
afl-fuzz -i fuzz_input -o fuzz_output ./build/fuzz_target_ubsan
```

### Sanitizers

#### AddressSanitizer (ASAN)
Detects:
- Heap buffer overflows
- Stack buffer overflows
- Global buffer overflows
- Use-after-free bugs
- Double-free bugs
- Memory leaks

#### UndefinedBehaviorSanitizer (UBSAN)
Detects:
- Integer overflow/underflow
- Shift errors
- Signed/unsigned conversion errors
- Null pointer dereference
- Type conversion errors

## File Structure

```
testing/service_manager_testing/
├── run_tests_new.sh           # Master test harness
├── run_tests.sh               # Legacy harness
├── Makefile                   # Build configuration
├── unity.h/c                  # Test framework
├── test_sm_protocol.c         # Level 2: Protocol tests
├── test_sm_registry.c         # Level 2: Registry tests
├── test_sm_crypto.c           # Level 2: Crypto tests
├── test_sm_rate_limit.c       # Level 2: Rate limit tests
├── integration_test.sh        # Level 3: Integration tests
├── e2e_test.sh                # Level 4: E2E tests
├── fuzz_target.c              # Level 5: Fuzz target
├── build/                     # Build artifacts
│   ├── test_sm_protocol       # Protocol test executable
│   ├── test_sm_registry       # Registry test executable
│   ├── test_sm_crypto         # Crypto test executable
│   ├── test_sm_rate_limit     # Rate limit test executable
│   ├── fuzz_target            # Fuzz target executable
│   ├── fuzz_target_asan       # ASAN-instrumented fuzz target
│   └── fuzz_target_ubsan      # UBSAN-instrumented fuzz target
├── fuzz_input/                # Fuzzing seed corpus
├── fuzz_output/               # Fuzzing results
└── README.md                  # This file
```

## Coverage

### Protocol Module
- Valid header validation
- Message type verification
- Payload length limits
- Constants validation

### Registry Module
- CRUD operations
- Duplicate detection
- Capacity limits
- Heartbeat tracking

### Cryptography Module
- HMAC computation
- Signature verification
- Timing-safe comparison
- Large message support

### Rate Limiter Module
- Quota enforcement
- Per-client isolation
- Window management
- Refill logic

### Integration
- Multi-threaded safety
- Signal handling
- Config reloading
- Graceful shutdown

## Expected Results

### Full Test Run

```
Level 1: Static Analysis
- CPPCheck: 0 errors
- Clang-tidy: 0 warnings
- GCC strict: 0 warnings
- Valgrind: 0 leaks

Level 2: Unit Tests
- Protocol: 10/10 passed
- Registry: 11/11 passed
- Crypto: 10/10 passed
- Rate Limit: 10/10 passed

Level 3: Integration Tests
- Server lifecycle: ✓
- Signal handling: ✓
- Persistence: ✓
- (10 total tests)

Level 4: E2E Tests
- Service lifecycle: ✓
- Authentication: ✓
- Crash recovery: ✓
- (15 total tests)

Level 5: Fuzz Testing
- Targets built: ✓
- Ready for AFL++
```

## Troubleshooting

### Compilation Errors

```bash
# Missing dependencies
apt install libssl-dev       # OpenSSL development
apt install libunitc-dev     # Unity test framework
apt install valgrind         # Memory checker
apt install cppcheck         # Static analyzer
apt install clang-tools      # Clang tools
```

### Test Failures

1. **Check build directory**: `rm -rf build && make clean`
2. **Rebuild service manager**: `cd ../.. && make clean && make`
3. **Check permissions**: `chmod +x *.sh`
4. **Verify dependencies**: `make info`

### Integration Test Failures

- Ensure servicemanager binary is compiled: `../../build/core/service_manager/servicemanager`
- Check socket permissions: `/tmp/sm_*.sock`
- Verify no other instance running: `pkill servicemanager`

## Performance Expectations

- **Static Analysis**: 5-10 seconds
- **Unit Tests**: 2-5 seconds
- **Integration Tests**: 10-15 seconds
- **E2E Tests**: 15-30 seconds
- **Fuzz Testing** (setup): <5 seconds

## Industry Standards

This framework meets/exceeds:
- MISRA C compliance checks
- CWE (Common Weakness Enumeration) coverage
- OWASP secure coding practices
- Google C++ style guidelines (adapted for C)
- CERT secure coding standards

## Continuous Integration

For CI/CD pipelines:

```bash
# Quick validation (< 30s)
make test-unit

# Full validation (< 120s)
make test

# Memory safety focus
make test-asan
```

## Support

For issues or enhancements:
1. Check test logs in `build/` directory
2. Run individual test for debugging
3. Use `-x` flag in bash tests for detailed output
4. Review core module documentation

---

**Framework Version**: 1.0
**Last Updated**: 2024
**Status**: Production Ready ✓

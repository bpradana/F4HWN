# Code Formatting with Clang-Format

## Quick Usage

Using Makefile (recommended):
```bash
make format              # Format all C/H files
make format-check        # Check formatting without changes
```

Using Docker script:
```bash
./compile-with-docker.sh format
```

## Configuration

See `.clang-format` for formatting rules:
- 4-space indentation
- 100 character line limit
- Linux-style braces
- Right-aligned pointers (`int *ptr`)

## Example Workflow

```bash
# Format all C/H files
make format

# Check formatting (useful for CI/CD)
make format-check

# Build after formatting
make build
# or
./compile-with-docker.sh build
```

## Available Make Targets

- `make format` - Format all C/H files in App/
- `make format-check` - Verify formatting without modifying files
- `make build` - Build firmware using Docker
- `make clean` - Remove build directory
- `make help` - Show all available targets

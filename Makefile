.PHONY: format format-check help clean

# Default target
help:
	@echo "F4HWN Firmware - Available Make Targets:"
	@echo ""
	@echo "  make format        - Format all C/H files in App/ directory"
	@echo "  make format-check  - Check formatting without modifying files"
	@echo "  make build         - Build firmware using Docker"
	@echo "  make clean         - Remove build directory"
	@echo ""

# Format all C/H files in App directory
format:
	@echo "🎨 Formatting all C/H files in App/..."
	@find App -path 'App/external' -prune -o \( -name '*.c' -o -name '*.h' \) -print | xargs clang-format -i --style=file
	@echo "✅ Formatting complete!"

# Check if files need formatting (useful for CI/CD)
format-check:
	@echo "🔍 Checking code formatting..."
	@find App -path 'App/external' -prune -o \( -name '*.c' -o -name '*.h' \) -print | xargs clang-format --dry-run --Werror --style=file
	@echo "✅ All files are properly formatted!"

# Build firmware using Docker script
build:
	@./compile-with-docker.sh build

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build directory..."
	@rm -rf build
	@echo "✅ Clean complete!"

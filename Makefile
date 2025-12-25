.PHONY: format format-check lint lint-check help clean

# Default target
help:
	@echo "F4HWN Firmware - Available Make Targets:"
	@echo ""
	@echo "  make format        - Format all C/H files in App/ directory"
	@echo "  make format-check  - Check formatting without modifying files"
	@echo "  make lint          - Run clang-tidy and auto-fix issues"
	@echo "  make lint-check    - Check code with clang-tidy without modifying files"
	@echo "  make build         - Build firmware using Docker"
	@echo "  make clean         - Remove build directory"
	@echo ""

# Format all C/H files in App directory
format:
	@./compile-with-docker.sh format

# Check if files need formatting (useful for CI/CD)
format-check:
	@./compile-with-docker.sh format-check

# Run static analysis with clang-tidy and auto-fix issues
lint:
	@./compile-with-docker.sh lint

# Run static analysis with clang-tidy and auto-fix issues (safe mode)
lint-force:
	@./compile-with-docker.sh lint-force

# Check code with clang-tidy without modifying files
lint-check:
	@./compile-with-docker.sh lint-check

# Build firmware using Docker script
build:
	@./compile-with-docker.sh build

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build directory..."
	@rm -rf build
	@echo "✅ Clean complete!"

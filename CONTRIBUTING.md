# Contributing to Antigravity Chrome Bridge

Thank you for considering contributing! Here's how you can help.

## Getting Started

1. Fork the repository
2. Clone your fork
3. Create a feature branch: `git checkout -b feature/my-feature`
4. Make your changes
5. Build and test: `cmake -B build && cmake --build build`
6. Commit: `git commit -m 'Add my feature'`
7. Push: `git push origin feature/my-feature`
8. Open a Pull Request

## Development Setup

### Prerequisites
- CMake 3.20+
- C++17 compiler
- Chrome browser (for testing)

### Building
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Testing
1. Start the built binary
2. Connect via Chrome Extension or CDP
3. Send MCP tool calls via stdin

## Code Style

- Use `ag` namespace for all code
- Follow existing formatting (4-space indentation)
- Keep functions focused and under 50 lines where possible
- Use `json` return types for all tool handlers
- Add error handling with try/catch for all external calls

## Reporting Bugs

Open an issue with:
- Steps to reproduce
- Expected vs actual behavior
- OS and Chrome version
- Bridge mode (CDP or Extension)

## Feature Requests

Open an issue describing:
- The problem you're solving
- Your proposed solution
- Any alternatives you've considered

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.

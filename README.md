# Multithreaded Proxy Server with LRU Cache

A high-performance HTTP/HTTPS proxy server implementation with caching capabilities and GUI monitoring.

## Features

- **Multithreaded Architecture**: Handles multiple client connections simultaneously using POSIX threads
- **Protocol Support**:
  - HTTP (GET, POST) request handling
  - HTTPS tunneling via CONNECT method
- **LRU Caching System**:
  - In-memory cache with doubly linked list implementation
  - Configurable cache size (default: 10 entries)
  - Cache hit/miss monitoring
- **Security Features**:
  - Domain blocking capabilities
  - Input validation and sanitization
  - Proper error handling
- **GUI Monitoring**:
  - Real-time request logging
  - Cache hit/miss visualization
  - Server status monitoring
- **Performance Optimizations**:
  - Non-blocking I/O using select()
  - Efficient memory management
  - Thread pooling

## Technical Architecture

### Core Components

1. **Request Parser**:

   - Parses HTTP headers and request lines
   - Validates protocol compliance
   - Extracts host, path, and method information

2. **Cache Manager**:

   - LRU cache implementation using doubly linked list
   - O(1) cache operations
   - Thread-safe caching mechanism

3. **Connection Handler**:

   - Multithreaded client handling
   - Efficient data tunneling
   - Error recovery mechanisms

4. **GUI Interface**:
   - GTK-based monitoring interface
   - Real-time status updates
   - Server statistics display

## Build Instructions

1. Dependencies:

   ```bash
   # Install required packages
   sudo apt-get install build-essential libgtk-3-dev
   ```

2. Compilation:

   ```bash
   make clean   # Clean previous builds
   make        # Build the proxy server
   ```

3. Execution:
   ```bash
   ./proxy     # Start the proxy server
   ```

## Usage

The proxy server listens on port 8080 by default and can be used with any web browser or HTTP client:

1. **Browser Configuration**:

   - Set HTTP proxy to: localhost:8080
   - Set HTTPS proxy to: localhost:8080

2. **Direct Usage**:
   ```bash
   curl -x http://localhost:8080 http://example.com
   ```

## Configuration

- `PORT`: Default 8080 (modify in source)
- `BUFFER_SIZE`: 8192 bytes
- `CACHE_SIZE`: 10 entries
- Blocked domains can be configured in the `is_blocked()` function

## Implementation Details

1. **Threading Model**:

   - One thread per client connection
   - Thread pooling for efficiency
   - Mutex-protected shared resources

2. **Caching Algorithm**:

   - LRU (Least Recently Used) implementation
   - O(1) insertion and lookup
   - Double-linked list + HashMap approach

3. **Protocol Support**:
   - Full HTTP/1.1 compliance
   - HTTPS tunneling support
   - Keep-alive connections

## Security Features

- Host blacklisting
- Request sanitization
- Buffer overflow prevention
- Error handling for malformed requests

## Monitoring and Logging

- Real-time request monitoring
- Cache performance statistics
- Error logging and reporting
- GUI-based status display

## Contributing

1. Fork the repository
2. Create a feature branch
3. Commit changes
4. Push to the branch
5. Create a Pull Request

## License

This project is licensed under the MIT License. See LICENSE file for details.

## Authors

Originally developed as part of Operating Systems coursework.

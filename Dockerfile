# ==============================================================================
# Stage 1: Build the React web frontend console
# ==============================================================================
FROM node:20-alpine AS frontend

WORKDIR /app/web

# Install dependencies deterministically
COPY web/package.json web/package-lock.json ./
RUN npm ci

# Copy web source and build production assets into /app/web/dist
COPY web/ ./
RUN npm run build

# ==============================================================================
# Stage 2: Build the C++ database engine and HTTP server
# ==============================================================================
FROM debian:bookworm-slim AS backend

# Install C++17 compiler toolchain, CMake (>= 3.20), and Ninja
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy CMake configuration, source files, and test manifests (required for CMake tree)
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY tests/ ./tests/

# Configure Release build and compile ONLY the emberdb_server target
RUN cmake -B build-out -G Ninja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build-out --target emberdb_server

# ==============================================================================
# Stage 3: Minimal runtime image
# ==============================================================================
FROM debian:bookworm-slim AS runtime

# Install runtime C++ standard library dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the compiled standalone C++ server binary from backend stage
COPY --from=backend /build/build-out/bin/emberdb_server /app/emberdb_server
RUN chmod +x /app/emberdb_server

# Copy the compiled production web assets from frontend stage
COPY --from=frontend /app/web/dist /app/web/dist

# Ensure ephemeral database storage directory exists
RUN mkdir -p /app/data

# Default port expected by Render Web Services
ENV PORT=10000
EXPOSE 10000

# Start EmberDB HTTP REST server on 0.0.0.0:$PORT with database in /app/data
CMD ["sh", "-c", "./emberdb_server --port ${PORT:-10000} --db data"]

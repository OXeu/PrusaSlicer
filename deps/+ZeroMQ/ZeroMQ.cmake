add_cmake_project(ZeroMQ
        URL "https://github.com/zeromq/zmqpp/archive/refs/tags/4.2.0.zip"
        URL_HASH SHA256=17c3e3de7603b31e24d2c2dd1c720f3adfb8fc627ff9fc70e60a3267a5441b44
        CMAKE_ARGS
        -DBUILD_TESTS=OFF
)

add_cmake_project(LibZeroMQ
        URL "https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.zip"
        URL_HASH SHA256=276f5c9213589c4ecf0d8c836018604e7a56542b10913cfc16d0adcfbc04298c
        CMAKE_ARGS
        -DBUILD_TESTS=OFF
)

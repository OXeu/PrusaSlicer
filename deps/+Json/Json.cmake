add_cmake_project(Json
    URL "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.zip"
    URL_HASH SHA256=34660b5e9a407195d55e8da705ed26cc6d175ce5a6b1fb957e701fb4d5b04022
    CMAKE_ARGS
        -DBUILD_TESTS=OFF
)
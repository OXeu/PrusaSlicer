add_cmake_project(Catch2
    URL "https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.zip"
    URL_HASH SHA256=11e422a9d5a0a1732b3845ebb374c2d17e9d04337f3e717b21210be4ec2ec45b
    CMAKE_ARGS
        -DCATCH_BUILD_TESTING:BOOL=OFF
        -DCMAKE_CXX_STANDARD=17
)

## 在 mbedtls 项目目录下创建工程，生成相关动态库文件
- mkdir build
- cd build
- cmake .. -DUSE_SHARED_MBEDTLS_LIBRARY=On -DCMAKE_INSTALL_PREFIX=/tmp/mbedtls
- make
- make install

## 进入 mytest 目录下生成可执行文件

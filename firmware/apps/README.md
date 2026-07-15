# firmware/apps/ — project template

Every app is a self-contained ESP-IDF project using the shared components.
Copy this template **exactly** (T10 creates the first real instance):

```
firmware/apps/<name>/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.c
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<name>)
```

`sdkconfig.defaults` (add app-specific lines below these):

```
CONFIG_IDF_TARGET="esp32"
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

`main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES <shared components this app uses>)
```

Notes:

- `sdkconfig` (generated) is git-ignored; only `sdkconfig.defaults` is
  committed. CI runs `idf.py set-target esp32 build` per app.
- Never copy a component's sources into an app — depend on it.
- Console-based apps use the IDF `esp_console` REPL on UART0 (see
  `bringup_radio` once it exists).

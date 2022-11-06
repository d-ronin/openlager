set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)

cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(SIZE arm-none-eabi-size)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(stm32StdLibTemplate C CXX ASM)

set(CMAKE_SYSTEM_PROCESSOR cortex-m4)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

set(LINKER_SCRIPT_NAME STM32F411CCUX_FLASH)
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/startup/${LINKER_SCRIPT_NAME}.ld)

add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=${CMAKE_SYSTEM_PROCESSOR} -g3 -std=gnu++${CMAKE_CXX_STANDARD} -Os -ffunction-sections -fdata-sections -fno-strict-aliasing -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -std=gnu++2a -fstack-usage --specs=nano.specs -mfloat-abi=soft -mthumb")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=${CMAKE_SYSTEM_PROCESSOR} -std=gnu${CMAKE_C_STANDARD} -g3 -Os -ffunction-sections -fdata-sections -fno-strict-aliasing -Wall -fstack-usage --specs=nano.specs -mfloat-abi=soft -mthumb")
set(CMAKE_EXE_LINKER_FLAGS "-mcpu=${CMAKE_SYSTEM_PROCESSOR} -g3 -T ${LINKER_SCRIPT} --specs=nosys.specs -Wl,-Map=${PROJECT_NAME}.map  -Wl,--gc-sections -static --specs=nano.specs -mfloat-abi=soft -mthumb -Wl,--start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group")
set(CMAKE_ASM_FLAGS "-mcpu=${CMAKE_SYSTEM_PROCESSOR} -g3 -c -x assembler-with-cpp --specs=nano.specs -mfloat-abi=soft -mthumb")

set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <LINK_FLAGS> -o <TARGET> <OBJECTS>")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <LINK_FLAGS> -o <TARGET> <OBJECTS>")

set(STARTUP_FILE_NAME startup_stm32f411ccux)
set(STARTUP_LOCATION "${CMAKE_SOURCE_DIR}/startup/${STARTUP_FILE_NAME}.s")

include_directories(.)
include_directories(inc)
include_directories(libs/fatfs)
include_directories(libs/cmsis/inc)
include_directories(libs/inc)
include_directories(libs/STM32F4xx_StdPeriph_Driver/inc)

add_definitions(-DSTM32F411xE -DUSE_STDPERIPH_DRIVER)

file(GLOB SOURCES "src/*.*" "libs/cmsis/src/*.*" "libs/fatfs/*.*" "libs/src/*.*" "libs/STM32F4xx_StdPeriph_Driver/src/*.*" "shared/*.*")

add_executable(${PROJECT_NAME} ${STARTUP_LOCATION} ${INCLUDE_FILES} ${SOURCES})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_OBJCOPY} ARGS -Oihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_OBJCOPY} ARGS -Obinary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${SIZE} ARGS --format=berkeley ${PROJECT_NAME}.elf)

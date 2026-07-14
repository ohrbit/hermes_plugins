# ESP-Hermes firmware — top-level CMake project
#
# DRAFT firmware for the M5Stack ESP32-S3 (fork of ESP-Claw edge_agent, LLM
# client stripped). The ESP is a *gateway client*, not an LLM client: it speaks
# WebSocket to the Hermes gateway (gateway/esp_hermes_ws.py) and does I/O only.
#
# NOTE: This is a DRAFT. It has not been compiled/flashed (no hardware yet).
# Build steps are documented in firmware/README.md.

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_hermes_client)

# Warn early if ESP-IDF is missing (developer forgot to source export.sh)
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not set. Source $IDF_PATH/export.sh first.")
endif()

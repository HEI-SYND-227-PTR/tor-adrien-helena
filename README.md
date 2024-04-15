[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-24ddc0f5d75046c5622901739e7c5dd533143b0c8e959d652212380cedb1ea36.svg)](https://classroom.github.com/a/BbCzAwyx)
# 227-PTR Token Ring base project

The base code for the Token Ring project was developed as part of the HEI Real-Time Programming (227-PTR) course.

The provided project is a real-time chat application based on a custom Token Ring protocol implementation using the RTX5 (CMSIS-RTOS2) RTOS.

This is a Keil uVision project that is ready to use. It was tested with Keil ARMCC version 5.06 update 7 (build 960). The project compiles without error but is not functional since the MAC layers have not been implemented.

The project runs on an ARM Cortex-M7 STM32F746 SoC at 216 MHz. uGFX (https://ugfx.io/) is used as a graphical library. The provided project has the stdout/ITM enabled (Debug printf Viewer) and the Event Recorder by default. TraceAnlyzer can be used to debug the real-time application. All configuration settings are available in the `main.h` header file.

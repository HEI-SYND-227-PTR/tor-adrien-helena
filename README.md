[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-24ddc0f5d75046c5622901739e7c5dd533143b0c8e959d652212380cedb1ea36.svg)](https://classroom.github.com/a/BbCzAwyx)
# 227-PTR Token Ring base project

The base code for the Token Ring project was developed as part of the HEI Real-Time Programming (227-PTR) course.

The provided project is a real-time chat application based on a custom Token Ring protocol implementation using the RTX5 (CMSIS-RTOS2) RTOS.

This is a Keil uVision project that is ready to use. It was tested with Keil ARMCC version 5.06 update 7 (build 960). The project compiles without error but is not functional since the MAC layers have not been implemented.

The project runs on an ARM Cortex-M7 STM32F746 SoC at 216 MHz. uGFX (https://ugfx.io/) is used as a graphical library. The provided project has the stdout/ITM enabled (Debug printf Viewer) and the Event Recorder by default. TraceAnlyzer can be used to debug the real-time application. All configuration settings are available in the `main.h` header file.


The project was modified by **Helena Syrbe** and **Adrien Bellon**, in order to add the MAC layer protocol.

This layer add the transmission of information between the PHYSICAL layer and the APPLICATION layer.

The MAC Sender:
Add a management of the token, we can create it, and transmit it when we don't have a message to send, or when we sent one message.
We manage the sending of messages, creation of the frame, calculate the checksum and transfer it to the other station through the PHY sender.
When we receive a message in return from the others stations, we reat according to the information, if there was an error, we send the message again, or if the station is offline, we don't send the message.
We can transmit messages between station, broadcast time to all the station or broadcast messages.

The MAC Receiver:
It will receive information from the PHY receiver (other station) and transfer the frame to the correct application depending of the informations in the frame.
When we receive a message, it will check the checksum of the message with a checksum that we calculated ourself. If it is correct, we send to the source station that we read the message and that it was correct, if not, we send that we read he message, but it was wrong.
When we receive a message that is not for us, we simply give the message directly to the PHY Sender.

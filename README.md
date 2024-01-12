This is a passion project I started in fall semester of senior year. I came across the TIDBYT display which utilizes APIs to show various statistics and other fun animations on an LED Display. Instead of buying it I wanted to challenge myself and create a prototype that is similar.

For this project I utilized an ESP32-S2-DevKitC-1, Waveshare RGB Full Color LED Matrix Panel 4mm Pitch 64x32 Pixels, a Micro SD Card Module and a MPU-6050 Accelerometer.

I have not had the opportunity to utilize the accelerometer. I plan to create a dynamic display which shows that movement of particles based on the rotation of the display.

My next steps are to develop a PCB which could holds these components together neatly as well as a 3d printing case to make it more aesthetic.

Complete firmware is shown in Version2.ino file.


**Features**
- Display Weather
- Display current song playing on spotify (Access Token must be hardcoded currently)
- Show a simulation of Conway's Game of Life
- Play any 64x32 GIF place on Micro SD Card.
- Features a web server in which brightness and the current GIF playing could be controlled

  

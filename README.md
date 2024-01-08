This is a passion project I started in fall semester of senior year. I came across the TIDBYT display which utilizes APIs to show various statistics and other fun animations on an LED Display. Instead of buying it I wanted to challenge myself and create a prototype that is similar.

For this project I utilized an ESP32-S2-DevKitC-1.
![esp32-s3-devkitc-1-v1-isometric](https://github.com/LleytonMartin/DIY-TIDBYT/assets/100320409/45a08b8d-d3df-434c-a5c7-66d70f7dee98)

I initially planned to use a regular ESP32-DevKit V1, but I ran it RAM issue running both WiFi and the display at the same time. This microcontroller offerd 8 mb of PSRAM which is overkill but was extremely helpful in allowing freedom in transmitting data.

I used the Waveshare RGB Full Color LED Matrix Panel 4mm Pitch 64x32 Pixels
![71-zJAi2jBL _AC_SL1500_](https://github.com/LleytonMartin/DIY-TIDBYT/assets/100320409/598959b6-9928-42aa-ab63-13d9ef577c5b)
This display was rather large, but looked much greater compared to smaller displays which squished the pixels together.

Other components included a Micro SD Card Module and a MPU-6050 Accelerometer

![OIP](https://github.com/LleytonMartin/DIY-TIDBYT/assets/100320409/04635eaa-e629-4c29-ae7a-83d84a4741f7)


![R](https://github.com/LleytonMartin/DIY-TIDBYT/assets/100320409/8bbde58b-8108-411d-a20f-f5cb11e670cf)

I have not had the opportunity to utilize the accelerometer. I plan to create a dynamic display which shows that movement of particles based on the rotation of the display.



My next steps are to develop a PCB which could holds these components together neatly as well as a 3d printing case to make it more aesthetic.

#include <Adafruit_Protomatter.h>
#include <Fonts/TomThumb.h> // Large friendly font
#include "WiFi.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "FS.h"
#include "SPIFFS.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h> 
#include <AnimatedGIF.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <WiFiClientSecure.h>
#include "secrets.h"
#include <Esp.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <SpotifyArduino.h>
#include <JPEGDecoder.h>


/********************************************************************
 * Pin mapping below is for LOLIN D32 (ESP 32)
 *
 * Default pin mapping used by this library is NOT compatable with the use of the 
 * ESP32-Arduino 'SD' card library (there is overlap). As such, some of the pins 
 * used for the HUB75 panel need to be shifted.
 * 
 * 'SD' card library requires GPIO 23, 18 and 19 
 *  https://github.com/espressif/arduino-esp32/tree/master/libraries/SD
 * 
 */

/*
 * Connect the SD card to the following pins:
 *
 * SD Card | ESP32
 *    D2       -
 *    D3       SS
 *    CMD      MOSI
 *    VSS      GND
 *    VDD      3.3V
 *    CLK      SCK
 *    VSS      GND
 *    D0       MISO
 *    D1       -
 */

//Remember if sd does not work
// #define CS_PIN    29
// #define MOSI_PIN  32
// #define CLK_PIN   30
// #define MISO_PIN  31


/**** HUB75 GPIO mapping ****/
// GPIO 34+ are on the ESP32 are input only!!
// https://randomnerdtutorials.com/esp32-pinout-reference-gpios/

#define R1_PIN  4
#define G1_PIN  5
#define B1_PIN  6
#define R2_PIN  7
#define G2_PIN  15
#define B2_PIN  16
#define A_PIN   18
#define B_PIN   8
#define C_PIN   3
#define D_PIN   42
#define E_PIN   -1 // required for 1/32 scan panels, like 64x64. Any available pin would do, i.e. IO32
#define LAT_PIN  40
#define OE_PIN   2
#define CLK_PIN  41
#define SDA 14
#define SCL 1



/***************************************************************
 * HUB 75 LED DMA Matrix Panel Configuration
 **************************************************************/
#define PANEL_RES_X 64      // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another

/**************************************************************/

MatrixPanel_I2S_DMA *dma_display = nullptr;
HTTPClient http;



//AsyncWebServer---------------------------------------------------------
AsyncWebServer server(80);
//Wifi-------------------------------------------------------------------
//#define AWS_IOT_SUBSCRIBE_TOPIC1 "esp32/gifnamessent"
//#define AWS_IOT_SUBSCRIBE_TOPIC2 "esp32/gifsent"
//#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/#"
//#define MQTT_MAX_PACKET_SIZE 3000

const char* input_parameter1 = "filename";
const char* input_parameter2 = "brightness";
WiFiClientSecure net = WiFiClientSecure();

//Async WebServer
void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}


const char index_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE HTML><html>
    <head>
      <title>Desk Display Web Server</title>
    </head>
    <body>
      <h2>Desk Display Webserver</h2>
      %FILENAMEPLACEHOLDER%
      <div>
      <b>Brightness</b>
      <input type = "range" min="1" max ="255" value = "255" class = "slider" id ="brightSlider">
      </div>
    <script>
      var xhr = new XMLHttpRequest();
      %BUTTONLISTENER%
      var slider = document.getElementById("brightSlider");
      slider.oninput = function() {
        xhr.open("GET", "/?brightness="+this.value, true);
        xhr.send();
      }
    </script>
    </body>
    </html>
  )rawliteral";

// Replaces placeholder with button section in your web page
//std::vector<std::string> SPIFFSGIFS;
std::vector<std::string> SDGIFS;
String processor(const String& var){

  if(var == "FILENAMEPLACEHOLDER"){
    String buttons = "";
    for(int i = 0; i < SDGIFS.size(); i++){
      buttons += "<div class=\"flex\"><p>";
      buttons += SDGIFS[i].c_str();
      buttons += "</p>";
      buttons += "<button class=\"switchBtn\"type=\"button\"";
      buttons += "id=\"button" + String(i+1) + "\"";
      buttons += ">Change to This</button></div>";
      //"<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"2\"><span class=\"slider\"></span></label>";
    }
    
    return buttons;
  }else if(var == "BUTTONLISTENER"){
    String script = "";
    script += "var btn;\n";
    script += "var buttonID;\n";
    for(int i = 0; i < SDGIFS.size(); i++){
      script += "buttonID = \"button" + String(i+1) + "\";\n";
      script += "btn" + String(i+1) + " = document.getElementById(buttonID);\n";
      script += "btn" + String(i+1) + ".addEventListener(\"click\", function(){const parameterValue = \"" ;
      script += SDGIFS[i].c_str();
      script += "\"; buttonPressed(parameterValue);});\n";
    }
    script += "function buttonPressed(fileName){ \n xhr.open(\"GET\", \"/?filename=\" + fileName, true); \n xhr.send();\n};\n";
    return script;
  }

  return String();
}



//Global Variables
String mode;
int16_t xPos = 0, yPos = 0; // Top-left pixel coord of GIF in matrix space
//char json[500];
int httpCode;
Adafruit_MPU6050 mpu;

const size_t jsonCapacity = 10240;
const size_t payloadBufferSize = 10240;

void* psramBuffer1 = ps_malloc(payloadBufferSize);

DynamicJsonDocument doc(jsonCapacity);
//char* json = (char*)psramBuffer1;

File file;


//Spotify Variables
char clientId[] = "4b71735b0cc346c6a8c5f94680a04ced";
char clientSecret[] = "447205b1c59e48148fb7c1a86fa61aff";

#define SPOTIFY_MARKET "IE"
#define SPOTIFY_REFRESH_TOKEN "AQBn4EjOcIbGHMK498lq9jnM2XJW4L88TH4IJAvVhmGt8z6L6PRHPkiYCHKiWSDMLmaFa5aIbWvlSdQ5VECQA4xTwQ5yRzkSdF2hQRVhAYE5MedetZCaLRRjQU0ml5EF6Ug"

SpotifyArduino spotify(net,clientId,clientSecret, SPOTIFY_REFRESH_TOKEN);
String currentArtistNames;
String currentTrackName;
String lastTrackName = "";
int trackTextOffset = 0;
int artistTextOffset = 0;
unsigned long spotifyScrollDelay = 60000;
bool displaySpotify = false;
bool scrollingText = false;
String albumArtUrl;
File albumArt;
bool gotImage;
int imageSize;
uint16_t** spotifyColor = nullptr;
std::string reg = "ABCDEFGHIJKLMNOPQURSTUVWXYZabcdefghjklmnopqrstuvwxyz?234567890,-";
std::string fives = "_";
std::string ones = "';:";
std::string twos = " i";
std::string threes = ".1()";


//GIF Functions---------------------------------------------------------------------
// Pass in ABSOLUTE PATH of GIF file to open
// Callbacks for file operations for the Animated GIF Library
AnimatedGIF gif;
File f;
String gifLocation;
static File FSGifFile; // temp gif file holder

const int maxGifDuration    = 30000; // ms, max GIF duration

int x_offset, y_offset;




// Code copied from AnimatedGIF examples




static void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  //log_d("GIFOpenFile( %s )\n", fname );
  FSGifFile = SD.open(fname);
  if (FSGifFile) {
    *pSize = FSGifFile.size();
    return (void *)&FSGifFile;
  }
  return NULL;
}


static void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
}


static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
  int32_t iBytesRead;
  iBytesRead = iLen;
  File *f = static_cast<File *>(pFile->fHandle);
  // Note: If you read a file all the way to the last byte, seek() stops working
  if ((pFile->iSize - pFile->iPos) < iLen)
      iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
  if (iBytesRead <= 0)
      return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}


static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
  //log_d("Seek time = %d us\n", i);
  return pFile->iPos;
}


// Draw a line of image directly on the LCD
void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > PANEL_RES_X)
      iWidth = PANEL_RES_X;
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y; // current line

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {// restore to background color
    for (x=0; x<iWidth; x++) {
      if (s[x] == pDraw->ucTransparent)
          s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) { // if transparency used
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while(x < iWidth) {
      c = ucTransparent-1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) { // done, stop
          s--; // back up to treat it like transparent
        } else { // opaque
            *d++ = usPalette[c];
            iCount++;
        }
      } // while looking for opaque pixels
      if (iCount) { // any opaque pixels?
          for(int xOffset = 0; xOffset < iCount; xOffset++ ){
            dma_display->drawPixel(x + xOffset, y, usTemp[xOffset]); // 565 Color Format
          }
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
            iCount++;
        else
            s--;
      }
      if (iCount) {
        x += iCount; // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    for (x=0; x<iWidth; x++)
      dma_display->drawPixel(x, y, usPalette[*s++]); // color 565
      /*
      usTemp[x] = usPalette[*s++];

      for (x=0; x<pDraw->iWidth; x++) {
        dma_display->drawPixel(x, y, usTemp[*s++]); // color 565
      } */     

  }
} /* GIFDraw() */


// Draw a line of image directly on the LED Matrix
int gifPlay( const char* gifPath )
{ // 0=infinite

  if( ! gif.open( gifPath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw ) ) {
    log_n("Could not open gif %s", gifPath );
  }

  Serial.print("Playing: "); Serial.println(gifPath);

  int frameDelay = 0; // store delay for the last frame
  int then = 0; // store overall delay

  while (gif.playFrame(true, &frameDelay)) {

    then += frameDelay;
    if( then > maxGifDuration ) { // avoid being trapped in infinite GIF's
      //log_w("Broke the GIF loop, max duration exceeded");
      break;
    }
  }

  gif.close();

  return then;
} /* ShowGIF() */
//-----------------------------------------------------------------------------------



// Conway's Game of Life ---------------------------------------
const int blockSize = 1;
const int border = 10;
const int gridRows = 32/blockSize;
const int  gridCols = 64/blockSize;
int currGrid[gridRows + 2*border][gridCols + 2*border] = {};
int nextGrid[gridRows + 2*border][gridCols + 2*border] = {};
int emptyGrid[gridRows + 2*border][gridCols + 2*border] = {};
//Number of Neighbors
int neighbors;
unsigned long timeDelay;
unsigned long startTime;

//My Functions
void createGrid(){
  memcpy(currGrid,emptyGrid,sizeof(currGrid));
  for(int i = 0; i < gridRows; i++){
    for(int j = 0; j < gridCols; j++){
      int randNumber = random(2);
      if(randNumber == 1){
        currGrid[i+border][j+border] = 1;
      }
    }
  }
}



//List SPIFFS Files
void listSPIFFSfiles(){
  File root = SPIFFS.open("/");

  File file = root.openNextFile();

  while(file){
    Serial.print("FILE: ");
    Serial.println(file.name());
    std::string fileName(file.name());
    SDGIFS.push_back(fileName);
    file.close();
    file = root.openNextFile();
    

  }
}

void listSDfiles(){
  File root = SD.open("/");

  File file = root.openNextFile();

  while(file){
    Serial.print("FILE: ");
    Serial.println(file.name());
    std::string fileName(file.name());
    SDGIFS.push_back(fileName);
    file.close();
    file = root.openNextFile();
    

  }
}


//--------------------------------------------------------------------------------------------
//Spotify Currently Playing
void currentlyPlayingCallback(CurrentlyPlaying currentlyPlaying){
  Serial.println("--------- Currently Playing ---------");
    displaySpotify = true;

    if(int(currentlyPlaying.currentlyPlayingType) == 0 && currentTrackName != String(currentlyPlaying.trackName)){

      Serial.print("Is Playing: ");
      if (currentlyPlaying.isPlaying)
      {
          Serial.println("Yes");
      }
      else
      {
          Serial.println("No");
      }
      Serial.println();

      Serial.print("Track: ");
      Serial.println(currentlyPlaying.trackName);

      currentTrackName = currentlyPlaying.trackName;
      currentArtistNames = "";
      Serial.println("Artists: ");
      for (int i = 0; i < currentlyPlaying.numArtists; i++)
      {
          currentArtistNames += String(currentlyPlaying.artists[i].artistName) + ", ";
          Serial.print("Name: ");
          Serial.println(currentlyPlaying.artists[i].artistName);
          Serial.print("Artist URI: ");
          Serial.println(currentlyPlaying.artists[i].artistUri);
          Serial.println();
      }
      currentArtistNames = currentArtistNames.substring(0,currentArtistNames.length()-2);

      Serial.print("Album: ");
      Serial.println(currentlyPlaying.albumName);
      Serial.println();
      
      long progress = currentlyPlaying.progressMs; // duration passed in the song
      long duration = currentlyPlaying.durationMs; // Length of Song
      Serial.print("Elapsed time of song (ms): ");
      Serial.print(progress);
      Serial.print(" of ");
      Serial.println(duration);
      Serial.println();

      SpotifyImage smallestImage = currentlyPlaying.albumImages[2];
      //char *my_url = const_cast<char *>(smallestImage.url);

      http.begin(net, String(smallestImage.url));
      int status = http.GET();
      if(status > 0){
        File albumImage = SD.open("/album.jpg","w+");
        if(albumImage){
          int len = http.getSize();
          //const int size = http.getSize();
          Serial.println("Image Opened");
          uint8_t buffer[128] = {0};

          WiFiClient * stream = http.getStreamPtr();

          while(http.connected() && (len > 0 || len == -1)){
            size_t size = stream->available();

            if(size){
              int c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
              albumImage.write(buffer,c);

              if(len > 0){
                len -= c;
              }
            }
            delay(1);
          }

          Serial.println("Reached 3");
        }else{
          Serial.println("Image not opened");
        }
        albumImage.close();
      }else{
        Serial.println("Failed to Get Request");
      }
      //Disconnect
      http.end();

        // Get the free heap memory in bytes
      uint32_t freeHeap = ESP.getFreeHeap();

      // Print the free heap memory to the Serial monitor
      Serial.print("Free Heap Memory: ");
      Serial.print(freeHeap);
      Serial.println(" bytes");


      File jpgFile = SD.open("/album.jpg", FILE_READ);
      Serial.println("Opened");
      JpegDec.decodeSdFile(jpgFile);
      Serial.println("Decoded");



      uint16_t *pImg;
      //uint16_t color[JpegDec.height][JpegDec.width];
      

      while(JpegDec.read()){
        pImg = JpegDec.pImage;

        int mcuXCoord = JpegDec.MCUx;
        int mcuYCoord = JpegDec.MCUy;
        int mcuWidth = JpegDec.MCUWidth;
        int mcuHeight = JpegDec.MCUHeight;
        int x = 0;
        int y = 0;
        // Get the number of pixels in the current MCU
        uint32_t mcuPixels = JpegDec.MCUWidth * JpegDec.MCUHeight;
        Serial.println("X: " + String(mcuXCoord) + " | Y: " +String(mcuYCoord));
        while(mcuPixels--){
          spotifyColor[mcuYCoord*mcuHeight + y][mcuXCoord*mcuWidth + x] = *pImg++;

          x++;
          if(x == mcuWidth){
            x = 0;
            y++;
          }
          if(y == mcuHeight){
            y = 0;
            x = 0;
          }
        }
      }
      jpgFile.close();
      dma_display->clearScreen();
    }
}

//Define one
float c;
float x;
float m;
float r1;
float g1;
float b1;
int RGB[3] = {};
void HSVtoRGB(float h, float s, float v){
  c = (v/100)*(s/100);
  x = c*(1-abs(fmod(h/60.0,2)-1));
  m = (v/100)-c;

  if(h < 60){
    r1 = c;
    g1 = x;
    b1 = 0;
  }else if(h < 120){
    r1 = x;
    g1 = c;
    b1 = 0;
  }else if(h < 180){
    r1 = 0;
    g1 = c;
    b1 = x;
  }else if(h < 240){
    r1 = 0;
    g1 = x;
    b1 = c;
  }else if(h < 300){
    r1 = x;
    g1 = 0;
    b1 = c;
  }else if(h < 360){
    r1 = c;
    g1 = 0;
    b1 = x;
  }

  RGB[0] = (r1+m)*255;
  RGB[1] = (g1+m)*255;
  RGB[2] = (b1+m)*255;
  //Serial.println();
  //Serial.println("H: " + String(h) + " |S: " + String(s) + " |V:" + String(v));
  //Serial.println("C: " + String(c) + " |X: " + String(x) + " |M:" + String(m));
  //Serial.println("R: " + String(RGB[0]) + " |G: " + String(RGB[1]) + " |G:" + String(RGB[2])));

}


//Interrupt Pins
void Change_Mode(){
  if(mode == "weather"){
    mode = "conway";
  }else{
    mode = "weather";
  }
}

//AWS IoT message received callback
//void messageReceived(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {



// SETUP - RUNS ONCE AT PROGRAM START --------------------------------------

void setup(void) {


  Serial.begin(115200);
  


  //Accelerometer
  Wire.begin(SDA,SCL);
  Serial.println(MPU6050_I2CADDR_DEFAULT);
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    delay(1000);

  }
  Serial.println("MPU6050 Found!");

  //Serial.println("PSRAM Size is: " + String(esp_spiram_get_size()) + " bytes");
  // **************************** Setup DMA Matrix ****************************
    HUB75_I2S_CFG mxconfig(
      PANEL_RES_X,   // module width
      PANEL_RES_Y,   // module height
      PANEL_CHAIN    // Chain length
    );

    // Need to remap these HUB75 DMA pins because the SPI SDCard is using them.
    // Otherwise the SD Card will not work.
    mxconfig.gpio.a = A_PIN;
    mxconfig.gpio.b = B_PIN;
    mxconfig.gpio.c = C_PIN;
    //mxconfig.gpio.d = D_PIN;    

    mxconfig.clkphase = false;
    mxconfig.latch_blanking = 4;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    
    //mxconfig.driver = HUB75_I2S_CFG::FM6126A;

    // Display Setup
    Serial.println("Starting Up");
    //Serial.println("PSRAM Size is: " + String(esp_spiram_get_size()) + " bytes");
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);


  
  
  //Connecting to Wifi--------------------------------------------------
  //const char* ssid = "SpectrumSetup-A8";
  //const char* password = "warmwhale272";
  const char* ssid = "Martin Home Wifi";
  const char* password = "b6b7e8d625";

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  while(WiFi.status() != WL_CONNECTED){
        Serial.print(".");
        delay(100);
  }



  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
  net.setInsecure();

  Serial.println("Refreshing Access Tokens");
  if (!spotify.refreshAccessToken()){
    Serial.println("Failed to get access tokens");
  }

  //SPIFFS Setup 
  // while(!SPIFFS.begin()){
  //   Serial.print("SPIFFS Failed to start:");
  //   SPIFFS.format();

  //   delay(1000);
  // }
  // Serial.println("SPIFFS Started");

  //List all files on SD
  while(!SD.begin()){
    Serial.println("SD failed to start!");
    delay(1000);
  }
  Serial.println("SD Started");

  listSDfiles();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html,processor);
        String fileRequest = "/";

        if(request->hasParam(input_parameter1)){
          fileRequest += request->getParam(input_parameter1)->value();
          Serial.println(fileRequest);
          gifLocation = fileRequest;
          if(mode == "custom"){
            gif.close();
            gif.reset();
          }
          mode = "custom";
          
        }
        if(request->hasParam(input_parameter2)){
          Serial.println("Brightness Changed");
          String inputMessage = request->getParam(input_parameter2)->value();
          int newBrightness = inputMessage.toInt();
          Serial.println("Brightness Set to: " + inputMessage);
          dma_display->setBrightness8(newBrightness);
        }
    });
  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
  Serial.println("HTTP server started");



  

  


  
  //Setup Grid
  createGrid();

  


  
//Allocate memory and start DMA display
  if( ! dma_display->begin() ){
      Serial.println("****** !KABOOM! HUB75 memory allocation failed ***********");

  }
  dma_display->setBrightness8(128); //0-255
  dma_display->clearScreen();
  // //Show LocalIp
   dma_display->setFont(&TomThumb);
   dma_display->setTextColor(dma_display->color565(0, 0, 255));
   dma_display->println(WiFi.localIP());


  delay(2000);



  //GIF Setup
  gif.begin(LITTLE_ENDIAN_PIXELS);


  //Spotify Setup
  // Allocate memory for a 2D uint16_t array in PSRAM
  int numRows = 64;
  int numCols = 64;
  spotifyColor = (uint16_t**)heap_caps_malloc(numRows * sizeof(uint16_t*), MALLOC_CAP_SPIRAM);
  
  if (spotifyColor == nullptr) {
    Serial.println("Failed to allocate memory in PSRAM");
    return;
  }

  for (int i = 0; i < numRows; i++) {
    spotifyColor[i] = (uint16_t*)heap_caps_malloc(numCols * sizeof(uint16_t), MALLOC_CAP_SPIRAM);

    if (spotifyColor[i] == nullptr) {
      Serial.println("Failed to allocate memory for row in PSRAM");
      // Free previously allocated memory to avoid leaks
      for (int j = 0; j < i; j++) {
        heap_caps_free(spotifyColor[j]);
      }
      heap_caps_free(spotifyColor);
      return;
    }
  }



  //GIF Test
  //dma_display->clearScreen();

  //To refresh certain screens
  startTime = 0;
  mode = "spotify";

  timeDelay = millis() + 60000;
  //attachInterrupt(35,Change_Mode,RISING);
  dma_display->clearScreen();


}

// LOOP - RUNS REPEATEDLY AFTER SETUP --------------------------------------

void loop(void) {
  AsyncElegantOTA.loop();
  
  if(mode == "conway"){
    //Conway's Game of Life---------------------------------------------------
    if(millis() - timeDelay >= 25){
      
      // Every frame, we clear the background and draw everything anew.
      // This happens "in the background" with double buffering, that's
      // why you don't see everything flicker. It requires double the RAM,
      // so it's not practical for every situation.

      memcpy(nextGrid,emptyGrid,sizeof(emptyGrid));

      dma_display->fillScreen(0); // Fill background black
      for (int i = 0; i < gridRows+2*border; i++){
        for(int j = 0; j < gridCols+2*border; j++){
          if(currGrid[i][j] == 1){
            if(i >= border && i < border+gridRows && j >= border && j < border+gridCols){
                //dma_display->drawRect(blockSize * (j-border), blockSize * (i-border), blockSize, blockSize, dma_display->color565(255,255,255));
                //max 65535
                HSVtoRGB(random(250),100,100);
                dma_display->drawRect(blockSize * (j-border), blockSize * (i-border), blockSize, blockSize, dma_display->color565(RGB[0],RGB[1],RGB[2]));
            }
                neighbors = -1;
                for(int k = i - 1; k < i + 2; k++){
                  for(int l = j - 1; l < j + 2; l++){
                      if(k >= 0 && k < gridRows + 2* border && l >= 0 && l < gridCols +2*border){
                        if(currGrid[k][l] == 1){
                          neighbors++;
                        }
                      }
                  }
                }
                if(neighbors == 2 || neighbors == 3){
                  nextGrid[i][j] = 1;
                }
          }else{
                neighbors = 0;
                for(int k = i - 1; k < i + 2; k++){
                  for(int l = j - 1; l < j + 2; l++){
                      if(k >= 0 && k < gridRows + 2* border && l >= 0 && l < gridCols +2*border){
                        if(currGrid[k][l] == 1){
                          neighbors++;
                        }
                      }
                  }
                }
                if(neighbors == 3){
                  nextGrid[i][j] = 1;
                }
          }
        }
      }
      memcpy(currGrid,nextGrid,sizeof(nextGrid));

      

      // AFTER DRAWING, A show() CALL IS REQUIRED TO UPDATE THE MATRIX!

      timeDelay = millis();
    }

    if(millis() - startTime >= 5*1000){
        
        startTime = millis();
        createGrid();
        Serial.println("Restarted");
      }



  }else if(mode == "weather"){
    if(startTime == 0 || millis() - startTime >= 60*10*1000){
      Serial.println("Weather");
      http.setTimeout(10000);
        http.begin("https://api.ip2location.io/?key=8864C1523C178DC2399D34C4E80DA93D");
        httpCode = http.GET();
        Serial.println("HTTP Code: " + String(httpCode));
        if(httpCode > 0){
          String payload = http.getString();
          //Getting Location
          char json[500];
          payload.toCharArray(json,500);
          //(http.getString()).toCharArray(json,500);
          deserializeJson(doc,json);
          float longitude = doc["longitude"];
          float latitude = doc["latitude"];
          //Serial.println(longitude);
          //Serial.println(latitude);
          
          //Getting Weather
          String request = "http://api.weatherapi.com/v1/forecast.json?key=731098f5dc994c8cab515146230809&q=";
          request = request + latitude + "," + longitude;
          
          http.begin(request);
          httpCode = http.GET();
          if(httpCode > 0){


            String payload = http.getString();

            //Get hour
            String hourString = payload.substring(payload.indexOf("localtime\":")+23,payload.indexOf("localtime\":")+25);
            
            //Serial.println("\nStatuscode: " + String(httpCode));
            String cutPayload = payload.substring(payload.indexOf("\"current\""));
            cutPayload = cutPayload.substring(0,cutPayload.indexOf("},\"forecast") + 1);
            cutPayload = "{" + cutPayload + "}";

            //payload = payload.substring(payload.indexOf("\"current\""))
            cutPayload.toCharArray(json,500);
            deserializeJson(doc,json);
            int temp = doc["current"]["temp_c"];
            int windSpeed = doc["current"]["wind_mph"];
            String windDir = doc["current"]["wind_dir"];
            int humidity = doc["current"]["humidity"];
            int uv = doc["current"]["uv"];
            int daytime = doc["current"]["is_day"];
        
            //Drawing
            dma_display->fillScreen(0);
            dma_display->setTextColor(dma_display->color565(255, 255, 0));
            if(daytime == 1){
              for(int i = 0; i < 32; i++){
                //Create Gradient
                HSVtoRGB((185+i*2),100.0,40);
                dma_display->drawLine(0,i,64,i,dma_display->color565(RGB[0],RGB[1],RGB[2]));
              }
              dma_display->fillCircle(0,0,12,dma_display->color565(255,255,0));
            }else{
              dma_display->fillCircle(0,0,12,dma_display->color565(100,100,100));
              dma_display->fillCircle(2,2,1,dma_display->color565(50,50,50));
              dma_display->fillCircle(7,5,1,dma_display->color565(30,30,30));
              dma_display->fillCircle(3,9,2,dma_display->color565(40,40,40));
            
              //Draw stars
              // for(int i = 15; i < 64; i = i + random(10)+10){
              //   dma_display->setCursor(i,random(12) + 3);
              //   dma_display->print("*");
              // }
            }
            Serial.println(temp);
            Serial.println(windSpeed);
            Serial.println(windDir);
            Serial.println(humidity);
            Serial.println(uv);

            dma_display->cp437(true);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->setCursor(2, 20);
            dma_display->setTextSize(1);
            dma_display->print(String(temp) + "°C");

            dma_display->setCursor(2, 27);
            dma_display->print(String(humidity) + "%");

            for(int i = 15; i < 32; i = i + 2){
              dma_display->drawPixel(15,i,dma_display->color565(255,255,255));
            }

            dma_display->setCursor(18,20);
            dma_display->print("UV:");
            ////////////

            HSVtoRGB((11-uv)*10,100.0,100.0);
            dma_display->fillRect(28,15,5,5,dma_display->color565(RGB[0],RGB[1],RGB[2]));
            ///////////
            dma_display->setCursor(18,27);
            dma_display->print(String(windDir) + " " + String(windSpeed) + "mph");

            //Draw grid for plotting next 5 temperatures

            dma_display->drawLine(40,2,40,13,dma_display->color565(255,255,255));
            dma_display->drawLine(40,14,61,14,dma_display->color565(255,255,255));
            
            cutPayload = payload.substring(payload.indexOf("\"hour\""));
            while(cutPayload.indexOf(",\"condition\":{}") > 0){
              cutPayload.remove(cutPayload.indexOf(",\"condition\":{}"),sizeof(",\"condition\":{}")-1);

            }
            cutPayload = cutPayload.substring(0,cutPayload.indexOf("]") + 1);
            cutPayload = "{" + cutPayload + "}";
            cutPayload.toCharArray(json,500);
            deserializeJson(doc,json);

            //Get Temperatures for next 5 hours
            int maxValue = doc["hour"][hourString.toInt()%24]["temp_c"];
            int minValue = doc["hour"][hourString.toInt()%24]["temp_c"];
            int temps[6] = {};
            temps[0] = temp;
            int pointer = 0;
            for(int i = 0; i < 5; i++){
              pointer = (hourString.toInt() + i)%24;
              int compare = doc["hour"][(hourString.toInt()+i)%24]["temp_c"];
              maxValue = max(maxValue,compare);
              minValue = min(minValue,compare);
              temps[i+1] = doc["hour"][(hourString.toInt()+i)%24]["temp_c"];
            }
            Serial.print("Max: " + String(maxValue) + " | Min: " + String(minValue));

            for(int i = 0; i < maxValue - minValue + 1; i++){
              if(maxValue - minValue > 0){
                dma_display->drawPixel(39,14-(12/(maxValue-minValue)+1)*(i),dma_display->color565(255,255,255));
              }
            }
            for(int i = 0; i < (sizeof(temps)/4)-1; i++){

              int y1 = 13 - ((11)/(maxValue-minValue)+1)*(temps[i] - minValue);
              int y2 = 13 - ((11)/(maxValue-minValue)+1)*(temps[i+1] - minValue);
              dma_display->drawPixel(41+(i)*20/(sizeof(temps)/4-1),15,dma_display->color565(255,255,255));
              dma_display->drawPixel(41+(i+1)*20/(sizeof(temps)/4-1),15,dma_display->color565(255,255,255));

              if(y1 < y2){
                dma_display->drawLine(41+i*20/(sizeof(temps)/4-1),y1,41+(i+1)*20/(sizeof(temps)/4-1),y2,dma_display->color565(0,255,0));
              }else{
                dma_display->drawLine(41+i*20/(sizeof(temps)/4-1),y1,41+(i+1)*20/(sizeof(temps)/4-1),y2,dma_display->color565(255,0,0));
              }
            }
            Serial.print("Max: " + String(maxValue) + " | Min: " + String(minValue));

          }else{
            Serial.println("Error on HTTP request");
            
          }

        }else{
          Serial.println("Error on HTTP request");
        }
        
        startTime = millis();

    }

    
  }else if(mode == "custom"){
    // if(millis() - timeDelay > 60000){
    //   timeDelay = millis();
    //   Serial.println("Sending Request..........");
 
    // }
    // char filePath[256] = {0};
    // //strcpy(filePath,gifLocation);
    // Serial.println("Getting Gif Path");
    // File gifFile = SD.open(gifLocation);
    // if(!gifFile.isDirectory()){  
    //   gifFile.close();            
      //memset(filePath, 0x0, sizeof(filePath));                
      //strcpy(filePath, gifFile.path());
      
      // Show it.
      Serial.println("Playing Gif");
      gifPlay(gifLocation.c_str());
      gif.reset();
      Serial.println("Finished Playing GIF");
    //}
   // gifFile.close();


  }else if(mode == "spotify"){
    if(millis() - timeDelay >= 2000 & !scrollingText){
      int status = spotify.getCurrentlyPlaying(currentlyPlayingCallback, SPOTIFY_MARKET);
      if (status == 200)
          {


          }
          else if (status == 204)
          { 
              dma_display->fillScreen(dma_display->color565(0,0,0));
              displaySpotify = false;
              Serial.println("Doesn't seem to be anything playing");
              dma_display->setCursor(0,5);
              dma_display->setTextColor(dma_display->color565(255, 0, 0));
              dma_display->print("No Song Playing");
          }
          else
          {
              Serial.print("Error: ");
              Serial.println(status);
          }
          timeDelay = millis();
    }

    if(millis() - spotifyScrollDelay >= 50){
      if(displaySpotify){
        dma_display->setTextWrap(false);
        
        //dma_display->clearScreen();
        //dma_display->fillScreen(dma_display->color565(0,0,0));
        Serial.println("Successfully got currently playing");
        dma_display->setTextSize(1);
        dma_display->setCursor(0, 5);
        dma_display->setTextColor(dma_display->color565(0,255,0));
        dma_display->print("NOW PLAYING");
        
        int image_border = 22;
        int textStart = image_border + 1;
        //Font width is 3 but a space is 1 pixel
        int fontWidth = 4;
        dma_display->setTextColor(dma_display->color565(255,255,255));
        //int trackTextOffset = 0;
        //int artistTextOffset = 0;

        if(textStart + currentTrackName.length() * fontWidth >= PANEL_RES_X){

          //Clear Specific Part
          //Subjective
          dma_display->fillRect(19, 11, PANEL_RES_X, 6, dma_display->color565(0,0,0));
          int charLengths[currentTrackName.length()] = {0};

          for(int i = 0; i < currentTrackName.length(); i++){
            if(reg.find(currentTrackName[i]) != std::string::npos){
              charLengths[i] = 4;
            }else if(fives.find(currentTrackName[i]) != std::string::npos){
              charLengths[i] = 5;
            }else if(ones.find(currentTrackName[i]) != std::string::npos){
              charLengths[i] = 1;
            }else if(threes.find(currentTrackName[i]) != std::string::npos){
              charLengths[i] = 3;
            }else if(twos.find(currentTrackName[i]) != std::string::npos){
              charLengths[i] = 2;
            }
          }

          int stringStart = 0;
          int total = 0;
          for(int i = 0; i < currentTrackName.length(); i++){
            total += charLengths[i];
            if(trackTextOffset >= total){
              stringStart = i+1;
            } 
          }
          total = 0;
          for(int i = 0; i < stringStart; i++){
            total += charLengths[i];
          }
          
          dma_display->setCursor(textStart - trackTextOffset + total,16);
          dma_display->print(currentTrackName.substring(stringStart));

          dma_display->setCursor(textStart - trackTextOffset + (1 + currentTrackName.length()*fontWidth),16);
          dma_display->print(currentTrackName);

          scrollingText = true;
          if(trackTextOffset == currentTrackName.length() * fontWidth){
            scrollingText = false;
            trackTextOffset = 0;
          }

          trackTextOffset += 1;
          
        }else{
          dma_display->setCursor(textStart,16);
          dma_display->setTextColor(dma_display->color565(255,255,255));
          dma_display->print(currentTrackName);
        }
        

        dma_display->setCursor(textStart, 26);
        dma_display->print(currentArtistNames);

        int newWidth = 16;
        int newHeight = 16;
        float xBin = float(JpegDec.width)/float(newWidth);
        float yBin = float(JpegDec.height)/float(newHeight);

        //Serial.println(String(xBin));

        for(int i = 0; i < newHeight; i++){
          for(int j = 0; j < newWidth; j++){
            int r = 0;
            int g = 0;
            int b = 0;
            for(int k = int(i*yBin); k < int((i+1)*yBin + 0.5); k++){
              for(int l = int(j*xBin); l < int((j+1)*xBin + 0.5); l++){
                r += ((spotifyColor[k][l] & 0xF800) >> 11) * 8;
                g += ((spotifyColor[k][l] & 0x07E0) >> 5) * 4;
                b += ((spotifyColor[k][l] & 0x001F) >> 0) * 8;
              }
            }
            int currYbin = int(i*yBin)-int((i+1)*yBin + 0.5);
            int currXbin = int(j*xBin)-int((j+1)*xBin + 0.5);
            int block = currYbin * currXbin;

            //dma_display->drawPixel(j, i, dma_display->spotifyColor565(255,255,255));
            dma_display->drawPixel(j+ 2, i + 10, dma_display->color565(r/(block), g/(block), b/(block)));

            
          }
        }






        

      }
      spotifyScrollDelay = millis();
    }

  }
  

}

/*
 Copyright (c) 2024 Miguel Tomas, http://www.aeonlabs.science  https://github.com/aeonSolutions

License Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
You are free to:
   Share — copy and redistribute the material in any medium or format
   Adapt — remix, transform, and build upon the material

The licensor cannot revoke these freedoms as long as you follow the license terms. Under the following terms:
Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. 
You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.

NonCommercial — You may not use the material for commercial purposes.

ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions under
 the same license as the original.

No additional restrictions — You may not apply legal terms or technological measures that legally restrict others
 from doing anything the license permits.

Notices:
You do not have to comply with the license for elements of the material in the public domain or where your use
 is permitted by an applicable exception or limitation.
No warranties are given. The license may not give you all of the permissions necessary for your intended use. 
For example, other rights such as publicity, privacy, or moral rights may limit how you use the material.


Before proceeding to download any of AeonLabs software solutions for open-source development
 and/or PCB hardware electronics development make sure you are choosing the right license for your project. See 
https://github.com/aeonSolutions/PCB-Prototyping-Catalogue/wiki/AeonLabs-Solutions-for-Open-Hardware-&-Source-Development
 for Open Hardware & Source Development for more information.

*/

#ifdef CORE_DEBUG_LEVEL
#undef CORE_DEBUG_LEVEL
#endif

#define CORE_DEBUG_LEVEL 3
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

#include <HardwareSerial.h>
#if defined(CONFIG_IDF_TARGET_ESP32) 
    #define mySerial Serial2
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    #define mySerial Serial1
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
   #define mySerial Serial2
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
   #define mySerial Serial1
#endif


#include <Adafruit_ADS1X15.h>
Adafruit_ADS1115 ads;  /* Use this for the 16-bit version */
// Pin connected to the ALERT/RDY signal for new sample notification.
constexpr int READY_PIN = 19;
bool ads115_offline = false;

#include <Wire.h>
#define SDA_PIN 5 
#define SCL_PIN 6
TwoWire I2C_BUS(0);
#define  LED_GREEN 18

#include <FS.h>            // Include the SPIFFS or LittleFS library
#include "SPIFFS.h"   // Use this for ESP32
#include "mbedtls/base64.h"

#include "esp_sntp.h"

#include "time.h"
#include "ESP32Time.h"
ESP32Time rtc(0);
uint32_t NTP_update_interval = 0;
long int last_ntp_sync = millis();
// NTP Time Server
const char* ntpServer_1 = "0.pool.ntp.org";
const char* ntpServer_2 = "time.nist.gov";
const char* ntpServer_3 = "1.pool.ntp.org";
const long timezone = 0;  // (utc+) TZ in hours 
const byte daysavetime = 1;

// GitHub credentials
const char* githubToken = " token here"; // Replace with your GitHub token
const char* githubUser = "aeonSolutions";              // Replace with your GitHub username
const char* githubRepo = "AeonLabs-Safety-Health";                  // Replace with your GitHub repository
const char* githubBranch = "main";                            // Replace with the branch you want to commit to

// File details
String filePath = "/dataset.csv";                           // The path to the file on the filesystem (SPIFFS/LittleFS)
const char* githubFilePath = "Measurements/datasets/dataset.csv";                      // Path in the GitHub repository

// WiFi credentials
#include <WiFi.h>          // For ESP32; use <ESP8266WiFi.h> for ESP8266
#include <HTTPClient.h>

// Port and headers
const char* githubHost = "api.github.com";
const int httpsPort = 443;  // HTTPS port

const char* ssid = " ssid here ";
const char* password = " pwr here ";

// *******************************************************************
void BlinkLED(int LED, int duration) {
  digitalWrite(LED, HIGH);   // sets the digital pin 13 on
  delay(duration * 1000);    // waits for a second
  digitalWrite(LED, LOW);   // sets the digital pin 13 off 
}

//*******************************************************************
void get_time(){
  tm timeinfo;

  log_i("Connecting to the Time Server....");
  // Init and get the time
  sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
  configTime( 3600*timezone, daysavetime*3600, ntpServer_1, ntpServer_2, ntpServer_3 );
  timeval tv;
  sntp_sync_time(&tv);

  log_i("Waiting for Time Sync");
  
  if (getLocalTime(&timeinfo) )
    rtc.setTimeStruct(timeinfo);

  get_sntp_sync_status();
  get_sntp_sync_mode();
  
  NTP_update_interval = sntp_get_sync_interval(); // in milisec 
  log_i("NTP Sync Interval = %d", NTP_update_interval);

  log_i("Local Time: %s", timeString);

  BlinkLED(LED_GREEN, 1);     
}

//*******************************************************************
void connectWIFI(int attempts, int duration){
    for(int i=0; i<attempts; i++){
      ESP_LOGI("log", "%s",  "Attempt n. " + String(i+1) + " to update Time"  );

      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      delay(100);
      WiFi.begin(ssid, password);
      WiFi.setTxPower( WIFI_POWER_11dBm );
      long int startConn = millis();
      while ( WiFi.status() != WL_CONNECTED &&   (millis() - startConn < (duration*1000) ) ) {
        delay(1000);
      }
      
      if ( WiFi.status() == WL_CONNECTED ){
        log_i("\nConnected to WiFi!");
        BlinkLED(LED_GREEN, 1);
        return;
      }     
    }
}

//********************************************************************
void get_sntp_sync_status(){
  sntp_sync_status_t syncStatus = sntp_get_sync_status();
  switch (syncStatus) {
    case SNTP_SYNC_STATUS_RESET:
      log_i("SNTP_SYNC_STATUS_RESET");
      break;

    case SNTP_SYNC_STATUS_COMPLETED:
      log_i("SNTP_SYNC_STATUS_COMPLETED");
      break;

    case SNTP_SYNC_STATUS_IN_PROGRESS:
      log_i("SNTP_SYNC_STATUS_IN_PROGRESS");
      break;

    default:
      log_e("Unknown Sync Status");
      break;
  }
}

//******************************************************************
void get_sntp_sync_mode(){
  sntp_sync_mode_t mode = sntp_get_sync_mode();
  switch (mode) {
    case SNTP_SYNC_MODE_IMMED:
      log_i("SNTP_SYNC_MODE_IMMED");
      break;

    case SNTP_SYNC_MODE_SMOOTH:
      log_i("SNTP_SYNC_MODE_SMOOTH");
      break;

    default:
      log_e("Unknown Sync Mode");
      break;
  }
}




// ******************************************************************
void setup() {
  I2C_BUS.begin(SDA_PIN, SCL_PIN, 100000);
  esp_log_level_set("*", ESP_LOG_INFO);  

  // Initialize device
  #if defined(CONFIG_IDF_TARGET_ESP32) 
    mySerial.begin(115200);
  #elif defined(CONFIG_IDF_TARGET_ESP32C3)
    mySerial.begin(115200, SERIAL_8N1, 20, 21); // rxpin, txpin
  #elif defined(CONFIG_IDF_TARGET_ESP32S2)
    mySerial.begin(115200);
  #elif defined(CONFIG_IDF_TARGET_ESP32S3)
    mySerial.begin(115200, SERIAL_8N1, 18, 17);// rxpin, txpin
  #endif
  
  log_i("Serial started....");
  delay(2000);

  pinMode(LED_GREEN, OUTPUT);

  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.disconnect();
  delay(100);


  log_i("Initialize filesystem...");
  // Initialize filesystem
  if (!SPIFFS.begin(true)) {
      log_i("Failed to mount file system");
      while (1){
        BlinkLED(LED_GREEN, 5);
      }
  }else{
      log_i("done!");
      BlinkLED(LED_GREEN, 1);      
  }

  // Connect to WiFi
  connectWIFI(5, 10);
  if ( WiFi.status() == WL_CONNECTED ){
    get_time();
  }


  filePath = "/dataset_" + String(rtc.getEpoch() )+ ".csv";   

  if ( createNewDatasetFile(filePath) == false ){
    while (1){
      BlinkLED(LED_GREEN, 5);
    }
  }

  log_i("Getting differential reading from AIN0 (P) and AIN1 (N)");
  log_i("ADC Range: +/- 6.144V (1 bit = 3mV/ADS1015, 0.1875mV/ADS1115)");

  // The ADC input range (or gain) can be changed via the following
  // functions, but be careful never to exceed VDD +0.3V max, or to
  // exceed the upper and lower limits if you adjust the input range!
  // Setting these values incorrectly may destroy your ADC!
  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV

  if (!ads.begin(0x48, &I2C_BUS)) {
    ads115_offline= true;
    log_i("Failed to initialize ADS.");
    BlinkLED(LED_GREEN, 5);
  }
  pinMode(READY_PIN, INPUT);

  log_i("setup end. ");
}

// -----------------------------------------------------------------------------

String getADSmeasurement(){
  mySerial.println("\nStarting measurements....");
  BlinkLED(LED_GREEN, 1);
  int16_t res1;
  int16_t res2 ;
  float tension_A1;
  float tension_A2;
  float tension_dif = 0.0;

  float energy_A1;
  float energy_A2;
  float energy_dif = 0.0;
  
  if (ads115_offline){
    res1= 1.0;
    res2= 2.0; 
    tension_A1  = 1.0;
    tension_A2  = 2.0;
  }else{
    res1= ads.readADC_SingleEnded(1);
    res2= ads.readADC_SingleEnded(2);
    tension_A1  = ads.computeVolts(res1);
    tension_A2  = ads.computeVolts(res2);
  }
  tension_dif = tension_A1 - tension_A2;
    
  mySerial.println("Voltage A1: " +String(tension_A1)+ " mV" ); 
  mySerial.println("Voltage A2: " +String(tension_A2)+ " mV" ); 
  mySerial.println("Differential 1-2: " + String(tension_dif) + " mV");
  
  mySerial.println("Energy A1: " + String(energy_A1,10) + " mW" ); 
  mySerial.println("Energy A2: " + String(energy_A2,10) + " mW" ); 
  mySerial.println("Differential 1-2: " + String(energy_dif,10) + " mW");

  //  E = 1/2 x C x V^2 -- Energy equals one-half times the capacitance (in farads) times the square of the voltage.
  //  The unit is watt-seconds, abbreviated Ws, or W-s
  energy_A1 = (float)0.5 * (float)100 * (float)0.000001 * (float)tension_A1;
  energy_A2 = (float)0.5 * (float)100 * (float)0.000001 * (float)tension_A2;
  energy_dif = energy_A1 - energy_A2;


  String dataRecord = String( rtc.getTimeDate(true) ) + ";" + String(tension_A1)+";"+ String(tension_A2)+";"+ String(tension_dif) + ";" + String(energy_A1,10)+";"+ String(energy_A2,10)+";"+ String(energy_dif,10)  +";\n";     
  return dataRecord;
}

/******************************************************************************************
 * Main Loop
 *****************************************************************************************/
unsigned long MEASUREMENT_INTERVAL        = 1 * 60 * 1000; // 10 sec //minutes to mili
unsigned long MEASUREMENT_INTERVAL_GITHUB = 5 * 60 * 1000;
long int last_upload                      = millis();
long int last_github_upload               = millis();

// adc_power_release()

void loop() {
  delay(100);
  if (millis() - last_ntp_sync > NTP_update_interval ){
    get_time();
    last_ntp_sync= millis();
  }

  // do measurements
  if (millis() - last_upload > MEASUREMENT_INTERVAL) {
    writeDataRecord( getADSmeasurement() );
    last_upload = millis();
  }

  if (millis() - last_github_upload > MEASUREMENT_INTERVAL_GITHUB) {
    mySerial.println("Starting upload....");
    uploadGitHub();
    last_github_upload = millis();
  }

}

//--------------------------------------------------------------------------------------------------------------------
void writeDataRecord(String dataRecord){
  File file = SPIFFS.open(filePath, FILE_APPEND);
  if (!file) {
    mySerial.println("Failed to open the  file");
    BlinkLED(LED_GREEN, 5);
    return;
  }

  // Write data to the file
  if (file.print(dataRecord)) {
      mySerial.println("Data written successfully!");
  } else {
    mySerial.println("Failed to write data");
    BlinkLED(LED_GREEN, 5);
  }

  // Close the file
  file.close();
}
//--------------------------------------------

bool createNewDatasetFile(String file_Path){
  // Check if the file exists
  if (!SPIFFS.exists(file_Path)) {
    // Create and open a new file
    File file = SPIFFS.open(file_Path, FILE_WRITE);
    if (!file) {
      log_i("Failed to create file");
      return false;
    }  
  
    log_i("File created successfully!");

    BlinkLED(LED_GREEN, 1);
    // Write data to the file
    if (file.print("Date & Tme; V(A1) mV; V(A2) mV; V(diff) mV; E(A1) mW; E(A2) mW; E(diff) mW; \n")) {
      log_i("Data written successfully!");
      // Close the file
      file.close();
      return true;   
    } else {
      log_i("Failed to write data");
      BlinkLED(LED_GREEN, 5);
      // Close the file
      file.close();
      return false;   
    }
  }
}
// -----------------------------------------------------------------------------------------------------
String encodeBase64(){
  // Read the file content
  File file = SPIFFS.open(filePath, "r");
  if (!file) {
      mySerial.println("Failed to open file for reading");
      BlinkLED(LED_GREEN, 5);
      return "-2";
  }

  String fileContent;
  while (file.available()) {
      fileContent += char(file.read());
  } 
  file.close();

  unsigned int fileSize = fileContent.length();    
  mySerial.println("File Content size = " + String( fileSize ) );

  if ( fileSize > 20000 ){  // 20 kb
    SPIFFS.remove(filePath);
    filePath = "/dataset_" + String( rtc.getEpoch() )+ ".csv";
    createNewDatasetFile(filePath);
  }

  unsigned int base64len = encode_base64_length(fileSize);
  mySerial.println("base64 size = " + String( base64len ) );

  unsigned char encodedContent[base64len];
  
  size_t output_size = 0;
  // note input is consumed in this step: it will be empty afterwards
  // Base64 encode the file content (GitHub API requires base64 encoded content)
  // int err =  base64_encode(output, pBuffer, fileSize); 
  int err = mbedtls_base64_encode(encodedContent, base64len, &output_size, (const unsigned char*) fileContent.c_str() , fileContent.length());
  if (err != 0) {
    switch(err){
      case(-0x002A):
        mySerial.println("error base64 encoding, error " +String(err) + " buffer too small. Buff size: " + String(base64len) );
        break;
      case(-0x002C):
        mySerial.println("error base64 encoding, error " +String(err) + " invalid char. buff size: " + String(base64len) );
        break;
      default:
        mySerial.println("error base64 encoding, error " +String(err) + " unk err.  buff size: " + String(base64len) );
        break;
    }
    BlinkLED(LED_GREEN, 5);  
    return "-1";	
  }

return String( (char*)encodedContent);
}

// --------------------------------------------------------------------------------------------------------------------------
 unsigned long encode_base64_length(unsigned long input_length) {
    return input_length * 4 / 3 + 4;  // Adjust size for Base64 encoding
 }

//----------------------------------------------------------------------------------------------------------------------------
void uploadGitHub(){
  if (WiFi.status() != WL_CONNECTED) {
    connectWIFI(5, 10);
  }

  // Upload file to GitHub
  if (WiFi.status() != WL_CONNECTED) {
    mySerial.println("WIFI not available. skipping upload.");
    return;
  }

  mySerial.println("get the file SHA Blob from GitHub if it exists ....");
  String fileSHA = getFileSHA();  // Get the SHA of the existing file
  if (fileSHA == "")
    mySerial.println("Not found.");

  mySerial.println("");
  
  // Create the JSON payload
  String jsonPayload="";

  if (fileSHA !=""){
    // Prepare JSON payload
    jsonPayload = "{ \"message\": \"Updated " + String(githubFilePath) + " via Arduino\", \"sha\":\""+ String(fileSHA) +"\", \"content\": \"" + String( encodeBase64() ) + "\", \"branch\": \"" + String(githubBranch) + "\" }";
  }else{
    // Prepare JSON payload
    jsonPayload = "{ \"message\": \"Add " + String(githubFilePath) + " via Arduino\", \"content\": \"" + String( encodeBase64() ) + "\", \"branch\": \"" + String(githubBranch) + "\" }";
  }
  

  // Create the GitHub API URL
  String githubUrl = "https://api.github.com/repos/" + String(githubUser) + "/" + String(githubRepo) + "/contents/" + String(githubFilePath);

  HTTPClient http;
  http.begin(githubUrl);
  http.addHeader("Authorization", "token " + String(githubToken));
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.PUT(jsonPayload); // Use PUT method to create/update file

  if (httpResponseCode > 0) {
    // Read the response body
    String jsonResponse = http.getString();
    http.end();
    mySerial.println("File uploaded successfully!");
    mySerial.println("Response: " + jsonResponse);
    // Parse the JSON response (optional, for error handling)
    // Check for success or failure
    if ( jsonResponse.indexOf("content") != -1 ) {
      mySerial.println("File pushed successfully!");
    } else if ( jsonResponse.indexOf("message") != -1 ) {
      mySerial.print("Error: ");
      int posStart = jsonResponse.indexOf("message");
      if (posStart == -1){
        mySerial.print(" not found");
        return ;
      }
      int posEnd = jsonResponse.indexOf("\"", posStart + 8 );
      mySerial.println( jsonResponse.substring(posStart+6 , posEnd) );
      BlinkLED(LED_GREEN, 5);
    }
  } else {
    mySerial.println("Error uploading file: " + http.errorToString(httpResponseCode) );
    http.end();
  }
  return ;
}


//-------------------------------------------------------------------------------
// Function to send a GET request to GitHub and extract the file's SHA blob
String getFileSHA() {
  if (WiFi.status() != WL_CONNECTED) {
    mySerial.println("WIFI not available. skipping upload.");
    return "-2";
  }
  
  mySerial.println("Free internal heap before TLS " + String( ESP.getFreeHeap()) );

  String sha = "";  

  // Create the GitHub API URL
  String githubUrl = "https://api.github.com/repos/" + String(githubUser) + "/" + String(githubRepo) + "/contents/" + String(githubFilePath) + "?ref=" + String(githubBranch);

  mySerial.print("Requesting URL: ");
  mySerial.println(githubUrl);

  // Begin the GET request
  HTTPClient http;
  http.begin(githubUrl);
  http.addHeader("Authorization", "token " + String(githubToken));
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
      String responseBody = http.getString();
      http.end();
      mySerial.println("SHA Blob request made successfully!");
      mySerial.println("Response: " + responseBody);
      return extractSHA(responseBody);
  } else {
    mySerial.println("Failed to retrieve file: " + http.errorToString(httpResponseCode) );
    http.end();
    return "-3";
  }
}

// -----------------------------------------------------------------------------
// Function to parse the JSON response and extract the 'sha' field
String extractSHA(String response) {
  int posStart = response.indexOf("sha");
  int posEnd = response.indexOf("\"", posStart + 8 );
  if (posStart == -1){
    mySerial.print("SHA Blob not found");
    return "-4";
  }

  String sha = response.substring(posStart+6, posEnd);
  mySerial.print("File SHA: ");
  mySerial.println(sha);
  return sha;
}

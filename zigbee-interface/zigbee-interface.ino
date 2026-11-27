const char *version = "V2.5 du 08/08/2026";
/**
 \brief Module Interface Utilisateur WIFI
*/
// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
//
// ne pas oublier l update du print version dans le setup
// 08/08/2026 : V2.5, ajout commande DSPTIME* pour log du date-time
// 14/07/2026 : V2.4, amelioration DISPLAY*, envoie datetime chaque minute sur WIFI
// 15/03/2026 : V2.1, ajout du bouton button1 en parallele de button (BOOT_PIN) pour montage sur dessus de boitier
//              - correction bug sur localtime (ajout setenv et tzset())
// 29/12/2025 : V2, ajout reception de messages zigbee
//              - bascule serial2 sur pin par defaut (gpio4, gpio5)
//              - modification de la methode zigbeeCore.cpp/esp_zb_app_signal_handler() : ajout un log_v de tracage 
// 11/12/2025 : ajout lors du setup :
//              - add lightoff() lors du setup
//              - add open network for 60 seconds
// ATTENTION
// 19/10/2025 : correction :  erase all flash = enabled + re-appairage de tous les devices
//              next step :   connection serie avec interface wifi (esp32 N°2)
//                            étude appofondie des structures zb_device_params_s et network_descriptor
// 18/10/2025 : le zigbee semble OK, mais plus aucune action possible sur les actionneurs
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @brief This example demonstrates simple Zigbee multi-light switch.
 *
 * The example demonstrates how to use Zigbee library to control multiple light bulbs.
 * The light bulbs are Zigbee devices, which are controlled by a Zigbee coordinator/router (Multi-Switch).
 * Settings are stored in NVS to not be lost after power loss.
 * Configuring and controlling the lights is done via serial input.
 *
 * Proper Zigbee mode must be selected in Tools->Zigbee mode
 * and also the correct partition scheme must be selected in Tools->Partition Scheme.
 *
 * Please check the README.md for instructions and more detailed description.
 *
 * Created by Jan Procházka (https://github.com/P-R-O-C-H-Y/)
 */

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include "ep/ZigbeeLight.h"
#include <Preferences.h>

////////////////////////////////////////////////////////////
//           SERIAL MANAGEMENT DECLARATIONS
bool xTrace = false;                   // false will stop logging
const String MySerialName = "ZIGBEE";  // device name (required) for serial management
#include "serial_management.h"
////////////////////////////////////////////////////////////
/* Time structure */
long last_time = 0; long last_minute = 0; long last_hour = 0; 
struct tm timeinfo; time_t secondes; time_t compute_secondes; long settime_secondes;
const String time_zone = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
#include "time.h"



#define ZIGBEE_ROLE ZIGBEE_COORDINATOR  // ZIGBEE_ROUTER for HomeAssistant integration, ZIGBEE_COORDINATOR for running own network
#define LED_PIN LED_BUILTIN

/* Zigbee switch configuration */
#define SWITCH_ENDPOINT_NUMBER 1
ZigbeeSwitch zbSwitch = ZigbeeSwitch(SWITCH_ENDPOINT_NUMBER);

// Créer un endpoint Light pour recevoir les commandes
#define ENDPOINT_ID 10
//ZigbeeLight zbLight = ZigbeeLight(ENDPOINT_ID);
/* Zigbee thermostat configuration */
#define THERMOSTAT_ENDPOINT_NUMBER   9
#define USE_RECEIVE_TEMP_WITH_SOURCE 1
ZigbeeThermostat zbThermostat = ZigbeeThermostat(THERMOSTAT_ENDPOINT_NUMBER);

// Définition des IDs de clusters que l'on veut supporter dans EP generique
//#define NB_CLUSTERS 4
//esp_zb_cluster_list_t *cluster_list[NB_CLUSTERS];

const int rx2pin = 4;    //serial2 rx port
const int tx2pin = 5;    //serial2 tx port

uint8_t button = BOOT_PIN;
uint8_t button1 = 10;
int buttonState;
int lastButtonState = LOW;
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

// To be stored in NVS to not be lost after power loss
Preferences prefs;

#define Max_Device 64
zb_device_params_t device_array[Max_Device];

typedef struct full_device_array_s {
  zb_device_params_t bound_device;    // zigbee structure (EP, shortAdr, IeeeAdr)
  String ihm_name;                    // logical name for user
  } full_device_array_t ;
full_device_array_t full_device_array[Max_Device];    // contient un zigbee device associé a un nom logique utilisateur
int nb_full_device_array = 0;                         // contient le nombre de position stockees dans full_device_array

/********************* Callbacks Zigbee ***************************/

// Callback pour les changements d'état du Light
void onLightStateChange(bool state) {
  digitalWrite(LED_PIN, state);
  Serial.printf("🔵 Light state changed: %s\n", state ? "ON" : "OFF");
  }

// Callback global pour les réponses par défaut
void onDefaultResponse(zb_cmd_type_t resp_to_cmd, esp_zb_zcl_status_t status, 
                       uint8_t endpoint, uint16_t cluster) {
  Serial.printf("📥 Default Response - Cmd: 0x%02X, Status: 0x%02X, EP: %d, Cluster: 0x%04X\n",
                resp_to_cmd, status, endpoint, cluster);
  }


void storeLightParams(zb_device_params_t *light, int light_number) {
  char key[10];
  snprintf(key, sizeof(key), "light_%d", light_number);
  prefs.putBytes(key, light, sizeof(zb_device_params_t));
  }

void loadLightParams(zb_device_params_t *light, int light_number) {
  char key[10];
  snprintf(key, sizeof(key), "light_%d", light_number);
  prefs.getBytes(key, light, sizeof(zb_device_params_t));
  }

void printIEEEAddress(const uint8_t *ieee_addr) {
      for (int i = 7; i > -1; i--) {
        if (ieee_addr[i] < 0x10) {
          Serial.print("0"); // Ajoute un zéro de tête si nécessaire
        }
        Serial.print(ieee_addr[i], HEX); if ( i>0 ) { Serial.print(':'); }
      }
    }
// Gestion de la liste des devices (full_device_array)
int addDevice (uint8_t endpoint, esp_zb_ieee_addr_t IeeeAdr, String DevName){    // add a new device in full_device_array, return Device_Rank in full_device_array
  int rank=-1;
  return (rank);
  }
int delDevice (uint8_t DevRank) {   // remove a device from full_device_array, return Device_Rank removed from full_device_array
  int result = -1;
  return(result);
  }
int isNameExists(String DevName) { // return rank n if DeviceName exists in full-device_array, else return -1
  int rank=-1;
  return (rank);
  }
int isDevExists(uint8_t endpoint, esp_zb_ieee_addr_t IeeeAdr) { // return rank n if Device exists in full-device_array, else return -1
  int rank=-1;
  return (rank);
  }
String getDevName (uint8_t DevRank) { // return DevName of rank from full_device_array, elsewise Null
  String result="";
  return (result);
  }
//
void display_config() {
  // Print bound devices
    zbSwitch.printBoundDevices(Serial);
    Serial.println("bound_devices = "); 
    // Affichage des appareils liés
    std::list<zb_device_params_t *> boundDevices = zbSwitch.getBoundDevices();
    // Parcourt la liste et affiche les informations
    int deviceCount = 0;
    for (auto it = boundDevices.begin(); it != boundDevices.end(); ++it) {
      zb_device_params_t *device = *it;
      // load full_device_array with boundDevices
      int rank = isDevExists(device->endpoint,device->ieee_addr);
      if (rank == -1) {
        rank = addDevice( device->endpoint,  device->ieee_addr, "");  // add device to full_device_array
      }
      // rank = device rank in full_device_array
      Serial.print("Appareil "); Serial.print(deviceCount++);
      Serial.print(", endPoint = "); Serial.print(device->endpoint);
      Serial.print(", short_addr = 0x"); Serial.print(device->short_addr, HEX); 
      Serial.print(", ieee_addr = "); printIEEEAddress(device->ieee_addr);
      Serial.println();
    }

    Serial.println("Configured Lights :");
    for ( int i=1; i<4; i++) { Serial.printf("Light %d: %d %s\n", i, device_array[i].endpoint, Zigbee.formatIEEEAddress(device_array[i].ieee_addr)); }
    //Serial.printf("Light 1: %d %s\n", device_array[1].endpoint, Zigbee.formatIEEEAddress(device_array[1].ieee_addr));
    //Serial.printf("Light 2: %d %s\n", device_array[2].endpoint, Zigbee.formatIEEEAddress(device_array[2].ieee_addr));
    //Serial.printf("Light 3: %d %s\n", device_array[3].endpoint, Zigbee.formatIEEEAddress(device_array[3].ieee_addr));
  }


int ExecCommande(String commande) {
  //      DECODAGE ET EXECUTION DES COMMANDES STANDARDS "EMET;commande"
  Serial.println("ExecCommande: "+commande);
  static String texte; texte.reserve(128); 
  char r[9]; 
  char sep=';'; String SRC=""; String action=""; int i;
  i=commande.indexOf(sep);
  if (i>0){
    SRC=commande.substring(0,i);
    action=commande.substring(i+1);
  }
  if (action.startsWith("REBOOT*")) {          // reboot device
    Serial.println("Rebooting..."); 
    log_to_wifi("Zigbee: rebooting .........."); logLocalTime();
    MyDelay(1000);
    ESP.restart();
  }
  else if (action.startsWith("SETTIME*")) {          // set system time
    secondes=action.substring(8).toDouble(); settime_secondes=millis()/1000;
    timeinfo=*localtime(&secondes); Serial.println(&timeinfo, "settime: %A, %d %B %Y %H:%M:%S");
  }
  else if (action.startsWith("GETTIME*")) {          // ask system time
    if (getLocalTime(&timeinfo)) {
      char r[9]; dtostrf(time(&secondes),8,0,r);
      texte=';'; texte+=SRC;  texte+=";SETTIME*"; texte += r;
      Serial.println(texte);
      serial_write(texte); 
    }
  }
  else if (action.startsWith("GET_CFG*")) {           // demande info du devices n
    // format "GET_CFG*n" avec n=numero du device demande
    // reponse :  si n existe, renvoyer les infos (EP, IeeeAdr) du device n "SEND_CFG*n,EP,IeeeAdr,Name"
    //            sinon renvoyer "SEND_CFG*"
    texte=";"+SRC+";SEND_CFG*";
    i=action.substring(8).toInt();
    
    dtostrf(time(&secondes),8,0,r);
    texte += r;
    serial_write(texte);
  }
  else if (action.startsWith("ZBCMD*")) {           // Commande Zigbee, format : "ZBCMD*1on"
    command_decode(action.substring(6));
  }
  else if (action.startsWith("ZBOPEN*")) {           // Commande Zigbee, ouvrir le reseau pour 180sec
    Zigbee.openNetwork(180);
    log_to_wifi("Zigbee: open network for 180 sec."); logLocalTime();
  }
  else if (action.startsWith("ZBDEV")) {           // commande de device On,Off, Toggle
    int i=action.indexOf("*");
    int device_number=action.substring(i).toInt();
    if (device_number<1 or device_number>99) { device_number = 1; }
    Serial.printf("ZbDev, device_numer=%d\n",device_number);
    if (action.startsWith("ZBDEVON*")) {          // commande ON, format : "ZBDEVON*1"
      zbSwitch.lightOn(device_array[device_number].endpoint, device_array[device_number].ieee_addr);
      }       
    else if (action.startsWith("ZBDEVOFF*")) {     // commande OFF, format : "ZBDEVOFF*1"
      zbSwitch.lightOff(device_array[device_number].endpoint, device_array[device_number].ieee_addr);
      }      
    else if (action.startsWith("ZBDEVTOG*")) {    // commande TOGGLE, format : "ZBDEVTOG*1"
      zbSwitch.lightToggle(device_array[device_number].endpoint, device_array[device_number].ieee_addr);
      }
  }
  else if (action.startsWith("DSPTIME*")) { logLocalTime(); }    // display time on monitor
  else { 
    Serial.println("Invalid Action : "+action);
  }
  return(1);
  }

void command_decode(String command){
  Serial.println(command);
  // decodage de la commande recue, format : 1on
  int device_number = command[0]-48 ;
  if (device_number<1 or device_number>9) { device_number = 9; } 
  String action = command.substring(1,3);
  Serial.print("decode : "); Serial.print(device_number); Serial.print(" "); Serial.println(action);
  //
  if (action == "on"){
    zbSwitch.lightOn(device_array[device_number].endpoint, device_array[device_number].ieee_addr);
  } else if (action == "of") {
    zbSwitch.lightOff(device_array[device_number].endpoint, device_array[device_number].ieee_addr);
  } else if (action == "to") {
    zbSwitch.lightToggle(device_array[device_number].endpoint, device_array[device_number].ieee_addr);
  } else if (action == "re") {
    // suppression un device
    uint8_t ieee_address_empty[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    device_array[device_number].endpoint = 0;
    memcpy(device_array[device_number].ieee_addr, ieee_address_empty, 8);
    storeLightParams(&device_array[device_number], device_number);
  } else {
    Serial.println ("Action non reconnue");
  }
  }

/* Callback pour gérer les messages entrants A DEBUGGER
void zbMessageReceived(esp_zb_app_signal_t *signal, uint16_t src_addr, uint8_t endpoint) {
  Serial.print("Message reçu de : 0x");
  Serial.println(src_addr, HEX);
  Serial.print("Endpoint : ");
  Serial.println(endpoint);

  // Vérifier le type de message
  if (signal->type == ESP_ZB_ZCL_CMD_ON_OFF_RESPONSE) {
    Serial.println("Commande ON/OFF reçue !");
    // Traiter la commande (par exemple, allumer/éteindre une LED)
    bool state = signal->u.cmd_on_off.on_off;
    Serial.print("Nouvel état : ");
    Serial.println(state ? "ON" : "OFF");
  }
  else if (signal->type == ESP_ZB_ZCL_CMD_TEMPERATURE_MEASUREMENT_RESPONSE) {
  Serial.println("Donnée de température reçue !");
  // Extraire la température du message
  float temperature = signal->u.cmd_temperature_measurement.measured_value;
  Serial.print("Température : ");
  Serial.print(temperature / 100.0); // Conversion en °C
  Serial.println(" °C");
  }
// Ajoutez d'autres types de messages selon vos besoins
  }
*/

void printLocalTime() {
  compute_secondes=secondes+((millis()/1000) - settime_secondes);   // compute tile since last settime
  timeinfo=*localtime(&compute_secondes);                           // load time in structure timeinfo
  Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");               // print date & time
  }

void logLocalTime() {
  compute_secondes=secondes+((millis()/1000) - settime_secondes);   // compute tile since last settime
  timeinfo=*localtime(&compute_secondes);                           // load time in structure timeinfo
  //Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");               // print date & time
  //
  char buffer[80];
  sprintf(buffer, "Zigbee: %02d/%02d/%d %02d:%02d:%02d", timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  log_to_wifi(String(buffer));     // log date-heure zigbee sur console wifi
  }

void log_to_wifi(String message){
  /**
   @brief fonction pour envoyer un message sur la log du device WIFI
  */
  String z1=";WIFI;DISPLAY*";
  z1=z1+message; //Serial.println(z1);
  serial_write(z1);
  }

/********************* Arduino functions **************************/
void MyDelay (long duree) { unsigned long start = millis(); while ( millis()-start < duree ) {;} }  // replace delay()
void setup() {
  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200); delay(100); Serial.print("Zigbee MultiSwitch - Début du setup, Version : "); Serial.println(version); 
  Serial2.begin(115200, SERIAL_8N1, 4, 5);  // And configure Serial2 on pins RX=D4, TX=D5
  MyDelay(10);
  log_to_wifi("Zigbee: starting ..........");

  // Initialize Preferences - to save data permanently on eeprom memory
  prefs.begin("lights", false);  // false=read/write mode, space name="lights"

  // Load saved light parameters
  //loadLightParams(&light_1, 1); loadLightParams(&light_2, 2); loadLightParams(&light_3, 3); 
  loadLightParams(&device_array[1], 1);
  loadLightParams(&device_array[2], 2);
  loadLightParams(&device_array[3], 3);

  // Init button switch
  pinMode(button, INPUT_PULLUP);
  pinMode(button1, INPUT_PULLUP);
  /*
  esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE); // Ajout du cluster de base (Obligatoire)
  esp_zb_cluster_list_add_on_off_cluster(esp_zb_cluster_list, esp_zb_on_off_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE); // ajout clusters CLIENT
  esp_zb_cluster_list_add_temperature_meas_cluster(esp_zb_cluster_list, esp_zb_temperature_meas_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
  // 2. Création de l'Endpoint générique (ex: ID 2)
  esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, {
        .endpoint = 2, 
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, // Home Automation
        .app_device_id = 0xFFFF,                  // ID Générique
        .app_device_version = 0
    });
  */
  // Configuration du Light endpoint
  //zbLight.setManufacturerAndModel("Espressif", "EP Light");
  //zbLight.onLightChange(onLightStateChange);

  // Configuration du thermostat endpoint
  zbThermostat.setManufacturerAndModel("Espressif", "ZigbeeThermostat");

  // Set Zigbee device name and model
  zbSwitch.setManufacturerAndModel("Espressif", "ZBMultiSwitch");

  // Configuration du callback global
  Zigbee.onGlobalDefaultResponse(onDefaultResponse);

  zbSwitch.onLightStateChange(onLightStateChange);

  // Set binding settings depending on the role
  if (ZIGBEE_ROLE == ZIGBEE_COORDINATOR) {
    zbSwitch.allowMultipleBinding(true);  // To allow binding multiple lights to the switch
  } else {
    zbSwitch.setManualBinding(true);  //Set manual binding to true, so binding is done on Home Assistant side
  }
  zbSwitch.allowMultipleBinding(true);  // To allow binding multiple lights to the switch

  // Add endpoint to Zigbee Core
  Serial.println("Adding ZigbeeSwitch endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbSwitch);
  //Serial.println("Adding zbLight endpoint to Zigbee Core");
  //Zigbee.addEndpoint(&zbLight);
  //Add endpoint to Zigbee Core
  Serial.println("Adding thermostat endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbThermostat);

  // When all EPs are registered, start Zigbee with given role
  Serial.println("Starting Zigbee Stack");
  if (!Zigbee.begin(ZIGBEE_ROLE, true )) {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  }

  Serial.println("Connecting to network");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connected to network");
  // lightoff all devices
  Serial.println("  --> SIG Input : All Lights OFF");
  zbSwitch.lightOff();
  //
  Serial.println("  --> Open Network for 180sec");
  Zigbee.openNetwork(180);
  // init de la time_zone
  setenv("TZ",time_zone.c_str(),1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
  // mise a l'heure
  MyDelay(500);
  serial_write(";WIFI;GETTIME*");   // ask for time set
  MyDelay(100);
  //
  Serial.println("MODULE "+MySerialName+" : Fin du setup"); digitalWrite(LED_BUILTIN, LOW);
  log_to_wifi("Zigbee: started ..........");
  }

void loop() {
  static unsigned long loop_average = 0; static unsigned long loop_time =0;
  loop_time=micros();
  //
  String command = ""; int x;
  // Handle button switch in loop()
  x=0;
  if (digitalRead(button) == LOW)  { x = button; }   // Push button pressed
  if (digitalRead(button1) == LOW) { x = button1; }  // Push button1 pressed
  
  if (x>0 & digitalRead(x) == LOW) {
    // Key debounce handling for button = x
    while (digitalRead(x) == LOW) {
      delay(50);
    }
    // Print bound devices
    //Serial.println("Bound devices:");
    //zbSwitch.printBoundDevices(Serial);
    //display_config() ;
    Serial.println("  --> SIG Input : All Lights Toggle");
      zbSwitch.lightToggle();
  }
  // Handle serial input to configure and control the lights
  String message_lu = "";
  if (Serial2.available()) {
    message_lu = serial_read(2);     // lecture de la commande sur le port serie 2
  }
  else if (Serial.available()) {
    message_lu = serial_read(0);     // lecture de la commande sur le port serie console
  }
  if (message_lu != "") {           // decodage message lu, format : DEST;Commande
    int i = message_lu.indexOf(";");
    String command = message_lu.substring(i+1);
    String DEST = message_lu.substring(0,i);
    Serial.print("Comande lue: "+DEST+","+command+'\n');
    //
    if (command.startsWith("ZBCMD*")) { command=command.substring(6); }
    if (command == "config") {
      //wait for light number, endpoint and ieee address
      Serial.println("Enter light number (1-3):");
      while (!Serial.available()) {
        delay(100);
      }
      int light_number = Serial.parseInt();
      Serial.println("Enter endpoint:");
      while (!Serial.available()) {
        delay(100);
      }
      int endpoint = Serial.parseInt();
      Serial.println("Enter ieee address:");
      while (!Serial.available()) {
        delay(100);
      }
      String ieee_address = Serial.readStringUntil('\n');
      ieee_address.trim();
      //convert ieee address to uint8_t array (format in string is 00:00:00:00:00:00:00:00)
      uint8_t ieee_address_array[8];
      int index = 0;
      bool valid = true;

      // Check if the string has the correct format (8 hex pairs with colons)
      if (ieee_address.length() != 23) {  // 8 pairs * 2 + 7 colons
        Serial.println("Invalid IEEE address format. Expected format: 00:00:00:00:00:00:00:00");
        valid = false;
      } else {
        for (int i = 0; i < ieee_address.length() && index < 8 && valid; i += 3) {
          // Check for colon at expected positions
          if (i > 0 && ieee_address.charAt(i - 1) != ':') {
            valid = false;
            break;
          }
          // Convert two hex characters to a byte
          char hex[3] = {ieee_address.charAt(i), ieee_address.charAt(i + 1), '\0'};
          char *endptr;
          long value = strtol(hex, &endptr, 16);
          if (*endptr != '\0' || value < 0 || value > 255) {
            valid = false;
            break;
          }
          // Store bytes in reverse order to match Zigbee standard
          ieee_address_array[7 - index++] = (uint8_t)value;
        }
      }

      if (!valid || index != 8) {
        Serial.println("Invalid IEEE address. Please enter a valid address in format: 00:00:00:00:00:00:00:00");
        return;
      }
      //set the light parameters
      if (light_number == 1) {
        //light_1.endpoint = endpoint;
        //memcpy(light_1.ieee_addr, ieee_address_array, 8);
        //storeLightParams(&light_1, 1);
        device_array[1].endpoint = endpoint;
        memcpy(device_array[1].ieee_addr, ieee_address_array, 8);
        storeLightParams(&device_array[1], 1);

      } else if (light_number == 2) {
        //light_2.endpoint = endpoint;
        //memcpy(light_2.ieee_addr, ieee_address_array, 8);
        //storeLightParams(&light_2, 2);
        device_array[2].endpoint = endpoint;
        memcpy(device_array[2].ieee_addr, ieee_address_array, 8);
        storeLightParams(&device_array[2], 2);
      } else if (light_number == 3) {
        device_array[3].endpoint = endpoint;
        memcpy(device_array[3].ieee_addr, ieee_address_array, 8);
        storeLightParams(&device_array[3], 3);
      }
      Serial.printf("Light %d configured\n", light_number);
    } else if (command == "remove") {
      /*
      //wait for light number
      Serial.println("Enter light number (1-3):");
      while (!Serial.available()) {
        delay(100);
      }
      int light_number = Serial.parseInt();
      uint8_t ieee_address_empty[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      if (light_number == 1) {
        //light_1.endpoint = 0;
        //memcpy(light_1.ieee_addr, ieee_address_empty, 8);
        //storeLightParams(&light_1, 1);
        device_array[1].endpoint = 0;
        memcpy(device_array[1].ieee_addr, ieee_address_empty, 8);
        storeLightParams(&device_array[1], 1);

      } else if (light_number == 2) {
        light_2.endpoint = 0;
        memcpy(light_2.ieee_addr, ieee_address_empty, 8);
        storeLightParams(&light_2, 2);
      } else if (light_number == 3) {
        light_3.endpoint = 0;
        memcpy(light_3.ieee_addr, ieee_address_empty, 8);
        storeLightParams(&light_3, 3);
        
      }
      Serial.printf("Light %d removed\n", light_number);
      */
    } else if (command == "on") {
      Serial.println("  --> SIG Input : All Lights ON");
      zbSwitch.lightOn();
    } else if (command == "off") {
      Serial.println("  --> SIG Input : All Lights OFF");
      zbSwitch.lightOff();
    } else if (command == "toggle") {
      Serial.println("  --> SIG Input : All Lights Toggle");
      zbSwitch.lightToggle();
    } else if (command == "freset") {
      Serial.println("  --> SIG Input : Factory Reset!");
      delay(1500);
      Zigbee.factoryReset();
    } else if (command == "open_network" || command == "open" ) {
      Serial.println("  --> SIG Input : Open Network");
      if (ZIGBEE_ROLE == ZIGBEE_COORDINATOR) {
        Zigbee.openNetwork(180);
      } else {
        Serial.println("Open network is only available for coordinator role");
      }
    } else if (command == "display_config" || command == "display") {
        display_config() ;
    } else { ExecCommande(message_lu);  // executer une commande standard format : "EMET;Commande" 
    }
  }
  // Print temperature sensor data each n seconds
  static uint32_t last_print = 0;
  if (millis() - last_print > 60000 || millis()<last_print ) {
    last_print = millis();
    Serial.printf("Loop zbThermostat.printBoundDevices");
    zbThermostat.printBoundDevices(Serial);
    printLocalTime();
    Serial.printf("loop average: %d microsec.\n",loop_average);
  }
  loop_average=(loop_average+(micros()-loop_time))/2; 
  }

/************************************
 * Rage
 * Against
 * The
 * Garage
 * Door
 * Opener
 *
 * Copyright (C) 2022  Paul Wieland
 *
 * GNU GENERAL PUBLIC LICENSE
 ************************************/

#include "ratgdo.h"

void setup(){
    swSerial.begin(9600, SWSERIAL_8N2, -1, OUTPUT_GDO, true);
    
    Serial.begin(115200);
    Serial.println("");

    #ifndef DISABLE_WIFI
    bootstrapManager.bootstrapSetup(manageDisconnections, manageHardwareButton, callback);
    
    // Setup mqtt topics
    baseTopic = String(mqttTopicPrefix) + deviceName;
    baseTopic.replace(" ", "_");
    baseTopic.toLowerCase();
    
    doorCommandTopic = baseTopic + "/command";
    doorStatusTopic = baseTopic + "/status";

    bootstrapManager.setMQTTWill(doorStatusTopic.c_str(),"offline",1,false,true);
    
    Serial.print("doorCommandTopic: ");
    Serial.println(doorCommandTopic);
    Serial.print("doorStatusTopic: ");
    Serial.println(doorStatusTopic);
    #endif

    // pinMode(LED_BUILTIN, OUTPUT);
    // digitalWrite(LED_BUILTIN, HIGH); // led off

    pinMode(TRIGGER_OPEN, INPUT_PULLUP);
    pinMode(TRIGGER_CLOSE, INPUT_PULLUP);
    pinMode(TRIGGER_LIGHT, INPUT_PULLUP);
    pinMode(STATUS_DOOR, OUTPUT);
    pinMode(STATUS_OBST, OUTPUT);
    pinMode(INPUT_RPM1, INPUT_PULLUP); // set to pullup to add support for reed switches
    pinMode(INPUT_RPM2, INPUT_PULLUP);
    pinMode(INPUT_OBST, INPUT);

    attachInterrupt(TRIGGER_OPEN,isrDoorOpen,CHANGE);
    attachInterrupt(TRIGGER_CLOSE,isrDoorClose,CHANGE);
    attachInterrupt(TRIGGER_LIGHT,isrLight,CHANGE);
    
    attachInterrupt(INPUT_OBST,isrObstruction,FALLING);

    attachInterrupt(INPUT_RPM1, isrRPM1, CHANGE);
    attachInterrupt(INPUT_RPM2, isrRPM2, CHANGE);

    delay(60); // 
    Serial.println("Setup Complete");
    Serial.println(" _____ _____ _____ _____ ____  _____ ");
    Serial.println("| __  |  _  |_   _|   __|    \\|     |");
    Serial.println("|    -|     | | | |  |  |  |  |  |  |");
    Serial.println("|__|__|__|__| |_| |_____|____/|_____|");
    Serial.print("version ");
    Serial.print(VERSION);
    #ifdef DISABLE_WIFI
    Serial.print(" (WiFi disabled)");
    #endif
    Serial.println("");

}


/*************************** MAIN LOOP ***************************/
void loop(){
    if (isConfigFileOk){
        // Bootsrap loop() with Wifi, MQTT and OTA functions
        bootstrapManager.bootstrapLoop(manageDisconnections, manageQueueSubscription, manageHardwareButton);

        if(!setupComplete){
            setupComplete = true;
            
            uint8_t mac[6];
            char macChar[5] = { 0 };
            wifi_get_macaddr(STATION_IF, mac);
            sprintf(macChar, "%02X%02X", mac[4], mac[5]);
            String macStr = String(macChar);

            // send discovery topic(s) for homeassistant
            Serial.println("Set Door Cover Config");
            JsonObject door = bootstrapManager.getJsonObject();
            door["~"] = baseTopic;
            door["name"] = deviceName;
            door["uniq_id"] = "ratgdo_" + macStr + "_cover";
            door["dev_cla"] = "garage";
            door["avty_t"] = "~/status";
            door["cmd_t"] = "~/command";
            door["stat_t"] = "~/status";
            door["pl_open"] = "open";
            door["pl_cls"] = "close";
            door["pl_stop"] = "stop";
            bootstrapManager.publish(("homeassistant/cover/ratgdo_" + macStr + "_cover/config").c_str(), door, true);

            Serial.println("Set Obstruction Sensor Config");
            JsonObject obstruction = bootstrapManager.getJsonObject();
            obstruction["~"] = baseTopic;
            obstruction["name"] = deviceName + " Obstruction";
            obstruction["uniq_id"] = "ratgdo_" + macStr + "_obstruction";
            obstruction["ic"] = "mdi:garage-alert";
            obstruction["dev_cla"] = "motion";
            obstruction["avty_t"] = "~/status";
            obstruction["stat_t"] = "~/status";
            obstruction["pl_on"] = "obstructed";
            obstruction["pl_off"] = "clear";
            bootstrapManager.publish(("homeassistant/binary_sensor/ratgdo_" + macStr + "_obstruction/config").c_str(), obstruction, true);

            Serial.println("Set Reed Switch Config");
            JsonObject reed = bootstrapManager.getJsonObject();
            reed["~"] = baseTopic;
            reed["name"] = deviceName + " Reed";
            reed["uniq_id"] = "ratgdo_" + macStr + "_reed";
            reed["dev_cla"] = "garage_door";
            reed["avty_t"] = "~/status";
            reed["stat_t"] = "~/status";
            reed["pl_on"] = "reed_open";
            reed["pl_off"] = "reed_closed";
            bootstrapManager.publish(("homeassistant/binary_sensor/ratgdo_" + macStr + "_reed/config").c_str(), reed, true);

            Serial.println("Set Light Button Config");
            JsonObject button = bootstrapManager.getJsonObject();
            button["~"] = baseTopic;
            button["name"] = deviceName + " Light";
            button["uniq_id"] = "ratgdo_" + macStr + "_light";
            button["avty_t"] = "~/status";
            button["cmd_t"] = "~/command";
            button["payload_press"] = "light";
            bootstrapManager.publish(("homeassistant/button/ratgdo_" + macStr + "_light/config").c_str(), button, true);

            // Broadcast that we are online
            Serial.println("Send Online Status");
            bootstrapManager.publish(doorStatusTopic.c_str(), "online", false);
        }
    }

    obstructionLoop();
    doorStateLoop();
    dryContactLoop();
}

/*************************** DETECTING THE DOOR STATE ***************************/
void IRAM_ATTR isrRPM1 () {
    int state = digitalRead(INPUT_RPM1);
    if (isrRPM1State == state) {
        return;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastRPM1StateUpdate < 2) {
        return;
    }

    lastRPM1StateUpdate = currentMillis;
    isrRPM1State = state;
    
    if (isrRPM1State == false && isrRPM2State == false) {
        counter++;
    }
}

void IRAM_ATTR isrRPM2 () {
    int state = digitalRead(INPUT_RPM2);
    if (isrRPM2State == state) {
        return;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastRPM2StateUpdate < 2) {
        return;
    }

    lastRPM2StateUpdate = currentMillis;
    isrRPM2State = state;

    if (isrRPM1State == false && isrRPM2State == false) {
        counter--;
    }
}

void doorStateLoop(){
    static int lastCounter = 0;

    // Wait 5 pulses before updating to door opening status
    if (counter - lastCounter > 5) {
        if (doorState != "opening") {
            Serial.println("Door Opening...");
            if (isConfigFileOk) {
                bootstrapManager.publish(doorStatusTopic.c_str(), "opening", false);
            }
        }
        lastCounter = counter;
        doorState = "opening";
    }

    if (lastCounter - counter > 5) {
        if (doorState != "closing") {
            Serial.println("Door Closing...");
            if (isConfigFileOk) {
                bootstrapManager.publish(doorStatusTopic.c_str(), "closing", false);
            }
        }
        lastCounter = counter;
        doorState = "closing";
    }

    // 250 millis after the last rotary encoder pulse, the door is stopped
    if (millis() - lastRPM2StateUpdate > 250) {
        // if the door was closing, and is now stopped, then the door is closed
        if (doorState == "closing") {
            doorState = "closed";
            sendDoorStatus();
            digitalWrite(STATUS_DOOR, LOW);
        }

        // if the door was opening, and is now stopped, then the door is open
        if (doorState == "opening") {
            doorState = "open";
            sendDoorStatus();
            digitalWrite(STATUS_DOOR, HIGH);
        }
    }
}

/*************************** DRY CONTACT CONTROL OF LIGHT & DOOR ***************************/
void IRAM_ATTR isrDebounce(const char *type){
    static unsigned long lastOpenDoorTime = 0;
    static unsigned long lastCloseDoorTime = 0;
    static unsigned long lastToggleLightTime = 0;
    static unsigned long lastToggleReedTime = 0;
    unsigned long currentMillis = millis();

    // Prevent ISR during the first 2 seconds after reboot
    if(currentMillis < 2000) return;

    if(strcmp(type, "openDoor") == 0){
        if(digitalRead(TRIGGER_OPEN) == LOW){
            // save the time of the falling edge
            lastOpenDoorTime = currentMillis;
        }else if(currentMillis - lastOpenDoorTime > 500 && currentMillis - lastOpenDoorTime < 10000){
            // now see if the rising edge was between 500ms and 10 seconds after the falling edge
            dryContactDoorOpen = true;
        }
    }

    if(strcmp(type, "closeDoor") == 0){
        if(digitalRead(TRIGGER_CLOSE) == LOW){
            // save the time of the falling edge
            lastCloseDoorTime = currentMillis;
        }else if(currentMillis - lastCloseDoorTime > 500 && currentMillis - lastCloseDoorTime < 10000){
            // now see if the rising edge was between 500ms and 10 seconds after the falling edge
            dryContactDoorClose = true;
        }
    }

    if(!altReedMode && strcmp(type, "toggleLight") == 0){
        if(digitalRead(TRIGGER_LIGHT) == LOW){
            // save the time of the falling edge
            lastToggleLightTime = currentMillis;
        }else if(currentMillis - lastToggleLightTime > 500 && currentMillis - lastToggleLightTime < 10000){
            // now see if the rising edge was between 500ms and 10 seconds after the falling edge
            dryContactToggleLight = true;
        }
    }

    if(altReedMode && strcmp(type, "toggleLight") == 0){
        if(currentMillis - lastToggleReedTime > 500){
            // now see if the change was between 500ms prior change
            lastToggleReedTime = currentMillis;
            altReedToggle = true;
        }
    }
}

void IRAM_ATTR isrDoorOpen(){
    isrDebounce("openDoor");
}

void IRAM_ATTR isrDoorClose(){
    isrDebounce("closeDoor");
}

void IRAM_ATTR isrLight(){
    isrDebounce("toggleLight");
}

// handle changes to the dry contact state
void dryContactLoop(){
    if(dryContactDoorOpen){
        Serial.println("Dry Contact: open the door");
        dryContactDoorOpen = false;
        openDoor();
    }

    if(dryContactDoorClose){
        Serial.println("Dry Contact: close the door");
        dryContactDoorClose = false;
        closeDoor();
    }

    if(!altReedMode && dryContactToggleLight){
        Serial.println("Dry Contact: toggle the light");
        dryContactToggleLight = false;
        toggleLight();
    }

    if (altReedMode && altReedToggle && isConfigFileOk) {
        altReedToggle = false;

        if(digitalRead(TRIGGER_LIGHT) == LOW) {
            Serial.println("Dry Contact: reed_closed");
            bootstrapManager.publish(doorStatusTopic.c_str(), "reed_closed", false);
        } else {
            Serial.println("Dry Contact: reed_open");
            bootstrapManager.publish(doorStatusTopic.c_str(), "reed_open", false);
        }
    }
}

/*************************** OBSTRUCTION DETECTION ***************************/
void IRAM_ATTR isrObstruction(){
    obstructionTimer = millis();
}

void obstructionLoop(){
    if(millis() - obstructionTimer > 60){
        obstructionDetected();
    } else {
        obstructionCleared();
    }
}

void obstructionDetected(){
    if (doorIsObstructed == false) {
        doorIsObstructed = true;
        digitalWrite(STATUS_OBST,HIGH);

        Serial.println("Obstruction Detected");

        if(isConfigFileOk){
            bootstrapManager.publish(doorStatusTopic.c_str(), "obstructed", false);
        }
    }
}

void obstructionCleared(){
    if(doorIsObstructed){
        doorIsObstructed = false;
        digitalWrite(STATUS_OBST,LOW);

        Serial.println("Obstruction Cleared");

        if(isConfigFileOk){
            bootstrapManager.publish(doorStatusTopic.c_str(), "clear", false);
        }
    }
}

void sendDoorStatus(){
    if(isConfigFileOk){
        Serial.print("Door state ");
        Serial.println(doorState);
        bootstrapManager.publish(doorStatusTopic.c_str(), doorState.c_str(), false);
    }
}

/********************************** MANAGE WIFI AND MQTT DISCONNECTION *****************************************/
void manageDisconnections(){
    setupComplete = false;
}

/********************************** MQTT SUBSCRIPTIONS *****************************************/
void manageQueueSubscription(){
    // example to topic subscription
    bootstrapManager.subscribe(doorCommandTopic.c_str());
    Serial.print("Subscribed to ");
    Serial.println(doorCommandTopic);
}

/********************************** MANAGE HARDWARE BUTTON *****************************************/
void manageHardwareButton(){
}

/********************************** MQTT CALLBACK *****************************************/
void callback(char *topic, byte *payload, unsigned int length){

    // Transform all messages in a JSON format
    StaticJsonDocument<BUFFER_SIZE> json = bootstrapManager.parseQueueMsg(topic, payload, length);

    doorCommand = (String)json[VALUE];
    Serial.println(doorCommand);

    if (doorCommand == "open"){
        Serial.println("MQTT: open the door");
        openDoor();
    }else if (doorCommand == "close"){
        Serial.println("MQTT: close the door");
        closeDoor();
    }else if (doorCommand == "stop"){
        Serial.println("MQTT: stop the door");
        stopDoor();
    }else if (doorCommand == "light"){
        Serial.println("MQTT: toggle the light");
        toggleLight();
    }else if(doorCommand == "query"){
        Serial.println("MQTT: query");
        sendDoorStatus();
    }else{
        Serial.println("Unknown mqtt command, ignoring");
    }
}

/************************* DOOR COMMUNICATION *************************/
/*
 * Transmit a message to the door opener over uart1
 * The TX1 pin is controlling a transistor, so the logic is inverted
 * A HIGH state on TX1 will pull the 12v line LOW
 * 
 * The opener requires a specific duration low/high pulse before it will accept a message
 */
void transmit(const byte* payload, unsigned int length){
  digitalWrite(OUTPUT_GDO, HIGH); // pull the line high for 1305 micros so the door opener responds to the message
  delayMicroseconds(1305);
  digitalWrite(OUTPUT_GDO, LOW); // bring the line low

  delayMicroseconds(1260); // "LOW" pulse duration before the message start
  swSerial.write(payload, length);
}

void openDoor(){
    if(doorState == "open" || doorState == "opening"){
        Serial.print("The door is already ");
        Serial.println(doorState);
        return;
    }

    doorState = "opening"; // It takes a couple of pulses to detect opening/closing. by setting here, we can avoid bouncing from rapidly repeated commands

    for(int i=0; i<7; i++){
        Serial.print("door_code[");
        Serial.print(i);
        Serial.println("]");

        transmit(DOOR_CODE[i],19);
        delayLoop(45);
    }
}

void closeDoor(){
    if(doorState == "closed" || doorState == "closing"){
        Serial.print("The door is already ");
        Serial.println(doorState);
        return;
    }

    doorState = "closing"; // It takes a couple of pulses to detect opening/closing. by setting here, we can avoid bouncing from rapidly repeated commands

    for(int i=0; i<7; i++){
        Serial.print("door_code[");
        Serial.print(i);
        Serial.println("]");

        transmit(DOOR_CODE[i],19);
        delayLoop(45);
    }
}

void stopDoor(){
    if(doorState == "closed" || doorState == "open"){
        Serial.print("The door is not moving ");
        Serial.println(doorState);
        return;
    }

    for(int i=0; i<7; i++){
        Serial.print("door_code[");
        Serial.print(i);
        Serial.println("]");

        transmit(DOOR_CODE[i],19);
        delayLoop(45);
    }
}

void toggleLight(){
    for(int i=0; i<6; i++){
        Serial.print("light_code[");
        Serial.print(i);
        Serial.println("]");

        transmit(LIGHT_CODE[i],19);
        delayLoop(45);
    }
}

void delayLoop(int count) {
    for (int i = 0; i < count; i++) {
        delay(1);
    }
}
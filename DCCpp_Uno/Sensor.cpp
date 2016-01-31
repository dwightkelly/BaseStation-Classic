/**********************************************************************

Sensor.cpp
COPYRIGHT (c) 2013-2016 Gregg E. Berman

Part of DCC++ BASE STATION for the Arduino

**********************************************************************/
/**********************************************************************

DCC++ BASE STATION supports Sensor inputs that can be connected to any Aruidno Pin 
not in use by this program.  Sensors can be of any type (infrared, magentic, mechanical...).
The only requirement is that when "activated" the Sensor must force the specified Arduino
Pin LOW (i.e. to ground), and when not activated, this Pin should remain HIGH (e.g. 5V),
or be allowed to float HIGH if use of the Arduino Pin's internal pull-up resistor is specified.

To ensure proper voltage levels, some part of the Sensor circuitry
MUST be tied back to the same ground as used by the Arduino.

The Sensor code below utilizes exponential smoothing to "de-bounce" spikes generated by
mechanical switches and transistors.  This avoids the need to create smoothing circuitry
for each sensor.  You may need to change these parameters through trial and error for your specific sensors.

To have this sketch monitor one or more Arduino pins for sensor triggers, first define/edit/delete
sensor definitions using the following variation of the "S" command:

  <S ID PIN PULLUP>:           creates a new sensor ID, with specified PIN and PULLUP
                               if sensor ID already exists, it is updated with specificed PIN and PULLUP
                               returns: <O> if successful and <X> if unsuccessful (e.g. out of memory)

  <S ID>:                      deletes definition of sensor ID
                               returns: <O> if successful and <X> if unsuccessful (e.g. ID does not exist)

  <S>:                         lists all defined sensors
                               returns: <Q ID PIN PULLUP> for each defined sensor or <X> if no sensors defined
  
where

  ID: the numeric ID (0-32767) of the sensor
  PIN: the arduino pin number the sensor is connected to
  PULLUP: 1=use internal pull-up resistor for PIN, 0=don't use internal pull-up resistor for PIN

Once all sensors have been properly defined, use the <E> command to store their definitions to EEPROM.
If you later make edits/additions/deletions to the sensor definitions, you must invoke the <E> command if you want those
new definitions updated in the EEPROM.  You can also clear everything stored in the EEPROM by invoking the <e> command.

All sensors defined as per above are repeatedly and sequentially checked within the main loop of this sketch.
If a Sensor Pin is found to have transitioned from one state to another, one of the following serial messages are generated:

  <Q ID>     - for transition of Sensor ID from HIGH state to LOW state (i.e. the sensor is triggered)
  <q ID>     - for transition of Sensor ID from LOW state to HIGH state (i.e. the sensor is no longer triggered)

Depending on whether the physical sensor is acting as an "event-trigger" or a "detection-sensor," you may
decide to ignore the <q ID> return and only react to <Q ID> triggers.

**********************************************************************/

#include "DCCpp_Uno.h"
#include "Sensor.h"
#include "EEStore.h"
#include <EEPROM.h>
#include "Comm.h"
#include "DccServer.h"

///////////////////////////////////////////////////////////////////////////////
  
void Sensor::check(){    
  Sensor *tt;
  DccServer *ss;

  for(ss=DccServer::firstDccServer;ss!=NULL;ss=ss->nextDccServer){        // instruct servers to activate sensorQuery request
    if(ss->active) {        //server is active
      Wire.beginTransmission(ss->data.snum+7);
      Wire.write("Q");
      ss->readyForQuery=(Wire.endTransmission()==0);
    } else {
      ss->readyForQuery=false;
    }
  }

  for(tt=firstSensor;tt!=NULL;tt=tt->nextSensor){
    if(tt->data.i2c<1){                                                                     // local sensor
      tt->signal=tt->signal*(1.0-SENSOR_DECAY)+digitalRead(tt->data.pin)*SENSOR_DECAY;
    } else {                                                                                // remote sensor
      ss=DccServer::get(tt->data.i2c);         // get pointer to remote server 
      if(ss!=NULL && ss->readyForQuery && tt->upLoaded){       // check that server is readyForQuery and this sensor has been uploaded
        byte x[2];
        Wire.beginTransmission(tt->data.i2c+7);
        Wire.write("q");
        Wire.write(tt->data.pin);           // snum on server equals pin
        Wire.endTransmission();
        Wire.requestFrom(tt->data.i2c+7,2);
        x[0]=Wire.read();                  // read back pin number for verification
        x[1]=Wire.read();                  // read active flag

        if(x[0]==tt->data.pin && x[1]!=tt->active)    // pin verified, and new active state received
          tt->signal=tt->active;                      // this will triggger a change in state below
        else
          tt->signal=!tt->active;                     // this will not trigger change of state (either because state is same, or invalid pin)
      }
    }
    
    if(!tt->active && tt->signal<0.5){
      tt->active=true;
      INTERFACE.print("<Q");
      INTERFACE.print(tt->data.snum);
      INTERFACE.print(">");
    } else if(tt->active && tt->signal>0.9){
      tt->active=false;
      INTERFACE.print("<q");
      INTERFACE.print(tt->data.snum);
      INTERFACE.print(">");
    }
  } // loop over all sensors
    
} // Sensor::check

///////////////////////////////////////////////////////////////////////////////

Sensor *Sensor::create(int snum, int pin, int pullUp, int i2c, int v){
  Sensor *tt;
  DccServer *ss;
  
  if(firstSensor==NULL){
    firstSensor=(Sensor *)calloc(1,sizeof(Sensor));
    tt=firstSensor;
  } else if((tt=get(snum))==NULL){
    tt=firstSensor;
    while(tt->nextSensor!=NULL)
      tt=tt->nextSensor;
    tt->nextSensor=(Sensor *)calloc(1,sizeof(Sensor));
    tt=tt->nextSensor;
  }

  if(tt==NULL){       // problem allocating memory
    if(v==1)
      INTERFACE.print("<X>");
    return(tt);
  }
  
  tt->data.snum=snum;
  tt->data.pin=pin;
  tt->data.i2c=i2c;
  tt->data.pullUp=(pullUp==0?LOW:HIGH);
  tt->active=false;
  tt->upLoaded=false;
  tt->signal=1;

  if(i2c<1){                    // local sensor
    pinMode(pin,INPUT);         // set mode to input
    digitalWrite(pin,pullUp);   // don't use Arduino's internal pull-up resistors for external infrared sensors --- each sensor must have its own 1K external pull-up resistor
  } else {                      // remote sensor
    ss=DccServer::get(i2c);    
    if(ss==NULL)                // if server not yet defined
      DccServer::create(i2c,1); // define this server
    if(ss!=NULL)                // if server is or was defined      
      ss->upLoaded=false;       // set uploaded flag to false (will be uploaded next time servers are checked)
  }

  if(v==1)
    INTERFACE.print("<O>");
  return(tt);
  
}

///////////////////////////////////////////////////////////////////////////////

Sensor* Sensor::get(int n){
  Sensor *tt;
  for(tt=firstSensor;tt!=NULL && tt->data.snum!=n;tt=tt->nextSensor);
  lastQueried=tt;
  return(tt); 
}
///////////////////////////////////////////////////////////////////////////////

void Sensor::remove(int n){
  Sensor *tt,*pp;
  
  for(tt=firstSensor;tt!=NULL && tt->data.snum!=n;pp=tt,tt=tt->nextSensor);

  if(tt==NULL){
    INTERFACE.print("<X>");
    return;
  }
  
  if(tt==firstSensor)
    firstSensor=tt->nextSensor;
  else
    pp->nextSensor=tt->nextSensor;

  free(tt);

  INTERFACE.print("<O>");
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::show(){
  Sensor *tt;

  if(firstSensor==NULL){
    INTERFACE.print("<X>");
    return;
  }
    
  for(tt=firstSensor;tt!=NULL;tt=tt->nextSensor){
    INTERFACE.print("<Q");
    INTERFACE.print(tt->data.snum);
    INTERFACE.print(" ");
    INTERFACE.print(tt->data.pin);
    INTERFACE.print(" ");
    INTERFACE.print(tt->data.pullUp);
    INTERFACE.print(" ");
    INTERFACE.print(tt->data.i2c);
    INTERFACE.print(">");
  }
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::status(){
  Sensor *tt;

  if(firstSensor==NULL){
    INTERFACE.print("<X>");
    return;
  }
    
  for(tt=firstSensor;tt!=NULL;tt=tt->nextSensor){
    INTERFACE.print(tt->active?"<Q":"<q");
    INTERFACE.print(tt->data.snum);
    INTERFACE.print(">");
  }
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::parse(char *c){
  int n,s,m,r;
  Sensor *t;
  
  switch(sscanf(c,"%d %d %d %d",&n,&s,&m,&r)){
    
    case 4:                     // argument is string with id number of sensor followed by a pin number and pullUp indicator (0=LOW/1=HIGH) and i2c board number (0=local, 1-120=servers)
      if(r>=0 && r<=120)
        create(n,s,m,r,1);
    break;

    case 3:                     // argument is string with id number of sensor followed by a pin number and pullUp indicator (0=LOW/1=HIGH)
      create(n,s,m,0,1);
    break;

    case 1:                     // argument is a string with id number only
      remove(n);
    break;
    
    case -1:                    // no arguments
      show();
    break;

    case 2:                     // invalid number of arguments
      INTERFACE.print("<X>");
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::load(){
  struct SensorData data;
  Sensor *tt;

  for(int i=0;i<EEStore::eeStore->data.nSensors;i++){
    EEPROM.get(EEStore::pointer(),data);  
    tt=create(data.snum,data.pin,data.pullUp,data.i2c);
    EEStore::advance(sizeof(tt->data));
  }  
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::store(){
  Sensor *tt;
  
  tt=firstSensor;
  EEStore::eeStore->data.nSensors=0;
  
  while(tt!=NULL){
    EEPROM.put(EEStore::pointer(),tt->data);
    EEStore::advance(sizeof(tt->data));
    tt=tt->nextSensor;
    EEStore::eeStore->data.nSensors++;
  }  
}

///////////////////////////////////////////////////////////////////////////////

boolean Sensor::upload(DccServer *ss){
  Sensor *tt;
  boolean okay;

  okay=true;

  for(tt=firstSensor;tt!=NULL;tt=tt->nextSensor){         // loop over all sensors
    if(tt->data.i2c==ss->data.snum && (!tt->upLoaded || ss->reset)){     // matches remote server ID for this upload request, and this sensor not yet uploaded or seever was reset
      Wire.beginTransmission(tt->data.i2c+7);
      Wire.write("S");
      Wire.write(tt->data.pin);   // set snum to pin number on server to ensure snum is only one byte (master will still refer to this with correct snum)
      Wire.write(tt->data.pin);
      Wire.write(tt->data.pullUp);
      tt->upLoaded=(Wire.endTransmission()==0);      // set status based on success of upload
      okay&=tt->upLoaded;                            // if fail, set okay to fail
    }
  }

  return(okay);
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::sensorQuery(){
  byte x[2];
  
  if(lastQueried!=NULL){
    x[0]=lastQueried->data.snum;      // same as pin number of this server
    x[1]=lastQueried->active;
  } else {
    x[0]=0;
    x[1]=0;
  }
  
  Wire.write(x,2);
}

///////////////////////////////////////////////////////////////////////////////

Sensor *Sensor::firstSensor=NULL;
Sensor *Sensor::lastQueried=NULL;


//ArduPWM PC fan controller Mk II - DEC 02 2018
//By Kyoudai Ken @Kyoudai_Ken (Twitter.com)

#include <arduino.h>
#include <EEPROM.h>

//#define DEBUG
#define CONSOLE_MODE

////Configuration////
//Thermistor (analog) pins
int thmA = 0;
int thmB = 1;
int thmC = 3;

//Thermistor calibration offset (°K/C)
float calOffsetA = 0;
float calOffsetB = 0;
float calOffsetC = 0;

//Thermistor pulldown resistor value in Ohms - Can also be used for thermistor calibration
float rPullDownA = 9990;
float rPullDownB = 9890;
float rPullDownC = 9960;

//PWM channel pin mapping
//You should not change these!
int ppA = 3;
int ppB = 9;
int ppC = 10;

//Memorized duty cycles for info output
float mDcA = 0;
float mDcB = 0;
float mDcC = 0;

//Memorized curve tables
byte cdta[3][169];
byte cdtal[3];

unsigned long now;
unsigned long prev;

#ifdef CONSOLE_MODE
bool psc=true;
#elif
bool psc=false;
#endif

////Setup////
void setup() {
  //Setup PWM for 25 KHz

  /* Pins:
  pin 3 = OC2B (timer 2 PWM output B)
  pin 11 = OC2A (timer 2 PWM output A)
  pin 9 = OC1B (timer 1 PWM output B)
  pin 10 = OC1A (timer 1 PWM output A)
  pin 5 = OC0B (timer 0 PWM output B)
  pin 6 = OC0A (timer 0 PWM output A) */

  // Configure Timer 1 for PWM @ 25 kHz.
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  TCCR1A = _BV(COM1A1)  // non-inverted PWM on ch. A
      | _BV(COM1B1)  // same on ch. B
      | _BV(WGM11);  // mode 10: ph. correct PWM, TOP = ICR1
  TCCR1B = _BV(WGM13)   // ditto
      | _BV(CS10);   // prescaler = 1
  ICR1   = 160;
  analogWrite( 9, 0);
  analogWrite(10, 0);
  
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  TCCR2A = _BV(COM2B1)
      | _BV(WGM20);
  TCCR2B = _BV(WGM22)
      | _BV(CS20);
  OCR2A  = 160;
  OCR2B = 0;

  pinMode(3, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);

  prev=0;
  Serial.begin(9600);

  //Zero curve memory
  unsigned int i;
  for(i=0;i<169;i++) {
    cdta[0][i]=0;
    cdta[1][i]=0;
    cdta[2][i]=0;
  }
  cdtal[0]=0;
  cdtal[1]=0;
  cdtal[2]=0;

  //for(i=0;i<512;i++) EEPROM.write(i,0);

  //Get data from the EEPROM
  if(EEPROM.read(0)>168||EEPROM.read(170)>168||EEPROM.read(340)>168||
     EEPROM.read(0)==0||EEPROM.read(170)==0||EEPROM.read(340)==0) goto EEPROM_BAD;
  for(unsigned int c=0;c<3;c++) {
    cdtal[c]=EEPROM.read(170*c);
    if(cdtal[c]>0) {
      for(i=1;i<=cdtal[c];i++) {
        cdta[c][i-1]=EEPROM.read(170*c+i);
        if((cdta[c][i-1]>100&&((i-1)%2)==0)||(cdta[c][i-1]>100&&((i-1)%2)==1)) goto EEPROM_BAD;
      }
      #ifdef DEBUG
      Serial.print("EEPROM.read() "); Serial.print(170*c, DEC); Serial.print(" to "); Serial.print(170*c+1+cdtal[c], DEC); Serial.print(", length "); Serial.println(cdtal[c], DEC);
      #endif
    }
  }
  goto EEPROM_OK;

  EEPROM_BAD:;
  //Set up default curves just ot get things going
  cdtal[0]=4; cdtal[1]=4; cdtal[2]=4;
  cdta[0][0]=0; cdta[0][1]=0; cdta[0][2]=0; cdta[0][3]=100;
  cdta[0][0]=0; cdta[1][1]=0; cdta[1][2]=0; cdta[1][3]=100;
  cdta[0][0]=0; cdta[2][1]=0; cdta[2][2]=0; cdta[2][3]=100;
  //Zero EEPROM
  for(i=0;i<512;i++)EEPROM.write(i,0);
  //Write to EEPROM:
  for(unsigned int c=0;c<3;c++) {
    if(cdtal[c]>0) {
      EEPROM.write(170*c, cdtal[c]);
      for(i=1;i<=cdtal[c];i++) EEPROM.write(170*c+i, cdta[c][i-1]);
    }
  }
  Serial.println("EEPROM BAD, default curves loaded and flashed. Please set up new curves if desired!");

  EEPROM_OK:;
}

// printFloat prints out the float 'value' rounded to 'places' places after the decimal point
void printFloat(float value, int places) {int digit;float tens=0.1;int tenscount=0;int i;float tempfloat=value;if(isnan(value)){Serial.print("!NAN");return;}float d=0.5;if(value<0)d*=-1.0;for(i=0;i<places;i++)d/=10.0;tempfloat +=  d;if(value<0)tempfloat *= -1.0;while((tens*10.0)<=tempfloat){tens*=10.0;tenscount+=1;}if(value<0)Serial.print('-');if(tenscount<=1)Serial.print(0,DEC);for(i=0;i<tenscount;i++){digit=(int)(tempfloat/tens);Serial.print(digit,DEC);tempfloat=tempfloat-((float)digit*tens);tens /= 10.0;}if(places<=0)return;Serial.print('.');for(i=0;i<places;i++){tempfloat*=10.0;digit=(int)tempfloat;Serial.print(digit,DEC);tempfloat=tempfloat-(float)digit;}}

////Little helpers to keep it clean and simple////
//Function for reading the temperature at a defined analog pin
float getTemperature(int pin, float pulldownR) {
  float logR=0;
  float temperature=0;
  
  //Multiprobe for smoother values
  for(byte i=0;i<50;i++) {
    logR+=log(pulldownR*(1023.0/(float)analogRead(pin)-1.0));
    delay(1);
  }
  logR=logR/50;
  
  switch (pin) {
    case 0: //Water sensor
      temperature=(1.0/(0.0011431512594581995 + 0.00023515745037380024 * logR + 6.187191114586837e-8 * logR * logR * logR))-273.15;
    case 3: //Ambient sensor
      temperature=(1.0/(0.0028561410879575405 + -0.00005243877323181964 * logR + 0.0000012584771402890711 * logR * logR * logR))-273.15;
    default: //Generic sensor
      temperature=(1.0/(-0.1745637090e-03+4.260403485e-04*logR+(-5.098359524e-07)*logR*logR*logR))-273.15;
      break;
  }

  return temperature;
}

//Curves for calculating the duty cycle from temperature are here!
//I did this to suit my needs, you can do changes to suit your needs!
//This function sets the actual duty cycle you programmed.
void setDutyCycle(float t0, float t1, float t2) {
  float dcA=100, dcB=100, dcC=100;
  float tW = t0-t2; if(tW<0) tW=0.1;
  byte p0=0; byte p1=0;
  bool found=false;

  //Find nearest curve points and interpolate
  if(tW==0) {
    dcA=cdta[0][1];
  } else {
    for(byte i=0;i<cdtal[0];i+=2) {
      if(cdta[0][i]>=tW) {
        p0=i-2;
        found=true;
        break;
      } 
    }
    if(found&&p0+2<=cdtal[0]) {
      p1=p0+2;
      dcA=((tW-cdta[0][p0])*(cdta[0][p1+1]-cdta[0][p0+1])/(cdta[0][p1]-cdta[0][p0]))+cdta[0][p0+1];
    }
  }

  p0=0;p1=0;found=false;
  if(t1==0) {
    dcA=cdta[1][1];
  } else {
    for(byte i=0;i<cdtal[1];i+=2) {
      if(cdta[1][i]>=t1) {
        p0=i-2;
        found=true;
        break;
      }
    }
    if(found&&p0+2<=cdtal[1]) {
      p1=p0+2;
      dcB=((t1-cdta[1][p0])*(cdta[1][p1+1]-cdta[1][p0+1])/(cdta[1][p1]-cdta[1][p0]))+cdta[1][p0+1];
    }
  }

  p0=0;p1=0;found=false;
  if(t2==0) {
    dcA=cdta[2][1];
  } else {
    for(byte i=0;i<cdtal[2];i+=2) {
      if(cdta[2][i]>=t2) {
        p0=i-2;
        found=true;
        break;
      }
    }
    if(found&&p0+2<=cdtal[2]) {
      p1=p0+2;
      dcC=((t2-cdta[2][p0])*(cdta[2][p1+1]-cdta[2][p0+1])/(cdta[2][p1]-cdta[2][p0]))+cdta[2][p0+1]+0.25*dcA;
    }
  }

  if(dcA<0)dcA=0; if(dcA>100)dcA=100;
  if(dcB<0)dcB=0; if(dcB>100)dcB=100;
  if(dcC<0)dcC=0; if(dcC>100)dcC=100;

  setPulseWith(ppA,dcA); mDcA=dcA;
  setPulseWith(ppB,dcB); mDcB=dcB;
  setPulseWith(ppC,dcC); mDcC=dcC;
}

//Just to make the control loop more clean
void setPulseWith(int pin, float pv) {
  switch (pin) {
    case 3:
      OCR2B = round(pv/100*OCR2A);
      break;
    case 9:
      analogWrite(9,round(pv/100*ICR1));
      break;
    case 10:
      analogWrite(10,round(pv/100*ICR1));
      break;
  }
}

////Main control loop////
void loop()
{
  unsigned int i;
  unsigned int oldi=0;
  unsigned int c=0;
  unsigned int len=0;
  byte curve;

  float tA=getTemperature(thmA,rPullDownA)+calOffsetA;
  float tB=getTemperature(thmB,rPullDownB)+calOffsetB;
  float tC=getTemperature(thmC,rPullDownC)+calOffsetC;
  setDutyCycle(tA,tB,tC);

  //Watch for serial commands
  String r = "";
  if(Serial.available()){
    delay(100);
    while (Serial.available()) {
      char c = Serial.read();
      if(c=='\n') break;
      r+=c;
    }
  }
  if(r!="") {
    if(r.startsWith("sc ")) {
      curve = r.substring(3,4).toInt();
      if(curve>=3) {
        Serial.println("e0");
        goto stop;
      }
      String curveData = r.substring(5)+" ";
      for(i=0;i<curveData.length();i++) {
        if(curveData[i]==' ') len++;
      }
      if(len>=169) {
        Serial.println("e1");
        goto stop;
      }
      if(len%2>0) {
        Serial.println("e2");
        goto stop;
      }
      byte data[len];
      for(i=0;i<curveData.length();i++) {
        if(curveData[i]==' ') {
          data[c++]=curveData.substring(oldi,i).toInt();
          oldi=i+1;
        }
      }
      if(data[0]!=0) {
        Serial.println("e3");
        goto stop;
      }
      //Write curve into memory
      for(i=0;i<len;i++) {
        cdta[curve][i]=data[i];
        if((i%2==0&&data[i]>100)||(i%2==1&&data[i]>100)) {
        Serial.println("e4");
        goto stop;
        }
      }
      cdtal[curve]=len;
      //Write to EEPROM:
      for(unsigned int c=0;c<3;c++) {
        if(cdtal[c]>0) {
          EEPROM.write(170*c, cdtal[c]);
          for(i=1;i<=cdtal[c];i++) EEPROM.write(170*c+i, cdta[c][i-1]);
          #ifdef DEBUG
          Serial.print("EEPROM.write() "); Serial.print(170*c, DEC); Serial.print(" to "); Serial.print(170*c+1+cdtal[c], DEC); Serial.print(", length "); Serial.println(cdtal[c], DEC);
          #endif
        }
      }
      Serial.println("ok");
    }
    if(r.startsWith("gc ")) {
      curve = r.substring(3,4).toInt();
      if(curve>=3) {
        Serial.println("e0");
        goto stop;
      }
      for(i=0;i<cdtal[curve];i++) {
        Serial.print(cdta[curve][i], DEC); Serial.print(" ");
      }
      Serial.println("");
    }
    if(r.startsWith("gs")) {
      printFloat(tA,1); Serial.print(" "); printFloat(mDcA,1); Serial.println("");
      printFloat(tB,1); Serial.print(" "); printFloat(mDcB,1); Serial.println("");
      printFloat(tC,1); Serial.print(" "); printFloat(mDcC,1); Serial.println("");
      #ifdef EN_RPM
      Serial.print(" "); Serial.print(rpm, DEC); Serial.println("");
      #endif
    }
    if(r.startsWith("psc")) {
      psc=true;
    }
    if(r.startsWith("!psc")) {
      psc=false;
    }
  }

  if(psc) {
    if(millis()>=prev+800) {
      printFloat(tA,1); Serial.print(" "); printFloat(mDcA,1); Serial.println("");
      printFloat(tB,1); Serial.print(" "); printFloat(mDcB,1); Serial.println("");
      printFloat(tC,1); Serial.print(" "); printFloat(mDcC,1); Serial.println("");
      #ifdef EN_RPM
      Serial.print(" "); Serial.print(rpm, DEC); Serial.println("");
      #endif
      prev=millis();
    }
  }
  stop:;
}
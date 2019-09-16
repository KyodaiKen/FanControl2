//ArduPWM PC fan controller Mk III - SEP 15 2019
//By Kyoudai Ken @Kyoudai_Ken (Twitter.com)

#include <arduino.h>
#include <stdlib.h>
#include <EEPROM.h>

//#define DEBUG
#define CONSOLE_MODE

////Configuration////
#define N_SENSORS 3
#define N_CURVES 3
//Thermistor (analog) pins
int thsp[N_SENSORS];

//Thermistor calibration (°K/C)
float thsco[N_SENSORS];

//Thermistor Steinhart-Hart coefficients
float thscs[N_SENSORS][3];

//Thermistor pulldown resistor value in Ohms - Can also be used for thermistor calibration
float thsRpd[N_SENSORS];

//PWM Curve pin mapping
//You should not change these!
int cpp[N_CURVES];

//Memorized temperatures
float t[N_SENSORS];
float ct[N_CURVES];

//Memorized duty cycles for info output
float cdc[N_CURVES];

//Memorized curve tables
#define CURVE_UB 158
byte cdta[N_CURVES][CURVE_UB];
byte cdtal[N_CURVES];

//Memorized mixer matrix
#define MATRIX_LENGTH N_SENSORS*N_CURVES*4
#define MATRIX_START CURVE_UB*N_CURVES
float m[N_CURVES][N_SENSORS];

#define EEPROM_CHECK_OFFSET 510
#define EEPROM_CHECK_LENGTH 2
#define EEPROM_CHECK_0 0xFC
#define EEPROM_CHECK_1 0xCF

unsigned long prev;

#ifdef CONSOLE_MODE
bool psc=true;
#endif
#ifndef CONSOLE_MODE
bool psc=false;
#endif

bool eepromok=false;

////Setup////
void setup() {
  /* Pins:
  pin 3 = OC2B (timer 2 PWM output B)
  pin 11 = OC2A (timer 2 PWM output A)
  pin 9 = OC1B (timer 1 PWM output B)
  pin 10 = OC1A (timer 1 PWM output A)
  pin 5 = OC0B (timer 0 PWM output B)
  pin 6 = OC0A (timer 0 PWM output A) */

  //Configure thermistor pins
  thsp[0]=0;
  thsp[1]=1;
  thsp[2]=3;

  //Configure thermistor pulldown resistor values
  thsRpd[0]=9990;
  thsRpd[1]=9890;
  thsRpd[2]=9960;

  //Configure thermistor calibration offsets and Steinhart-Hart coefficients
  thsco[0]=0;
  thsco[1]=0;
  thsco[2]=0;
  thscs[0][0]=0.0011431512594581995;
  thscs[0][1]=0.00023515745037380024;
  thscs[0][2]=6.187191114586837e-8;
  //thscs[1][0]=-0.1745637090e-03;
  //thscs[1][1]=4.260403485e-04;
  //thscs[1][2]=-5.098359524e-07;
  thscs[1][0]=0.0028561410879575405;
  thscs[1][1]=-0.00005243877323181964;
  thscs[1][2]=0.0000012584771402890711;
  thscs[2][0]=0.0028561410879575405;
  thscs[2][1]=-0.00005243877323181964;
  thscs[2][2]=0.0000012584771402890711;

  //Configure channel's PWM output pins
  cpp[0]=3;
  cpp[1]=9;
  cpp[2]=10;

  // Configure Timer 1 and 2 for PWM @ 25 kHz.
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  TCCR1A = _BV(COM1A1)
         | _BV(COM1B1)
         | _BV(WGM11);
  TCCR1B = _BV(WGM13)
         | _BV(CS10);
  ICR1   = 160;
  
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  TCCR2A = _BV(COM2B1)
         | _BV(WGM20);
  TCCR2B = _BV(WGM22)
         | _BV(CS20);
  OCR2A  = 160;

  //Set pins up for output
  unsigned int i;
  for(i=0;i<N_CURVES;i++){
    pinMode(cpp[i], OUTPUT);
    setPulseWith(cpp[i], 100);
  }

  prev=0;
  Serial.begin(115200);

  //Zero curve and matrix memory
  for(unsigned int c=0;c<N_CURVES;c++){
    for(i=0;i<CURVE_UB;i++)cdta[c][i]=0;cdtal[c]=0;
    for(unsigned int s=0;s<N_SENSORS;s++)m[c][s]=0;
  }

  //Get data from the EEPROM
  if(EEPROM.read(0)>CURVE_UB-1||EEPROM.read(CURVE_UB)>CURVE_UB-1||EEPROM.read(CURVE_UB*2)>CURVE_UB-1||
     EEPROM.read(0)==0||EEPROM.read(CURVE_UB)==0||EEPROM.read(CURVE_UB*2)==0||
     (EEPROM.read(EEPROM_CHECK_OFFSET)!=0xFC&&EEPROM.read(EEPROM_CHECK_OFFSET+1)!=0xCF)) goto EEPROM_BAD;
  readCurves();
  readMatrix();
  if(!eepromok) goto EEPROM_BAD;
  goto EEPROM_OK;

  EEPROM_BAD:;
  //Set up default curves just ot get things going
  cdtal[0]=4; cdtal[1]=4; cdtal[2]=4;
  cdta[0][0]=0; cdta[0][1]=0; cdta[0][2]=0; cdta[0][3]=100;
  cdta[0][0]=0; cdta[1][1]=0; cdta[1][2]=0; cdta[1][3]=100;
  cdta[0][0]=0; cdta[2][1]=0; cdta[2][2]=0; cdta[2][3]=100;
  m[0][0]=1;m[0][1]=0;m[0][2]=0;
  m[1][0]=0;m[1][1]=1;m[1][0]=0;
  m[2][0]=0;m[2][1]=0;m[2][0]=1;
  //Zero EEPROM
  for(i=0;i<512;i++)EEPROM.write(i,0);
  //Write to EEPROM:
  writeCurves();
  writeMatrix();
  EEPROM.write(EEPROM_CHECK_OFFSET,0xFC);
  EEPROM.write(EEPROM_CHECK_OFFSET+1,0xCF);
  Serial.println("EEPROM BAD, default curves loaded and flashed. Please set up new curves if desired!");

  EEPROM_OK:;
}

float unpackFloat(const unsigned char *buff) {
  union {
    float f;
    unsigned char b[4];
  } u;
  u.b[3] = buff[3];
  u.b[2] = buff[2];
  u.b[1] = buff[1];
  u.b[0] = buff[0];
  return u.f;
}

int packFloat(void *buf, float x) {
    unsigned char *b = (unsigned char *)buf;
    unsigned char *p = (unsigned char *) &x;
    b[0] = p[0];
    b[1] = p[1];
    b[2] = p[2];
    b[3] = p[3];
    return 4;
}

void readCurves() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    cdtal[c]=EEPROM.read(CURVE_UB*c);
    if(cdtal[c]>0) {
      for(unsigned int i=1;i<=cdtal[c];i++) {
        cdta[c][i-1]=EEPROM.read(CURVE_UB*c+i);
        if((cdta[c][i-1]>100&&((i-1)%2)==0)||(cdta[c][i-1]>100&&((i-1)%2)==1)) goto EEPROM_BAD;
      }
      #ifdef DEBUG
      Serial.print("EEPROM.read() "); Serial.print(CURVE_UB*c, DEC); Serial.print(" to "); Serial.print(CURVE_UB*c+1+cdtal[c], DEC); Serial.print(", length "); Serial.println(cdtal[c], DEC);
      #endif
    }
  }
  goto EEPROM_OK;
  EEPROM_BAD:;
  eepromok=false;
  EEPROM_OK:;
  eepromok=true;
}

void readMatrix() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    for(unsigned int s=0;s<N_SENSORS;s++) {
      byte buff[4];
      for(unsigned int i=0;i<sizeof(buff);i++)buff[i]=EEPROM.read(MATRIX_START+c*N_CURVES*4+s*4+i);
      #ifdef DEBUG
      Serial.print("EEPROM.read() "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4, DEC); Serial.print(" to "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4+3, DEC); Serial.print(", length "); Serial.println(4, DEC);
      #endif
      m[c][s]=unpackFloat(buff);
    }
  }
}

void writeCurves() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    if(cdtal[c]>0) {
      EEPROM.write(CURVE_UB*c, cdtal[c]);
      for(unsigned int i=1;i<=cdtal[c];i++) EEPROM.write(CURVE_UB*c+i, cdta[c][i-1]);
      #ifdef DEBUG
      Serial.print("EEPROM.read() "); Serial.print(CURVE_UB*c, DEC); Serial.print(" to "); Serial.print(CURVE_UB*c+1+cdtal[c], DEC); Serial.print(", length "); Serial.println(cdtal[c], DEC);
      #endif
    }
  }
  EEPROM.write(EEPROM_CHECK_OFFSET,0xFC);
  EEPROM.write(EEPROM_CHECK_OFFSET+1,0xCF);
}

void writeMatrix() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    for(unsigned int s=0;s<N_SENSORS;s++) {
      byte buff[4]; float f=m[c][s];
      packFloat(buff, f);
      for(unsigned int i=0;i<sizeof(buff);i++){EEPROM.write(MATRIX_START+c*N_CURVES*4+s*4+i, buff[i]);}
      #ifdef DEBUG
      Serial.print(" - EEPROM.write() "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4, DEC); Serial.print(" to "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4+3, DEC); Serial.print(", length "); Serial.println(4, DEC);
      #endif
    }
  }
  EEPROM.write(EEPROM_CHECK_OFFSET,0xFC);
  EEPROM.write(EEPROM_CHECK_OFFSET+1,0xCF);
}

void printFloat(float value, unsigned int w, unsigned int p) {
  char s[8];
  dtostrf(value, w, p, s);
  Serial.print(s);
}

////Little helpers to keep it clean and simple////
//Function for reading the temperatures at a all analog pins
void getTemperatures() {
  for(unsigned int s=0;s<N_SENSORS;s++) {
    t[s]=0;
    //Multiprobe for smoother values
    for(byte i=0;i<50;i++) {
      float logR=log(thsRpd[s]*(1023.0/(float)analogRead(thsp[s])-1.0));
      t[s]+=(1.0/(thscs[s][0]+thscs[s][1]*logR+thscs[s][2]*logR*logR*logR))-273.15-thsco[s];
    }
    t[s]=t[s]/50;
    if(isnan(t[s])) t[s]=0;
  }
}

//Just to make the control loop more clean
void setPulseWith(int pin, float pv) {
  float upper_bound;
  switch (pin) {
    case 3:
      upper_bound=OCR2A;
      break;
    default:
      upper_bound=ICR1;
      break;
  }
  analogWrite(pin,round(pv/100*upper_bound));
}

float getDutyCycle(unsigned int c, float mt) {
  bool found=false;
  byte p0=0; byte p1=0;
  float dc;
  if(mt==0) return cdta[c][1];
  for(byte i=0;i<cdtal[c];i+=2) {
    if(cdta[c][i]>=mt) {
      p0=i-2;
      found=true;
      break;
    } 
  }
  if(found&&p0+2<=cdtal[c]) {
    p1=p0+2;
    dc=((mt-cdta[c][p0])*(cdta[c][p1+1]-cdta[c][p0+1])/(cdta[c][p1]-cdta[c][p0]))+cdta[c][p0+1];
  } else {
    dc=NAN;
  }
  return dc<0?0:dc;
}

float matrix(unsigned int c, float* temps) {
  float mt=0;
  for(unsigned int s=0;s<N_SENSORS;s++)mt+=temps[s]*m[c][s];
  return (mt<0?0:mt);
}

//This function sets the actual duty cycle you programmed.
void setDutyCycles() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    float mt=matrix(c,t);
    ct[c]=mt;
    cdc[c]=getDutyCycle(c,mt);
    setPulseWith(cpp[c],cdc[c]);
  }
}

//Print sensors
void printSensors(bool ondemand) {
  if(!ondemand)Serial.println("");
  Serial.print("t ");
  for(unsigned int s=0;s<N_SENSORS;s++) {
    printFloat(t[s],6,2);
    if(s<N_SENSORS-1)Serial.print(" ");
  }
  Serial.println("");
  Serial.print("m ");
  for(unsigned int c=0;c<N_CURVES;c++) {
    printFloat(ct[c],6,2);
    if(c<N_SENSORS-1)Serial.print(" ");
  }
  Serial.println("");
  Serial.print("s ");
  for(unsigned int c=0;c<N_CURVES;c++) {
    printFloat(cdc[c],6,2);
    if(c<N_SENSORS-1)Serial.print(" ");
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

  getTemperatures();
  setDutyCycles();

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
  if(psc==false) {
    if(r!="") {
      if(r.startsWith("sc ")) {
        curve = r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        String curveData = r.substring(5)+" ";
        for(i=0;i<curveData.length();i++) {
          if(curveData[i]==' ') len++;
        }
        if(len>=CURVE_UB) {
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
        writeCurves();
        Serial.println("ok");
      }
      if(r.startsWith("sm ")) {
        curve=r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        String matrixData = r.substring(5)+" ";
        len=0;
        for(i=0;i<matrixData.length();i++) {
          if(matrixData[i]==' ') len++;
        }
        if(len!=N_SENSORS) {
          Serial.println("e1");
          goto stop;
        }
        //Write matrix into memory
        float data[len];
        for(i=0;i<matrixData.length();i++) {
          if(matrixData[i]==' ') {
            data[c++]=matrixData.substring(oldi,i).toFloat();
            oldi=i+1;
          }
        }
        for(i=0;i<len;i++) m[curve][i]=data[i];
        //Write to EEPROM:
        writeMatrix();
        Serial.println("ok");
      }
      if(r.startsWith("gc ")) {
        curve = r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        for(i=0;i<cdtal[curve];i++) {
          Serial.print(cdta[curve][i], DEC); Serial.print(" ");
        }
        Serial.println("");
      }
      if(r.startsWith("gm ")) {
        curve = r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        for(i=0;i<N_SENSORS;i++) {
          Serial.print(m[curve][i], DEC); Serial.print(" ");
        }
        Serial.println("");
      }
      if(r.startsWith("gs")) {
        printSensors(true);
      }
      if(r.startsWith("tc")) {
        curve=r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        float tt=r.substring(5).toFloat();
        Serial.print("s "); printFloat(getDutyCycle(curve,tt),6,2); Serial.println("");
      }
      if(r.startsWith("tm")) {
        curve=r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        String tempData = r.substring(5)+" ";
        for(i=0;i<tempData.length();i++) {
          if(tempData[i]==' ') len++;
        }
        if(len!=N_SENSORS) {
          Serial.println("e1");
          goto stop;
        }
        float data[len];
        for(i=0;i<tempData.length();i++) {
          if(tempData[i]==' ') {
            data[c++]=tempData.substring(oldi,i).toFloat();
            oldi=i+1;
          }
        }
        float tt=matrix(curve,data);
        Serial.print("m "); printFloat(tt,6,2); Serial.println("");
        Serial.print("s "); printFloat(getDutyCycle(curve,tt),6,2); Serial.println("");
      }
    }
    if(r.startsWith("psc")) {
      psc=true;
    }
  } else {
    if(r.startsWith("!")) {
      psc=false;
    }
  }

  if(psc) {
    if(millis()>=prev+1000) {
      printSensors(false);
      prev=millis();
    }
  }
  stop:;
}
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
double thsco[N_SENSORS];

//Thermistor Steinhart-Hart coefficients
double thscs[N_SENSORS][3];

//Thermistor pulldown resistor value in Ohms - Can also be used for thermistor calibration
float thsRpd[N_SENSORS];

//PWM Curve pin mapping
//You should not change these!
int cpp[N_CURVES];

//Memorized temperatures
double t[N_SENSORS];
double ct[N_CURVES];

//Memorized duty cycles for info output
float cdc[N_CURVES];

//Memorized curve tables
#define CURVE_UB 31
#define CURVE_FIELD_LEN 5
#define CURVE_LEN CURVE_UB*CURVE_FIELD_LEN
#define CURVE_STRUCT_LEN (CURVE_LEN+1)

struct curvePoint {
  float temp;
  byte  dc;
};

curvePoint cdta[N_CURVES][CURVE_UB];
byte cdtal[N_CURVES];

//Memorized mixer matrix
#define MATRIX_LENGTH N_SENSORS*N_CURVES*4
#define MATRIX_START N_CURVES*CURVE_STRUCT_LEN
float m[N_CURVES][N_SENSORS];

#define EEPROM_CHECKSUM_OFFSET 505
unsigned long prev;

#ifdef CONSOLE_MODE
bool psc=true;
#endif
#ifndef CONSOLE_MODE
bool psc=false;
#endif

bool eepromok=false;

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

curvePoint readCurvePoint(unsigned int eeindex) {
  unsigned char buff[5];
  for(unsigned int i=0;i<5;i++){
    buff[i]=EEPROM.read(eeindex+i);
    #ifdef DEBUG
    Serial.print(buff[i], HEX); Serial.print(" ");
    #endif
  }
  curvePoint cp;
  cp.temp=unpackFloat(buff);
  cp.dc=buff[4];
  return cp;
}

void writeCurvePoint(unsigned int eeindex, curvePoint cp) {
  unsigned char buff[5];
  packFloat(buff, cp.temp);
  buff[4]=cp.dc;
  for(unsigned int i=0;i<5;i++){
    #ifdef DEBUG
    Serial.print(buff[i], HEX); Serial.print(" ");
    #endif
    EEPROM.write(eeindex+i,buff[i]);
  }
}

void readCurves() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    cdtal[c]=EEPROM.read(CURVE_STRUCT_LEN*c);
    unsigned int ci=0;
    if(cdtal[c]>0) {
      for(unsigned int i=0;i<cdtal[c]*CURVE_FIELD_LEN;i+=CURVE_FIELD_LEN) {
        cdta[c][ci]=readCurvePoint(CURVE_STRUCT_LEN*c+i+1);
        if(cdta[c][ci].temp>100||cdta[c][ci].dc>100) goto EEPROM_BAD;
        #ifdef DEBUG
        Serial.print("CURVES: EEPROM.read() "); Serial.print(CURVE_STRUCT_LEN*c+i+1, DEC); Serial.print(" to "); Serial.print(CURVE_STRUCT_LEN*c+i+1+CURVE_FIELD_LEN, DEC); Serial.println("");
        #endif
        ci++;
      }
    }
  }
  goto EEPROM_OK;
  EEPROM_BAD:;
  eepromok=false;
  goto EEND;
  EEPROM_OK:;
  eepromok=true;
  EEND:;
}

void readMatrix() {
  if(eepromok) { 
    for(unsigned int c=0;c<N_CURVES;c++) {
      for(unsigned int s=0;s<N_SENSORS;s++) {
        byte buff[4];
        for(unsigned int i=0;i<sizeof(buff);i++)buff[i]=EEPROM.read(MATRIX_START+c*N_CURVES*4+s*4+i);
        #ifdef DEBUG
        Serial.print("MATRIX: EEPROM.read() "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4, DEC); Serial.print(" to "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4+3, DEC); Serial.print(", length "); Serial.println(4*N_SENSORS, DEC);
        #endif
        m[c][s]=unpackFloat(buff);
      }
    }
  }
}

void writeCurves() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    if(cdtal[c]>0) {
      EEPROM.write(CURVE_STRUCT_LEN*c, cdtal[c]);
      #ifdef DEBUG
      Serial.print("CURVES: EEPROM.write() len "); Serial.print(cdtal[c], DEC); Serial.print(" at "); Serial.print(CURVE_STRUCT_LEN*c, DEC);
      #endif
      unsigned int i=0;
      for(unsigned int ci=0;ci<cdtal[c];ci++) {
        writeCurvePoint(CURVE_STRUCT_LEN*c+i+1,cdta[c][ci]);
        #ifdef DEBUG
        Serial.print("cp "); Serial.print(CURVE_STRUCT_LEN*c+i+1, DEC); Serial.print(" to "); Serial.print(CURVE_STRUCT_LEN*c+i+1+CURVE_FIELD_LEN, DEC); Serial.println("");
        #endif
        i+=CURVE_FIELD_LEN;
      }
    }
  }
}

void writeMatrix() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    for(unsigned int s=0;s<N_SENSORS;s++) {
      byte buff[4]; float f=m[c][s];
      packFloat(buff, f);
      for(unsigned int i=0;i<sizeof(buff);i++){EEPROM.write(MATRIX_START+c*N_CURVES*4+s*4+i, buff[i]);}
      #ifdef DEBUG
      Serial.print("MATRIX: EEPROM.write() "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4, DEC); Serial.print(" to "); Serial.print(MATRIX_START+c*N_CURVES*4+s*4+3, DEC); Serial.print(", length "); Serial.println(4*N_SENSORS, DEC);
      #endif
    }
  }
}


uint32_t CRC32(byte* data, uint32_t ignore_offset) {
  const uint32_t crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
  };

  uint32_t crc = ~0L;
  for (uint32_t i=0;i<sizeof(data);++i) {
    byte val=0;
    if(i>ignore_offset&&i<ignore_offset+4) {
      val=0;
    } else {
      val=data[i];
    }
    crc=crc_table[(crc^val)&0x0f]^(crc>>4);
    crc=crc_table[(crc^(val>>4))&0x0f]^(crc>>4);
    crc=~crc;
  }
  return crc;
}

void writeEEPROM_CRC() {
  byte e[512]; for(uint16_t i=0;i<512;i++)e[i]=EEPROM[i];
  EEPROM.put(EEPROM_CHECKSUM_OFFSET, CRC32(e, EEPROM_CHECKSUM_OFFSET));
}

bool checkEEPROM_CRC() {
  uint32_t crc=0;
  EEPROM.get(EEPROM_CHECKSUM_OFFSET,crc);
  byte e[512]; for(uint16_t i=0;i<512;i++)e[i]=EEPROM[i];
  bool ok=(CRC32(e, EEPROM_CHECKSUM_OFFSET)==crc);
  return ok;
}

void printFloat(double value, unsigned int w, unsigned int p) {
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
    for(byte i=0;i<100;i++) {
      double logR=log(thsRpd[s]*(1023.0/(double)analogRead(thsp[s])-1.0));
      t[s]+=(1.0/(thscs[s][0]+thscs[s][1]*logR+thscs[s][2]*logR*logR*logR))-273.15-thsco[s];
    }
    t[s]=t[s]/100;
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
  if(mt==0) return cdta[c][0].dc;
  for(byte i=0;i<cdtal[c];i++) {
    if(cdta[c][i].temp>=mt) {
      p0=i-1;
      found=true;
      break;
    } 
  }
  if(found&&p0+1<=cdtal[c]) {
    p1=p0+1;
    dc=((mt-cdta[c][p0].temp)*(cdta[c][p1].dc-cdta[c][p0].dc)/(cdta[c][p1].temp-cdta[c][p0].temp))+cdta[c][p0].dc;
  } else {
    dc=NAN;
  }
  return dc<0?0:dc;
}

double matrix(unsigned int c, double* temps) {
  double mt=0;
  for(unsigned int s=0;s<N_SENSORS;s++)mt+=temps[s]*m[c][s];
  return (mt<0?0:mt);
}

//This function sets the actual duty cycle you programmed.
void setDutyCycles() {
  for(unsigned int c=0;c<N_CURVES;c++) {
    double mt=matrix(c,t);
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
    for(i=0;i<CURVE_UB;i++){
      cdta[c][i].temp=0;
      cdta[c][i].dc=0;
      cdtal[c]=0;
    }
    for(unsigned int s=0;s<N_SENSORS;s++)m[c][s]=0;
  }

  //Get data from the EEPROM
  if(EEPROM.read(0)>CURVE_UB||EEPROM.read(CURVE_STRUCT_LEN)>CURVE_UB||EEPROM.read(CURVE_STRUCT_LEN*2)>CURVE_UB||
     EEPROM.read(0)==0||EEPROM.read(CURVE_STRUCT_LEN)==0||EEPROM.read(CURVE_STRUCT_LEN*2)==0) goto EEPROM_BAD;
  if(!checkEEPROM_CRC()) goto EEPROM_BAD;
  readCurves();
  readMatrix();
  if(!eepromok) goto EEPROM_BAD;
  goto EEPROM_OK;

  EEPROM_BAD:;
  //Set up default curves just ot get things going
  cdtal[0]=2; cdtal[1]=2; cdtal[2]=2;
  cdta[0][0].temp=0; cdta[0][0].dc=0; cdta[0][1].temp=0; cdta[0][1].dc=100;
  cdta[1][0].temp=0; cdta[1][0].dc=0; cdta[1][1].temp=0; cdta[1][1].dc=100;
  cdta[2][0].temp=0; cdta[2][0].dc=0; cdta[2][1].temp=0; cdta[2][1].dc=100;
  m[0][0]=1;m[0][1]=0;m[0][2]=0;
  m[1][0]=0;m[1][1]=1;m[1][0]=0;
  m[2][0]=0;m[2][1]=0;m[2][0]=1;
  //Zero EEPROM
  for(i=0;i<512;i++)EEPROM.write(i,0);
  //Write to EEPROM:
  writeCurves();
  writeMatrix();
  Serial.println("EEPROM BAD, default curves loaded and flashed. Please set up new curves if desired!");

  EEPROM_OK:;
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
        float data[len];
        for(i=0;i<curveData.length();i++) {
          if(curveData[i]==' ') {
            data[c++]=curveData.substring(oldi,i).toFloat();
            oldi=i+1;
          }
        }
        if(data[0]!=0) {
          Serial.println("e3");
          goto stop;
        }
        //Write curve into memory
        unsigned int ci=0;
        for(i=0;i<len;i+=2) {
          if(data[i]>100||data[i+1]>100) {
            Serial.println("e4");
            goto stop;
          }
          cdta[curve][ci].temp=data[i];
          cdta[curve][ci].dc=(byte)data[i+1];
          ci++;
        }
        cdtal[curve]=ci;
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
        double data[len];
        for(i=0;i<matrixData.length();i++) {
          if(matrixData[i]==' ') {
            data[c++]=matrixData.substring(oldi,i).toFloat();
            oldi=i+1;
          }
        }
        for(i=0;i<len;i++) m[curve][i]=data[i];
        Serial.println("ok");
      }
      if(r.startsWith("gc ")) {
        curve = r.substring(3,4).toInt();
        if(curve>=N_CURVES) {
          Serial.println("e0");
          goto stop;
        }
        for(i=0;i<cdtal[curve];i++) {
          Serial.print(cdta[curve][i].temp); Serial.print(" "); Serial.print(cdta[curve][i].dc); Serial.print(" ");
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
        double data[len];
        for(i=0;i<tempData.length();i++) {
          if(tempData[i]==' ') {
            data[c++]=tempData.substring(oldi,i).toFloat();
            oldi=i+1;
          }
        }
        double tt=matrix(curve,data);
        Serial.print("m "); printFloat(tt,6,2); Serial.println("");
        Serial.print("s "); printFloat(getDutyCycle(curve,tt),6,2); Serial.println("");
      }
      if(r.startsWith("we")) {
        writeMatrix();
        writeCurves();
        writeEEPROM_CRC();
        Serial.println("ok");
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
//ArduPWM PC fan controller Mk IV - NOV 03 2021
//By Kyoudai Ken @Kyoudai_Ken (Twitter.com)
//#include <arduino.h>
#include <stdlib.h>
#include <EEPROM.h>

//#define DEBUG

#define RXTX_TIMEOUT 4000
#define SERIAL_BAUDRATE 115200

////ID MESSAGE////
#define IDMSG "KyoudaiKen FCNG"

#pragma region PROTOCOL
////REQUEST BYTES////
#define RQST_IDENTIFY 0xF0
#define RQST_CAPABILITIES 0xFC

#define RQST_GET_CURVE 0xAA
#define RQST_GET_MATRIX 0xAB
#define RQST_GET_CAL_RESISTRS 0xAC
#define RQST_GET_CAL_OFFSETS 0xAD
#define RQST_GET_CAL_SH_COEFFS 0xAE
#define RQST_GET_EERPOM_HEALTH 0xE0
#define RQST_GET_PINS 0xAF
#define RQST_GET_SENSOR_READINGS 0xBA

#define RQST_SET_CURVE 0xA0
#define RQST_SET_MATRIX 0xA1
#define RQST_SET_ID 0xA2
#define RQST_SET_CAL_RESISTRS 0xA3
#define RQST_SET_CAL_OFFSETS 0xA4
#define RQST_SET_CAL_SH_COEFFS 0xA5
#define RQST_SET_PINS 0xA6

#define RQST_WRITE_TO_EEPROM 0xDD
#define RQST_READ_FROM_EEPROM 0xDF

////RESPONSE BYTES////
#define RESP_OK 0x01
#define RESP_ERR 0xFF
#define RESP_WRN 0xB6

////RESPONSE ERRORS////
#define ERR_INDEX_OUT_OF_BOUNDS 0x10
#define ERR_TEMP_HIGHER_THAN_HUNDRED 0x11
#define ERR_DUTY_CYCLE_OUT_OF_RANGE 0x12
#define ERR_TIMEOUT 0xCC
#define ERR_EEPROM 0xEE
#pragma endregion PROTOCOL

#pragma region CONFIGURATION
#define N_SENSORS 3
#define N_CURVES 3
#define N_TEMP_PROBE_TESTS 1
#define N_RAVG 72 //memory hog!!!

//Curve data layout
#define CURVE_UB 12
#define CURVE_FIELD_LEN 5
#define CURVE_LEN CURVE_UB *CURVE_FIELD_LEN
#define CURVE_STRUCT_LEN (CURVE_LEN + 1)

//Matrix data layout
#define MATRIX_LENGTH N_SENSORS *N_CURVES * 4
#define MATRIX_START N_CURVES *CURVE_STRUCT_LEN

//EEPROM offsets
#define EEPROM_CAL_COEFF_OFFSET 470
#define EEPROM_CAL_OFFST_OFFSET 458
#define EEPROM_CAL_RESIS_OFFSET 446
#define EEPROM_CHECKSUM_OFFSET 507
#define EEPROM_PINS_OFFSET 440
#define EEPROM_ID_OFFSET 506
#pragma endregion CONFIGURATION

#pragma region WORKING_VARIABLES
//Thermistor (analog) pins
unsigned char thsp[N_SENSORS];

//Thermistor calibration (°K/C)
float thsco[N_SENSORS];

//Thermistor Steinhart-Hart coefficients
float thscs[N_SENSORS][3];

//Thermistor pulldown resistor value in Ohms - Can also be used for thermistor calibration
float thsRpd[N_SENSORS];

//PWM Curve pin mapping
unsigned char cpp[N_CURVES];

//Memorized temperatures
float tr[N_SENSORS][N_RAVG]; //Readings
unsigned char tp; //Rolling average index position
float t[N_SENSORS]; //Rolling average normalized temps
float ct[N_CURVES];

//Memorized duty cycles for info output
float cdc[N_CURVES];

struct curvePoint
{
    float           temp;
    unsigned char   dc;
};

//Curves and matrix data
curvePoint cdta[N_CURVES][CURVE_UB];
unsigned char cdtal[N_CURVES];
float m[N_CURVES][N_SENSORS];

bool eeprom_warning = false;
bool is_eeprom_ok = false;
#pragma endregion WORKING_VARIABLES

#pragma region EEPROM_TOOLS
void readCurves()
{
    if (!is_eeprom_ok)
        return;

    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        cdtal[c] = EEPROM.read(CURVE_STRUCT_LEN * c);
        unsigned int ci = 0;
        if (cdtal[c] > 0)
        {
            for (unsigned int i = 0; i < cdtal[c] * CURVE_FIELD_LEN; i += CURVE_FIELD_LEN)
            {
                EEPROM.get(CURVE_STRUCT_LEN * c + i + 1, cdta[c][ci].temp);
                EEPROM.get(CURVE_STRUCT_LEN * c + i + 5, cdta[c][ci].dc);
                if (cdta[c][ci].temp > 100 || cdta[c][ci].dc > 100)
                {
                    is_eeprom_ok = false;
                    return;
                }
#ifdef DEBUG
                Serial.print("CURVES: EEPROM.read() ");
                Serial.print(CURVE_STRUCT_LEN * c + i + 1, DEC);
                Serial.print(" to ");
                Serial.print(CURVE_STRUCT_LEN * c + i + 1 + CURVE_FIELD_LEN, DEC);
                Serial.println("");
#endif
                ci++;
            }
        }
    }
    is_eeprom_ok = true;
}

void writeCurves()
{
    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        if (cdtal[c] > 0)
        {
            EEPROM.write(CURVE_STRUCT_LEN * c, cdtal[c]);
#ifdef DEBUG
            Serial.print("CURVES: EEPROM.write() len ");
            Serial.print(cdtal[c], DEC);
            Serial.print(" at ");
            Serial.print(CURVE_STRUCT_LEN * c, DEC);
#endif
            unsigned int i = 0;
            for (unsigned int ci = 0; ci < cdtal[c]; ci++)
            {
                EEPROM.put(CURVE_STRUCT_LEN * c + i + 1, cdta[c][ci].temp);
                EEPROM.put(CURVE_STRUCT_LEN * c + i + 5, cdta[c][ci].dc);
#ifdef DEBUG
                Serial.print("cp ");
                Serial.print(CURVE_STRUCT_LEN * c + i + 1, DEC);
                Serial.print(" to ");
                Serial.print(CURVE_STRUCT_LEN * c + i + 1 + CURVE_FIELD_LEN, DEC);
                Serial.println("");
#endif
                i += CURVE_FIELD_LEN;
            }
        }
    }
}

void readMatrix()
{
   if (!is_eeprom_ok)
        return;

    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        for (unsigned int s = 0; s < N_SENSORS; s++)
        {
#ifdef DEBUG
            Serial.print("MATRIX: EEPROM.read() ");
            Serial.print(MATRIX_START + c * N_CURVES * 4 + s * 4, DEC);
            Serial.print(" to ");
            Serial.print(MATRIX_START + c * N_CURVES * 4 + s * 4 + 3, DEC);
            Serial.print(", length ");
            Serial.println(4 * N_SENSORS, DEC);
#endif
            EEPROM.get(MATRIX_START + c * N_CURVES * 4 + s * 4, m[c][s]);
        }
    }
}

void writeMatrix()
{
    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        for (unsigned int s = 0; s < N_SENSORS; s++)
        {
            EEPROM.put(MATRIX_START + c * N_CURVES * 4 + s * 4, m[c][s]);
#ifdef DEBUG
            Serial.print("MATRIX: EEPROM.write() ");
            Serial.print(MATRIX_START + c * N_CURVES * 4 + s * 4, DEC);
            Serial.print(" to ");
            Serial.print(MATRIX_START + c * N_CURVES * 4 + s * 4 + 3, DEC);
            Serial.print(", length ");
            Serial.println(4 * N_SENSORS, DEC);
#endif
        }
    }
}

void readThermostatCalibration() {
    unsigned char i = 0;
    
    for(i = 0; i < N_SENSORS; i++) {
        EEPROM.get(EEPROM_CAL_RESIS_OFFSET + i * 4, thsRpd[i]); //Read resistor values
        EEPROM.get(EEPROM_CAL_OFFST_OFFSET + i * 4, thsco[i]); //Read callibtration offsets
        
        EEPROM.get(EEPROM_CAL_COEFF_OFFSET + i * 12, thscs[i][0]); //Read Steinhart-Hart coefficients
        EEPROM.get(EEPROM_CAL_COEFF_OFFSET + i * 12 + 4, thscs[i][1]); //Read Steinhart-Hart coefficients
        EEPROM.get(EEPROM_CAL_COEFF_OFFSET + i * 12 + 8, thscs[i][2]); //Read Steinhart-Hart coefficients

        //Serial.print(thscs[i][0],12); Serial.print(" "); Serial.print(thscs[i][1],12); Serial.print(" "); Serial.println(thscs[i][2],12); 
    }
}

void writeThermostatCalibration() {
    unsigned char i = 0;
    
    for(i = 0; i < N_SENSORS; i++) {
        EEPROM.put(EEPROM_CAL_RESIS_OFFSET + i * 4, thsRpd[i]); //Write resistor values
        EEPROM.put(EEPROM_CAL_OFFST_OFFSET + i * 4, thsco[i]); //Write callibtration offsets

        EEPROM.put(EEPROM_CAL_COEFF_OFFSET + i * 12, thscs[i][0]); //Write Steinhart-Hart coefficients
        EEPROM.put(EEPROM_CAL_COEFF_OFFSET + i * 12 + 4, thscs[i][1]); //Write Steinhart-Hart coefficients
        EEPROM.put(EEPROM_CAL_COEFF_OFFSET + i * 12 + 8, thscs[i][2]); //Write Steinhart-Hart coefficients
    }
}

void readPinConfig()
{
    unsigned char i = 0;

    //Read thermal sensor pins
    for(i = 0; i < N_SENSORS; i++)
    {
        EEPROM.get(EEPROM_PINS_OFFSET + i, thsp[i]);
    }

    //Read PWM channel pins
    for(i = 0; i < N_SENSORS; i++)
    {
        EEPROM.get(EEPROM_PINS_OFFSET + i + 3, cpp[i]);
    }
}

void writePinConfig()
{
    unsigned char i = 0;

    //Read thermal sensor pins
    for(i = 0; i < N_SENSORS; i++)
    {
        EEPROM.put(EEPROM_PINS_OFFSET + i, thsp[i]);
    }

    //Read PWM channel pins
    for(i = 0; i < N_SENSORS; i++)
    {
        EEPROM.put(EEPROM_PINS_OFFSET + i + 3, cpp[i]);
    }
}

unsigned long CALC_EEPROM_CRC32()
{
    const unsigned long crc_table[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };

    unsigned long crc = ~0L;

    for (int i = 0; i < EEPROM.length(); ++i)
    {
        unsigned char val = 0;
        if (i >= EEPROM_CHECKSUM_OFFSET && i < EEPROM_CHECKSUM_OFFSET + 4)
        {
            val = 0;
        }
        else
        {
            val = EEPROM[i];
        }
        crc = crc_table[(crc ^ val) & 0x0f] ^ (crc >> 4);
        crc = crc_table[(crc ^ (val >> 4)) & 0x0f] ^ (crc >> 4);
        crc = ~crc;
    }
    return crc;
}

void writeEEPROM_CRC()
{
    EEPROM.put(EEPROM_CHECKSUM_OFFSET, CALC_EEPROM_CRC32());
}

bool checkEEPROM_CRC()
{
    unsigned long ee_crc = 0;
    EEPROM.get(EEPROM_CHECKSUM_OFFSET, ee_crc);
    return (CALC_EEPROM_CRC32() == ee_crc);
    //return false;
}
#pragma endregion EEPROM_TOOLS

#pragma region HELPERS
//Function for reading the temperatures at a all analog pins
/*
void getTemperaturesOLD()
{
    unsigned char tests = N_TEMP_PROBE_TESTS;
    float logR = 0;

    for (unsigned char s = 0; s < N_SENSORS; s++)
    {
        t[s] = 0;
        //Multiprobe for smoother values
        for (byte i = 0; i < tests; i++)
        {
            logR = log(thsRpd[s] * (1023.0 / (float)analogRead(thsp[s]) - 1.0));
            t[s] += (1.0 / (thscs[s][0] + thscs[s][1] * logR + thscs[s][2] * logR * logR * logR)) - 273.15 - thsco[s];
        }
        t[s] = t[s] / tests;
        if (isnan(t[s])) t[s] = 0;
    }
}
*/

//Function for reading the temperatures at a all analog pins over rolling average
void getTemperatures()
{
    for (unsigned char s = 0; s < N_SENSORS; s++)
    {
        //Read analog pin and get ADC value, then convert it to the resistor reading of the temperature sensor
        float logR = log(thsRpd[s] * (1023.0 / (float)analogRead(thsp[s]) - 1.0));
        
        //Convert to celsius using Steinhart-Hart-Calibration-Cooefficients
        tr[s][tp] = (1.0 / (thscs[s][0] + thscs[s][1] * logR + thscs[s][2] * logR * logR * logR)) - 273.15 - thsco[s];

        //Fill the whole rolling average array with the reading if this is the first reading
        if(tp == 0 && t[s] == 1000) {
            for(unsigned char i = 1; i < N_RAVG; i++) {
                tr[s][i] = tr[s][tp];
            }
        }

        //If there's a reading error, set it to 100°C to prevent overheating due to thermal sensor failure
        if(isnan(tr[s][tp])) tr[s][tp] = 100.0;

        //Calculate average now
        t[s] = tr[s][0];
        for (unsigned char i = 1; i < N_RAVG; i++) {
             t[s] += tr[s][i];
        }
        t[s] /= N_RAVG;
        #ifdef DEBUG
        //Serial.print(t[s]); Serial.print(" ");
        #endif
    }
    #ifdef DEBUG
    //Serial.println();
    #endif

    //Update pointer position
    tp++; if(tp == N_RAVG) tp = 0;
}

//Just to make the control loop more clean
void setPulseWith(int pin, float dc)
{
    float upper_bound;
    switch (pin)
    {
        case 3:
            upper_bound = OCR2A;
            break;
        default:
            upper_bound = ICR1;
            break;
    }
    analogWrite(pin, round(dc / 100 * upper_bound));
}

//Calculate the dutycycle by interpolating between the curve points
float getDutyCycle(unsigned int c, float mt)
{
    bool found = false;
    int p0 = 0;
    int p1 = 0;

    //Find the lowest curve point for our matrix value
    for (int i = 0; i < cdtal[c]; i++)
    {
        if (cdta[c][i].temp >= mt)
        {
            p0 = i - 1;
            found = true;
            break;
        }
    }

    //In case the curve doesn't start with temp 0 or the temperature is below the minimum curve point, return lowest duty cycle
    if(p0 < 0) return cdta[c][0].dc;


    if (found && p0 + 1 < cdtal[c])
    {
        //Interpolate between points
        p1 = p0 + 1;
        float interpolated = ((mt - cdta[c][p0].temp) * (cdta[c][p1].dc - cdta[c][p0].dc) / (cdta[c][p1].temp - cdta[c][p0].temp)) + cdta[c][p0].dc;
        return (interpolated < 0) ? 0 : interpolated;
    }
    else
    {
        //We overshot the last curve point, return the highest duty cycle in the curve
        return cdta[c][cdtal[c] - 1].dc;
    }
}

float matrix(unsigned int c, float *temps)
{
    float mt = 0;
    for (unsigned int s = 0; s < N_SENSORS; s++) {
        mt += temps[s] * m[c][s];
    }
    return mt;
}

//This function sets the actual duty cycle you programmed.
void setDutyCycles()
{
    #ifdef DEBUG
    Serial.print(t[0]); Serial.print(" "); Serial.print(t[1]); Serial.print(" "); Serial.print(t[2]);
    #endif
    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        ct[c] = matrix(c, t);
        cdc[c] = getDutyCycle(c, ct[c]);
        #ifdef DEBUG
        Serial.print(" {'c': "); Serial.print(c); Serial.print(", 'mt': "); Serial.print(ct[c]); Serial.print(", 'dc': "); Serial.print(cdc[c]); Serial.print("},");
        #endif
        setPulseWith(cpp[c], cdc[c]);
    }
    #ifdef DEBUG
    Serial.println();
    #endif
    delay(27);
}

void doFanControl() {
    getTemperatures();
    setDutyCycles();
}

#pragma endregion HELPERS

#pragma region SERIAL_TOOLS

//Serialize float so C# can read it
void serialWriteFloat(float f)
{
    unsigned char *b = (unsigned char *)&f;
    Serial.write(b, 4);
}

float serialReadFloat() {
    unsigned char buff[4];
    Serial.readBytes(buff, 4);
    return ((float*)buff)[0];
}

void say_hello() {
    Serial.print(IDMSG); Serial.write((unsigned char)EEPROM.read(EEPROM_ID_OFFSET));
    Serial.flush();
}

void sendError(unsigned char err_code, unsigned char resp_code) {
    Serial.write((unsigned char)RESP_ERR);
    Serial.write(resp_code);
    Serial.write(err_code);
    Serial.flush();
}

void sendOK(unsigned char resp_code) {
    Serial.write((unsigned char)RESP_OK);
    Serial.write(resp_code);
    Serial.flush();
}

#pragma endregion SERIAL_TOOLS

void setup()
{
    unsigned char i;
    
    Serial.begin(SERIAL_BAUDRATE);

    #pragma region CONFIG_TIMERS_AND_PWM
    // Configure Timer 1 for PWM @ 25 kHz.
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(CS10);
    ICR1 = 160;

    // Configure Timer 2 for PWM @ 25 kHz.
    TCCR2A = 0;
    TCCR2B = 0;
    TCNT2 = 0;
    TCCR2A = _BV(COM2B1) | _BV(WGM20);
    TCCR2B = _BV(WGM22) | _BV(CS20);
    OCR2A = 160;

    //Set pins up for output
    for (i = 0; i < N_CURVES; i++)
    {
        pinMode(cpp[i], OUTPUT);
        setPulseWith(cpp[i], 100);
    }
    #pragma endregion CONFIG_TIMERS_AND_PWM

    #pragma region READ_EEPROM
    //Zero curve and matrix memory
    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        for (i = 0; i < CURVE_UB; i++)
        {
            cdta[c][i].temp = 0;
            cdta[c][i].dc = 0;
            cdtal[c] = 0;
        }
        for (unsigned int s = 0; s < N_SENSORS; s++)
            m[c][s] = 0;
    }

    //Get data from the EEPROM
    if (
            (
                   EEPROM.read(0) > CURVE_UB
                || EEPROM.read(CURVE_STRUCT_LEN) > CURVE_UB
                || EEPROM.read(CURVE_STRUCT_LEN * 2) > CURVE_UB
                || EEPROM.read(0) == 0
                || EEPROM.read(CURVE_STRUCT_LEN) == 0
                || EEPROM.read(CURVE_STRUCT_LEN * 2) == 0
            )
            || !checkEEPROM_CRC()
        )
    {
        #pragma region CONFIG_PINS
        /* Pins:
        * pin 3 = OC2B (timer 2 PWM output B)
        * pin 11 = OC2A (timer 2 PWM output A)
        * pin 9 = OC1B (timer 1 PWM output B)
        * pin 10 = OC1A (timer 1 PWM output A)
        * pin 5 = OC0B (timer 0 PWM output B)
        * pin 6 = OC0A (timer 0 PWM output A)
        */

        //Configure thermistor pin defaults
        thsp[0] = 0;
        thsp[1] = 1;
        thsp[2] = 3;
        
        //Configure channel's PWM output pin defaults
        cpp[0] = 3;
        cpp[1] = 9;
        cpp[2] = 10;
        #pragma endregion CONFIG_PINS

        #pragma region THEMRISTORS
        //Configure thermistor pulldown resistor value defaults
        thsRpd[0] = 9990;
        thsRpd[1] = 9890;
        thsRpd[2] = 9960;

        //Configure thermistor calibration offsets and Steinhart-Hart coefficient defaults
        thsco[0] = 0;
        thsco[1] = 0;
        thsco[2] = 0;
        thscs[0][0] = 0.0011431512594581995;
        thscs[0][1] = 0.00023515745037380024;
        thscs[0][2] = 6.187191114586837e-8;
        thscs[1][0] = 0.0028561410879575405;
        thscs[1][1] = -0.00005243877323181964;
        thscs[1][2] = 0.0000012584771402890711;
        thscs[2][0] = 0.0028561410879575405;
        thscs[2][1] = -0.00005243877323181964;
        thscs[2][2] = 0.0000012584771402890711;
        #pragma endregion THEMRISTORS

        #pragma region CURVES_AND_MATRIX
        //Set up default curves and matrix just ti get things going
        
        /*
        for (unsigned char c = 0; c < N_CURVES; c++)
        {
            
            //Set curve array length accordingly to the points we add
            cdtal[c] = 2;
            //Create a linear curve between 0°C ≙ 0% and 100°C ≙ 100%
            cdta[c][0].temp = 0;
            cdta[c][0].dc = 33;
            cdta[c][1].temp = 100;
            cdta[c][1].dc = 33;
            

            //Set up a simple channel = output matrix
            for (unsigned char s = 0; s < N_SENSORS; s++)
            {
                m[c][s] = 1;
            }
        }
        */

        /* Kyo specific */
        cdtal[0] = 4;
        cdta[0][0].temp = 2;
        cdta[0][0].dc = 15;
        cdta[0][1].temp = 3;
        cdta[0][1].dc = 18;
        cdta[0][2].temp = 6;
        cdta[0][2].dc = 33;
        cdta[0][3].temp = 20;
        cdta[0][3].dc = 100;

        cdtal[1] = 1;
        cdta[1][0].temp = 0;
        cdta[1][0].dc = 33;

        cdtal[2] = 3;
        cdta[2][0].temp = 0;
        cdta[2][0].dc = 24;
        cdta[2][1].temp = 3;
        cdta[2][1].dc = 33;
        cdta[2][2].temp = 12;
        cdta[2][2].dc = 100;

        m[0][0] = 1;
        m[0][1] = 0;
        m[0][2] = -1;

        m[1][0] = 0;
        m[1][1] = 1;
        m[1][2] = 0;

        m[2][0] = 0.5;
        m[2][1] = 0.5;
        m[2][2] = -1;

        #pragma endregion CURVES_AND_MATRIX

        //Write to EEPROM:
        writePinConfig();
        writeThermostatCalibration();
        writeCurves();
        writeMatrix();
        writeEEPROM_CRC();

        #ifdef DEBUG
        readPinConfig();
        readThermostatCalibration();
        readCurves();
        readMatrix();
        #endif
        
        eeprom_warning = true;
        is_eeprom_ok = true;
    }
    else
    {
        is_eeprom_ok = true;
        readPinConfig();
        readThermostatCalibration();
        readCurves();
        readMatrix();
    }
    #pragma endregion READ_EEPROM

    #pragma region PREP_THERMAL_READINGS
     //Rolling average position need to start at zero
    tp = 0;

    //Set temperatures to 1000 so it is known that it's the first reading
    t[0] = 1000;
    t[1] = 1000;
    t[2] = 1000;
    #pragma endregion PREP_THERMAL_READINGS

    say_hello();
}

void loop()
{
    unsigned char i;
    unsigned char request = 0x00;
    unsigned char rqst_id;
    long timestamp = 0;

    doFanControl();

    //Watch for serial commands
    if (Serial.available() == 0)
        return;

    request = Serial.read();

    switch (request)
    {
        case RQST_IDENTIFY:

            sendOK(request);
            say_hello();

            break;
        case RQST_CAPABILITIES:

            sendOK(request);
            Serial.write((unsigned char)N_SENSORS);
            Serial.write((unsigned char)N_CURVES);

            break;

        case RQST_GET_EERPOM_HEALTH:

            if(eeprom_warning)
            {
                sendOK(request);
                Serial.write((unsigned char)0x00);
            }
            else
            {
                sendOK(request);
                Serial.write((unsigned char)0x01);
            }

            break;
        case RQST_GET_SENSOR_READINGS:

            sendOK(request);
            for (unsigned char s = 0; s < N_SENSORS; s++)
            {
                serialWriteFloat(t[s]);
            }

            for (unsigned char c = 0; c < N_CURVES; c++)
            {
                serialWriteFloat(ct[c]);
                serialWriteFloat(cdc[c]);
            }

            break;

        case RQST_READ_FROM_EEPROM:

            readPinConfig();
            readThermostatCalibration();
            readCurves();
            readMatrix();
            sendOK(request);

            break;
        case RQST_WRITE_TO_EEPROM:

            writePinConfig();
            writeThermostatCalibration();
            writeCurves();
            writeMatrix();
            writeEEPROM_CRC();
            sendOK(request);

            break;

        case RQST_SET_ID:

            timestamp = millis();
            while (Serial.available() != 1 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if (Serial.available() == 1)
            {

                unsigned char new_id = Serial.read();
                EEPROM.write(EEPROM_ID_OFFSET, new_id);
                writeEEPROM_CRC();
                sendOK(request);

            }
            else
            {

                sendError(ERR_TIMEOUT, request);

            }

            break;

        case RQST_GET_CAL_RESISTRS:

            sendOK(request);
            for(i = 0; i < N_SENSORS; i++) serialWriteFloat(thsRpd[i]);

            break;

        case RQST_GET_CAL_OFFSETS:

            sendOK(request);
            for(i = 0; i < N_SENSORS; i++) serialWriteFloat(thsco[i]);

            break;

        case RQST_GET_CAL_SH_COEFFS:

            sendOK(request);
            for(i = 0; i < N_SENSORS; i++)
            {
                serialWriteFloat(thscs[i][0]);
                serialWriteFloat(thscs[i][1]);
                serialWriteFloat(thscs[i][2]);
            }

            break;

        case RQST_GET_PINS:

            sendOK(request);
            for(i = 0; i < N_SENSORS; i++) Serial.write(thsp[i]);
            for(i = 0; i < N_CURVES; i++) Serial.write(cpp[i]);

            break;

        case RQST_GET_CURVE:

            timestamp = millis();
            while (Serial.available() != 1 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if (Serial.available() == 1)
            {

                rqst_id = Serial.read(); //rqst_id is the curve

                if(rqst_id > N_CURVES)
                {
                    sendError(ERR_INDEX_OUT_OF_BOUNDS, request);
                }
                else
                {
                    sendOK(request);
                    Serial.write(cdtal[rqst_id]); //Send length (byte)

                    for(i = 0; i < cdtal[rqst_id]; i++) {
                        serialWriteFloat(cdta[rqst_id][i].temp); //Send temperature (float32)
                        Serial.write(cdta[rqst_id][i].dc); //Send duty cycle (byte)
                    }
                }

            }
            else
            {
                sendError(ERR_TIMEOUT, request);
            }

            break;

        case RQST_GET_MATRIX:

            //Wait for a byte that indicates the curve number
            timestamp = millis();
            while (Serial.available() != 1 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if (Serial.available() == 1)
            {
                rqst_id = Serial.read(); //rqst_id is the curve for the matrix

                if (rqst_id > N_CURVES)
                {
                    sendError(ERR_INDEX_OUT_OF_BOUNDS, request);
                }
                else
                {
                    sendOK(request);
                    for (unsigned char s = 0; s < N_SENSORS; s++) serialWriteFloat(m[rqst_id][s]);
                }
            }
            else
            {
                sendError(ERR_TIMEOUT, request);
            }

            break;

        case RQST_SET_CURVE:

            //Wait for 2 bytes. One for the curve ID and one for the length.
            timestamp = millis();
            while (Serial.available() < 2 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if (Serial.available() > 2)
            {
                rqst_id = Serial.read(); //Read curve id to be updaterd
                unsigned char sc_data_len = Serial.read(); //Read number of curve points

                //Wait for the rest of the data if it wasn't transmitted already. 
                timestamp = millis();
                while (Serial.available() != sc_data_len * CURVE_FIELD_LEN && millis() - timestamp < RXTX_TIMEOUT) doFanControl();
 
                if (Serial.available() != sc_data_len * CURVE_FIELD_LEN)
                {
                    sendError(ERR_TIMEOUT, request);
                }
                else
                {
                    if (rqst_id > N_CURVES && sc_data_len > CURVE_UB)
                    {
                        sendError(ERR_INDEX_OUT_OF_BOUNDS, request);
                    }
                    else
                    {
                        //Read the data and store it in our memory array
                        for(i = 0; i < sc_data_len; i++) {
                            float s_temp = serialReadFloat();
                            unsigned char s_dc = Serial.read();
                            if(s_dc > 100) s_dc = 100;
                            cdta[rqst_id][i].temp = s_temp;
                            cdta[rqst_id][i].dc = s_dc;
                        }

                        cdtal[rqst_id] = sc_data_len; //Store length

                        sendOK(request);
                    }
                }
            }
            else
            {
                sendError(ERR_TIMEOUT, request);
            }

            break;

        case RQST_SET_MATRIX:

            //Wait for the curve ID for the matrix
            timestamp = millis();
            while (Serial.available() < 1 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if (Serial.available() > 1)
            {
                rqst_id = Serial.read(); //Read curve id to be updaterd

                //Wait for the rest of the data if it wasn't transmitted already. 
                timestamp = millis();
                while (Serial.available() != 12 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

                if(Serial.available() != 12)
                {
                    sendError(ERR_TIMEOUT, request);
                }
                else
                {
                    if (rqst_id > N_CURVES)
                    {
                        sendError(ERR_INDEX_OUT_OF_BOUNDS, request);
                    }
                    else
                    {
                        //Read the data and store it in our memory array
                        for(i = 0; i < 3; i++) {
                            m[rqst_id][i] = serialReadFloat();
                        }

                        sendOK(request);
                    }
                }
            }
            else
            {
                sendError(ERR_TIMEOUT, request);
            }

            break;

        case RQST_SET_CAL_RESISTRS:

            //Wait for the data
            timestamp = millis();
            while (Serial.available() != N_SENSORS * 4 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if(Serial.available() != N_SENSORS * 4)
            {
                sendError(ERR_TIMEOUT, request);
            }
            else
            {
                for(i = 0; i < N_SENSORS; i++) thsRpd[i] = serialReadFloat();
                sendOK(request);
            }

            break;

        case RQST_SET_CAL_OFFSETS:

            //Wait for the data
            timestamp = millis();
            while (Serial.available() != N_SENSORS * 4 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if(Serial.available() != N_SENSORS * 4)
            {
                sendError(ERR_TIMEOUT, request);
            }
            else
            {
                for(i = 0; i < N_SENSORS; i++) thsco[i] = serialReadFloat();
                sendOK(request);
            }

            break;

        case RQST_SET_CAL_SH_COEFFS:

            //Wait for the data
            timestamp = millis();
            while (Serial.available() != N_SENSORS * 12 && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if(Serial.available() != N_SENSORS * 12)
            {
                sendError(ERR_TIMEOUT, request);
            }
            else
            {
                for(i = 0; i < N_SENSORS; i++) {
                    thscs[i][0] = serialReadFloat();
                    thscs[i][1] = serialReadFloat();
                    thscs[i][2] = serialReadFloat();
                }
                sendOK(request);
            }

            break;

        case RQST_SET_PINS:

            //Wait for the data
            timestamp = millis();
            while (Serial.available() != N_SENSORS + N_CURVES && millis() - timestamp < RXTX_TIMEOUT) doFanControl();

            if(Serial.available() != N_SENSORS + N_CURVES)
            {
                sendError(ERR_TIMEOUT, request);
            }
            else
            {
                for(i = 0; i < N_SENSORS; i++) thsp[i] = Serial.read();
                for(i = 0; i < N_CURVES; i++) cpp[i] = Serial.read();
                sendOK(request);
            }

            break;
    }
}
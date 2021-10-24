//ArduPWM PC fan controller Mk III - SEP 15 2019
//By Kyoudai Ken @Kyoudai_Ken (Twitter.com)
//#include <arduino.h>
#include <stdlib.h>
#include <EEPROM.h>

//#define DEBUG

////ID MESSAGE////
#define IDMSG "KyoudaiKen FCNG"

#pragma region STATE_MACHINE
#define MODE_READY 0

#define MODE_SET_CURVE 2
#define MODE_SET_MATRIX 3
#define MODE_GET_CURVE 6

#define MODE_SM_DEFAULT 0
#define MODE_SM_CURVE 1
#define MODE_SM_SC_POINT 2
#define MODE_SM_SM_MATRIX 2
#pragma endregion STATE_MACHINE

#pragma region PROTOCOL
////REQUEST BYTES////
#define RQST_IDENTIFY 0xF0
#define RQST_CAPABILITIES 0xFC
#define RQST_SET_CURVE 0xA0
#define RQST_SET_MATRIX 0xA1
#define RQST_GET_CURVE 0xAA
#define RQST_GET_MATRIX 0xAB
#define RQST_GET_SENSORS 0xBA
#define RQST_WRITE_TO_EEPROM 0xDD
#define RQST_READ_FROM_EEPROM 0xDF
#define RQST_NEXT 0xFA
#define RQST_END 0xEF

////RESPONSE BYTES////
#define RESP_OK 0x01
#define RESP_ERR 0xFF
#define RESP_WRN 0xB6
#define RESP_END 0xFE

////RESPONSE ERRORS////
#define ERR_INDEX_OUT_OF_BOUNDS 0x10
#define ERR_TEMP_HIGHER_THAN_HUNDRED 0x11
#define ERR_DUTY_CYCLE_OUT_OF_RANGE 0x12
#define ERR_TIMEOUT 0xCC
#define ERR_EEPROM 0xEE
#define WRN_MAX_PONTS_REACHED 0xC4
#pragma endregion PROTOCOL

#pragma region CONFIGURATION
#define N_SENSORS 3
#define N_CURVES 3
#define N_TEMP_PROBE_TESTS 1
#define N_RAVG 32

/* 
 * The following constants can be used if your micro controller is more powerful
 * You can enhance the curve upper bound (maximum number of points) CURVE_UB
 * Do not forget to adjust the EEPROM offsets!
 */

//Curve data layout
#define CURVE_UB 12
#define CURVE_FIELD_LEN 5
#define CURVE_LEN CURVE_UB *CURVE_FIELD_LEN
#define CURVE_STRUCT_LEN (CURVE_LEN + 1)

//Matrix data layout
#define MATRIX_LENGTH N_SENSORS *N_CURVES * 4
#define MATRIX_START N_CURVES *CURVE_STRUCT_LEN

//EEPROM offsets
#define EEPROM_CHECKSUM_OFFSET 505
#pragma endregion CONFIGURATION

#pragma region WORKING_VARIABLES
//Interaction mode
unsigned char mode;
unsigned char submode;

unsigned char current_curve;
unsigned char current_index;

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
byte cdtal[N_CURVES];
float m[N_CURVES][N_SENSORS];

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

uint32_t CRC32(byte *data, uint32_t ignore_offset)
{
    const uint32_t crc_table[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };

    uint32_t crc = ~0L;
    for (uint32_t i = 0; i < sizeof(data); ++i)
    {
        byte val = 0;
        if (i > ignore_offset && i < ignore_offset + 4)
        {
            val = 0;
        }
        else
        {
            val = data[i];
        }
        crc = crc_table[(crc ^ val) & 0x0f] ^ (crc >> 4);
        crc = crc_table[(crc ^ (val >> 4)) & 0x0f] ^ (crc >> 4);
        crc = ~crc;
    }
    return crc;
}

void writeEEPROM_CRC()
{
    byte e[512];
    for (uint16_t i = 0; i < 512; i++)
        e[i] = EEPROM[i];
    EEPROM.put(EEPROM_CHECKSUM_OFFSET, CRC32(e, EEPROM_CHECKSUM_OFFSET));
}

bool checkEEPROM_CRC()
{
    uint32_t crc = 0;
    EEPROM.get(EEPROM_CHECKSUM_OFFSET, crc);
    byte e[512];
    for (uint16_t i = 0; i < 512; i++)
        e[i] = EEPROM[i];
    bool ok = (CRC32(e, EEPROM_CHECKSUM_OFFSET) == crc);
    return ok;
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
    }

    //Update pointer position
    tp++; if(tp == N_RAVG) tp = 0;
}

//Just to make the control loop more clean
void setPulseWith(int pin, float pv)
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
    analogWrite(pin, round(pv / 100 * upper_bound));
}

//Calculate the dutycycle by interpolating between the curve points
float getDutyCycle(unsigned int c, float mt)
{
    bool found = false;
    byte p0 = 0;
    byte p1 = 0;
    float dc;
    if (mt == 0)
        return cdta[c][0].dc;

    for (byte i = 0; i < cdtal[c]; i++)
    {
        if (cdta[c][i].temp >= mt)
        {
            p0 = i - 1;
            found = true;
            break;
        }
    }
    if (found && p0 + 1 <= cdtal[c])
    {
        p1 = p0 + 1;
        dc = ((mt - cdta[c][p0].temp) * (cdta[c][p1].dc - cdta[c][p0].dc) / (cdta[c][p1].temp - cdta[c][p0].temp)) + cdta[c][p0].dc;
    }
    else
    {
        dc = 100;
    }
    return dc < 0 ? 0 : dc;
}

float matrix(unsigned int c, float *temps)
{
    float mt = 0;
    for (unsigned int s = 0; s < N_SENSORS; s++)
        mt += temps[s] * m[c][s];
    return (mt < 0 ? 0 : mt);
}

//This function sets the actual duty cycle you programmed.
void setDutyCycles()
{
    for (unsigned int c = 0; c < N_CURVES; c++)
    {
        ct[c] = matrix(c, t);
        cdc[c] = getDutyCycle(c, ct[c]);
        setPulseWith(cpp[c], cdc[c]);
    }
    delay(10);
}
#pragma endregion HELPERS

#pragma region SERIAL_TOOLS
//Write binary sensor data to serial for LHM
void serialWriteFloat(float f)
{
    unsigned char *b = (unsigned char *)&f;
    Serial.write(b, 4);
}

void sendSensorDataBinary()
{
    for (unsigned char s = 0; s < N_SENSORS; s++)
    {
        serialWriteFloat(t[s]);
    }
    for (unsigned char c = 0; c < N_CURVES; c++)
    {
        serialWriteFloat(ct[c]);
    }
    for (unsigned char c = 0; c < N_CURVES; c++)
    {
        serialWriteFloat(cdc[c]);
    }
}

unsigned char serialReadLine(uint16_t timeout_ms, char buff[]) {
    //loop until we have new data until we got a \n sequence.
    timeout_ms /= 10;
    unsigned char index = 0;

    for (uint16_t to = 0; to < timeout_ms; to++) {
        delay(10);
        if (Serial.available()) {
            buff[index++] = Serial.read();
            if(buff[index]==0x0D)
                break;
        } else {
            if(index+1>=64)
                break;
        }
    }

    //Flush all the rest
    Serial.flush();

    return index;
}
#pragma endregion SERIAL_TOOLS

void setup()
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

    //Configure thermistor pins
    thsp[0] = 0;
    thsp[1] = 1;
    thsp[2] = 3;
    
    //Configure channel's PWM output pins
    cpp[0] = 3;
    cpp[1] = 9;
    cpp[2] = 10;
    #pragma endregion CONFIG_PINS

    #pragma region CONFIG_THERMISTOR_CALIBRATION
    //Configure thermistor pulldown resistor values
    thsRpd[0] = 9990;
    thsRpd[1] = 9890;
    thsRpd[2] = 9960;

    //Configure thermistor calibration offsets and Steinhart-Hart coefficients
    thsco[0] = 0;
    thsco[1] = 0;
    thsco[2] = 0;
    thscs[0][0] = 0.0011431512594581995;
    thscs[0][1] = 0.00023515745037380024;
    thscs[0][2] = 6.187191114586837e-8;
    //thscs[1][0]=-0.1745637090e-03;
    //thscs[1][1]=4.260403485e-04;
    //thscs[1][2]=-5.098359524e-07;
    thscs[1][0] = 0.0028561410879575405;
    thscs[1][1] = -0.00005243877323181964;
    thscs[1][2] = 0.0000012584771402890711;
    thscs[2][0] = 0.0028561410879575405;
    thscs[2][1] = -0.00005243877323181964;
    thscs[2][2] = 0.0000012584771402890711;

    //Rolling average positions need to start at zero
    tp = 0;

    //Set temperatures to 1000 so it is known that it's the first reading
    t[0] = 1000;
    t[1] = 1000;
    t[2] = 1000;
    #pragma endregion CONFIG_THERMISTOR_CALIBRATION

    #pragma region CONFIG_TIMERS_AND_PWM
    // Configure Timer 1 and 2 for PWM @ 25 kHz.
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(CS10);
    ICR1 = 160;

    TCCR2A = 0;
    TCCR2B = 0;
    TCNT2 = 0;
    TCCR2A = _BV(COM2B1) | _BV(WGM20);
    TCCR2B = _BV(WGM22) | _BV(CS20);
    OCR2A = 160;

    //Set pins up for output
    unsigned int i;
    for (i = 0; i < N_CURVES; i++)
    {
        pinMode(cpp[i], OUTPUT);
        setPulseWith(cpp[i], 100);
    }

    Serial.begin(115200);
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
        //Set up default curves and matrix just ti get things going
        for (unsigned char c = 0; c < N_CURVES; c++)
        {
            //Set curve array length accordingly to the points we add
            cdtal[c] = 2;
            //Create a linear curve between 0°C ≙ 0% and 100°C ≙ 100%
            cdta[c][0].temp = 0;
            cdta[c][0].dc = 0;
            cdta[c][1].temp = 100;
            cdta[c][1].dc = 100;

            //Set up a simple channel = output matrix
            for (unsigned char s = 0; s < N_SENSORS; s++)
            {
                m[c][s] = 1;
            }
        }

        /*
        cdtal[0]=2; cdtal[1]=2; cdtal[2]=2;

        cdta[0][0].temp=0; cdta[0][0].dc=0; cdta[0][1].temp=100; cdta[0][1].dc=100;
        cdta[1][0].temp=0; cdta[1][0].dc=0; cdta[1][1].temp=100; cdta[1][1].dc=100;
        cdta[2][0].temp=0; cdta[2][0].dc=0; cdta[2][1].temp=100; cdta[2][1].dc=100;

        m[0][0]=1;m[0][1]=0;m[0][2]=0;
        m[1][0]=0;m[1][1]=1;m[1][0]=0;
        m[2][0]=0;m[2][1]=0;m[2][0]=1;
        */

        //Zero EEPROM
        for (i = 0; i < 512; i++)
            EEPROM.write(i, 0);
        //Write to EEPROM:
        writeCurves();
        writeMatrix();
        Serial.write(RESP_ERR);
        Serial.write(ERR_EEPROM);
        is_eeprom_ok = false;
    }
    else
    {
        readCurves();
        readMatrix();
        is_eeprom_ok = true;
    }
    #pragma endregion READ_EEPROM

    //Set initial mode
    mode = MODE_READY;
    submode = MODE_SM_DEFAULT;

    Serial.println(IDMSG);
}

void loop()
{
    unsigned char i;
    unsigned char request = 0x00;

    getTemperatures();
    setDutyCycles();

    //Watch for serial commands
    if (Serial.available() == 0)
        return;

    request = Serial.read();

    /* Mode code */
    switch (mode)
    {
        case MODE_READY:
        {
            switch (request)
            {
            case RQST_IDENTIFY:

                Serial.print(IDMSG);

                break;
            case RQST_CAPABILITIES:

                Serial.write(N_SENSORS);
                Serial.write(N_CURVES);

                break;
            case RQST_GET_SENSORS:

                sendSensorDataBinary();

                break;
            case RQST_READ_FROM_EEPROM:

                readCurves();
                readMatrix();
                Serial.write(RESP_OK);

                break;
            case RQST_WRITE_TO_EEPROM:

                writeMatrix();
                writeCurves();
                writeEEPROM_CRC();
                Serial.write(RESP_OK);

                break;
            case RQST_SET_CURVE:

                mode = MODE_SET_CURVE;
                submode = MODE_SM_CURVE;
                Serial.write(RESP_OK);

                break;

            case RQST_SET_MATRIX:

                mode = MODE_SET_MATRIX;
                submode = MODE_SM_CURVE;
                Serial.write(RESP_OK);

                break;

            case RQST_GET_CURVE:

                mode = MODE_GET_CURVE;
                submode = MODE_SM_CURVE;
                Serial.write(RESP_OK);

                break;

            case RQST_GET_MATRIX:

                //Wait for a byte that indicates the curve number
                i = 0;
                current_curve = 0;
                while (Serial.available() != 1 && i < 200)
                {
                    delay(1);
                    i++;
                }

                if (Serial.available() == 1)
                {
                    current_curve = Serial.read();

                    if (current_curve > N_CURVES)
                    {
                        mode = MODE_READY;
                        submode = MODE_SM_DEFAULT;
                        Serial.write(RESP_ERR);
                        Serial.write(ERR_INDEX_OUT_OF_BOUNDS);
                    }
                    else
                    {
                        for (unsigned char s = 0; s < N_SENSORS; s++)
                            serialWriteFloat(m[current_curve][s]);
                    }
                }
                else
                {
                    mode = MODE_READY;
                    submode = MODE_SM_DEFAULT;
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;

            case 's': //human setup mode
            case 'S':

                Serial.println("d");

                //Check if there is more information to read. If not, cancel. 
                if (Serial.available() <= 1)
                    return;

                char setWhat = Serial.read();
                char chrCurve[2];
                chrCurve[1] = 0x00;
                chrCurve[0] = Serial.read();
                unsigned char curve = 0xFF;
                
                //In case the next byte is a space, get next byte and parse it as number, if available.
                if(chrCurve[0]==0x20) {
                    if(Serial.available()) {
                        chrCurve[0] = Serial.read();
                        curve = (unsigned char)atoi(chrCurve);
                    } else {
                        Serial.println("No curve given");
                        return;
                    }
                } else {
                    curve = (unsigned char)atoi(chrCurve);
                }

                if(curve>N_CURVES) {
                    Serial.println("Invalid curve");
                    return;
                }

                switch (setWhat) {
                    case 'c': //Curve
                    case 'C':

                        unsigned char tcpl;
                        tcpl = 0;
                        curvePoint tcp[CURVE_UB];
                        for (unsigned char i = 0; i < CURVE_UB; i++) {
                            tcp[i].temp=0;
                            tcp[i].dc=0;
                        }

                        bool cancel;
                        cancel = false;
                    
                        char buff[64];
                        for(unsigned char i = 0; i < 64; i++) buff[i] = 0;
                        unsigned char bl;
                        bl = 0;
                        //Loop until an 'f' is sent
                        while(!cancel) {

                            bl = serialReadLine(10000, buff);

                            bool other = false; // LOL
                            char temp[bl];
                            unsigned char temp_idx = 0;
                            for(unsigned char i = 0; i < bl; i++) temp[i] = 0;
                            char dc[bl];
                            unsigned char dc_idx = 0;
                            for(unsigned char i = 0; i < bl; i++) dc[i] = 0;

                            for(unsigned char i = 0; i < bl; i++) {
                                if(buff[i] == ' '
                                    || buff[i] == ';'
                                    || buff[i] == ','
                                    || buff[i] == ':') {
                                    other=true;
                                } else {
                                    if(!other) {
                                        temp[temp_idx++] = buff[i];
                                    } else {
                                        dc[dc_idx++] = buff[i];
                                    }
                                }
                            }

                            if(other!=true) {
                                Serial.println("No value pair detected");
                                continue;
                            }

                            tcp[tcpl++].temp = (float)atof(temp);
                            tcp[tcpl].dc = (byte)atoi(dc);

                            if(tcp[tcpl].temp > 100 || tcp[tcpl].dc > 100) {
                                Serial.println("Value out of range");
                                tcpl--;
                                continue;
                            }

                            if(bl == 1 && buff[0] == 'f')
                                cancel = true;

                        }

                        //Copy the temp data into the real curve data
                        for(unsigned char i = 0; i < tcpl; i++)
                            cdta[curve][i] = tcp[i];
                        cdtal[curve] = tcpl;

                    break;

                    case 'm': //Matrix
                    case 'M':

                        //

                    break;
                }

                break;
            }
            break;
        }
        case MODE_SET_CURVE:
        {
            switch (submode)
            {
            case MODE_SM_CURVE:

                //Wait for a byte that indicates the curve number
                i = 0;
                while (Serial.available() != 1 && i < 200)
                {
                    delay(1);
                    i++;
                }
                if (Serial.available() == 1)
                {
                    current_curve = Serial.read();

                    if (current_curve > N_CURVES)
                    {
                        mode = MODE_READY;
                        submode = MODE_SM_DEFAULT;
                        Serial.write(RESP_ERR);
                        Serial.write(ERR_INDEX_OUT_OF_BOUNDS);
                    }
                    else
                    {
                        //Next step
                        Serial.write(RESP_OK);
                        submode = MODE_SM_SC_POINT;
                        current_index = 0;
                    }
                }
                else
                {
                    mode = MODE_READY;
                    submode = MODE_SM_DEFAULT;
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;
            case MODE_SM_SC_POINT:

                unsigned char rqnext = 0;
                i = 0;
                while (Serial.available() != 1 && i < 200)
                {
                    delay(1);
                    i++;
                }

                if (Serial.available() == 1)
                {
                    rqnext = Serial.read();

                    if (rqnext == RQST_NEXT)
                    {
                        //Wait for 6 bytes containing a float for the temperature and a byte for the duty cycle
                        i = 0;
                        while (Serial.available() != 5 && i < 200)
                        {
                            delay(1);
                            i++;
                        }

                        if (Serial.available() == 5)
                        {
                            if (current_index + 1 >= CURVE_UB)
                            {
                                //Cancel if maximum curve points reached, finalizing
                                cdtal[current_curve] = current_index;
                                current_index = 0;
                                mode = MODE_READY;
                                submode = MODE_SM_DEFAULT;
                                Serial.write(RESP_WRN);
                                Serial.write(WRN_MAX_PONTS_REACHED);
                            }
                            else
                            {
                                cdta[current_curve][current_index].temp = Serial.parseFloat();
                                cdta[current_curve][current_index].dc = Serial.read();
                                current_index++;
                            }
                        }
                        else
                        {
                            mode = MODE_READY;
                            submode = MODE_SM_DEFAULT;
                            Serial.write(RESP_ERR);
                            Serial.write(ERR_TIMEOUT);
                        }
                    }
                    else
                    {
                        //Finish signal sent, finalizing
                        cdtal[current_curve] = current_index;
                        current_index = 0;
                        mode = MODE_READY;
                        submode = MODE_SM_DEFAULT;
                        Serial.write(RESP_OK);
                    }
                }
                else
                {
                    mode = MODE_READY;
                    submode = MODE_SM_DEFAULT;
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;
            }
            break;
        }
        case MODE_SET_MATRIX:
        {
            switch (request)
            {
            case MODE_SM_CURVE:

                //Wait for a byte that indicates the curve number
                i = 0;
                while (Serial.available() != 1 && i < 200)
                {
                    delay(1);
                    i++;
                }
                if (Serial.available() == 1)
                {
                    current_curve = Serial.read();
                    //Next step
                    Serial.write(RESP_OK);
                    submode = MODE_SM_SM_MATRIX;
                }
                else
                {
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;

            case MODE_SM_SM_MATRIX:

                //Wait for bytes that contain floats for the matrix definition
                i = 0;
                while (Serial.available() != N_SENSORS * 4 && i < 200)
                {
                    delay(1);
                    i++;
                }

                if (Serial.available() == N_SENSORS * 4 && i)
                {
                    for (unsigned char i = 0; i < N_SENSORS; i++)
                        m[current_curve][i] = Serial.parseFloat();
                }
                else
                {
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;
            }

            break;
        }
        case MODE_GET_CURVE:
        {
            switch (request)
            {
            case MODE_SM_CURVE:

                //Wait for a byte that indicates the curve number
                i = 0;
                while (Serial.available() != 1 && i < 200)
                {
                    delay(1);
                    i++;
                }

                if (Serial.available() == 1)
                {
                    current_curve = Serial.read();

                    if (current_curve > N_CURVES)
                    {
                        mode = MODE_READY;
                        submode = MODE_SM_DEFAULT;
                        Serial.write(RESP_ERR);
                        Serial.write(ERR_INDEX_OUT_OF_BOUNDS);
                    }
                    else
                    {
                        //Next step
                        Serial.write(RESP_OK);
                        submode = MODE_SM_SC_POINT;
                        current_index = 0;
                    }
                }
                else
                {
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;

            case MODE_SM_SC_POINT:
                unsigned char rqnext = 0;

                i = 0;
                while (Serial.available() != 1 && i < 200)
                {
                    delay(1);
                    i++;
                }

                if (Serial.available() == 1)
                {
                    rqnext = Serial.read();

                    if (rqnext == RQST_NEXT)
                    {
                        if (current_index + 1 >= CURVE_UB)
                        {
                            current_index = 0;
                            mode = MODE_READY;
                            submode = MODE_SM_DEFAULT;
                            Serial.write(RESP_END);
                        }
                        else
                        {
                            serialWriteFloat(cdta[current_curve][current_index].temp);
                            Serial.write(cdta[current_curve][current_index].dc);
                            current_index++;
                        }
                    }
                }
                else
                {
                    Serial.write(RESP_ERR);
                    Serial.write(ERR_TIMEOUT);
                }

                break;
            }
        }
    }
}

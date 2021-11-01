using System.Text;

namespace CustomFanController
{
    internal static class Protocol
    {
        internal const int BufferSize = 64;

        internal const int Timeout = 5000;
        internal static readonly TimeSpan CommandAnswerTimeout = TimeSpan.FromSeconds(5);

        public static class HandShake
        {
            internal const int BaudRate = 115200;
            internal const int AttemptsToConnect = 5;
            internal const string ResponsePrefixHandShake = "KyoudaiKen FCNG";
            internal static readonly byte[] ResponsePrefixHandShakeBytes = Encoding.ASCII.GetBytes(ResponsePrefixHandShake);
        }

        ////REQUEST BYTES////
        public static class Request
        {
            internal const byte RQST_IDENTIFY = 0xF0;
            internal const byte RQST_CAPABILITIES = 0xFC;

            internal const byte RQST_GET_CURVE = 0xAA;
            internal const byte RQST_GET_MATRIX = 0xAB;
            internal const byte RQST_GET_CAL_RESISTRS = 0xAC;
            internal const byte RQST_GET_CAL_OFFSETS = 0xAD;
            internal const byte RQST_GET_CAL_SH_COEFFS = 0xAE;
            internal const byte RQST_GET_PINS = 0xAF;
            internal const byte RQST_GET_SENSOR_READINGS = 0xBA;
            internal const byte RQST_GET_EERPOM_HEALTH = 0xE0;
            internal const byte RQST_GET_ALL_SENSORS = 0xBD;
            
            internal const byte RQST_SET_CURVE = 0xA0;
            internal const byte RQST_SET_MATRIX = 0xA1;
            internal const byte RQST_SET_ID = 0xA2;
            internal const byte RQST_SET_CAL_RESISTRS = 0xA3;
            internal const byte RQST_SET_CAL_OFFSETS = 0xA4;
            internal const byte RQST_SET_CAL_SH_COEFFS = 0xA5;
            internal const byte RQST_SET_PINS = 0xA6;

            internal const byte RQST_WRITE_TO_EEPROM = 0xDD;
            internal const byte RQST_READ_FROM_EEPROM = 0xDF;
        }

        ////RESPONSE BYTES////
        public static class Status
        {
            internal const byte RESP_OK = 0x01;
            internal const byte RESP_ERR = 0xFF;
            internal const byte RESP_WRN = 0xB6;
        }

        ////RESPONSE ERRORS////
        public static class Error
        {
            internal const byte ERR_INDEX_OUT_OF_BOUNDS = 0x10;
            internal const byte ERR_TEMP_HIGHER_THAN_HUNDRED = 0x11;
            internal const byte ERR_DUTY_CYCLE_OUT_OF_RANGE = 0x12;
            internal const byte ERR_TIMEOUT = 0xCC;
            internal const byte ERR_EEPROM = 0xEE;
        }
    }
}

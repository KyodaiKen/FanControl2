using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using RJCP.IO.Ports;

namespace comtest
{
    public class FanControllerOld
    {
        ////ID MESSAGE////
        private const string IDMSG = "KyoudaiKen FCNG";
        ////REQUEST BYTES////
        private const byte RQST_IDENTIFY = 0xF0;
        private const byte RQST_CAPABILITIES = 0xFC;
        private const byte RQST_SET_CURVE = 0xA0;
        private const byte RQST_SET_MATRIX = 0xA1;
        private const byte RQST_GET_CURVE = 0xAA;
        private const byte RQST_GET_MATRIX = 0xAB;
        private const byte RQST_GET_SENSORS = 0xBA;
        private const byte RQST_WRITE_TO_EEPROM = 0xDD;
        private const byte RQST_READ_FROM_EEPROM = 0xDF;
        private const byte RQST_NEXT = 0xFA;
        private const byte RQST_END = 0xEF;

        ////RESPONSE BYTES////
        private const byte RESP_OK = 0x01;
        private const byte RESP_ERR = 0xFF;
        private const byte RESP_WRN = 0xB6;
        private const byte RESP_END = 0xFE;

        ////RESPONSE ERRORS////
        private const byte ERR_INDEX_OUT_OF_BOUNDS = 0x10;
        private const byte ERR_TEMP_HIGHER_THAN_HUNDRED = 0x11;
        private const byte ERR_DUTY_CYCLE_OUT_OF_RANGE = 0x12;
        private const byte ERR_TIMEOUT = 0xCC;
        private const byte ERR_EEPROM = 0xEE;
        private const byte WRN_MAX_PONTS_REACHED = 0xC4;

        ////SERIAL PARAMETERS////
        private const int baudrate = 115200;
        private const int timeout = 500;

        //Curve point struct
        struct CurvePoint
        {
            float temp;
            byte duty_cycle;
        }

        private SerialPortStream _port;

        public string Name { get; set; }

        public FanControllerOld(string portPath)
        {
            uint l = 0;
            _port = new SerialPortStream(portPath, baudrate);
            _port.Open();

            //Wait until data arrived, we expect 15 bytes
            l = 0;
            while (_port.BytesToRead == 0)
            {
                Thread.Sleep(10);
                l++;
                if (l == timeout)
                {
                    _port.Close();
                    _port.Dispose();
                    _port = null;
                    throw new OperationCanceledException("Timeout");
                }
            }

            string ident = _port.ReadExisting();
            Console.WriteLine("--- Hello message start --- ");
            Console.WriteLine(ident);
            Console.WriteLine("--- Hello message end --- ");

            if (!ident.Contains(IDMSG))
            {
                _port.Close();
                _port.Dispose();
                _port = null;
                throw new OperationCanceledException("Not a fan controller!");
            }
        }
    }

    public class FanContManager
    {
        private Dictionary<int, FanControllerOld> _controllers;
        public Dictionary<int, FanControllerOld> Controllers { get { return _controllers; } }
        public FanContManager()
        {
            //Enumerate controllers "KyoudaiKen FCNG"
            _controllers = new Dictionary<int, FanControllerOld>();
            foreach (string serialPortPath in SerialPortStream.GetPortNames())
            {
                Console.WriteLine($"--> Probing port {serialPortPath}...");
                try
                {
                    FanControllerOld candidate = new FanControllerOld(serialPortPath);
                    //If there was no errors thrown, add it to our collection.
                    _controllers.Add(_controllers.Count + 1, candidate);
                    Console.WriteLine($"Controller on serial port {serialPortPath} added to controller pool");
                }
                catch (Exception e)
                {
                    Console.WriteLine($"Ignored device on port {serialPortPath} because of following exception: ");
                    Console.WriteLine(e.ToString());
                }
            }
            Console.WriteLine($"--> Gathered {_controllers.Count} KyoudaiKen FanControl NextGen controller" + (_controllers.Count != 1 ? "s!" : "!"));
        }
    }
}

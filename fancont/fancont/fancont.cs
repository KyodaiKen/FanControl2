using System;
using System.IO.Ports;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace fanset
{
    class fancont
    {
        public Dictionary<uint, SerialPort> Controllers { get; set; }
        public fancont()
        {
            foreach (string strPort in SerialPort.GetPortNames())
            {
                var port = new SerialPort(strPort, 115200);
                port.Open();

                if (port.IsOpen)
                {
                    //check if this is a fan controller and note it's name and add it to the Controllers dictionary
                    uint _contrID = 0xFD07;
                    Controllers.Add(_contrID, port);
                }

            }
        }
    }
}
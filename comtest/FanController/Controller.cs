using System;
using System.IO.Ports;
using System.Threading.Tasks;

namespace FanController
{
    public class Controller
    {
        public delegate void StatusUpdatedEvent(string DeviceId, string Data);
        public event StatusUpdatedEvent? StatusUpdated;

        public delegate void SettingsUpdatedEvent(string DeviceId, string Data);
        public event SettingsUpdatedEvent? SettingsUpdated;

        private readonly SerialPort SerialPort;
        private bool Listening;

        public string DeviceName { get; private set; }

        internal Controller(SerialPort SerialPort, string DeviceName)
        {
            this.SerialPort = SerialPort;

            this.DeviceName = DeviceName;
        }

        public void StartListening()
        {
            if (Listening)
            {
                return;
            }

            if (!SerialPort.IsOpen)
            {
                SerialPort.Open();
            }

            Listening = true;

            // Non awaited so it works in other thread

            Task.Run(() =>
            {
                while (Listening)
                {
                    var data = SerialPort.ReadLine();

                    if (data.StartsWith(Constants.ResponsePrefixStatus))
                    {
                        StatusUpdated?.Invoke(DeviceName, data[Constants.ResponsePrefixStatus.Length..]);
                    }
                    else if (data.StartsWith(Constants.ResponsePrefixSettings))
                    {
                        // Process Commands
                        SettingsUpdated?.Invoke(DeviceName, data[Constants.ResponsePrefixSettings.Length..]);
                    }
                }
            });
        }

        public void StopListening()
        {
            if (!Listening)
            {
                return;
            }

            if (SerialPort.IsOpen)
            {
                SerialPort.Close();
            }

            Listening = false;
        }

#warning need proper implementation and to process the data recived from each command
        public void SetNewCurve(string data)
        {
            throw new NotImplementedException();

            SerialPort.WriteLine("NewData");
        }

        public void GetCurrentCurve()
        {
            throw new NotImplementedException();

            SerialPort.WriteLine("NewData");
        }

        public void SetName(string newName)
        {
            SerialPort.WriteLine($"Name{newName}");
        }

        public void GetName(string newName)
        {
            SerialPort.WriteLine($"Name?");
        }
    }
}
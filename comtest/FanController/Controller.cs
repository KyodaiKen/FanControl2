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

        public string DeviceId { get; private set; }

        internal Controller(SerialPort SerialPort, string DeviceId)
        {
            this.SerialPort = SerialPort;
            this.DeviceId = DeviceId;
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
                        StatusUpdated?.Invoke(DeviceId, data[Constants.ResponsePrefixStatus.Length..]);
                    }
                    else if (data.StartsWith(Constants.ResponsePrefixSettings))
                    {
                        SettingsUpdated?.Invoke(DeviceId, data[Constants.ResponsePrefixSettings.Length..]);
                    }
                    else if (data.StartsWith(Constants.ResponsePrefixHandShake))
                    {
                        var val = data[Constants.ResponsePrefixHandShake.Length..];
                        DeviceId = val;
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

#warning need proper implementation
        public void SetNewCurve(string data)
        {
            throw new NotImplementedException();

            SerialPort.WriteLine("NewData");
        }

        public void SetName(string newName)
        {
            SerialPort.WriteLine($"Name{newName}");
        }
    }
}
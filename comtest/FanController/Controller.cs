using System.IO.Ports;

namespace FanController
{
    public class Controller
    {
        public delegate void SensorsUpdateEvent(string DeviceId, object Data);
        public event SensorsUpdateEvent? OnSensorsUpdate;

        public delegate void ErrorEvent(string DeviceId, object Data);
        public event ErrorEvent? OnError;

        public delegate void WarningEvent(string DeviceId, object Data);
        public event WarningEvent? OnWarning;

        private readonly SerialPort SerialPort;
        private bool Listening;

        private readonly EventWaitHandle waitHandle = new AutoResetEvent(true);

        public string DeviceName { get; private set; }

        private static readonly Dictionary<byte, byte[]> CommandAnswers = new();

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

            var readBuffer = new byte[64];

            Task.Run(() =>
            {
                while (Listening)
                {
                    // Read till the patter is found, use if needed to process larger inputs that needs several packages to arrive
                    //var data = SerialPort.ReadTo("EOF");

                    var data = SerialPort.Read(readBuffer, 0, readBuffer.Length);

                    if (data < 1)
                    {
                        OnError?.Invoke(DeviceName, "No data was read");
                    }

                    // Status Byte
                    var status = readBuffer[0];

                    if (status == Commands.Status.RESP_ERR)
                    {
                        OnError?.Invoke(DeviceName, readBuffer);
                        continue;
                    }

                    if (status == Commands.Status.RESP_WRN)
                    {
                        OnWarning?.Invoke(DeviceName, readBuffer);
                    }

                    // Expected Behaviour
                    //if (status == Commands.Status.RESP_OK)
                    //{
                    //    OnWarning?.Invoke(DeviceName, readBuffer);
                    //}

                    // Kind of response Byte
                    var kind = readBuffer[1];

                    if (kind == Commands.Request.RQST_GET_SENSORS)
                    {
                        OnSensorsUpdate?.Invoke(DeviceName, readBuffer);
                    }
                    else
                    {
                        if (!CommandAnswers.ContainsKey(kind))
                        {
                            CommandAnswers.Add(kind, readBuffer);
                        }
                        else
                        {
                            CommandAnswers[kind] = readBuffer;
                        }
                        waitHandle.Set();
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

#warning needs to be tested, seriously, dont use this
        public void SetNewCurve(Curve data)
        {
            throw new NotImplementedException();

            var bin = data.ConvertToBinary();

            SerialPort.Write(bin, 0, bin.Length);
        }

        public Curve GetCurrentCurve()
        {
            const byte commandKey = Commands.Request.RQST_GET_CURVE;
            SerialPort.SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    // Convert data to curve

                    CommandAnswers.Remove(commandKey);
                    return new Curve();
                }
            }
        }
    }
}
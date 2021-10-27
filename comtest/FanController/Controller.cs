using RJCP.IO.Ports;

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

        private readonly SerialPortStream SerialPort;
        private bool Listening;

        private readonly EventWaitHandle waitHandle = new AutoResetEvent(true);

        public string DeviceName { get; private set; }

        private static readonly Dictionary<byte, byte[]> CommandAnswers = new();

        internal Controller(SerialPortStream SerialPort, string DeviceName)
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

            var readBuffer = new byte[Protocol.BufferSize];

            // Send it to other thread so it doesn't block the main thread
            Task.Run(async () =>
            {
                while (Listening)
                {
                    // Proposal => Read till ending pattern is found, use if needed to process larger inputs that needs several packages to arrive

                    var readTask = SerialPort.ReadAsync(readBuffer).AsTask();

                    if (await Task.WhenAny(readTask, Task.Delay(Protocol.HandShake.Timeout)) != readTask)
                    {
                        OnWarning?.Invoke(DeviceName, "Timeout");
                        continue;
                    }

                    var data = await readTask;

                    if (data < 1)
                    {
                        OnError?.Invoke(DeviceName, "No data was read");
                    }

                    // Status Byte
                    var status = readBuffer[0];

                    if (status == Protocol.Status.RESP_ERR)
                    {
                        OnError?.Invoke(DeviceName, readBuffer);
                        // Abort Operation
                        continue;
                    }

                    if (status == Protocol.Status.RESP_WRN)
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

                    if (kind == Protocol.Request.RQST_GET_SENSORS)
                    {
#warning need proper deserialization
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

                        // new data recived, send pulse so listeners can check if it's what they need
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

        public async Task SendCommand(byte command, byte[]? payload = null)
        {
            const int maxPayloadSize = Protocol.BufferSize - 1;
            var preparedCommand = new byte[Protocol.BufferSize];
            preparedCommand[0] = command;

            if (payload is not null)
            {
                if (payload.Length > Protocol.BufferSize - 1)
                {
                    throw new ArgumentException($"Payload too big, currently is '{payload.Length}', and the max allowed is '{maxPayloadSize}'");
                }

                Array.Copy(payload, 0, preparedCommand, 1, payload.Length);
            }

            await SerialPort.SendCommand(preparedCommand);
        }

#warning needs to be tested, seriously, dont use this for now
        public async Task SetNewCurve(Curve data)
        {
            var payload = data.ConvertToBinary();

            await SendCommand(Protocol.Request.RQST_SET_CURVE, payload);
        }

        public async Task<Curve> GetCurrentCurve()
        {
            const byte commandKey = Protocol.Request.RQST_GET_CURVE;
            await SendCommand(commandKey);

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
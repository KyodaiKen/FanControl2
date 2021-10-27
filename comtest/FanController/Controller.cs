using RJCP.IO.Ports;

namespace FanController
{
    public class Controller
    {
        public delegate void SensorsUpdateEvent(byte DeviceId, object Data);
        public event SensorsUpdateEvent? OnSensorsUpdate;

        public delegate void ErrorEvent(byte DeviceId, object Data);
        public event ErrorEvent? OnError;

        public delegate void WarningEvent(byte DeviceId, object Data);
        public event WarningEvent? OnWarning;

        private readonly SerialPortStream SerialPort;
        private bool Listening;

        private readonly EventWaitHandle waitHandle = new AutoResetEvent(true);

        public byte DeviceID { get; private set; }
        public string DeviceName { get; set; }
        public DeviceCapabilities? DeviceCapabilities { get; private set; }

        private static readonly Dictionary<byte, byte[]> CommandAnswers = new();

        internal Controller(SerialPortStream SerialPort, byte DeviceID)
        {
            this.SerialPort = SerialPort;
            this.DeviceID = DeviceID;
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
                        OnWarning?.Invoke(DeviceID, "Timeout");
                        continue;
                    }

                    var data = await readTask;

                    if (data < 1)
                    {
                        OnError?.Invoke(DeviceID, "No data was read");
                    }

                    // Status Byte
                    var status = readBuffer[0];

                    if (status == Protocol.Status.RESP_ERR)
                    {
                        OnError?.Invoke(DeviceID, readBuffer);
                        // Abort Operation
                        continue;
                    }

                    if (status == Protocol.Status.RESP_WRN)
                    {
                        OnWarning?.Invoke(DeviceID, readBuffer);
                    }

                    // Expected Behaviour
                    //if (status == Commands.Status.RESP_OK)
                    //{
                    //    OnWarning?.Invoke(DeviceID, readBuffer);
                    //}

                    // Kind of response Byte
                    var kind = readBuffer[1];

                    if (kind == Protocol.Request.RQST_GET_SENSORS)
                    {
#warning need proper deserialization
                        OnSensorsUpdate?.Invoke(DeviceID, readBuffer);
                    }
                    else
                    {
                        if (!CommandAnswers.ContainsKey(kind))
                        {
                            CommandAnswers.Add(kind, readBuffer[2..readBuffer.Length]);
                        }
                        else
                        {
                            CommandAnswers[kind] = readBuffer[2..readBuffer.Length];
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
            int payload_len = 0;
            if (payload != null) payload_len = payload.Length;

            var preparedCommand = new byte[payload_len + 1];
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

        public async Task GetDeviceCapabilities()
        {
            const byte commandKey = Protocol.Request.RQST_CAPABILITIES;

            Console.WriteLine($"Sending get capabilities command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine($"Received data!");

                    DeviceCapabilities = new DeviceCapabilities()
                    {
                        NumberOfSensors = CommandAnswers[commandKey][0],
                        NumberOfChannels = CommandAnswers[commandKey][1]
                    };

                    break;
                }
            }
        }

        public async Task<Curve> GetCurve(byte channelId)
        {
            const byte commandKey = Protocol.Request.RQST_GET_CURVE;
            byte[] payload = new byte[1];
            payload[0] = channelId;

            Console.WriteLine($"Sending get curve command for curve ID {channelId} to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine($"Received data!");

                    // Convert data to curve
                    byte len = CommandAnswers[commandKey][0];
                    byte[] data = CommandAnswers[commandKey][1..]; //Remove the length from so we only have the curve data.

                    Console.WriteLine($"Data length: {len}");

                    CurvePoint[] cps = new CurvePoint[len];
                    //Those offsets are headache to the power of 1000

                    //Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < len; i++)
                    {
                        int di = i * 5;
                        float temp = BitConverter.ToSingle(data[di..(di + 4)]);
                        byte dc = data[(di+4)..(di + 5)][0];
                        cps.Append(new CurvePoint(temp, dc));

                        Console.WriteLine($"Curve point {i}: {temp} => {dc} added!");
                    }

                    CommandAnswers.Remove(commandKey);
                    Curve curve = new Curve();
                    curve.ChannelId = channelId;
                    curve.CurvePoints = cps;
                    return curve;
                }
            }
        }

        public async Task<Curve> GetMatrix(byte channelId)
        {

            if (DeviceCapabilities == null) throw new NotSupportedException("Device capabilities unknown at this point!");

            const byte commandKey = Protocol.Request.RQST_GET_MATRIX;
            byte[] payload = new byte[1];
            payload[0] = channelId;

            Console.WriteLine($"Sending get matrix command for channel ID {channelId} to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine($"Received data!");

                    // Convert data to curve
                    byte[] data = CommandAnswers[commandKey]; //Remove the length from so we only have the curve data.

                    Console.WriteLine($"Data length: {DeviceCapabilities.NumberOfChannels} (from device capabilities), length from data: {data.Length/4}");

                    //Those offsets are headache to the power of 1000
                    //Deserializing could be made better by just using a struct array and copy the data into it...
                    /*
                    for (int i = 0; i < len; i++)
                    {
                        int di = i * 5;
                        float temp = BitConverter.ToSingle(data[di..(di + 4)]);
                        byte dc = data[(di + 4)..(di + 5)][0];
                        cps.Append(new CurvePoint(temp, dc));

                        Console.WriteLine($"Curve point {i}: {temp} => {dc} added!");
                    }
                   

                    CommandAnswers.Remove(commandKey);
                    Curve curve = new Curve();
                    curve.ChannelId = channelId;
                    curve.CurvePoints = cps;
                    return curve; */
                    return null;
                }
            }
        }
    }
}
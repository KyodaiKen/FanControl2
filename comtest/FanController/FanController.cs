using RJCP.IO.Ports;

namespace FanController
{
    public class FanController
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

        private static readonly Dictionary<byte, byte[]> CommandAnswers = new();

        public byte DeviceID { get; set; }
        public string DeviveName { get; set; }

        public DeviceCapabilities DeviceCapabilities { get; set; }
        public ControllerConfig ControllerConfig { get; set; }

        internal FanController(SerialPortStream SerialPort, byte DeviceID)
        {
            this.SerialPort = SerialPort;
            this.DeviceID = DeviceID;
        }

        #region "Listener"
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

                    if (!CommandAnswers.ContainsKey(kind))
                    {
                        CommandAnswers.Add(kind, readBuffer[2..data]);
                    }
                    else
                    {
                        CommandAnswers[kind] = readBuffer[2..data];
                    }

                    // new data recived, send pulse so listeners can check if it's what they need
                    waitHandle.Set();
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
        #endregion

        #region "GET"
        public async Task<DeviceCapabilities> GetDeviceCapabilities()
        {
            const byte commandKey = Protocol.Request.RQST_CAPABILITIES;

            Console.WriteLine($"Sending get capabilities command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);

            DeviceCapabilities dc;

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine($"Received data!");

                    dc = new DeviceCapabilities()
                    {
                        NumberOfSensors = CommandAnswers[commandKey][0],
                        NumberOfChannels = CommandAnswers[commandKey][1]
                    };

                    break;
                }
            }

            return dc;
        }

        public async Task<ControllerConfig> GetControllerConfig()
        {
            byte commandKey = Protocol.Request.RQST_GET_CAL_RESISTRS;
            
            ControllerConfig cc = new ControllerConfig();

            cc.ThermalSensors = new ThermalSensor[DeviceCapabilities.NumberOfSensors];
            byte[] data;

            Console.WriteLine($"Sending get calibration resistor values command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    data = CommandAnswers[commandKey];
                    if (data.Length != DeviceCapabilities.NumberOfSensors * 4) throw new ArgumentException("Controller returned the wrong number of sensor information");
                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                    {
                        if(cc.ThermalSensors[i] == null) cc.ThermalSensors[i] = new ThermalSensor();
                        cc.ThermalSensors[i].CalibrationResistorValue = BitConverter.ToSingle(data.AsSpan()[(i * 4)..(i * 4 + 4)]);
                        Console.WriteLine($"Retrieved resistor value {cc.ThermalSensors[i].CalibrationResistorValue} for sensor ID {i}");
                    }

                    break;
                }
            }

            commandKey = Protocol.Request.RQST_GET_CAL_OFFSETS;
            Console.WriteLine($"Sending get calibration offset values command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    data = CommandAnswers[commandKey];
                    if (data.Length != DeviceCapabilities.NumberOfSensors * 4) throw new ArgumentException("Controller returned the wrong number of sensor information");
                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                    {
                        cc.ThermalSensors[i].CalibrationOffset = BitConverter.ToSingle(data.AsSpan()[(i * 4)..(i * 4 + 4)]);
                        Console.WriteLine($"Retrieved offset value {cc.ThermalSensors[i].CalibrationOffset} for sensor ID {i}");
                    }

                    break;
                }
            }

            commandKey = Protocol.Request.RQST_GET_CAL_SH_COEFFS;
            Console.WriteLine($"Sending get calibration Steinhart-Hart coefficients command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);
            
            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    data = CommandAnswers[commandKey];
                    if (data.Length != DeviceCapabilities.NumberOfSensors * 12) throw new ArgumentException("Controller returned the wrong number of sensor information");
                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                    {
                        float[] values = new float[3];
                        int di = i * 12;
                        for (int c = 0; c < 3; c++)
                        {
                            int idc = c * 4;
                            values[c] = BitConverter.ToSingle(data.AsSpan()[(di + idc)..(di + idc + 4)]);
                        }
                        cc.ThermalSensors[i].CalibrationSteinhartHartCoefficients = values;
                        Console.WriteLine($"Retrieved Steinhart-Hart coefficients {string.Join(" ", cc.ThermalSensors[i].CalibrationSteinhartHartCoefficients)} for sensor ID {i}");
                    }

                    break;
                }
            }

            commandKey = Protocol.Request.RQST_GET_PINS;
            Console.WriteLine($"Sending get thermistor and PWM channel pin numbers command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);

            cc.PWMChannels = new PWMChannel[DeviceCapabilities.NumberOfChannels];

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    data = CommandAnswers[commandKey];
                    if (data.Length != DeviceCapabilities.NumberOfSensors + DeviceCapabilities.NumberOfChannels) throw new ArgumentException("Controller returned the wrong number of sensor information");
                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                    {
                        cc.ThermalSensors[i].Pin = data[i];
                        Console.WriteLine($"Retrieved pin number for for sensor ID {i}: {data[i]}");
                    }

                    for (int i = 0; i < DeviceCapabilities.NumberOfChannels; i++)
                    {
                        cc.PWMChannels[i] = new PWMChannel();
                        cc.PWMChannels[i].Pin = data[i + 3];
                        Console.WriteLine($"Retrieved pin number for for PWM channel ID {i}: {data[i + 3]}");
                    }

                    break;
                }
            }

            return cc;
        }

        public async Task<Readings> GetReadings()
        {
            Readings Readings = new Readings();

            const byte commandKey = Protocol.Request.RQST_GET_SENSOR_READINGS;
            Console.WriteLine($"Sending get readings command to controller with the id {this.DeviceID}...");
            await SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine($"Received data!");

                    // Convert data to curve
                    byte[] data = CommandAnswers[commandKey];

                    //Check data length and validate it
                    if (data.Length != DeviceCapabilities.NumberOfSensors * 4 + DeviceCapabilities.NumberOfChannels * 8) throw new ArgumentException("Data length does not match device capabilities.");

                    Console.WriteLine($"Data length: {data.Length}");
                    //Those offsets are headache to the power of 1000

                    Readings.TemperatureReadings = new TemperatureReading[DeviceCapabilities.NumberOfSensors];

                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < Readings.TemperatureReadings.Length; i++)
                    {
                        int di = i * 4;
                        Readings.TemperatureReadings[i] = new TemperatureReading();
                        Readings.TemperatureReadings[i].Temperature = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]);

                        Console.WriteLine($"Temperature sensor ... {i}: {Readings.TemperatureReadings[i].Temperature }");
                    }

                    Readings.ChannelReadings = new ChannelReading[DeviceCapabilities.NumberOfChannels];

                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < Readings.TemperatureReadings.Length; i++)
                    {
                        int di = i * 8 + 12;
                        Readings.ChannelReadings[i] = new ChannelReading();
                        Readings.ChannelReadings[i].MatrixResult = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]);
                        Readings.ChannelReadings[i].DutyCycle = BitConverter.ToSingle(data.AsSpan()[(di + 4)..(di + 8)]);

                        Console.WriteLine($"Matrix results channel {i}: {Readings.ChannelReadings[i].MatrixResult}");
                        Console.WriteLine($"Duty cycle channel ... {i}: {Readings.ChannelReadings[i].DutyCycle}");
                    }

                    CommandAnswers.Remove(commandKey);

                    return Readings;
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

                    #warning Deserializing could be made better by just using a struct array and copy the data into it...
                    for (int i = 0; i < len; i++)
                    {
                        int di = i * 5;
                        float temp = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]);
                        byte dc = data[(di+4)..(di + 5)][0];
                        cps.Append(new CurvePoint()
                        {
                            Temperature = temp,
                            DutyCycle = dc
                        });

                        Console.WriteLine($"Curve point {i}: {temp} => {dc} added!");
                    }

                    CommandAnswers.Remove(commandKey);
                    return new Curve()
                    {
                        ChannelId = channelId,
                        CurvePoints = cps
                    };
                }
            }
        }

        public async Task<Matrix> GetMatrix(byte channelId)
        {
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

                    if (data.Length / 4 != DeviceCapabilities.NumberOfChannels)
                        throw new InvalidDataException("Returned data lentgh has a different length than expected according to" +
                            "the number of channels on this controller.");

                    //Those offsets are headache to the power of 1000
                    #warning Deserializing could be made better by just using a struct array and copy the data into it...

                    float[] matrix = new float[DeviceCapabilities.NumberOfChannels];

                    for (int i = 0; i < matrix.Length; i++)
                    {
                        int di = i * 4;
                        matrix[i] = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]);

                        Console.Write($"{matrix[i]} ");
                    }
                    Console.WriteLine();

                    return new Matrix()
                    {
                        ChannelId = channelId,
                        MatrixPoints = matrix
                    };
                }
            }
        }
        #endregion

        #region "SET"
        public async Task<bool> SetCurve(byte curveID, Curve curve)
        {
            const byte commandKey = Protocol.Request.RQST_SET_CURVE;

            Console.WriteLine($"Sending set curve command to controller with the id {DeviceID}...");

            int ncp = curve.CurvePoints.Count();
            byte[] payload = new byte[ncp * 5 + 2];

            payload[0] = curveID;
            payload[1] = (byte)curve.CurvePoints.Count();

            for (int i = 0; i < ncp; i++)
            {
                Array.Copy(BitConverter.GetBytes(curve.CurvePoints[i].Temperature), 0, payload, i * 5 + 2, 4);
                payload[i * 5 + 6] = curve.CurvePoints[i].DutyCycle;
            }

            Console.WriteLine($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            return true;
        }

        public async Task<bool> SetMatrix(byte curveID, Matrix matrix)
        {
            const byte commandKey = Protocol.Request.RQST_SET_MATRIX;

            Console.WriteLine($"Sending set matrix command to controller with the id {DeviceID}...");

            byte[] payload = new byte[13];

            payload[0] = curveID;

            for (int i = 0; i < 3; i++)
            {
                Array.Copy(BitConverter.GetBytes(matrix.MatrixPoints[i]), 0, payload, i * 4 + 1, 4);
            }

            Console.WriteLine($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            return true;
        }

        public async Task<bool> SetControllerConfig(ControllerConfig ControllerConfig)
        {
            byte commandKey = Protocol.Request.RQST_SET_CAL_RESISTRS;

            Console.WriteLine($"Sending set calibration resistor values command to controller with the id {DeviceID}...");

            byte[] payload = new byte[DeviceCapabilities.NumberOfSensors * 4];

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationResistorValue), 0, payload, i * 4, 4);
            }

            Console.WriteLine($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            commandKey = Protocol.Request.RQST_SET_CAL_OFFSETS;
            Console.WriteLine($"Sending set calibration offsets command to controller with the id {DeviceID}...");

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationOffset), 0, payload, i * 4, 4);
            }

            Console.WriteLine($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            commandKey = Protocol.Request.RQST_SET_CAL_SH_COEFFS;
            Console.WriteLine($"Sending set calibration Steinhart-Hart coefficients command to controller with the id {DeviceID}...");

            payload = new byte[DeviceCapabilities.NumberOfSensors * 12];

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationSteinhartHartCoefficients[0]), 0, payload, i * 12, 4);
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationSteinhartHartCoefficients[1]), 0, payload, i * 12 + 4, 4);
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationSteinhartHartCoefficients[2]), 0, payload, i * 12 + 8, 4);
            }

            Console.WriteLine($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            commandKey = Protocol.Request.RQST_SET_PINS;
            Console.WriteLine($"Sending set pins command to controller with the id {DeviceID}...");

            payload = new byte[DeviceCapabilities.NumberOfChannels + DeviceCapabilities.NumberOfSensors];

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                payload[i] = ControllerConfig.ThermalSensors[i].Pin;
            }

            for (int i = 0; i < DeviceCapabilities.NumberOfChannels; i++)
            {
                payload[i + 3] = ControllerConfig.PWMChannels[i].Pin;
            }

            Console.WriteLine($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            return true;
        }
        #endregion

        #region "Requests"
        public async Task<bool> RequestStoreToEEPROM()
        {
            const byte commandKey = Protocol.Request.RQST_WRITE_TO_EEPROM;

            Console.WriteLine($"Sending RQST_WRITE_TO_EEPROM to controller with the id {DeviceID}...");
            await SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            return true;
        }

        public async Task<bool> RequestReadFromEEPROM()
        {
            const byte commandKey = Protocol.Request.RQST_READ_FROM_EEPROM;

            Console.WriteLine($"Sending RQST_READ_FROM_EEPROM to controller with the id {DeviceID}...");
            await SendCommand(commandKey);

            while (true)
            {
                waitHandle.WaitOne();

                if (CommandAnswers.ContainsKey(commandKey))
                {
                    Console.WriteLine("OK!");
                    break;
                }
            }

            return true;
        }
        #endregion "Requests"
    }
}
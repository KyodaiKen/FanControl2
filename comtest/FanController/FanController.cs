using Microsoft.Extensions.Logging;
using RJCP.IO.Ports;
using System.Text;

namespace CustomFanController
{
    public class FanController : IDisposable
    {
        #region "Events"
        public delegate void SensorsUpdateEvent(byte DeviceId, object Data);
        public event SensorsUpdateEvent? OnSensorsUpdate;

        public delegate void ErrorEvent(byte DeviceId, object Data);
        public event ErrorEvent? OnError;

        public delegate void WarningEvent(byte DeviceId, object Data);
        public event WarningEvent? OnWarning;

        #endregion "Events"

        #region Private locals

        private readonly SerialPortStream SerialPort;
        // Message queue, it helps to sync the answers to the commands sent to the controller
        private static readonly Dictionary<byte, byte[]> CommandAnswers = new();
        private bool Listening;
        private bool disposedValue;
        private readonly EventWaitHandle waitHandle = new AutoResetEvent(true);

        private readonly ILogger? Logger;
        #endregion

        #region Properties
        public byte DeviceID { get; set; }
        public string DeviceName { get; set; }

        public bool EEPROM_OK { get; private set; }

        public DeviceCapabilities DeviceCapabilities { get; set; }
        public ControllerConfig ControllerConfig { get; set; }
        public FanControlConfig FanControlConfig { get; set; }
        #endregion

        internal FanController(SerialPortStream SerialPort, byte DeviceID, ILogger? Logger = null)
        {
            this.Logger = Logger;
            this.SerialPort = SerialPort;
            this.DeviceID = DeviceID;
        }

        #region Listener
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

                    if (await Task.WhenAny(readTask, Task.Delay(Protocol.Timeout)) != readTask)
                    {
                        const string errmsg = "Data receive timeout";
                        Logger?.LogWarning(errmsg);
                        OnWarning?.Invoke(DeviceID, errmsg);
                        continue;
                    }

                    var data = await readTask;

                    Logger?.LogDebug($"Data Received {Convert.ToHexString(readBuffer[0..data])}");

                    if (data < 1)
                    {
                        const string errmsg = "No data received!";
                        Logger?.LogError(errmsg);
                        OnError?.Invoke(DeviceID, errmsg);
                        //Abort operation
                        continue;
                    }

                    // Status Byte
                    var status = readBuffer[0];

                    if (status == Protocol.Status.RESP_ERR)
                    {
                        Logger?.LogError("Error received");
                        OnError?.Invoke(DeviceID, readBuffer);
                        // Abort Operation
                        continue;
                    }

                    if (status == Protocol.Status.RESP_WRN)
                    {
                        const string errmsg = "Warning received";
                        Logger?.LogWarning(errmsg, readBuffer);
                        OnWarning?.Invoke(DeviceID, readBuffer);
                    }

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

        internal async Task InitializeDevice()
        {
            StartListening();

            DeviceCapabilities = await GetDeviceCapabilities();
            ControllerConfig = await GetControllerConfig();

            //Gather FanControlConfig
            FanControlConfig = new FanControlConfig
            {
                Curves = new Curve[DeviceCapabilities.NumberOfChannels],
                Matrixes = new Matrix[DeviceCapabilities.NumberOfChannels]
            };

            for (byte c = 0; c < DeviceCapabilities.NumberOfChannels; c++)
            {
                FanControlConfig.Curves[c] = await GetCurve(c);
                FanControlConfig.Matrixes[c] = await GetMatrix(c);
            }
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
                SerialPort.Dispose();
            }

            Listening = false;
        }
        #endregion

        #region Get Commands
        private async Task<DeviceCapabilities> GetDeviceCapabilities()
        {
            const byte commandKey = Protocol.Request.RQST_CAPABILITIES;

            Logger?.LogInformation($"Sending get capabilities command...");

            await SendCommand(commandKey);

            DeviceCapabilities dc = null;

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                dc = new DeviceCapabilities()
                {
                    NumberOfSensors = data[0],
                    NumberOfChannels = data[1]
                };

                Logger?.LogInformation($"NumberOfSensors => {data[0]}, NumberOfChannels => {data[1]}");
            });

            return dc;
        }

        public async Task<ControllerConfig> GetControllerConfig()
        {
            byte commandKey = Protocol.Request.RQST_GET_CAL_RESISTRS;

            var cc = new ControllerConfig
            {
                ThermalSensors = new ThermalSensor[DeviceCapabilities.NumberOfSensors]
            };

            Logger?.LogInformation($"Sending get calibration resistor values command...");
            await SendCommand(commandKey);
            var sb = new StringBuilder();

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                if (data.Length != DeviceCapabilities.NumberOfSensors * 4)
                {
                    throw new ArgumentException("Controller returned the wrong number of sensor information");
                }

                for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                {
                    if (cc.ThermalSensors[i] == null) cc.ThermalSensors[i] = new ThermalSensor();
                    cc.ThermalSensors[i].CalibrationResistorValue = BitConverter.ToSingle(data.AsSpan()[(i * 4)..(i * 4 + 4)]);
                    sb.AppendLine($"Retrieved resistor value {cc.ThermalSensors[i].CalibrationResistorValue} for sensor ID {i}");
                }
            });
            Logger?.LogDebug(sb.ToString());
            sb.Clear();

            commandKey = Protocol.Request.RQST_GET_CAL_OFFSETS;
            Logger?.LogInformation($"Sending get calibration offset values command...");
            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                if (data.Length != DeviceCapabilities.NumberOfSensors * 4)
                {
                    throw new ArgumentException("Controller returned the wrong number of sensor information");
                }

                for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                {
                    cc.ThermalSensors[i].CalibrationOffset = BitConverter.ToSingle(data.AsSpan()[(i * 4)..(i * 4 + 4)]);
                    sb.AppendLine($"Retrieved offset value {cc.ThermalSensors[i].CalibrationOffset} for sensor ID {i}");
                }
            });

            Logger?.LogDebug(sb.ToString());
            sb.Clear();

            commandKey = Protocol.Request.RQST_GET_CAL_SH_COEFFS;
            Logger?.LogInformation($"Sending get calibration Steinhart-Hart coefficients command...");
            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                if (data.Length != DeviceCapabilities.NumberOfSensors * 12)
                {
                    throw new ArgumentException("Controller returned the wrong number of sensor information");
                }

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
                    sb.AppendLine($"Retrieved Steinhart-Hart coefficients {string.Join(" ", cc.ThermalSensors[i].CalibrationSteinhartHartCoefficients)} for sensor ID {i}");
                }
            });

            Logger?.LogDebug(sb.ToString());
            sb.Clear();

            commandKey = Protocol.Request.RQST_GET_PINS;
            Logger?.LogInformation($"Sending get thermistor and PWM channel pin numbers command...");
            cc.PWMChannels = new PWMChannel[DeviceCapabilities.NumberOfChannels];

            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                if (data.Length != DeviceCapabilities.NumberOfSensors + DeviceCapabilities.NumberOfChannels)
                {
                    throw new ArgumentException("Controller returned the wrong number of sensor information");
                }

                for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
                {
                    cc.ThermalSensors[i].Pin = data[i];
                    sb.AppendLine($"Retrieved pin number for sensor ID {i}: {data[i]}");
                }

                for (int i = 0; i < DeviceCapabilities.NumberOfChannels; i++)
                {
                    cc.PWMChannels[i] = new PWMChannel
                    {
                        Pin = data[i + 3]
                    };
                    sb.AppendLine($"Retrieved pin number for PWM channel ID {i}: {data[i + 3]}");
                }
            });


            Logger?.LogDebug(sb.ToString());
            sb.Clear();

            //Check EEPROM health
            commandKey = Protocol.Request.RQST_GET_EERPOM_HEALTH;
            Logger?.LogInformation($"Sending get EEPROM health command...");

            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                if(data[0] == 0x01)
                {
                    EEPROM_OK = true;
                    Logger?.LogInformation("EEPROM OK!");
                }
                else
                {
                    EEPROM_OK = false;
                }
            });

            if(!EEPROM_OK)
            {
                Logger?.LogWarning($"The controller had an EEPROM error and was reset to factory defaults!");
            }

            return cc;
        }

        public async Task<Readings> GetReadings()
        {
            var Readings = new Readings();

            const byte commandKey = Protocol.Request.RQST_GET_SENSOR_READINGS;
            Logger?.LogInformation($"Sending get readings command...");
            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation($"Received data, length {data.Length}");

                var sb = new StringBuilder();

                // Convert data to curve

                //Check data length and validate it
                if (data.Length != DeviceCapabilities.NumberOfSensors * 4 + DeviceCapabilities.NumberOfChannels * 8)
                {
                    throw new ArgumentException("Data length does not match device capabilities.");
                }

                Readings.TemperatureReadings = new TemperatureReading[DeviceCapabilities.NumberOfSensors];

                for (int i = 0; i < Readings.TemperatureReadings.Length; i++)
                {
                    int di = i * 4;
                    Readings.TemperatureReadings[i] = new TemperatureReading
                    {
                        Temperature = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)])
                    };

                    sb.AppendLine($"Temperature sensor ... {i}: {Readings.TemperatureReadings[i].Temperature }");
                }

                Readings.ChannelReadings = new ChannelReading[DeviceCapabilities.NumberOfChannels];

                for (int i = 0; i < Readings.TemperatureReadings.Length; i++)
                {
                    int di = i * 8 + 12;
                    Readings.ChannelReadings[i] = new ChannelReading
                    {
                        MatrixResult = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]),
                        DutyCycle = BitConverter.ToSingle(data.AsSpan()[(di + 4)..(di + 8)])
                    };

                    sb.AppendLine($"Matrix results channel {i}: {Readings.ChannelReadings[i].MatrixResult}");
                    sb.AppendLine($"Duty cycle channel ... {i}: {Readings.ChannelReadings[i].DutyCycle}");
                }
                Logger?.LogDebug(sb.ToString()); 
            });

            return Readings;
        }

        public async Task<Curve> GetCurve(byte channelId)
        {
            const byte commandKey = Protocol.Request.RQST_GET_CURVE;
            byte[] payload = new byte[1];
            payload[0] = channelId;

            Logger?.LogInformation($"Sending get curve command for curve ID {channelId}...");
            await SendCommand(commandKey, payload);

            var curve = new Curve();

            var sb = new StringBuilder();

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (value) =>
            {
                // Convert data to curve
                byte len = value[0];
                byte[] data = value[1..]; //Remove the length from so we only have the curve data.

                sb.AppendLine($"Number of curve points: {len}");

                CurvePoint[] cps = new CurvePoint[len];
                //Those offsets are headache to the power of 1000

                for (int i = 0; i < len; i++)
                {
                    int di = i * 5;
                    float temp = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]);
                    byte dc = data[(di + 4)..(di + 5)][0];
                    cps[i] = new CurvePoint()
                    {
                        Temperature = temp,
                        DutyCycle = dc
                    };

                    sb.AppendLine($"Curve point {i}: {temp} => {dc} added!");
                }
                curve.ChannelId = channelId;
                curve.CurvePoints = cps;
            });

            Logger?.LogDebug(sb.ToString());

            return curve;
        }

        public async Task<Matrix> GetMatrix(byte channelId)
        {
            const byte commandKey = Protocol.Request.RQST_GET_MATRIX;
            byte[] payload = new byte[1];
            payload[0] = channelId;

            Logger?.LogInformation($"Sending get matrix command for channel ID {channelId}...");
            await SendCommand(commandKey, payload);

            var matrixObj = new Matrix();

            var sb = new StringBuilder();

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                //Convert data to curve
                //Remove the length from so we only have the curve data.

                sb.AppendLine($"Data length: {DeviceCapabilities.NumberOfChannels} (from device capabilities), length from data: {data.Length / 4}");

                if (data.Length / 4 != DeviceCapabilities.NumberOfChannels)
                    throw new InvalidDataException("Returned data lentgh has a different length than expected according to" +
                        "the number of channels on this controller.");

                float[] matrix = new float[DeviceCapabilities.NumberOfChannels];

                for (int i = 0; i < matrix.Length; i++)
                {
                    int di = i * 4;
                    matrix[i] = BitConverter.ToSingle(data.AsSpan()[di..(di + 4)]);

                    sb.Append($"{matrix[i]} ");
                }

                sb.AppendLine();

                matrixObj.ChannelId = channelId;
                matrixObj.MatrixPoints = matrix;

            });

            Logger?.LogDebug(sb.ToString());

            return matrixObj;
        }
        #endregion

        #region Set Commands
        public async Task<bool> SetCurve(byte curveID, Curve curve)
        {
            const byte commandKey = Protocol.Request.RQST_SET_CURVE;

            Logger?.LogInformation($"Sending set curve command...");

            int ncp = curve.CurvePoints.Length;
            byte[] payload = new byte[ncp * 5 + 2];

            payload[0] = curveID;
            payload[1] = (byte)curve.CurvePoints.Length;

            for (int i = 0; i < ncp; i++)
            {
                Array.Copy(BitConverter.GetBytes(curve.CurvePoints[i].Temperature), 0, payload, i * 5 + 2, 4);
                payload[i * 5 + 6] = curve.CurvePoints[i].DutyCycle;
            }

            Logger?.LogDebug($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            return true;
        }

        public async Task<bool> SetMatrix(byte curveID, Matrix matrix)
        {
            const byte commandKey = Protocol.Request.RQST_SET_MATRIX;

            Logger?.LogInformation($"Sending set matrix command...");

            byte[] payload = new byte[13];

            payload[0] = curveID;

            for (int i = 0; i < 3; i++)
            {
                Array.Copy(BitConverter.GetBytes(matrix.MatrixPoints[i]), 0, payload, i * 4 + 1, 4);
            }

            Logger?.LogDebug($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            return true;
        }

        public async Task<bool> SetControllerConfig(ControllerConfig ControllerConfig)
        {
            byte commandKey = Protocol.Request.RQST_SET_CAL_RESISTRS;

            Logger?.LogInformation($"Sending set calibration resistor values command...");

            byte[] payload = new byte[DeviceCapabilities.NumberOfSensors * 4];

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationResistorValue), 0, payload, i * 4, 4);
            }

            Logger?.LogDebug($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            commandKey = Protocol.Request.RQST_SET_CAL_OFFSETS;
            Logger?.LogInformation($"Sending set calibration offsets command...");

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationOffset), 0, payload, i * 4, 4);
            }

            Logger?.LogDebug($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            commandKey = Protocol.Request.RQST_SET_CAL_SH_COEFFS;
            Logger?.LogInformation($"Sending set calibration Steinhart-Hart coefficients command...");

            payload = new byte[DeviceCapabilities.NumberOfSensors * 12];

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationSteinhartHartCoefficients[0]), 0, payload, i * 12, 4);
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationSteinhartHartCoefficients[1]), 0, payload, i * 12 + 4, 4);
                Array.Copy(BitConverter.GetBytes(ControllerConfig.ThermalSensors[i].CalibrationSteinhartHartCoefficients[2]), 0, payload, i * 12 + 8, 4);
            }

            Logger?.LogDebug($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            commandKey = Protocol.Request.RQST_SET_PINS;
            Logger?.LogInformation($"Sending set pins command...");

            payload = new byte[DeviceCapabilities.NumberOfChannels + DeviceCapabilities.NumberOfSensors];

            for (int i = 0; i < DeviceCapabilities.NumberOfSensors; i++)
            {
                payload[i] = ControllerConfig.ThermalSensors[i].Pin;
            }

            for (int i = 0; i < DeviceCapabilities.NumberOfChannels; i++)
            {
                payload[i + 3] = ControllerConfig.PWMChannels[i].Pin;
            }

            Logger?.LogDebug($"Sending payload {Convert.ToHexString(payload)}");

            await SendCommand(commandKey, payload);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            return true;
        }
        #endregion

        #region Requests
        public async Task<bool> RequestStoreToEEPROM()
        {
            const byte commandKey = Protocol.Request.RQST_WRITE_TO_EEPROM;

            Logger?.LogInformation($"Sending RQST_WRITE_TO_EEPROM...");
            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            return true;
        }

        public async Task<bool> RequestReadFromEEPROM()
        {
            const byte commandKey = Protocol.Request.RQST_READ_FROM_EEPROM;

            Logger?.LogInformation($"Sending RQST_READ_FROM_EEPROM...");
            await SendCommand(commandKey);

            await DoWhenAnswerRecivedWithTimeoutAsync(commandKey, (data) =>
            {
                Logger?.LogInformation("OK!");
            });

            return true;
        }
        #endregion

        #region Helpers

        private async Task SendCommand(byte command, byte[]? payload = null)
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
                    throw new ArgumentException($"Payload '{payload.Length}' is too large. Maximum allowed is '{maxPayloadSize}'");
                }

                Array.Copy(payload, 0, preparedCommand, 1, payload.Length);
            }

            await SerialPort.SendCommand(preparedCommand);
        }

        private async Task DoWhenAnswerRecivedWithTimeoutAsync(byte commandKey, Action<byte[]> job)
        {
            var tokenSource = new CancellationTokenSource(Protocol.CommandAnswerTimeout);
            CancellationToken cancellationToken = tokenSource.Token;

            var work = Task.Run(() =>
            {
                // Were we already canceled?
                cancellationToken.ThrowIfCancellationRequested();

                while (true)
                {
                    // Poll on this property if you have to do
                    // other cleanup before throwing.
                    if (cancellationToken.IsCancellationRequested)
                    {
                        // Clean up here, then...
                        cancellationToken.ThrowIfCancellationRequested();
                    }

                    waitHandle.WaitOne();

                    if (CommandAnswers.TryGetAndRemove(commandKey, out var data))
                    {
                        job(data);
                        return;
                    }
                }
            }, tokenSource.Token); // Pass same token to Task.Run.

            try
            {
                await work;
            }
            catch
            {
                throw;
            }
            finally
            {
                tokenSource.Dispose();
                work.Dispose();
            }
        }

        #endregion  Helpers

        #region IDisposable Implementation
        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                if (disposing)
                {
                    StopListening();
                }

                // TODO: free unmanaged resources (unmanaged objects) and override finalizer
                // TODO: set large fields to null
                disposedValue = true;
            }
        }

        // // TODO: override finalizer only if 'Dispose(bool disposing)' has code to free unmanaged resources
        // ~FanController()
        // {
        //     // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        //     Dispose(disposing: false);
        // }

        public void Dispose()
        {
            // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
            Dispose(disposing: true);
            GC.SuppressFinalize(this);
        }
        #endregion
    }
}
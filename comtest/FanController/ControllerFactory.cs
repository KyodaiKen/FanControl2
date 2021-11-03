using Microsoft.Extensions.Logging;
using RJCP.IO.Ports;

namespace CustomFanController
{
    public class ControllerFactory
    {
#warning linux may have issues
        //https://github.com/jcurl/RJCP.DLL.SerialPortStream#40-installation
        public static async Task<List<FanController>> GetCompatibleDevicesAsync(ILoggerFactory? loggerFactory = null)
        {
            var Logger = loggerFactory?.CreateLogger($"{nameof(ControllerFactory)}");

            var controllers = new List<FanController>();

            var availablePortDescription = SerialPortStream.GetPortDescriptions();

            foreach (var availablePort in availablePortDescription)
            {
                var currentPort = new SerialPortStream(availablePort.Port, Protocol.HandShake.BaudRate);

                try
                {
                    Logger?.LogInformation($"Opening {availablePort.Port} => {availablePort.Description}");

                    currentPort.Open();

                    currentPort.WriteTimeout = Timeout.Infinite;
                    currentPort.ReadTimeout = Timeout.Infinite;

                    // HandShakePacket
                    await currentPort.SendCommand(Protocol.Request.RQST_IDENTIFY);

                    var buffer = new byte[Protocol.BufferSize];

                    byte deviceId = 0;

                    for (int i = 0; i < Protocol.HandShake.AttemptsToConnect; i++)
                    {
                        var readTask = currentPort.ReadAsync(buffer).AsTask();
                        var timeout = Task.Delay(Protocol.Timeout);

                        if (await Task.WhenAny(readTask, timeout) == timeout)
                        {
                            Logger?.LogWarning("Reading Timeout");
                            continue;
                        }

                        // Sucess reading The buffer

                        var bytesReadCount = await readTask;

                        const string IncompatibleDevice = "Incompatible Device";

                        if (bytesReadCount < Protocol.HandShake.ResponsePrefixHandShakeBytes.Length)
                        {
                            // Incompatible device falls here
                            Logger?.LogWarning(IncompatibleDevice);
                            break;
                        }

                        byte[] message = buffer[..(Protocol.HandShake.ResponsePrefixHandShakeBytes.Length)];
                        if (!message.SequenceEqual(Protocol.HandShake.ResponsePrefixHandShakeBytes))
                        {
                            // Incompatible device falls here
                            Logger?.LogWarning(IncompatibleDevice);
                            break;
                        }

                        deviceId = buffer[Protocol.HandShake.ResponsePrefixHandShakeBytes.Length];

                        var loggerName = loggerFactory?.CreateLogger($"{nameof(FanController)}[{currentPort.PortName} {deviceId:X}]");

                        var contr = new FanController(currentPort, deviceId, loggerName);

                        await contr.InitializeDevice();

                        controllers.Add(contr);

                        Logger?.LogInformation($"Added controller with ID {deviceId} on {currentPort.PortName} to the controller pool!");
                        break;

                    }

                }
                catch (Exception ex)
                {
                    Logger?.LogWarning(ex.ToString());
                }
            }

            if (controllers.Count < 1)
            {
                Logger?.LogCritical("No controllers found!");
                return null;
            }
            else {
                Logger?.LogInformation($"Gathered {controllers.Count} controller" + (controllers.Count != 1 ? "s!" : "!"));
            }
            return controllers;
        }
    }
}
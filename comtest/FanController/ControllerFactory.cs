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
            var Logger = loggerFactory?.CreateLogger<ControllerFactory>();

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
#warning no deviceId may lead to here as well
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

#warning for protocol integrity, move this into the device initialization routine. I'ld recommend a class variable to store the data as well so it can be known by the end user
                        int start = 0;
                        if (buffer[0] == Protocol.Status.RESP_ERR && buffer[1] == Protocol.Error.ERR_EEPROM)
                        {
                            start = 2;
                            Logger?.LogWarning($"The controller on port {currentPort.PortName} had an EEPROM error and was reset to factory defaults!");
                        }

#warning check that, it may not be right
                        deviceId = buffer[Protocol.HandShake.ResponsePrefixHandShakeBytes.Length + start];

                        var loggerName = loggerFactory?.CreateLogger($"{nameof(FanController)} => {currentPort} => {deviceId}");

                        var contr = new FanController(currentPort, deviceId, loggerName);

                        await contr.InitializeDevice();

                        controllers.Add(contr);

                        Logger?.LogInformation($"Added controller with ID {deviceId} on {currentPort.PortName} to the controller pool!");
                        break;

                    }

                }
                catch (Exception ex)
                {
                    Logger?.LogError(ex.ToString());
                }
            }

            Logger?.LogInformation($"Gathered {controllers.Count} controller" + (controllers.Count != 1 ? "s!" : "!"));
            return controllers;
        }
    }
}
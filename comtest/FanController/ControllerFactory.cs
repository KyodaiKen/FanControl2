using System.Text;
using RJCP.IO.Ports;

namespace FanController
{
    public class ControllerFactory
    {
#warning linux may have issues
        //https://github.com/jcurl/RJCP.DLL.SerialPortStream#40-installation
        public static async Task<List<Controller>> GetCompatibleDevicesAsync()
        {
            var controllers = new List<Controller>();

            var availablePortDescription = SerialPortStream.GetPortDescriptions();

            foreach (var availablePort in availablePortDescription)
            {
                var currentPort = new SerialPortStream(availablePort.Port, Protocol.HandShake.BaudRate);

                try
                {
#warning to do better implementation with logger
                    Console.WriteLine($"Opening {availablePort.Port} => {availablePort.Description}");

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

                        if (await Task.WhenAny(readTask, Task.Delay(Protocol.HandShake.Timeout)) == readTask)
                        {
                            // Sucess reading The buffer

                            var bytesReadCount = await readTask;

                            if (bytesReadCount < Protocol.HandShake.ResponsePrefixHandShakeBytes.Length)
                            {
                                // Incompatible device falls here
                                continue;
                            }

                            //TODO: If EEPROM error occurrs, RESP_ERR+ERR_EEPROM are sent BEFORE the welcome message!

                            byte[] message = buffer[0..(Protocol.HandShake.ResponsePrefixHandShakeBytes.Length)];
                            if (!message.SequenceEqual(Protocol.HandShake.ResponsePrefixHandShakeBytes))
                            {
                                // Incompatible device falls here
                                continue;
                            }

                            deviceId = buffer[Protocol.HandShake.ResponsePrefixHandShakeBytes.Length];

                            controllers.Add(new Controller(currentPort, deviceId));
                            Console.WriteLine($"Added controller with ID {deviceId} on {currentPort.PortName} to the controller pool!");
                            break;
                        }
                    }
                    
                }
                catch (Exception ex)
                {
#warning to do better implementation with logger
                    Console.WriteLine(ex.ToString());
                }
            }

            return controllers;
        }
    }
}
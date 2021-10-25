using System.IO.Ports;
using System.Text;

namespace FanController
{
    public class ControllerFactory
    {
        public static async Task<List<Controller>> GetCompatibleDevicesAsync()
        {
            var controllers = new List<Controller>();

            var portsConected = SerialPort.GetPortNames();

            foreach (var port in portsConected)
            {
                var currentPort = new SerialPort(port);

                try
                {
                    currentPort.Open();

                    currentPort.WriteTimeout = Timeout.Infinite;
                    currentPort.ReadTimeout = Timeout.Infinite;

                    //currentPort.NewLine = Constants.NewLineOverride;

                    // HandShakePacket
                    currentPort.SendCommand(Commands.Request.RQST_IDENTIFY);

                    var buffer = new byte[64];

                    var getDevice = new Task<byte[]>(() =>
                    {
                        while (true)
                        {
                            var dataRead = currentPort.Read(buffer, 0, buffer.Length);
                            if (dataRead < Constants.ResponsePrefixHandShakeBytes.Length)
                            {
                                // Incompatible device falls here
                                continue;
                            }

                            if (buffer[0..Constants.ResponsePrefixHandShakeBytes.Length] != Constants.ResponsePrefixHandShakeBytes)
                            {
                                // Incompatible device falls here
                                continue;
                            }

                            var id = buffer[Constants.ResponsePrefixHandShakeBytes.Length..buffer.Length];

                            return id;
                        }
                    });

                    getDevice.Start();

                    if (await Task.WhenAny(getDevice, Task.Delay(Constants.Timeout)) != getDevice)
                    {
                        // timeout logic
                        currentPort.Close();
                        continue;
                    }

                    var device = Encoding.ASCII.GetString(await getDevice);

                    controllers.Add(new Controller(currentPort, device));
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
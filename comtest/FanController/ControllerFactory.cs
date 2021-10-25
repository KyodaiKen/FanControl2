using System.IO.Ports;

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

                    currentPort.NewLine = Constants.NewLineOverride;

                    // HandShakePacket
                    currentPort.SendCommand(Commands.Request.RQST_IDENTIFY);

                    var getDevice = new Task<string>(() =>
                    {
                        while (true)
                        {
                            if (currentPort.BytesToRead > 0)
                            {
                                var device = currentPort.ReadExisting();
                                if (device?.StartsWith(Constants.ResponsePrefixHandShake) == true)
                                {
                                    return device[Constants.ResponsePrefixHandShake.Length..];
                                }
                            }
                        }
                    });

                    getDevice.Start();

                    if (await Task.WhenAny(getDevice, Task.Delay(Constants.Timeout)) != getDevice)
                    {
                        // timeout logic
                        currentPort.Close();
                        continue;
                    }

                    var device = await getDevice;

                    if (device != Constants.CompatibleDeviceId)
                    {
                        continue;
                    }

                    currentPort.WriteLine("Who?");

                    var getWho = new Task<string>(() =>
                    {
                        while (true)
                        {
                            var who = currentPort.ReadLine();
                            if (who?.StartsWith(Constants.ResponsePrefixHandShake) == true)
                            {
                                return who[Constants.ResponsePrefixHandShake.Length..];
                            }
                        }
                    });

                    getWho.Start();

                    if (await Task.WhenAny(getWho, Task.Delay(Constants.Timeout)) != getWho)
                    {
                        // timeout logic
                        currentPort.Close();
                        continue;
                    }

                    var deviceName = await getWho;

                    controllers.Add(new Controller(currentPort, deviceName));
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
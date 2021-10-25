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

                    currentPort.WriteTimeout = Constants.Timeout;
                    currentPort.ReadTimeout = Constants.Timeout;

                    currentPort.NewLine = Constants.NewLineOverride;

                    currentPort.WriteLine("Device?");

                    var getDevice = new Task<string>(() =>
                    {
                        while (true)
                        {
                            var device = currentPort.ReadLine();
                            if (device?.StartsWith(Constants.ResponsePrefixHandShake) == true)
                            {
                                return device[Constants.ResponsePrefixHandShake.Length..];
                            }
                        }
                    });

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

                    if (await Task.WhenAny(getWho, Task.Delay(Constants.Timeout)) != getWho)
                    {
                        // timeout logic
                        currentPort.Close();
                        continue;
                    }

                    var deviceId = await getWho;

                    controllers.Add(new Controller(currentPort, deviceId));
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
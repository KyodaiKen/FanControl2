using FanController;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace comtest
{
    public static class Program
    {
        static List<global::FanController.FanController> controllers;
        static async Task Main(string[] args)
        {
            controllers = await ControllerFactory.GetCompatibleDevicesAsync();

            foreach (var controller in controllers)
            {
                controller.OnSensorsUpdate += OnSensorsUpdate;
                controller.StartListening();

                await controller.GetDeviceCapabilities();
                await controller.GetThermalSensorCalibration();

                await controller.GetCurve(0);
                await controller.GetMatrix(0);
            }

            Console.CancelKeyPress += Console_CancelKeyPress;

            await Task.Delay(-1);
        }

        private static void OnSensorsUpdate(byte DeviceId, object Data)
        {
            Console.WriteLine($"Device => '{DeviceId}' Data => '{Data}'");
        }

        private static void Console_CancelKeyPress(object sender, ConsoleCancelEventArgs e)
        {
            foreach (var item in controllers)
            {
                item.StopListening();
            }

            Environment.Exit(0);
        }
    }
}

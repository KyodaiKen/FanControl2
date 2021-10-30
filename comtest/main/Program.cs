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
                controller.OnError += OnError;
                controller.OnSensorsUpdate += OnSensorsUpdate;
                controller.StartListening();

                //Test EEPROM commands
                //await controller.RequestStoreToEEPROM();
                //await controller.RequestReadFromEEPROM();

                //Test set curve
                await controller.SetCurve(0, new Curve()
                {
                    CurvePoints = new CurvePoint[2]
                    {
                        new CurvePoint()
                        {
                            Temperature = 2,
                            DutyCycle = 16
                        },
                        new CurvePoint() {
                            Temperature = 4,
                            DutyCycle = 28
                        }
                    }
                });

                //Get curve 0 for testing
                await controller.GetCurve(0);

                //Test set matrix 0 for testing
                await controller.SetMatrix(0, new Matrix()
                {
                    MatrixPoints = new float[3]
                    {
                        0.5f,
                        0.5f,
                        -1
                    }
                });

                //Get matrix 0 for testing
                await controller.GetMatrix(0);

                await controller.GetReadings();

                await controller.SetControllerConfig(controller.ControllerConfig);
            }

            Console.CancelKeyPress += Console_CancelKeyPress;

            await Task.Delay(-1);
        }

        private static void OnSensorsUpdate(byte DeviceId, object Data)
        {
            Console.WriteLine($"Device => '{DeviceId}' Data => '{Data}'");
        }

        private static void OnError(byte deviceId, object Data)
        {
            string message = "";
            if (Data.GetType() == typeof(string)) message = (string)Data;
            if (Data.GetType() == typeof(byte[])) message = Convert.ToHexString((byte[])Data);

            Console.WriteLine($"Device {deviceId} returned the error {message}");
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

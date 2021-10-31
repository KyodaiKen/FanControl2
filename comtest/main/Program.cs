using CustomFanController;
using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace comtest
{
    public static class Program
    {
        private static List<FanController> controllers;

        private static ILogger MainLogger;
        static async Task<int> Main(string[] args)
        {
            var loggerFactory = LoggerFactory.Create(builder =>
            {
                builder
                    .SetMinimumLevel(LogLevel.Trace)
                    .AddFilter("Microsoft", LogLevel.Warning)
                    .AddFilter("System", LogLevel.Warning)
                    .AddConsole()
                    .AddFile($"./Logs/{nameof(FanController)}-{{Date}}.txt", minimumLevel: LogLevel.Trace, levelOverrides: null, isJson: false, fileSizeLimitBytes: 1073741824, retainedFileCountLimit: 10);
                    //.AddEventLog()
                    ;
            });

            MainLogger = loggerFactory.CreateLogger($"{nameof(Program)} => {nameof(Main)}");

            MainLogger.LogTrace("ProgramStart");

            controllers = await ControllerFactory.GetCompatibleDevicesAsync(loggerFactory);

            if(controllers == null)
            {
                return 100; //Exit with error 100
            }

            var sw_tests = new Stopwatch();
            sw_tests.Start();

            foreach (var controller in controllers)
            {
                controller.OnError += OnError;
                controller.OnSensorsUpdate += OnSensorsUpdate;

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

                //Test get readings
                var sw_readings = new Stopwatch();
                sw_readings.Start();
                await controller.GetReadings();
                sw_readings.Stop();
                MainLogger.LogTrace($"GetReadings() took {sw_readings.ElapsedMilliseconds} ms");

                //Test set config again
                await controller.SetControllerConfig(controller.ControllerConfig);
            }

            sw_tests.Stop();
            MainLogger.LogTrace($"Tests took {sw_tests.ElapsedTicks} ticks");

            Console.CancelKeyPress += Console_CancelKeyPress;

            await Task.Delay(-1);
            return 0;
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
                item.Dispose();
            }

            MainLogger.LogTrace("ProgramStop");

            Environment.Exit(0);
        }
    }
}

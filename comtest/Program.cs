﻿using FanController;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace comtest
{
    class Program
    {
        static List<Controller> controllers;
        static async Task Main(string[] args)
        {
            controllers = await ControllerFactory.GetCompatibleDevicesAsync();

            foreach (var controller in controllers)
            {
                controller.StatusUpdated += Controller_StatusUpdated;
                controller.SettingsUpdated += Controller_SettingsUpdated;
                controller.StartListening();
            }

            Console.CancelKeyPress += Console_CancelKeyPress;

            await Task.Delay(-1);
        }

        private static void Controller_SettingsUpdated(string DeviceId, string Data)
        {
            Console.WriteLine($"Settings Updated For => '{DeviceId}' => '{Data}'");
        }

        private static void Controller_StatusUpdated(string DeviceId, string Data)
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

﻿namespace FanController
{
    public class DeviceCapabilities
    {
        public byte NumberOfSensors;
        public byte NumberOfChannels;
    }

    public class ThermalSensor
    {
        float[] CalibrationSteinhartHartCoefficients { get; set; }
        float CalibrationOffset { get; set; }
        float CalibrationResistorValue { get; set; }
        byte Pin { get; set; }
    }

    public class PWMChannel
    {
        public byte Pin { get; set; }
    }

    public class ControllerConfig
    {
        public Dictionary<byte, ThermalSensor> ThermalSensors { get; set; }
        public Dictionary<byte, PWMChannel> PWMChannels { get; set; }
    }

    public class Matrix
    {
        public byte ChannelId { get; set; }
        public float[] MatrixPoints { get; set; }
    }

    public class CurvePoint
    {
        public float Temperature { get; set; }
        public byte DutyCycle { get; set; }
    }

    public class Curve
    {
        public byte ChannelId { get; set; }
        public byte Length { get; set; }
        public CurvePoint[] CurvePoints { get; set; }
    }

}

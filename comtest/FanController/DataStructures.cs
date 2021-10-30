namespace CustomFanController
{
    public class DeviceCapabilities
    {
        public byte NumberOfSensors;
        public byte NumberOfChannels;
    }

    public class ThermalSensor
    {
        public float[]? CalibrationSteinhartHartCoefficients { get; set; }
        public float CalibrationOffset { get; set; }
        public float CalibrationResistorValue { get; set; }
        public byte Pin { get; set; }
    }

    public class PWMChannel
    {
        public byte Pin { get; set; }
    }

    public class ControllerConfig
    {
        public ThermalSensor[]? ThermalSensors { get; set; }
        public PWMChannel[]? PWMChannels { get; set; }
    }

    public class FanControlConfig
    {
        public Matrix[]? Matrixes { get; set; }
        public Curve[]? Curves { get; set; }
    }

    public class Matrix
    {
        public byte ChannelId { get; set; }
        public float[]? MatrixPoints { get; set; }
    }

    public class CurvePoint
    {
        public float Temperature { get; set; }
        public byte DutyCycle { get; set; }
    }

    public class Curve
    {
        public byte ChannelId { get; set; }
        public CurvePoint[]? CurvePoints { get; set; }
    }

    public class ChannelReading
    {
        public float MatrixResult { get; set; }
        public float DutyCycle { get; set; }
    }

    public class TemperatureReading
    {
        public float Temperature { get; set; }
    }

    public class Readings
    {
        public TemperatureReading[]? TemperatureReadings { get; set; }
        public ChannelReading[]? ChannelReadings { get; set; }
    }
}

namespace FanController
{
    public class DeviceCapabilities
    {
        public byte NumberOfSensors;
        public byte NumberOfChannels;
    }

    public class ThermalSensorCalibration
    {
        public byte SensorId { get; set; }
        float[] SteinhartHart { get; set; }
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

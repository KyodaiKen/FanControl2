namespace FanController
{
    public class DeviceCapabilities
    {
        public byte NumberOfSensors;
        public byte NumberOfChannels;
    }

    public class Matrix
    {
        public byte ChannelId { get; set; }
        public float[] MatrixPoints { get; set; }

        public Matrix(byte channelId, float[] matrix_points)
        {
            ChannelId = channelId;
            MatrixPoints = matrix_points;
        }
    }

    public class CurvePoint
    {
        public float Temperature { get; set; }
        public byte DutyCycle { get; set; }

        public CurvePoint(float temp, byte dc)
        {
            Temperature = temp;
            DutyCycle = dc;
        }
    }

    public class Curve
    {
        public byte ChannelId { get; set; }
        public byte Length { get; set; }
        public CurvePoint[] CurvePoints { get; set; }
    }

}

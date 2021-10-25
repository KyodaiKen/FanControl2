namespace FanController
{
    public class CurvePoint
    {
        public float Temperature { get; set; }
        public byte DutyCycle { get; set; }
    }

    public class Curve
    {
        public byte CurveId { get; set; }
        public byte Length { get; set; }
        public CurvePoint[] CurvePoints { get; set; }
    }
}

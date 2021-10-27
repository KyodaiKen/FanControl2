using RJCP.IO.Ports;
using System.Runtime.Serialization.Formatters.Binary;

namespace FanController
{
    // Investigate BinaryReader Class https://docs.microsoft.com/en-us/dotnet/api/system.io.binaryreader?view=net-6.0
    public static class Extensions
    {
        public static async Task SendCommand(this SerialPortStream SerialPort, params byte[] data)
        {
            await SerialPort.WriteAsync(data, 0, data.Length);
        }

        private static readonly BinaryFormatter BinaryFormatter = new();
        public static byte[] ConvertToBinary<T>(this T data)
        {
            var ms = new MemoryStream();
            BinaryFormatter.Serialize(ms, data);

            return ms.ToArray();
        }

        public static T ConvertToObject<T>(this byte[] data)
        {
            var memStream = new MemoryStream(data)
            {
                Position = 0
            };

            T obj = (T)BinaryFormatter.Deserialize(memStream);

            return obj;
        }
    }
}

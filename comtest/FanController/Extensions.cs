using System.IO.Ports;
using System.Runtime.Serialization.Formatters.Binary;

namespace FanController
{
    // Investigate BinaryReader Class https://docs.microsoft.com/en-us/dotnet/api/system.io.binaryreader?view=net-6.0
    public static class Extensions
    {
        private static readonly BinaryFormatter BinaryFormatter = new BinaryFormatter();
        public static byte[] ConvertToBinary<T>(this T data)
        {
            MemoryStream ms = new MemoryStream();
            BinaryFormatter.Serialize(ms, data);

            return ms.ToArray();
        }

        public static T ConvertToObject<T>(this byte[] data)
        {
            MemoryStream memStream = new MemoryStream(data);
            memStream.Position = 0;

            T obj = (T)BinaryFormatter.Deserialize(memStream);

            return obj;
        }

        public static void SendCommand(this SerialPort SerialPort, params byte[] data)
        {
            SerialPort.Write(data, 0, data.Length);
        }
    }
}

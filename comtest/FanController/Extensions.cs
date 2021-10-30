using RJCP.IO.Ports;
using System.Runtime.Serialization.Formatters.Binary;

namespace CustomFanController
{
    // Investigate BinaryReader Class https://docs.microsoft.com/en-us/dotnet/api/system.io.binaryreader?view=net-6.0
    public static class Extensions
    {
        public static async Task SendCommand(this SerialPortStream SerialPort, params byte[] data)
        {
            await SerialPort.WriteAsync(data, 0, data.Length);
#warning Test again, it was reported to fail sometimes if using FlushAsync()
            //Fix reliability issues
            await SerialPort.FlushAsync();
        }

        //private static readonly BinaryFormatter BinaryFormatter = new();
        //public static byte[] ConvertToBinary<T>(this T data)
        //{
        //    var ms = new MemoryStream();
        //    BinaryFormatter.Serialize(ms, data);

        //    return ms.ToArray();
        //}

        //public static T ConvertToObject<T>(this byte[] data)
        //{
        //    var memStream = new MemoryStream(data)
        //    {
        //        Position = 0
        //    };

        //    T obj = (T)BinaryFormatter.Deserialize(memStream);

        //    return obj;
        //}

        // Remove current data so the next time wait's for the updated data instead of reading the old cache
        public static bool TryGetAndRemove<T, Y>(this Dictionary<T, Y> Dictionary, T key, out Y data)
        {
            if (!Dictionary.ContainsKey(key))
            {
                data = default;
                return false;
            }

            data = Dictionary[key];
            Dictionary.Remove(key);
            return true;
        }
    }
}

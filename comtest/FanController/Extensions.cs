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
            //Fix reliability issues
            while(SerialPort.BytesToWrite > 0) await SerialPort.FlushAsync();
        }

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

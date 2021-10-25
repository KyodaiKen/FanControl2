namespace FanController
{
    internal class Constants
    {
        internal const string CompatibleDeviceId = "KyoController";
        internal const string NewLineOverride = "\r\n";

#warning adjust timeout
        internal const int Timeout = 100000;

        internal const string ResponsePrefixStatus = "Status";
        internal const string ResponsePrefixSettings = "Settings";
        internal const string ResponsePrefixHandShake = "HandShake";
    }
}
